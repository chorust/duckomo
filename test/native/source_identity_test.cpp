#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include "core_functions_extension.hpp"
#include "query_result_compat.hpp"
#include "duckomo_extension.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace duckdb {
#if DUCKOMO_DUCKDB_API_GENERATION < 2
void ExtensionHelper::LoadAllExtensions(DuckDB &database) {
	database.LoadStaticExtension<DuckomoExtension>();
}
#endif
} // namespace duckdb

namespace {
using namespace duckdb;
using duckomo_test::Query;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

std::string Scalar(Connection &connection, const std::string &sql) {
	auto result = Query(connection, sql);
	Require(result->RowCount() == 1 && result->ColumnCount() == 1, "expected one scalar result: " + sql);
	return result->GetValue(0, 0).ToString();
}

std::string SeparatedRead() {
	return "read_om('test/data/raw.om', dimensions := map(['value'], [['row','column']]), "
	       "grid := {'version':1,'type':'rotated_latlon','numeric_policy':'float64_v1', "
	       "'earth':{'model':'sphere','radius_m':6371229.0}, "
	       "'layout':{'nx':3,'ny':2,'order':'separate'}, "
	       "'parameters':{'x0':0.0,'y0':0.0,'dx':1.0,'dy':1.0,"
	       "'north_pole_latitude':90.0,'north_pole_longitude':0.0,'rotation':0.0}}, "
	       "spatial_axes := ['row','column'], include_source := true)";
}

std::string FlatRead(const std::string &path, const std::string &variable, const std::string &order) {
	return "read_om('test/data/grids/" + path + "', dimensions := map(['" + variable + "'], [['point']]), "
	       "grid := {'version':1,'type':'rotated_latlon','numeric_policy':'float64_v1', "
	       "'earth':{'model':'sphere','radius_m':6371229.0}, "
	       "'layout':{'nx':4,'ny':3,'order':'" + order + "'}, "
	       "'parameters':{'x0':0.0,'y0':0.0,'dx':1.0,'dy':1.0,"
	       "'north_pole_latitude':90.0,'north_pole_longitude':0.0,'rotation':0.0}}, "
	       "spatial_axes := ['point'], include_source := true)";
}

std::string GaussianRegionRead() {
	return "read_om('test/data/grids/x-fastest.om', "
	       "dimensions := map(['flattened_x_fastest/value'], [['point']]), "
	       "grid := {'version':1,'type':'reduced_gaussian','numeric_policy':'float64_v1', "
	       "'earth':{'model':'wgs84','semi_major_m':6378137.0,'inverse_flattening':298.257223563}, "
	       "'layout':{'order':'row_major'}, 'parameters':{'n':1,'latitude_rule':'explicit_v1',"
	       "'rows':[{'latitude':60.0,'point_count':6,'longitude_origin':0.0,'longitude_step':60.0},"
	       "{'latitude':-60.0,'point_count':6,'longitude_origin':0.0,'longitude_step':60.0}],"
	       "'subset_segments':[{'parent_row':0,'parent_begin':3,'count':3},"
	       "{'parent_row':1,'parent_begin':0,'count':6},{'parent_row':0,'parent_begin':0,'count':3}]}}, "
	       "spatial_axes := ['point'], include_source := true)";
}

std::string InterleavedRead() {
	return "read_om('test/data/grids/interleaved.om', dimensions := map("
	       "['interleaved/temperature','interleaved/humidity'], "
	       "[['time','latitude_axis','level','longitude_axis','lead_time','member','run'], "
	       "['time','latitude_axis','level','longitude_axis','lead_time','member','run']]), "
	       "grid := {'version':1,'type':'rotated_latlon','numeric_policy':'float64_v1', "
	       "'earth':{'model':'sphere','radius_m':6371229.0}, "
	       "'layout':{'nx':4,'ny':3,'order':'separate'}, "
	       "'parameters':{'x0':0.0,'y0':0.0,'dx':1.0,'dy':1.0,"
	       "'north_pole_latitude':90.0,'north_pole_longitude':0.0,'rotation':0.0}}, "
	       "spatial_axes := ['latitude_axis','longitude_axis'], include_source := true)";
}

void RequireNoValueDecoder(Connection &connection) {
	const auto metrics = Scalar(connection, "SELECT metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1");
	Require(metrics.find("\"schema_version\":4") != std::string::npos &&
	            metrics.find("\"value_totals\":{\"index_bytes\":0,\"index_requests\":0,\"data_bytes\":0,"
	                         "\"data_requests\":0,\"decoded_chunks\":0") != std::string::npos,
	        "source-only identity projection must keep all value index/data/decode costs at zero");
}

void TestSeparateSourceIdentity(Connection &connection) {
	auto result = Query(connection,
	                    "SELECT om_source.object_id, om_source.object_version, om_source.version_strength, "
	                    "om_source.content_verified, om_source.grid_id, om_source.layout_id, "
	                    "om_source.logical_index, om_source.point_index, om_source.parent_point_index, "
	                    "om_source.axis_indices[1], om_source.axis_indices[2] FROM " + SeparatedRead() +
	                    " ORDER BY om_source.logical_index");
	Require(result->RowCount() == 6 && result->ColumnCount() == 11,
	        "separate source identity returns one record for each source logical position");
	std::string object_id, grid_id, layout_id;
	for (idx_t row = 0; row < result->RowCount(); row++) {
		const auto source_id = result->GetValue(0, row).GetValue<std::string>();
		if (row == 0) {
			object_id = source_id;
			grid_id = result->GetValue(4, row).GetValue<std::string>();
			layout_id = result->GetValue(5, row).GetValue<std::string>();
		} else {
			Require(source_id == object_id, "one local object has a stable opaque object identity across rows");
			Require(result->GetValue(4, row).GetValue<std::string>() == grid_id &&
				        result->GetValue(5, row).GetValue<std::string>() == layout_id,
			        "canonical grid and layout identities stay stable across rows");
		}
		Require(source_id.rfind("sha256:", 0) == 0 && result->GetValue(1, row).IsNull() &&
			        result->GetValue(2, row).GetValue<std::string>() == "unverifiable" &&
			        !result->GetValue(3, row).GetValue<bool>(),
		        "unversioned local input remains opaque and is never labeled content-verified");
		const auto logical = static_cast<std::uint64_t>(row);
		Require(result->GetValue(6, row).GetValue<std::uint64_t>() == logical &&
			        result->GetValue(7, row).GetValue<std::uint64_t>() == logical &&
			        result->GetValue(8, row).GetValue<std::uint64_t>() == logical &&
			        result->GetValue(9, row).GetValue<std::uint64_t>() == logical / 3 &&
			        result->GetValue(10, row).GetValue<std::uint64_t>() == logical % 3,
		        "separate-axis identity preserves row-major logical, local, parent, and axis positions");
	}
	RequireNoValueDecoder(connection);

	result = Query(connection,
	               "SELECT om_source.logical_index, om_source.point_index FROM " + SeparatedRead() +
	               " WHERE om_source.logical_index > 2 ORDER BY om_source.logical_index DESC");
	Require(result->RowCount() == 3, "filtered source rows retain the original logical positions; rows=" +
	                                          std::to_string(result->RowCount()));
	for (idx_t row = 0; row < result->RowCount(); row++) {
		const auto expected = static_cast<std::uint64_t>(5 - row);
		Require(result->GetValue(0, row).GetValue<std::uint64_t>() == expected &&
			        result->GetValue(1, row).GetValue<std::uint64_t>() == expected,
		        "sort order never renumbers source identity by output position");
	}
	RequireNoValueDecoder(connection);
}

void TestFlattenedAndGaussianIdentity(Connection &connection) {
	for (const auto &layout : {std::pair<std::string, std::string>{"x-fastest.om", "x_fastest"},
	                           {"y-fastest.om", "y_fastest"}}) {
		const auto variable = layout.first == "x-fastest.om" ? "flattened_x_fastest/value" : "flattened_y_fastest/value";
		auto result = Query(connection,
		                    "SELECT om_source.logical_index, om_source.point_index, om_source.parent_point_index, "
		                    "om_source.axis_indices[1] FROM " + FlatRead(layout.first, variable, layout.second) +
		                    " ORDER BY om_source.logical_index");
		Require(result->RowCount() == 12, "flattened source layout exposes all twelve original positions");
		for (idx_t row = 0; row < result->RowCount(); row++) {
			const auto logical = static_cast<std::uint64_t>(row);
			Require(result->GetValue(0, row).GetValue<std::uint64_t>() == logical &&
				        result->GetValue(1, row).GetValue<std::uint64_t>() == logical &&
				        result->GetValue(2, row).GetValue<std::uint64_t>() == logical &&
				        result->GetValue(3, row).GetValue<std::uint64_t>() == logical,
			        "flattened x/y storage order retains its original point-axis position");
		}
		RequireNoValueDecoder(connection);
	}

	auto result = Query(connection,
	                    "SELECT om_source.logical_index, om_source.point_index, om_source.parent_point_index "
	                    "FROM " + GaussianRegionRead() + " ORDER BY om_source.logical_index");
	const std::vector<std::uint64_t> parent_positions{3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1, 2};
	Require(result->RowCount() == parent_positions.size(), "Gaussian region emits every local point once");
	for (idx_t row = 0; row < result->RowCount(); row++) {
		Require(result->GetValue(0, row).GetValue<std::uint64_t>() == row &&
			        result->GetValue(1, row).GetValue<std::uint64_t>() == row &&
			        result->GetValue(2, row).GetValue<std::uint64_t>() == parent_positions[row],
		        "Gaussian source identity preserves local order and the declared parent mapping");
	}
	RequireNoValueDecoder(connection);
}

void TestInterleavedAxesAndParallelIdentity(Connection &connection) {
	Query(connection, "SET threads=4");
	Query(connection, "SET duckomo_max_threads=4");
	auto result = Query(connection,
	                    "SELECT om_source.logical_index, om_source.axis_indices[1], om_source.axis_indices[2], "
	                    "om_source.axis_indices[3], om_source.axis_indices[4], om_source.axis_indices[5], "
	                    "om_source.axis_indices[6], om_source.axis_indices[7] FROM " + InterleavedRead() +
	                    " WHERE lat BETWEEN 0.99 AND 1.01 AND lon BETWEEN 1.99 AND 2.01 "
	                    "ORDER BY om_source.logical_index DESC");
	Require(result->RowCount() == 32, "the same geographic point retains all non-spatial records");
	std::vector<std::uint64_t> positions;
	for (idx_t row = 0; row < result->RowCount(); row++) {
		const auto logical = result->GetValue(0, row).GetValue<std::uint64_t>();
		const auto time = result->GetValue(1, row).GetValue<std::uint64_t>();
		const auto latitude = result->GetValue(2, row).GetValue<std::uint64_t>();
		const auto level = result->GetValue(3, row).GetValue<std::uint64_t>();
		const auto longitude = result->GetValue(4, row).GetValue<std::uint64_t>();
		const auto lead = result->GetValue(5, row).GetValue<std::uint64_t>();
		const auto member = result->GetValue(6, row).GetValue<std::uint64_t>();
		const auto run = result->GetValue(7, row).GetValue<std::uint64_t>();
		const auto reconstructed = ((((((time * 3 + latitude) * 2 + level) * 4 + longitude) * 2 + lead) * 2 + member) * 2 + run);
		Require(logical == reconstructed && latitude == 1 && longitude == 2,
		        "interleaved source axes reconstruct the original row-major logical position");
		if (row > 0) {
			Require(positions.back() > logical, "parallel results ordered by source identity keep their source positions");
		}
		positions.push_back(logical);
	}
	RequireNoValueDecoder(connection);
}

void TestLongLineParallelSourceOrder(Connection &connection) {
	const std::string query =
	    "read_om('test/data/grids/long-line.om', dimensions := map(['long_line/value'], "
	    "[['latitude_axis','longitude_axis']]), grid := {'nx':262145,'ny':1,'lat0':0.0,'lon0':-65.536,"
	    "'dlat':1.0,'dlon':0.0005,'order':'separate'}, spatial_axes := ['latitude_axis','longitude_axis'], "
	    "include_source := true)";
	const auto summary = Query(connection,
	                           "SELECT count(*)::UBIGINT, count(*) FILTER (WHERE om_source.logical_index = "
	                           "om_source.axis_indices[2])::UBIGINT, min(om_source.logical_index), "
	                           "max(om_source.logical_index), sum(om_source.logical_index) FROM " + query);
	const std::uint64_t count = 262145;
	const auto expected_sum = count * (count - 1) / 2;
	Require(summary->GetValue(0, 0).GetValue<std::uint64_t>() == count &&
	            summary->GetValue(1, 0).GetValue<std::uint64_t>() == count &&
	            summary->GetValue(2, 0).GetValue<std::uint64_t>() == 0 &&
	            summary->GetValue(3, 0).GetValue<std::uint64_t>() == count - 1 &&
	            summary->GetValue(4, 0).GetValue<std::uint64_t>() == expected_sum,
	        "parallel long-line source mapping preserves every original logical/axis index");
	const auto active_workers = Scalar(
	    connection,
	    "SELECT regexp_extract(metrics, '\"max_active_workers\":([0-9]+)', 1)::BIGINT "
	    "FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1");
	Require(std::stoull(active_workers) >= 2, "long-line source identities are produced by concurrent scan workers");
	RequireNoValueDecoder(connection);
}

} // namespace

int main() {
	try {
		DBConfig config;
		config.SetOptionByName("allow_unsigned_extensions", true);
		DuckDB database(nullptr, &config);
		#if DUCKOMO_DUCKDB_API_GENERATION >= 2
		database.LoadStaticExtension<DuckomoExtension>();
		#endif
		database.LoadStaticExtension<CoreFunctionsExtension>();
		Connection connection(database);
		TestSeparateSourceIdentity(connection);
		TestFlattenedAndGaussianIdentity(connection);
		TestInterleavedAxesAndParallelIdentity(connection);
		TestLongLineParallelSourceOrder(connection);
		std::cout << "source identity checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "source identity checks failed: " << error.what() << '\n';
		return 1;
	}
}
