#include "duckdb.hpp"
#include "duckdb_test_compat.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "core_functions_extension.hpp"
#include "duckomo/local_file.hpp"
#include "duckomo/local_file.hpp"
#include "duckomo/metadata.hpp"
#include "duckomo/reader.hpp"
#include "duckomo/region_sampling.hpp"
#include "duckomo_extension.hpp"
#include "query_result_compat.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <thread>
#include <vector>

extern "C" {
#include "om_variable.h"
}

namespace duckdb {
DUCKOMO_TEST_LOAD_ALL_EXTENSIONS(database) {
	database.LoadStaticExtension<DuckomoExtension>();
}
} // namespace duckdb

namespace {
using namespace duckdb;
using namespace duckdb::duckomo;

void Require(bool condition, const std::string &message);

std::filesystem::path SamplingFixture(const std::string &name) {
#ifdef DUCKOMO_SAMPLING_FIXTURE_PATH
	return std::filesystem::path(DUCKOMO_SAMPLING_FIXTURE_PATH) / name;
#else
	return std::filesystem::path("test/data/sampling") / name;
#endif
}

std::string SqlLiteral(const std::string &value) {
	std::string result = "'";
	for (const auto character : value) {
		result.push_back(character);
		if (character == '\'') result.push_back('\'');
	}
	result.push_back('\'');
	return result;
}

std::unique_ptr<::duckomo_test::CollectedQueryResult> Query(Connection &connection, const std::string &sql) {
	return ::duckomo_test::Query(connection, sql);
}

std::vector<std::uint64_t> ParseIndices(const Value &value) {
	const auto text = value.ToString();
	Require(text.size() >= 2 && text.front() == '[' && text.back() == ']', "axis index value must be a list");
	std::vector<std::uint64_t> result;
	std::stringstream stream(text.substr(1, text.size() - 2));
	std::string item;
	while (std::getline(stream, item, ',')) {
		if (item.empty()) continue;
		result.push_back(std::stoull(item));
	}
	return result;
}

std::string LastScanMetrics(Connection &connection) {
	const auto row = Query(connection,
	    "SELECT metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1");
	return row->GetValue(0, 0).GetValue<std::string>();
}

std::string ValueTotals(const std::string &metrics) {
	const auto start = metrics.find("\"value_totals\":");
	Require(start != std::string::npos, "sampling metrics must include value totals");
	const auto end = metrics.find('}', start);
	Require(end != std::string::npos, "sampling metrics value totals must be complete");
	return metrics.substr(start, end - start + 1);
}

std::uint64_t JsonUnsigned(const std::string &json, const std::string &key) {
	const auto marker = "\"" + key + "\":";
	const auto marker_position = json.find(marker);
	Require(marker_position != std::string::npos, "metrics JSON is missing unsigned field " + key);
	const auto begin = marker_position + marker.size();
	const auto end = json.find_first_not_of("0123456789", begin);
	Require(end != begin, "metrics JSON field is not an unsigned integer: " + key);
	return std::stoull(json.substr(begin, end == std::string::npos ? end : end - begin));
}

std::vector<float> ReadOfficialValues(const std::string &path) {
	std::ifstream input(path);
	Require(input.good(), "official-reader CSV must be readable: " + path);
	std::string line;
	Require(static_cast<bool>(std::getline(input, line)) && line == "index,value", "official-reader CSV header must match");
	std::vector<float> result;
	while (std::getline(input, line)) {
		const auto comma = line.find(',');
		Require(comma != std::string::npos && std::stoull(line.substr(0, comma)) == result.size(),
		        "official-reader CSV indexes must be contiguous");
		result.push_back(std::stof(line.substr(comma + 1)));
	}
	return result;
}

void CheckOfficialCsvParity(Connection &connection, const std::string &scan, const std::string &column,
                            const std::string &csv_path,
                            const std::function<std::uint64_t(const std::vector<std::uint64_t> &)> &logical_index,
                            std::uint64_t expected_rows = 12, std::uint64_t expected_source = 12) {
	const auto rows = Query(connection, "SELECT sampling.axis_indices, " + column + ", "
	                                   "sampling.source_point_index, om_source.logical_index FROM " + scan);
	const auto official = ReadOfficialValues(csv_path);
	std::set<std::vector<std::uint64_t>> seen;
	for (idx_t row = 0; row < rows->RowCount(); ++row) {
		const auto axes = ParseIndices(rows->GetValue(0, row));
		Require(seen.insert(axes).second, "nearest result duplicated an original axis index tuple");
		const auto expected_index = logical_index(axes);
		Require(rows->GetValue(2, row).GetValue<std::uint64_t>() == expected_source,
		        "nearest source index must match the independent frozen locator oracle");
		Require(rows->GetValue(3, row).GetValue<std::uint64_t>() == expected_index,
		        "nearest source logical index must match independently reconstructed original axes");
		Require(expected_index < official.size() && !rows->GetValue(1, row).IsNull() &&
		            rows->GetValue(1, row).GetValue<float>() == official[expected_index],
		        "nearest value must equal the pinned official-reader value at the independent logical index");
	}
	Require(rows->RowCount() == expected_rows, "nearest scan must return the complete expected non-spatial index multiset");
}

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}
BoundSchema MakeSchema(const std::vector<std::uint64_t> &shape, const std::vector<std::string> &axes) {
	BoundSchema schema;
	schema.shape = shape;
	schema.row_count = 1;
	for (auto length : shape) schema.row_count *= length;
	BoundVariable value;
	value.canonical_path = "/value";
	value.column_name = "value";
	value.type = duckdb::LogicalType::FLOAT;
	value.shape = shape;
	value.row_count = schema.row_count;
	value.inferred_axes = axes;
	schema.variables.push_back(std::move(value));
	return schema;
}
void TestRegionSamplingOptions() {
	auto parsed = ParseRegionSamplingOptions(nullptr, nullptr, nullptr);
	Require(parsed.method == RegionMethod::Points && !parsed.max_distance_m && !parsed.max_distance_explicit &&
	            !parsed.out_of_bounds_explicit,
	        "omitted options preserve points defaults");
	duckdb::Value nearest_default("nearest");
	parsed = ParseRegionSamplingOptions(&nearest_default, nullptr, nullptr);
	Require(parsed.method == RegionMethod::Nearest && parsed.out_of_bounds == OutOfBoundsPolicy::Null,
	        "nearest defaults to a null outside-domain policy");
	duckdb::Value nearest_explicit("nearest");
	duckdb::Value zero_distance = duckdb::Value::DOUBLE(0.0);
	duckdb::Value error_policy("error");
	parsed = ParseRegionSamplingOptions(&nearest_explicit, &zero_distance, &error_policy);
	Require(parsed.max_distance_m == std::optional<double>(0.0) && parsed.out_of_bounds == OutOfBoundsPolicy::Error,
	        "nearest accepts exact zero distance and explicit strict policy");
	const auto must_reject = [](const duckdb::Value *method, const duckdb::Value *distance,
	                            const duckdb::Value *policy, const std::string &message) {
		bool rejected = false;
		try { (void)ParseRegionSamplingOptions(method, distance, policy); }
		catch (const duckdb::Exception &error) {
			rejected = std::string(error.what()).find("[region_") != std::string::npos;
		}
		Require(rejected, message);
	};
	auto unknown = duckdb::Value("bilinear");
	auto wrong_case = duckdb::Value("Nearest");
	auto null_method = duckdb::Value();
	auto points = duckdb::Value("points");
	auto nearest = duckdb::Value("nearest");
	auto negative = duckdb::Value::DOUBLE(-1);
	auto infinity = duckdb::Value::DOUBLE(std::numeric_limits<double>::infinity());
	auto string_distance = duckdb::Value("10");
	auto boolean_distance = duckdb::Value::BOOLEAN(true);
	auto explicit_points_distance = duckdb::Value::DOUBLE(0);
	auto policy = duckdb::Value("null");
	auto invalid_policy = duckdb::Value("ignore");
	must_reject(&unknown, nullptr, nullptr, "unknown method must be rejected");
	must_reject(&wrong_case, nullptr, nullptr, "method matching is case-sensitive");
	must_reject(&null_method, nullptr, nullptr, "NULL method must be rejected");
	must_reject(&points, &explicit_points_distance, nullptr, "points rejects explicitly supplied nearest options");
	must_reject(&nearest, &negative, nullptr, "negative distance must be rejected");
	must_reject(&nearest, &infinity, nullptr, "infinite distance must be rejected");
	must_reject(&nearest, &string_distance, nullptr, "text is not accepted as distance");
	must_reject(&nearest, &boolean_distance, nullptr, "boolean is not accepted as distance");
	must_reject(&nearest, nullptr, &invalid_policy, "unknown outside policy must be rejected");
	must_reject(&points, nullptr, &policy, "points rejects an explicit default outside policy");
}

void TestNonSpatialLogicalMapping() {
	RegularGrid grid(4, 3, -1, -2, 1, 1, GridStorageOrder::Separate);
	auto schema = MakeSchema({2, 3, 5, 4}, {"member", "lat", "time", "lon"});
	auto layout = BindSpatialLayout(schema, {{"member", "lat", "time", "lon"}}, grid, {"lat", "lon"});
	auto source = layout.SourcePosition({1, 4}, 2, 1, 0);
	Require(source.axis_indices == std::vector<std::uint64_t>({1, 1, 4, 2}),
	        "interleaved non-spatial indices must be restored at their original axis positions");
	Require(source.logical_index == (((1 * 3 + 1) * 5 + 4) * 4 + 2),
	        "source logical index must use original row-major strides");
	bool rejected = false;
	try {
		(void)layout.SourcePosition({1}, 2, 1, 0);
	} catch (const ReaderError &) {
		rejected = true;
	}
	Require(rejected, "a missing non-spatial axis position must be rejected");
	NearestAxisCursor cursor(layout);
	Require(cursor.ResultCount() == 10, "nearest cursor result upper bound includes only non-spatial shape");
	auto first = cursor.Next(3);
	Require(first.indices == std::vector<std::vector<std::uint64_t>>({{0, 0}, {0, 1}, {0, 2}}) && !first.exhausted,
	        "nearest cursor returns bounded row-major non-spatial combinations");
	auto rest = cursor.Next(20);
	Require(rest.indices.size() == 7 && rest.indices.front() == std::vector<std::uint64_t>({0, 3}) && rest.exhausted,
	        "nearest cursor exhausts after the non-spatial combinations only");
	NearestAxisCursor filtered(layout, [](std::size_t axis, std::uint64_t index) {
		return axis != 2 || index == 4;
	});
	auto selected = filtered.Next(20);
	Require(selected.indices.size() == 2 && selected.indices[0] == std::vector<std::uint64_t>({0, 4}) &&
	            selected.indices[1] == std::vector<std::uint64_t>({1, 4}),
	        "nearest cursor can apply per-axis safe selection without expanding spatial axes");
	AxisPredicate predicate;
	predicate.necessary_conditions.push_back({2, AxisComparison::Equal, duckdb::Value::BIGINT(1)});
	SemanticAxis selected_semantic;
	selected_semantic.kind = SemanticAxisKind::Member;
	selected_semantic.axis_name = "time";
	selected_semantic.axis_index = 2;
	selected_semantic.axis_length = 5;
	selected_semantic.stride = 4;
	selected_semantic.output_type = duckdb::LogicalType::BIGINT;
	selected_semantic.integer_members = {0, 1, 2, 3, 4};
	const SemanticAxes selected_semantics{selected_semantic};
	AxisSelectionCursor axis_selection(schema.shape, selected_semantics, predicate);
	NearestAxisCursor selected_cursor(layout, axis_selection);
	Require(selected_cursor.ResultCount() == 2, "selected cursor result bound counts only accepted non-spatial indices");
	const auto selected_batch = selected_cursor.Next(8);
	Require(selected_batch.indices == std::vector<std::vector<std::uint64_t>>({{0, 1}, {1, 1}}) && selected_batch.exhausted,
	        "selected cursor skips an unselected non-spatial range via the shared axis iterator");
	SpatialLayout overflow_layout;
	overflow_layout.shape = {static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), 2};
	overflow_layout.axes = {"a", "b"};
	overflow_layout.strides = {2, 1};
	overflow_layout.non_spatial_axes = {0, 1};
	bool overflow_rejected = false;
	try {
		( void )NearestAxisCursor(overflow_layout);
	} catch (const ReaderError &error) {
		overflow_rejected = error.Code() == ReaderErrorCode::ShapeOverflow;
	}
	Require(overflow_rejected, "nearest result upper bound must use checked signed-64-bit multiplication");
	SpatialLayout boundary_layout;
	boundary_layout.shape = {static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), 1};
	boundary_layout.axes = {"a", "b"};
	boundary_layout.strides = {1, 1};
	boundary_layout.non_spatial_axes = {0};
	Require(NearestAxisCursor(boundary_layout).ResultCount() ==
	            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
	        "nearest result upper bound accepts the signed 64-bit limit exactly");
}

std::pair<std::uint64_t, std::uint64_t> FirstValueLookupRange(Connection &connection,
                                                               const std::filesystem::path &path) {
	OmV3Reader reader(LocalFile::Open(*connection.context, path.string()));
	const auto tree = ReadMetadataTree(reader);
	Require(!tree.arrays.empty(), "nearest failure fixture has no value array");
	const auto &array = tree.arrays.front();
	const auto *variable = om_variable_init(array.metadata_owner->Data());
	Require(variable != nullptr, "nearest failure fixture metadata could not be opened by the official reader");
	const auto *metadata = reinterpret_cast<const OmVariableArrayV3_t *>(variable);
	Require(om_variable_get_dimensions_count(variable) > 0 && metadata->lut_size > 0,
	        "nearest failure fixture has no value lookup table");
	return {metadata->lut_offset, metadata->lut_size};
}

void TestNearestReadFailuresAreErrorsAndRecover(Connection &connection) {
	namespace fs = std::filesystem;
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	auto run_failure = [&](const std::string &name, auto mutate) {
		const auto path = fs::temp_directory_path() / ("duckomo-nearest-" + name + "-" + std::to_string(stamp) + ".om");
		fs::copy_file(SamplingFixture("small-separate.om"), path, fs::copy_options::overwrite_existing);
		const auto sql = "SELECT sum(\"small-separate/temperature\") FROM read_om_regions(" + SqlLiteral(path.string()) +
		                 ", regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText("
		                 "'POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
		                 "region_crs:='grid_lonlat', dimensions:=['member','time','level','lat','lon'], "
		                 "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
		                 "spatial_axes:=['lat','lon'], axes:={"
		                 "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
		                 "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
		                 "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
		                 "method:='nearest')";
		const auto statement = "nearest_failure_" + name;
		Query(connection, "PREPARE " + statement + " AS " + sql);
		mutate(path);
		auto failed = connection.Query("EXECUTE " + statement);
		Require(failed && failed->HasError(), "nearest value read must preserve " + name + " as a query error");
		Require(!failed->GetError().empty() && failed->GetError().find("source_null") == std::string::npos,
		        "nearest " + name + " must not be converted to source_null");
		Query(connection, "DEALLOCATE " + statement);
		fs::remove(path);
		const auto recovered = Query(connection, "SELECT count(*) FROM read_om_regions(" +
		    SqlLiteral(SamplingFixture("small-separate.om").string()) + ", "
		    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText("
		    "'POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
		    "region_crs:='grid_lonlat', dimensions:=['member','time','level','lat','lon'], "
		    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
		    "spatial_axes:=['lat','lon'], axes:={"
		    "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
		    "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
		    "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
		    "method:='nearest')");
		Require(recovered->GetValue(0, 0).GetValue<std::uint64_t>() == 12,
		        "valid nearest query must recover on the same connection after " + name);
	};
	run_failure("object_change", [](const fs::path &path) {
		std::ofstream changed(path, std::ios::binary | std::ios::app);
		Require(changed.good(), "could not open nearest object for mutation");
		changed.put('\0');
		Require(changed.good(), "could not mutate nearest object");
	});
	run_failure("short_read", [](const fs::path &path) {
		const auto size = fs::file_size(path);
		Require(size > 0, "nearest short-read fixture is empty");
		fs::resize_file(path, size - 1);
	});
	run_failure("decoder_failure", [&](const fs::path &path) {
		const auto [offset, count] = FirstValueLookupRange(connection, path);
		std::fstream changed(path, std::ios::binary | std::ios::in | std::ios::out);
		Require(changed.good(), "could not open nearest object for decoder corruption");
		changed.seekp(static_cast<std::streamoff>(offset));
		for (std::uint64_t index = 0; index < count; ++index) changed.put(static_cast<char>(0xff));
		Require(changed.good(), "could not corrupt nearest value lookup table");
	});
	const auto path = fs::temp_directory_path() / ("duckomo-nearest-permission-" + std::to_string(stamp) + ".om");
	fs::copy_file(SamplingFixture("small-separate.om"), path, fs::copy_options::overwrite_existing);
	const std::string prepared = "nearest_permission_failure";
	Query(connection, "PREPARE " + prepared + " AS SELECT sum(\"small-separate/temperature\") FROM read_om_regions(" +
	    SqlLiteral(path.string()) + ", regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText("
	    "'POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['member','time','level','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], axes:={"
	    "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	    "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
	    "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
	    "method:='nearest')");
	const auto original_permissions = fs::status(path).permissions();
	fs::permissions(path, fs::perms::none);
	auto denied = connection.Query("EXECUTE " + prepared);
	fs::permissions(path, original_permissions);
	Require(denied && denied->HasError(), "nearest value read must fail when the prepared source loses read permission");
	Require(denied->GetError().find("source_null") == std::string::npos,
	        "nearest permission failure must not be converted to source_null");
	Query(connection, "DEALLOCATE " + prepared);
	fs::remove(path);
}

void TestNearestProjectionDependencies(Connection &connection) {
	const auto source = SqlLiteral(SamplingFixture("small-separate.om").string());
	const auto regions = "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText("
	                    "'POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	                    "region_crs:='grid_lonlat', dimensions:=['member','time','level','lat','lon'], "
	                    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	                    "spatial_axes:=['lat','lon'], axes:={"
	                    "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	                    "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
	                    "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
	                    "method:='nearest'";
	const auto status = Query(connection, "SELECT count(*)::UBIGINT, first(typeof(sampling.status)), "
	                                      "first(sampling.status) FROM read_om_regions(" + source + ", " + regions + ")");
	Require(status->GetValue(0, 0).GetValue<std::uint64_t>() == 12 &&
	            status->GetValue(1, 0).GetValue<std::string>() == "VARCHAR" &&
	            status->GetValue(2, 0).GetValue<std::string>() == "ok",
	        "sampling geometry leaf projects with DuckDB's VARCHAR type");
	const auto geometry_metrics = LastScanMetrics(connection);
	const auto geometry_totals = ValueTotals(geometry_metrics);
	Require(geometry_metrics.find("\"status_dependency_complete\":null") != std::string::npos &&
	            geometry_metrics.find("\"required_state_variables\":[]") != std::string::npos,
	        "geometry-only projections report no fabricated variable-status observations");
	Require(geometry_totals.find("\"index_bytes\":0") != std::string::npos &&
	            geometry_totals.find("\"index_requests\":0") != std::string::npos &&
	            geometry_totals.find("\"data_bytes\":0") != std::string::npos &&
	            geometry_totals.find("\"data_requests\":0") != std::string::npos &&
	            geometry_totals.find("\"decoded_chunks\":0") != std::string::npos,
	        "a geometry-only sampling leaf must perform zero value-array index, data, and decode I/O");

	const auto count_rows = Query(connection, "SELECT count(*)::UBIGINT FROM read_om_regions(" + source + ", " + regions + ")");
	Require(count_rows->GetValue(0, 0).GetValue<std::uint64_t>() == 12 &&
	            ValueTotals(LastScanMetrics(connection)).find("\"data_bytes\":0") != std::string::npos &&
	            ValueTotals(LastScanMetrics(connection)).find("\"decoded_chunks\":0") != std::string::npos,
	        "COUNT(*) emits all nearest rows without reading or decoding value arrays");
	const auto count_metrics = LastScanMetrics(connection);

	const auto source_rows = Query(connection, "SELECT count(*)::UBIGINT, min(om_source.logical_index)::UBIGINT "
	                                           "FROM read_om_regions(" + source + ", " + regions + ", include_source:=true)");
	Require(source_rows->GetValue(0, 0).GetValue<std::uint64_t>() == 12 &&
	            source_rows->GetValue(1, 0).GetValue<std::uint64_t>() == 12 &&
	            ValueTotals(LastScanMetrics(connection)).find("\"data_bytes\":0") != std::string::npos &&
	            ValueTotals(LastScanMetrics(connection)).find("\"decoded_chunks\":0") != std::string::npos,
	        "source identity projection emits original logical indices without value-array I/O");

	const auto variables = Query(connection, "SELECT first(typeof(sampling.variables)), "
	                                         "first(sampling.variables['small-separate/temperature'].status), "
	                                         "first(sampling.variables['small-separate/humidity'].status), "
	                                         "count(*)::UBIGINT FROM read_om_regions(" + source + ", " + regions + ")");
	Require(variables->GetValue(0, 0).GetValue<std::string>().find("MAP(VARCHAR") == 0 &&
	            variables->GetValue(1, 0).GetValue<std::string>() == "ok" &&
	            variables->GetValue(2, 0).GetValue<std::string>() == "ok" &&
	            variables->GetValue(3, 0).GetValue<std::uint64_t>() == 12,
	        "sampling variables MAP leaf includes real statuses for every bound array");
	const auto map_metrics = LastScanMetrics(connection);
	Require(map_metrics.find("\"status_dependency_complete\":true") != std::string::npos &&
	            map_metrics.find("\"status_rows_observed\":12") != std::string::npos &&
	            map_metrics.find("\"required_state_variables\":[\"/small-separate/humidity\",\"/small-separate/temperature\"]") != std::string::npos &&
	            map_metrics.find("\"variable_status\":{") != std::string::npos &&
	            map_metrics.find("\"/small-separate/humidity\":{\"rows_observed\":12,\"ok\":12,\"source_null\":0,\"empty_geometry\":0,\"outside_domain\":0,\"beyond_max_distance\":0}") != std::string::npos &&
	            map_metrics.find("\"/small-separate/temperature\":{\"rows_observed\":12,\"ok\":12,\"source_null\":0,\"empty_geometry\":0,\"outside_domain\":0,\"beyond_max_distance\":0}") != std::string::npos &&
	            map_metrics.find("\"target_prepare_ns\":") != std::string::npos &&
	            map_metrics.find("\"locator_cpu_ns\":null") == std::string::npos,
	        "nearest metrics bind complete per-variable status counters, dependency scope, target preparation and locator CPU");
	Require(map_metrics.find("\"method\":\"nearest\"") != std::string::npos &&
	            map_metrics.find("\"algorithm\":\"sphere_haversine_f64_v1\"") != std::string::npos &&
	            map_metrics.find("\"representative\":\"point_on_surface_v1\"") != std::string::npos &&
	            map_metrics.find("\"count_unit\":\"region_axis_result\",\"region_target_count\":1,\"result_upper_bound\":12,\"result_rows_observed\":12,\"result_rows_exact\":12,\"result_count_complete\":true") != std::string::npos &&
	            map_metrics.find("\"source_rows_requested\":12,\"distinct_sources_observed\":12") != std::string::npos &&
	            map_metrics.find("\"regions\":{\"applicable\":false") != std::string::npos &&
	            map_metrics.find("\"matched_pairs_exact\":null,\"match_count_complete\":null") != std::string::npos,
	        "nearest metrics publish sampling identity and exact region-axis counts while nulling points-only regions metrics");
	Require(map_metrics.find("\"/small-separate/temperature\":{\"index_bytes\":") != std::string::npos &&
	            map_metrics.find("\"/small-separate/humidity\":{\"index_bytes\":") != std::string::npos &&
	            ValueTotals(map_metrics).find("\"data_bytes\":0") == std::string::npos &&
	            ValueTotals(map_metrics).find("\"decoded_chunks\":0") == std::string::npos,
	        "a requested variables MAP leaf must read actual data for every bound array: " + map_metrics);

	const auto full_struct = Query(connection, "SELECT sampling FROM read_om_regions(" + source + ", " + regions + ")");
	Require(full_struct->RowCount() == 12 && !full_struct->GetValue(0, 0).IsNull() &&
	            StructValue::GetChildren(full_struct->GetValue(0, 0)).size() == 15,
	        "a FULL_READ sampling projection must return the complete STRUCT");
	const auto full_struct_metrics = LastScanMetrics(connection);
	Require(full_struct_metrics.find("\"/small-separate/temperature\":{\"index_bytes\":") != std::string::npos &&
	            full_struct_metrics.find("\"/small-separate/humidity\":{\"index_bytes\":") != std::string::npos,
	        "a FULL_READ sampling STRUCT must depend on every bound variable status");

	const auto selected_array = Query(connection, "SELECT first(len(map_keys(sampling.variables)))::UBIGINT, "
	                                             "first(sampling.variables['small-separate/temperature'].status) "
	                                             "FROM read_om_regions(" + source + ", " + regions + ", "
	                                             "variable:='small-separate/temperature')");
	Require(selected_array->GetValue(0, 0).GetValue<std::uint64_t>() == 1 &&
	            selected_array->GetValue(1, 0).GetValue<std::string>() == "ok",
	        "a variable-qualified scan reports only its bound variable in the status map");
	const auto selected_array_metrics = LastScanMetrics(connection);
	Require(selected_array_metrics.find("\"/small-separate/temperature\":{\"index_bytes\":") != std::string::npos &&
	            selected_array_metrics.find("\"/small-separate/humidity\":") == std::string::npos,
	        "a variable-qualified status MAP reads only its bound variable");

	const auto repeated_leaf = Query(connection, "SELECT sampling.status AS first_status, "
	                                             "sampling.status AS duplicate_status FROM read_om_regions(" + source + ", " + regions + ") LIMIT 1");
	Require(repeated_leaf->RowCount() == 1 && repeated_leaf->GetValue(0, 0).GetValue<std::string>() == "ok" &&
	            repeated_leaf->GetValue(1, 0).GetValue<std::string>() == "ok" &&
	            ValueTotals(LastScanMetrics(connection)).find("\"data_bytes\":0") != std::string::npos,
	        "duplicate geometry leaf projections preserve values without adding value dependencies");
	const auto partial_status = Query(connection, "SELECT sampling.variables['small-separate/temperature'].status "
	                                              "FROM read_om_regions(" + source + ", " + regions + ") LIMIT 1");
	Require(partial_status->RowCount() == 1 && partial_status->GetValue(0, 0).GetValue<std::string>() == "ok",
	        "LIMIT returns the visible per-variable status row");
	const auto partial_status_metrics = LastScanMetrics(connection);
	Require(partial_status_metrics.find("\"scan_complete\":false") != std::string::npos &&
	            partial_status_metrics.find("\"result_rows_exact\":null") != std::string::npos &&
	            partial_status_metrics.find("\"result_count_complete\":false") != std::string::npos &&
	            partial_status_metrics.find("\"status_dependency_complete\":false") != std::string::npos &&
	            partial_status_metrics.find("\"status_rows_observed\":1") != std::string::npos,
	        "LIMIT publishes observed status rows without marking status dependencies or exact counts complete");

	const auto value = Query(connection, "SELECT first(\"small-separate/temperature\")::FLOAT, "
	                                      "count(*)::UBIGINT FROM read_om_regions(" + source + ", " + regions + ")");
	Require(value->GetValue(0, 0).GetValue<float>() == 12.0F &&
	            value->GetValue(1, 0).GetValue<std::uint64_t>() == 12,
	        "a direct variable projection retains its original FLOAT value");
	const auto value_metrics = LastScanMetrics(connection);
	Require(value_metrics.find("\"/small-separate/temperature\":{\"index_bytes\":") != std::string::npos &&
	            value_metrics.find("\"/small-separate/humidity\":{\"index_bytes\":0,\"index_requests\":0,\"data_bytes\":0,\"data_requests\":0,\"decoded_chunks\":0") != std::string::npos,
	        "a direct value projection reads only its selected source array");
	std::cout << "projection_io geometry=" << geometry_totals
	          << " count=" << ValueTotals(count_metrics)
	          << " map=" << ValueTotals(map_metrics)
	          << " full=" << ValueTotals(full_struct_metrics)
	          << " selected=" << ValueTotals(selected_array_metrics)
	          << " direct=" << ValueTotals(value_metrics) << '\n';
}

void TestNearestMemoryScaling(Connection &connection, std::optional<std::uint32_t> only_dimension = std::nullopt) {
	struct MemoryResult {
		std::uint32_t dimension;
		std::uint64_t rows;
		std::uint64_t owned_upper_bound;
		std::uint64_t work_peak;
		std::uint64_t selected_bytes;
		std::uint64_t selected_chunks;
		std::uint64_t locator_cpu_ns;
		std::uint64_t full_bytes;
		std::uint64_t full_chunks;
	};
	std::vector<MemoryResult> results;
	const std::vector<std::uint32_t> dimensions = only_dimension.has_value()
	                                                 ? std::vector<std::uint32_t>{*only_dimension}
	                                                 : std::vector<std::uint32_t>{64U, 256U};
	for (const auto dimension : dimensions) {
		const auto prefix = "memory-" + std::to_string(dimension);
		const auto path = SqlLiteral(SamplingFixture(prefix + ".om").string());
		const auto grid = "grid:={'nx':" + std::to_string(dimension) + ", 'ny':" +
		                  std::to_string(dimension) + ", 'lat0':0.0, 'lon0':0.0, "
		                  "'dlat':" + std::to_string(4.0 / (dimension - 1)) + ", 'dlon':" +
		                  std::to_string(4.0 / (dimension - 1)) + ", 'order':'separate'}";
		const auto target_region = "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText("
		                           "'POLYGON((1.99 1.99,2.01 1.99,2.01 2.01,1.99 2.01,1.99 1.99))'))}], ";
		const auto nearest_sql = "SELECT count(*)::UBIGINT, min(sampling.source_point_index)::UBIGINT, "
		                        "sum(\"" + prefix + "/temperature\")::DOUBLE FROM read_om_regions(" + path + ", " +
		                        target_region + "region_crs:='grid_lonlat', dimensions := ['member','time','level','lat','lon'], " +
		                        grid + ", spatial_axes := ['lat','lon'], method := 'nearest')";
		const auto selected = Query(connection, nearest_sql);
		Require(selected->GetValue(0, 0).GetValue<std::uint64_t>() == 12 &&
		            !selected->GetValue(1, 0).IsNull() && !selected->GetValue(2, 0).IsNull(),
		        prefix + " nearest memory workload must produce all twelve axis result rows and values");
		const auto nearest_metrics = LastScanMetrics(connection);
		Require(nearest_metrics.find("\"query_owned_complete\":true") != std::string::npos &&
		            nearest_metrics.find("\"query_owned_released_at_terminal\":true") != std::string::npos &&
		            nearest_metrics.find("\"components\":{") != std::string::npos &&
		            nearest_metrics.find("\"bind\":{\"peak_owned_capacity_bytes\":") != std::string::npos &&
		            nearest_metrics.find("\"global_control\":{\"peak_owned_capacity_bytes\":") != std::string::npos &&
		            nearest_metrics.find("\"selector\":{\"peak_owned_capacity_bytes\":") != std::string::npos &&
		            nearest_metrics.find("\"task_positions\":{\"peak_owned_capacity_bytes\":") != std::string::npos &&
		            nearest_metrics.find("\"batch_segments\":{\"peak_owned_capacity_bytes\":") != std::string::npos &&
		            nearest_metrics.find("\"decoder\":{\"peak_owned_capacity_bytes\":") != std::string::npos &&
		            nearest_metrics.find("\"coalescing\":{\"peak_owned_capacity_bytes\":") != std::string::npos &&
		            nearest_metrics.find("\"transport_control\":{\"peak_owned_capacity_bytes\":") != std::string::npos &&
		            nearest_metrics.find("\"engine_output_vector_bytes\":null") != std::string::npos &&
		            nearest_metrics.find("\"spatial_internal_memory_complete\":false") != std::string::npos,
		        prefix + " nearest ledger must report complete DuckOMO-owned components and identify external memory scopes");
		const auto owned_upper = JsonUnsigned(nearest_metrics, "query_owned_upper_bound_peak_bytes");
		const auto work_peak = JsonUnsigned(nearest_metrics, "owned_work_peak_bytes");
		const auto selected_totals = ValueTotals(nearest_metrics);
		const auto selected_bytes = JsonUnsigned(selected_totals, "data_bytes");
		const auto selected_chunks = JsonUnsigned(selected_totals, "decoded_chunks");
		const auto locator_cpu_ns = JsonUnsigned(nearest_metrics, "locator_cpu_ns");
		const auto full = Query(connection, "SELECT sum(\"" + prefix + "/temperature\")::DOUBLE FROM read_om(" + path + ", " +
		                                   "variable:='" + prefix + "/temperature', " + grid + ", "
		                                   "spatial_axes := ['lat','lon'], "
		                                   "dimensions := ['member','time','level','lat','lon'])");
		Require(!full->GetValue(0, 0).IsNull(), prefix + " full-value baseline must consume the selected array");
		const auto full_totals = ValueTotals(LastScanMetrics(connection));
		const auto full_bytes = JsonUnsigned(full_totals, "data_bytes");
		const auto full_chunks = JsonUnsigned(full_totals, "decoded_chunks");
		Require(selected_bytes < full_bytes && selected_chunks < full_chunks,
		        prefix + " nearest values must read and decode fewer chunks than the same-variable full baseline");
		results.push_back({dimension, 12, owned_upper, work_peak, selected_bytes, selected_chunks,
		                   locator_cpu_ns, full_bytes, full_chunks});
		Require(work_peak <= 512 * 1024,
		        "nearest worker scratch peak must remain within the 512 KiB per-worker budget");
	}
	if (results.size() == 2) {
		Require(results[1].owned_upper_bound <= 2 * results[0].owned_upper_bound,
		        "16x nearest spatial growth must remain within a 2x owned-memory upper bound");
		Require(results[1].work_peak <= 2 * results[0].work_peak,
		        "16x nearest spatial growth must remain within a 2x worker work-peak bound");
	}
	const auto skippable_path = SqlLiteral(SamplingFixture("skippable-values.om").string());
	const auto skippable_grid = "grid:={'nx':1024,'ny':1024,'lat0':-10.0,'lon0':-10.0,"
	                           "'dlat':0.01955,'dlon':0.01955,'order':'separate'}";
	const auto skippable_region = "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText("
	                             "'POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], ";
	const auto selected_large = Query(connection,
	    "SELECT count(*)::UBIGINT, sum(\"skippable-values/temperature\")::DOUBLE FROM read_om_regions(" +
	    skippable_path + ", " + skippable_region + "region_crs:='grid_lonlat', dimensions := ['lat','lon'], " +
	    skippable_grid + ", spatial_axes := ['lat','lon'], method := 'nearest')");
	Require(selected_large->GetValue(0, 0).GetValue<std::uint64_t>() == 1 &&
	            !selected_large->GetValue(1, 0).IsNull(),
	        "skippable-values nearest read must return and consume exactly one selected source value");
	const auto skippable_nearest_totals = ValueTotals(LastScanMetrics(connection));
	const auto skippable_nearest_bytes = JsonUnsigned(skippable_nearest_totals, "data_bytes");
	const auto skippable_nearest_chunks = JsonUnsigned(skippable_nearest_totals, "decoded_chunks");
	const auto skippable_full = Query(connection,
	    "SELECT sum(\"skippable-values/temperature\")::DOUBLE FROM read_om(" + skippable_path + ", " +
	    skippable_grid + ", spatial_axes := ['lat','lon'], dimensions := ['lat','lon'])");
	Require(!skippable_full->GetValue(0, 0).IsNull(),
	        "skippable-values baseline must consume the complete official-reader array");
	const auto skippable_full_totals = ValueTotals(LastScanMetrics(connection));
	const auto skippable_full_bytes = JsonUnsigned(skippable_full_totals, "data_bytes");
	const auto skippable_full_chunks = JsonUnsigned(skippable_full_totals, "decoded_chunks");
	Require(skippable_nearest_bytes < skippable_full_bytes && skippable_nearest_chunks < skippable_full_chunks,
	        "skippable-values nearest must reduce actual value bytes and decoded chunks on the same full object");
	std::cout << "nearest_skippable " << skippable_nearest_bytes << ',' << skippable_nearest_chunks << ','
	          << skippable_full_bytes << ',' << skippable_full_chunks << '\n';
	std::cout << "nearest_memory";
	for (const auto &result : results) {
		std::cout << ' ' << result.dimension << ':' << result.owned_upper_bound << ',' << result.work_peak << ','
		          << result.selected_bytes << ',' << result.selected_chunks << ',' << result.locator_cpu_ns << ','
		          << result.full_bytes << ',' << result.full_chunks;
	}
	std::cout << '\n';
}

void TestNearestStatusAndNonfiniteValues(Connection &connection) {
	const auto small_file = SamplingFixture("small-separate.om").string();
	const auto regions = Query(connection,
	    "SELECT region_id || ':' || sampling.status || ':' || "
	    "sampling.variables['small-separate/temperature'].status || ':' || "
	    "sampling.variables['small-separate/temperature'].source_count::VARCHAR || ':' || "
	    "sampling.variables['small-separate/temperature'].valid_source_count::VARCHAR || ':' || "
	    "(lat IS NULL)::VARCHAR || ':' || (lon IS NULL)::VARCHAR || ':' || "
	    "(sampling.source_point_index IS NULL)::VARCHAR || ':' || "
	    "(sampling.source_lon IS NULL)::VARCHAR || ':' || (sampling.source_lat IS NULL)::VARCHAR || ':' || "
	    "(sampling.distance_m IS NULL)::VARCHAR || ':' || (om_source IS NULL)::VARCHAR "
	    "FROM read_om_regions(" + SqlLiteral(small_file) + ", regions:=["
	    "{'id':'Empty','wkb':ST_AsWKB(ST_GeomFromText('POLYGON EMPTY'))},"
	    "{'id':'Outside','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((10 10,11 10,11 11,10 11,10 10))'))},"
	    "{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['member','time','level','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], axes:={"
	    "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	    "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
	    "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
	    "method:='nearest', include_source:=true) WHERE sampling.axis_indices=[0,0,0] ORDER BY region_id");
	Require(regions->RowCount() == 3 &&
	            regions->GetValue(0, 0).GetValue<std::string>() ==
	                "Empty:empty_geometry:empty_geometry:0:0:true:true:true:true:true:true:true" &&
	            regions->GetValue(0, 1).GetValue<std::string>() ==
	                "Gap:ok:ok:1:1:false:false:false:false:false:false:false" &&
	            regions->GetValue(0, 2).GetValue<std::string>() ==
	                "Outside:outside_domain:outside_domain:0:0:false:false:true:true:true:true:true",
	        "empty-source and outside batches must emit complete rows without inventing a source or ending before Gap");
	const auto state_metrics = LastScanMetrics(connection);
	Require(state_metrics.find("\"sampling\":{\"method\":\"nearest\"") != std::string::npos &&
	            state_metrics.find("\"result_rows_exact\":36,\"result_count_complete\":true") != std::string::npos &&
	            state_metrics.find("\"status_dependency_complete\":true") != std::string::npos &&
	            state_metrics.find("\"/small-separate/temperature\":{\"rows_observed\":36,\"ok\":12,\"source_null\":0,\"empty_geometry\":12,\"outside_domain\":12,\"beyond_max_distance\":0}") != std::string::npos,
	        "complete state metrics count EMPTY, outside, and valid Gap rows without attributing fake sources: " + state_metrics);

	const auto nonfinite = Query(connection,
	    "SELECT (isinf(value) AND value > 0)::VARCHAR || ':' || sampling.variables['value'].status || ':' || "
	    "sampling.variables['value'].source_count::VARCHAR || ':' || "
	    "sampling.variables['value'].valid_source_count::VARCHAR || ':' || sampling.source_point_index::VARCHAR "
	    "FROM read_om_regions('test/data/regions/regions_nonfinite.om', "
	    "regions:=[{'id':'PositiveInf','wkb':ST_AsWKB(ST_GeomFromText("
	    "'POLYGON((0.9 -0.1,1.1 -0.1,1.1 0.1,0.9 0.1,0.9 -0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['lat','lon'], "
	    "grid:={'nx':2,'ny':2,'lat0':0.0,'lon0':0.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], method:='nearest')");
	Require(nonfinite->RowCount() == 1 &&
	            nonfinite->GetValue(0, 0).GetValue<std::string>() == "true:ok:1:1:1",
	        "positive infinity remains a valid source value and contributes to the complete source counts");
	const auto negative_infinity = Query(connection,
	    "SELECT (isinf(value) AND value < 0)::VARCHAR || ':' || sampling.variables['value'].status || ':' || "
	    "sampling.variables['value'].valid_source_count::VARCHAR || ':' || sampling.source_point_index::VARCHAR "
	    "FROM read_om_regions('test/data/regions/regions_nonfinite.om', "
	    "regions:=[{'id':'NegativeInf','wkb':ST_AsWKB(ST_GeomFromText("
	    "'POLYGON((-0.1 0.9,0.1 0.9,0.1 1.1,-0.1 1.1,-0.1 0.9))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['lat','lon'], "
	    "grid:={'nx':2,'ny':2,'lat0':0.0,'lon0':0.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], method:='nearest')");
	Require(negative_infinity->RowCount() == 1 &&
	            negative_infinity->GetValue(0, 0).GetValue<std::string>() == "true:ok:1:2",
	        "negative infinity remains a valid source value and contributes to the complete source counts");
}

void TestNearestCancellationAndRecovery(Connection &connection) {
	Query(connection, "SET threads=4; SET duckomo_max_threads=4");
	std::ostringstream region_list;
	region_list << "regions:=[";
	for (idx_t region = 0; region < 64; ++region) {
		if (region) region_list << ',';
		region_list << "{'id':'R" << region << "','wkb':ST_AsWKB(ST_GeomFromText("
		            << "'POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}";
	}
	region_list << "], ";
	const auto sql = "SELECT region_id, sampling.axis_indices, \"small-many-axis/temperature\" FROM read_om_regions(" +
	                 SqlLiteral(SamplingFixture("small-many-axis.om").string()) + ", " + region_list.str() +
	                 "region_crs:='grid_lonlat', dimensions := ['sample','lat','lon'], "
	                 "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	                 "spatial_axes := ['lat','lon'], method := 'nearest')";
	std::atomic<bool> first_result_ready{false};
	std::atomic<bool> continue_fetch{false};
	std::atomic<bool> query_finished{false};
	std::string query_error;
	std::thread query_thread([&] {
		try {
			auto result = duckdb::duckomo_test::SendQuery(connection, sql);
			if (!result) {
				query_error = "nearest cancellation query returned no result";
			} else if (result->HasError()) {
				query_error = result->GetError();
			} else {
				first_result_ready.store(result->Fetch() != nullptr, std::memory_order_release);
				while (!continue_fetch.load(std::memory_order_acquire)) std::this_thread::yield();
				while (result->Fetch()) {
				}
				if (result->HasError()) query_error = result->GetError();
			}
		} catch (const std::exception &error) {
			query_error = error.what();
		}
		query_finished.store(true, std::memory_order_release);
	});
	while (!first_result_ready.load(std::memory_order_acquire) && !query_finished.load(std::memory_order_acquire)) {
		std::this_thread::yield();
	}
	if (first_result_ready.load(std::memory_order_acquire)) connection.Interrupt();
	continue_fetch.store(true, std::memory_order_release);
	query_thread.join();
	std::transform(query_error.begin(), query_error.end(), query_error.begin(),
	               [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	Require(first_result_ready.load(std::memory_order_acquire) &&
	            (query_error.find("interrupt") != std::string::npos || query_error.find("cancel") != std::string::npos),
	        "nearest scan interruption after a visible batch must fail with cancellation: " + query_error);
	const auto cancelled_metrics = LastScanMetrics(connection);
	Require(cancelled_metrics.find("\"status\":\"cancelled\"") != std::string::npos &&
	            cancelled_metrics.find("\"scan_complete\":false") != std::string::npos &&
	            cancelled_metrics.find("\"result_rows_exact\":null") != std::string::npos &&
	            cancelled_metrics.find("\"query_owned_released_at_terminal\":true") != std::string::npos,
	        "cancelled nearest execution must retain incomplete counts and release owned state");
	const auto recovered = Query(connection,
	    "SELECT count(*)::UBIGINT FROM read_om_regions(" + SqlLiteral(SamplingFixture("small-many-axis.om").string()) +
	    ", regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText("
	    "'POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions := ['sample','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes := ['lat','lon'], method := 'nearest')");
	Require(recovered->GetValue(0, 0).GetValue<std::uint64_t>() == 513 &&
	            LastScanMetrics(connection).find("\"status\":\"success\"") != std::string::npos,
	        "a full nearest scan must recover on the same connection after cancellation");
}

void TestNearestScanParity(Connection &connection) {
	const auto conflict_scan = "read_om_regions(" + SqlLiteral(SamplingFixture("small-spatial-only.om").string()) +
	    ", regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText("
	    "'POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['time','lon'], "
	    "grid:={'nx':2,'ny':2,'lat0':0.0,'lon0':0.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['time','lon'], valid_times:=[TIMESTAMP '2026-01-01',TIMESTAMP '2026-01-02'], "
	    "method:='nearest')";
	for (const auto &projection : {"count(*)", "valid_time"}) {
		const auto invalid = connection.Query("SELECT " + std::string(projection) + " FROM " + conflict_scan);
		Require(invalid->HasError() && invalid->GetError().find("temporal axis must not overlap") != std::string::npos,
		        "temporal/spatial conflict must fail at bind independently of projected columns");
	}
	const auto small_file = SamplingFixture("small-separate.om").string();
	const auto spatial_first_file = SamplingFixture("small-spatial-first.om").string();
	const auto many_file = SamplingFixture("small-many-axis.om").string();
	const auto boundary_file = SamplingFixture("small-boundary-axis.om").string();
	const auto reverse_file = SamplingFixture("small-reverse-both.om").string();
	const auto reverse_lat_file = SamplingFixture("small-reverse-lat.om").string();
	const auto reverse_lon_file = SamplingFixture("small-reverse-lon.om").string();
	const auto flat_lon_file = SamplingFixture("small-flat-lon.om").string();
	const auto flat_lat_file = SamplingFixture("small-flat-lat.om").string();
	const auto unmapped_file = SamplingFixture("small-unmapped.om").string();
	const auto spatial_only_file = SamplingFixture("small-spatial-only.om").string();
	const auto scan = "read_om_regions(" + SqlLiteral(small_file) + ", "
	                  "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	                  "region_crs:='grid_lonlat', dimensions:=['member','time','level','lat','lon'], "
	                  "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	                  "spatial_axes:=['lat','lon'], axes:={"
	                  "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	                  "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
	                  "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
	                  "method:='nearest', include_source:=true)";
	const auto csv_small = SamplingFixture("small-separate.temperature.csv").string();
	const auto csv_humidity = SamplingFixture("small-separate.humidity.csv").string();
	const auto result = Query(connection,
	                          "SELECT count(*)::UBIGINT, count(DISTINCT sampling.source_point_index)::UBIGINT, "
	                          "min(\"small-separate/temperature\")::FLOAT, max(\"small-separate/temperature\")::FLOAT, "
	                          "min(om_source.logical_index)::UBIGINT, max(om_source.logical_index)::UBIGINT FROM " + scan);
	Require(result->RowCount() == 1 && result->GetValue(0, 0).GetValue<std::uint64_t>() == 12 &&
	            result->GetValue(1, 0).GetValue<std::uint64_t>() == 1 &&
	            result->GetValue(2, 0).GetValue<float>() == 12.0F &&
	            result->GetValue(3, 0).GetValue<float>() == 211012.0F &&
	            result->GetValue(4, 0).GetValue<std::uint64_t>() == 12 &&
	            result->GetValue(5, 0).GetValue<std::uint64_t>() == 287,
	        "nearest scan must preserve official values and complete source positions");
	const auto direct_reference = Query(
	    connection,
	    "WITH actual AS MATERIALIZED (SELECT sampling.axis_indices AS axes, "
	    "\"small-separate/temperature\" AS temperature, \"small-separate/humidity\" AS humidity, "
	    "om_source.logical_index AS logical_index FROM " + scan + "), "
	    "expected AS MATERIALIZED (SELECT actual.axes, temperature.value::FLOAT AS temperature, "
	    "humidity.value::FLOAT AS humidity, actual.logical_index FROM actual JOIN read_csv_auto(" +
	        SqlLiteral(SamplingFixture("small-separate.temperature.csv").string()) +
	        ") temperature ON temperature.index=actual.logical_index JOIN read_csv_auto(" +
	        SqlLiteral(SamplingFixture("small-separate.humidity.csv").string()) +
	        ") humidity ON humidity.index=actual.logical_index), "
	    "differences AS ((SELECT axes,temperature,humidity FROM actual EXCEPT ALL "
	    "SELECT axes,temperature,humidity FROM expected) UNION ALL "
	    "(SELECT axes,temperature,humidity FROM expected EXCEPT ALL "
	    "SELECT axes,temperature,humidity FROM actual)) SELECT count(*)::UBIGINT FROM differences");
	Require(direct_reference->GetValue(0, 0).GetValue<std::uint64_t>() == 0,
	        "every nearest value must match the pinned official-reader CSV at its reconstructed source index");
	CheckOfficialCsvParity(connection, scan, "\"small-separate/temperature\"", csv_small,
	                       [](const std::vector<std::uint64_t> &axes) {
	                       return ((axes.at(0) * 3 + axes.at(1)) * 2 + axes.at(2)) * 25 + 12;
	                       });
	CheckOfficialCsvParity(connection, scan, "\"small-separate/humidity\"", csv_humidity,
	                       [](const std::vector<std::uint64_t> &axes) {
	                       return ((axes.at(0) * 3 + axes.at(1)) * 2 + axes.at(2)) * 25 + 12;
	                       });

	const auto interleaved = Query(
	    connection,
	    "SELECT sampling.axis_indices, \"small-spatial-first/temperature\", om_source.logical_index FROM "
	    "read_om_regions(" + SqlLiteral(spatial_first_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['lat','lon','member','time','level'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], axes:={"
	    "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	    "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
	    "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
	    "method:='nearest', include_source:=true) WHERE sampling.axis_indices=[1,2,1]");
	Require(interleaved->RowCount() == 1 && interleaved->GetValue(0, 0).ToString() == "[1, 2, 1]" &&
	            interleaved->GetValue(1, 0).GetValue<float>() == 211012.0F &&
	            interleaved->GetValue(2, 0).GetValue<std::uint64_t>() == 155,
	        "leading spatial axes must retain original row-major source identity");
	const auto interleaved_scan =
	    "read_om_regions(" + SqlLiteral(spatial_first_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['lat','lon','member','time','level'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], axes:={'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00',"
	    "'step':INTERVAL '1 hour'},'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
	    "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
	    "method:='nearest', include_source:=true)";
	CheckOfficialCsvParity(connection, interleaved_scan, "\"small-spatial-first/temperature\"",
	                       SamplingFixture("small-spatial-first.temperature.csv").string(),
	                       [](const std::vector<std::uint64_t> &axes) {
	                       return ((12 * 2 + axes.at(0)) * 3 + axes.at(1)) * 2 + axes.at(2);
	                       });

	const auto duplicate_labels = Query(connection,
	    "SELECT count(*)::UBIGINT, count(DISTINCT sampling.axis_indices)::UBIGINT, "
	    "count(DISTINCT member)::UBIGINT FROM read_om_regions(" + SqlLiteral(small_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText("
	    "'POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['member','time','level','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], axes:={"
	    "'member':{'axis':'member','values':[0::BIGINT,0::BIGINT]},"
	    "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	    "'level':{'axis':'level','values':[100::BIGINT,100::BIGINT],'kind':'pressure','unit':'hPa'}}, "
	    "method:='nearest')");
	Require(duplicate_labels->GetValue(0, 0).GetValue<std::uint64_t>() == 12 &&
	            duplicate_labels->GetValue(1, 0).GetValue<std::uint64_t>() == 12 &&
	            duplicate_labels->GetValue(2, 0).GetValue<std::uint64_t>() == 1,
	        "duplicate semantic coordinate labels retain distinct original axis positions and tuples");

	Query(connection, "SET threads=1");
	const auto boundary = Query(connection,
	    "SELECT count(*)::UBIGINT, min(sampling.axis_indices[1])::UBIGINT, "
	    "max(sampling.axis_indices[1])::UBIGINT, max(\"small-boundary-axis/temperature\")::FLOAT "
	    "FROM read_om_regions(" + SqlLiteral(boundary_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText("
	    "'POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['sample','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], method:='nearest')");
	Require(boundary->GetValue(0, 0).GetValue<std::uint64_t>() == 2049 &&
	            boundary->GetValue(1, 0).GetValue<std::uint64_t>() == 0 &&
	            boundary->GetValue(2, 0).GetValue<std::uint64_t>() == 2048 &&
	            boundary->GetValue(3, 0).GetValue<float>() == 12.0F,
	        "nearest task boundary emits complete axis tuples across the 2048-row maximum");
	const auto boundary_metrics = LastScanMetrics(connection);
	const auto task_capacity_marker = std::string("\"task_positions\":{\"peak_owned_capacity_bytes\":");
	const auto task_capacity_position = boundary_metrics.find(task_capacity_marker);
	Require(task_capacity_position != std::string::npos &&
	            std::stoull(boundary_metrics.substr(task_capacity_position + task_capacity_marker.size())) ==
	                2048 * (2 * sizeof(std::uint64_t) + sizeof(std::vector<std::uint64_t>)),
	        "task vector capacity must be accounted once during execution");
	Query(connection, "SET threads=4");
	const auto task_count_marker = std::string("\"tasks\":{\"created\":");
	const auto task_count_position = boundary_metrics.find(task_count_marker);
	const auto task_rows_marker = std::string("\"max_task_rows\":");
	const auto task_rows_position = boundary_metrics.find(task_rows_marker);
	Require(task_count_position != std::string::npos &&
	            std::stoull(boundary_metrics.substr(task_count_position + task_count_marker.size())) == 2 &&
	            task_rows_position != std::string::npos &&
	            std::stoull(boundary_metrics.substr(task_rows_position + task_rows_marker.size())) == 2048,
	        "value projection reaches one 2048-row axis task and one remaining row: " + boundary_metrics);
	const auto map_boundary = Query(connection,
	    "SELECT count(sampling.variables['small-boundary-axis/temperature'].status)::UBIGINT "
	    "FROM read_om_regions(" + SqlLiteral(boundary_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText("
	    "'POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['sample','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], method:='nearest')");
	Require(map_boundary->GetValue(0, 0).GetValue<std::uint64_t>() == 2049,
	        "status-map projection emits every row when worker scratch forces smaller batches");
	const auto map_boundary_metrics = LastScanMetrics(connection);
	const auto map_task_position = map_boundary_metrics.find(task_count_marker);
	const auto map_task_rows_position = map_boundary_metrics.find(task_rows_marker);
	Require(map_task_position != std::string::npos &&
	            std::stoull(map_boundary_metrics.substr(map_task_position + task_count_marker.size())) == 2 &&
	            map_task_rows_position != std::string::npos &&
	            std::stoull(map_boundary_metrics.substr(map_task_rows_position + task_rows_marker.size())) == 2048,
	        "status-map projection reaches the 2048-row cap when its scratch estimate permits it");
	const auto map_scratch_marker = std::string("\"coalescing\":{\"peak_owned_capacity_bytes\":");
	const auto map_scratch_position = map_boundary_metrics.find(map_scratch_marker);
	Require(map_scratch_position != std::string::npos &&
	            std::stoull(map_boundary_metrics.substr(map_scratch_position + map_scratch_marker.size())) <= 512 * 1024,
	        "dynamically shrunk status-map batches keep worker scratch within 512 KiB");

	const auto many = Query(
	    connection,
	    "SELECT count(*)::UBIGINT, min(list_extract(sampling.axis_indices, 1))::UBIGINT, "
	    "max(list_extract(sampling.axis_indices, 1))::UBIGINT, "
	    "sum(\"small-many-axis/temperature\")::BIGINT FROM "
	    "read_om_regions(" + SqlLiteral(many_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['sample','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], method:='nearest')");
	Require(many->RowCount() == 1 && many->GetValue(0, 0).GetValue<std::uint64_t>() == 513 &&
	            many->GetValue(1, 0).GetValue<std::uint64_t>() == 0 &&
	            many->GetValue(2, 0).GetValue<std::uint64_t>() == 512 &&
	            many->GetValue(3, 0).GetValue<std::int64_t>() == 6156,
	        "nearest scan must continue across bounded tasks until every non-spatial row is returned");
	const auto bounded_task_metrics = LastScanMetrics(connection);
	Require(bounded_task_metrics.find("\"count_unit\":\"region_axis_result\"") != std::string::npos &&
	            bounded_task_metrics.find("\"result_rows_exact\":513") != std::string::npos &&
	            bounded_task_metrics.find("\"result_count_complete\":true") != std::string::npos &&
	            bounded_task_metrics.find("\"exact_candidate_records\":513") != std::string::npos &&
	            bounded_task_metrics.find("\"count_complete\":true") != std::string::npos &&
	            bounded_task_metrics.find("\"matched_pairs_exact\":null") != std::string::npos &&
	            bounded_task_metrics.find("\"applicable\":false") != std::string::npos &&
	            bounded_task_metrics.find("\"coalescing\":{\"peak_owned_capacity_bytes\":") != std::string::npos,
	        "nearest metrics distinguish completed region-axis rows from inapplicable points pair counts");
	const auto scratch_peak_start = bounded_task_metrics.find("\"coalescing\":{\"peak_owned_capacity_bytes\":");
	Require(scratch_peak_start != std::string::npos, "nearest scratch peak metric must be present");
	const auto scratch_peak_begin = scratch_peak_start + std::string("\"coalescing\":{\"peak_owned_capacity_bytes\":").size();
	const auto scratch_peak = std::stoull(bounded_task_metrics.substr(scratch_peak_begin));
	Require(scratch_peak <= 512 * 1024, "nearest bounded task scratch must remain within 512 KiB");
	const auto one_batch = Query(connection,
	    "SELECT sampling.axis_indices FROM read_om_regions(" + SqlLiteral(many_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['sample','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], method:='nearest') LIMIT 1");
	Require(one_batch->RowCount() == 1,
	        "nearest early stop returns one visible result row despite unordered task scheduling");
	const auto early_metrics = LastScanMetrics(connection);
	Require(early_metrics.find("\"scan_complete\":false") != std::string::npos &&
	            early_metrics.find("\"result_rows_exact\":null") != std::string::npos &&
	            early_metrics.find("\"result_count_complete\":false") != std::string::npos &&
	            early_metrics.find("\"exact_candidate_records\":null") != std::string::npos &&
	            early_metrics.find("\"count_complete\":false") != std::string::npos,
	        "nearest LIMIT must not publish incomplete axis result counts as exact");
	const auto recovered_after_early_stop = Query(connection, "SELECT sum(\"small-many-axis/temperature\")::BIGINT FROM "
	    "read_om_regions(" + SqlLiteral(many_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['sample','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], method:='nearest')");
	Require(recovered_after_early_stop->GetValue(0, 0).GetValue<std::int64_t>() == 6156,
	        "full nearest scan recovers on the same connection after LIMIT early termination");

	const auto worker_regions =
	    "regions:=[{'id':'A','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))},"
	    "{'id':'B','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))},"
	    "{'id':'C','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))},"
	    "{'id':'D','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}]";
	std::string worker_reference;
	for (const auto workers : {1, 2, 4}) {
		Query(connection, "SET threads=4");
		Query(connection, "SET duckomo_max_threads=" + std::to_string(workers));
		const auto worker_result = Query(connection,
		    "SELECT md5(string_agg(region_id || ':' || sampling.axis_indices::VARCHAR || ':' || "
		    "sampling.source_point_index::VARCHAR || ':' || \"small-boundary-axis/temperature\"::VARCHAR, "
		    "'\\n' ORDER BY region_id, sampling.axis_indices)), count(*)::UBIGINT, "
		    "count(DISTINCT sampling.source_point_index)::UBIGINT FROM read_om_regions(" + SqlLiteral(boundary_file) + ", " +
		    worker_regions + ", region_crs:='grid_lonlat', dimensions:=['sample','lat','lon'], "
		    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
		    "spatial_axes:=['lat','lon'], method:='nearest')");
		Require(worker_result->GetValue(0, 0).GetValue<std::string>().size() == 32 &&
		            worker_result->GetValue(1, 0).GetValue<std::uint64_t>() == 8196 &&
		            worker_result->GetValue(2, 0).GetValue<std::uint64_t>() == 1,
		        "nearest multiworker query returns every shared-source region-axis row");
		const auto fingerprint = worker_result->GetValue(0, 0).GetValue<std::string>();
		if (worker_reference.empty()) worker_reference = fingerprint;
		else Require(fingerprint == worker_reference, "nearest 1/2/4-worker complete result multisets must match");
		const auto worker_metrics = LastScanMetrics(connection);
		const auto marker = std::string("\"max_active_workers\":");
		const auto marker_position = worker_metrics.find(marker);
		Require(marker_position != std::string::npos, "nearest metrics report active worker high-water mark");
		const auto active_workers = std::stoull(worker_metrics.substr(marker_position + marker.size()));
		Require(active_workers >= static_cast<std::uint64_t>(workers),
		        "nearest task batches must permit the configured worker count");
	}

	const auto reverse_scan =
	    "read_om_regions(" + SqlLiteral(reverse_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['member','time','level','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':2.0,'lon0':2.0,'dlat':-1.0,'dlon':-1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], axes:={'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00',"
	    "'step':INTERVAL '1 hour'},'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
	    "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
	    "method:='nearest', include_source:=true)";
	const auto reverse = Query(
	    connection,
	    "SELECT sampling.axis_indices, \"small-reverse-both/temperature\", sampling.source_point_index, "
	    "om_source.logical_index FROM read_om_regions(" + SqlLiteral(reverse_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['member','time','level','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':2.0,'lon0':2.0,'dlat':-1.0,'dlon':-1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], axes:={"
	    "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	    "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
	    "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
	    "method:='nearest', include_source:=true) WHERE sampling.axis_indices=[1,2,1]");
	Require(reverse->RowCount() == 1 && reverse->GetValue(0, 0).ToString() == "[1, 2, 1]" &&
	            reverse->GetValue(1, 0).GetValue<float>() == 211012.0F &&
	            reverse->GetValue(2, 0).GetValue<std::uint64_t>() == 12 &&
	            reverse->GetValue(3, 0).GetValue<std::uint64_t>() == 287,
	        "reverse latitude and longitude steps must preserve source identity and direct value");
	CheckOfficialCsvParity(connection, reverse_scan, "\"small-reverse-both/temperature\"",
	                       SamplingFixture("small-reverse-both.temperature.csv").string(),
	                       [](const std::vector<std::uint64_t> &axes) {
	                       return ((axes.at(0) * 3 + axes.at(1)) * 2 + axes.at(2)) * 25 + 12;
	                       });

	const auto flat_scan_sql = [&](const std::string &path, const std::string &order) {
		return "read_om_regions(" + SqlLiteral(path) + ", "
		       "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
		       "region_crs:='grid_lonlat', dimensions:=['member','time','level','point'], "
		       "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':" +
		           SqlLiteral(order) + "}, spatial_axes:=['point'], axes:={"
		       "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
		       "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
		       "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
		       "method:='nearest', include_source:=true)";
	};
	const auto flat_lon_scan = flat_scan_sql(flat_lon_file, "lon_fastest");
	const auto flat_lon_rows = Query(connection,
	    "SELECT count(*)::UBIGINT, count(DISTINCT sampling.source_point_index)::UBIGINT, "
	    "min(\"small-flat-lon/temperature\")::FLOAT, max(\"small-flat-lon/temperature\")::FLOAT FROM " + flat_lon_scan);
	Require(flat_lon_rows->GetValue(0, 0).GetValue<std::uint64_t>() == 12 &&
	            flat_lon_rows->GetValue(1, 0).GetValue<std::uint64_t>() == 1 &&
	            flat_lon_rows->GetValue(2, 0).GetValue<float>() == 12.0F &&
	            flat_lon_rows->GetValue(3, 0).GetValue<float>() == 211012.0F,
	        "longitude-fastest flattened scan must preserve independent source positions");
	CheckOfficialCsvParity(connection, flat_lon_scan, "\"small-flat-lon/temperature\"",
	                       SamplingFixture("small-flat-lon.temperature.csv").string(),
	                       [](const std::vector<std::uint64_t> &axes) {
	                       return ((axes.at(0) * 3 + axes.at(1)) * 2 + axes.at(2)) * 25 + 12;
	                       });

	const auto reverse_axis_scan = [&](const std::string &path, double lat0, double lon0,
	                                   double dlat, double dlon, const std::string &column) {
		return "read_om_regions(" + SqlLiteral(path) + ", "
		       "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
		       "region_crs:='grid_lonlat', dimensions:=['member','time','level','lat','lon'], grid:={"
		       "'nx':5,'ny':5,'lat0':" + std::to_string(lat0) + ",'lon0':" + std::to_string(lon0) +
		       ",'dlat':" + std::to_string(dlat) + ",'dlon':" + std::to_string(dlon) +
		       ",'order':'separate'}, spatial_axes:=['lat','lon'], axes:={"
		       "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
		       "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
		       "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
		       "method:='nearest', include_source:=true)";
	};
	for (const auto &profile : std::vector<std::tuple<std::string, std::string, double, double, double, double>>{
	         {"small-reverse-lat", reverse_lat_file, 2.0, -2.0, -1.0, 1.0},
	         {"small-reverse-lon", reverse_lon_file, -2.0, 2.0, 1.0, -1.0}}) {
		const auto &name = std::get<0>(profile);
		const auto &path = std::get<1>(profile);
		const auto scan_profile = reverse_axis_scan(path, std::get<2>(profile), std::get<3>(profile),
		                                            std::get<4>(profile), std::get<5>(profile), name);
		CheckOfficialCsvParity(connection, scan_profile, "\"" + name + "/temperature\"",
		                       SamplingFixture(name + ".temperature.csv").string(),
		                       [](const std::vector<std::uint64_t> &axes) {
		                       return ((axes.at(0) * 3 + axes.at(1)) * 2 + axes.at(2)) * 25 + 12;
		                       });
	}

	const auto flat_lat_scan =
	    "read_om_regions(" + SqlLiteral(flat_lat_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['member','time','level','point'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'lat_fastest'}, "
	    "spatial_axes:=['point'], axes:={"
	    "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	    "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
	    "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
	    "method:='nearest', include_source:=true)";
	const auto flat_lat = Query(connection,
	    "SELECT count(*)::UBIGINT, count(DISTINCT sampling.source_point_index)::UBIGINT, "
	    "min(\"small-flat-lat/temperature\")::FLOAT, max(\"small-flat-lat/temperature\")::FLOAT FROM " + flat_lat_scan);
	Require(flat_lat->GetValue(0, 0).GetValue<std::uint64_t>() == 12 &&
	            flat_lat->GetValue(1, 0).GetValue<std::uint64_t>() == 1 &&
	            flat_lat->GetValue(2, 0).GetValue<float>() == 12.0F &&
	            flat_lat->GetValue(3, 0).GetValue<float>() == 211012.0F,
	        "latitude-fastest flattened storage must preserve independent source point indices");
	CheckOfficialCsvParity(connection, flat_lat_scan, "\"small-flat-lat/temperature\"",
	                       SamplingFixture("small-flat-lat.temperature.csv").string(),
	                       [](const std::vector<std::uint64_t> &axes) {
	                       return ((axes.at(0) * 3 + axes.at(1)) * 2 + axes.at(2)) * 25 + 12;
	                       });

	const auto unmapped_scan =
	    "read_om_regions(" + SqlLiteral(unmapped_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['other','lat','member','lon','time'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], axes:={'member':{'axis':'member','values':[10::BIGINT,20::BIGINT]}}, "
	    "method:='nearest', include_source:=true)";
	const auto unmapped = Query(connection,
	    "SELECT sampling.axis_indices, \"small-unmapped/temperature\", om_source.logical_index FROM " + unmapped_scan +
	    " WHERE sampling.axis_indices=[1,1,2]");
	Require(unmapped->RowCount() == 1 && unmapped->GetValue(0, 0).ToString() == "[1, 1, 2]" &&
	            unmapped->GetValue(1, 0).GetValue<float>() == 210012.0F &&
	            unmapped->GetValue(2, 0).GetValue<std::uint64_t>() == 233,
	        "unmapped source axes must remain in original axis positions and logical index");
	CheckOfficialCsvParity(connection, unmapped_scan, "\"small-unmapped/temperature\"",
	                       SamplingFixture("small-unmapped.temperature.csv").string(),
	                       [](const std::vector<std::uint64_t> &axes) {
	                       return (((axes.at(0) * 5 + 2) * 2 + axes.at(1)) * 5 + 2) * 3 + axes.at(2);
	                       }, 12, 12);

	const auto spatial_only_scan =
	    "read_om_regions(" + SqlLiteral(spatial_only_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((-0.9 -0.9,-0.8 -0.9,-0.8 -0.8,-0.9 -0.8,-0.9 -0.9))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['lat','lon'], "
	    "grid:={'nx':2,'ny':2,'lat0':-1.0,'lon0':-1.0,'dlat':2.0,'dlon':2.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], method:='nearest', include_source:=true)";
	const auto spatial_only = Query(connection,
	    "SELECT count(*)::UBIGINT, first(sampling.axis_indices), min(\"small-spatial-only/temperature\")::FLOAT, "
	    "min(sampling.source_point_index)::UBIGINT FROM " + spatial_only_scan);
	Require(spatial_only->GetValue(0, 0).GetValue<std::uint64_t>() == 1 &&
	            spatial_only->GetValue(1, 0).ToString() == "[]" &&
	            spatial_only->GetValue(2, 0).GetValue<float>() == 1.0F &&
	            spatial_only->GetValue(3, 0).GetValue<std::uint64_t>() == 0,
	        "a spatial-only array must emit one row with empty axis indices");
	CheckOfficialCsvParity(connection, spatial_only_scan, "\"small-spatial-only/temperature\"",
	                       SamplingFixture("small-spatial-only.temperature.csv").string(),
	                       [](const std::vector<std::uint64_t> &) { return 0; }, 1, 0);
}
} // namespace

int main(int argc, char **argv) {
	try {
		TestRegionSamplingOptions();
		TestNonSpatialLogicalMapping();
		std::optional<std::uint32_t> memory_dimension;
		if (argc >= 3) {
			const std::string option(argv[2]);
			const std::string prefix = "--memory-dimension=";
			Require(option.rfind(prefix, 0) == 0, "unknown region_sampling_test option: " + option);
			const auto dimension = static_cast<std::uint32_t>(std::stoul(option.substr(prefix.size())));
			Require(dimension == 64 || dimension == 256, "memory dimension must be 64 or 256");
			memory_dimension = dimension;
		}
		const auto *spatial_path = argc >= 2 ? argv[1] : std::getenv("DUCKOMO_SPATIAL_EXTENSION");
		if (spatial_path != nullptr && spatial_path[0] != '\0') {
			DBConfig config;
			config.SetOptionByName("allow_unsigned_extensions", true);
			config.options.maximum_threads = 4;
			DuckDB database(nullptr, &config);
	DUCKOMO_TEST_LOAD_EXTENSIONS(database);
			database.LoadStaticExtension<CoreFunctionsExtension>();
			Connection connection(database);
			Query(connection, "LOAD " + SqlLiteral(spatial_path));
			if (memory_dimension.has_value()) {
				TestNearestMemoryScaling(connection, memory_dimension);
				std::cout << "region_sampling_test: ok\n";
				return 0;
			}
			TestNearestProjectionDependencies(connection);
			TestNearestMemoryScaling(connection);
			TestNearestCancellationAndRecovery(connection);
			TestNearestReadFailuresAreErrorsAndRecover(connection);
			TestNearestStatusAndNonfiniteValues(connection);
			TestNearestScanParity(connection);
		}
		std::cout << "region_sampling_test: ok\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "region_sampling_test: " << error.what() << '\n';
		return 1;
	}
}
