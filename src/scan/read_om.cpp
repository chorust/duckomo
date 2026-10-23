#include "duckomo/read_om.hpp"
#include "duckomo/region_geometry.hpp"
#include "duckomo/region_input.hpp"
#include "duckomo/region_sampling.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <limits>
#include <mutex>
#include <optional>
#include <sys/resource.h>
#include <sstream>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "duckdb/common/exception.hpp"
#include "duckdb/common/exception/binder_exception.hpp"
#include "duckdb/common/error_data.hpp"
#include "duckdb/common/constants.hpp"
#include "duckdb/common/table_column.hpp"
#include "duckdb/common/types/validity_mask.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/main/client_context_state.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/planner/column_binding.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckomo/batch.hpp"
#include "duckomo/build_identity.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include "duckomo/axis_filter.hpp"
#include "duckomo/axis_selection.hpp"
#include "duckomo/domain_bbox.hpp"
#include "duckomo/dimensions.hpp"
#include "duckomo/domain_registry.hpp"
#include "duckomo/gaussian_grid.hpp"
#include "duckomo/grid_definition.hpp"
#include "duckomo/grid_info.hpp"
#include "duckomo/grid_identity.hpp"
#include "duckomo/grid_selection.hpp"
#include "duckomo/local_file.hpp"
#include "duckomo/metrics.hpp"
#include "duckomo/metadata.hpp"
#include "duckomo/projection.hpp"
#include "duckomo/remote_file.hpp"
#include "duckomo/reader.hpp"
#include "duckomo/regular_grid.hpp"
#include "duckomo/projected_grid.hpp"
#include "duckomo/schema.hpp"
#include "duckomo/semantic_axes.hpp"
#include "duckomo/spatial_filter.hpp"
#include "duckomo/spatial_layout.hpp"
#include "duckomo/spatial_selection.hpp"
#include "mbedtls_wrapper.hpp"

namespace duckdb {
namespace duckomo {

namespace {

constexpr idx_t SCAN_TASK_POSITION_WINDOW = 65536;
constexpr std::uint64_t MAX_REGION_GEOMETRY_SCRATCH_BYTES = 64 * 1024;

std::unique_ptr<ReadAtFile> OpenOmReadAt(ClientContext &context, const std::string &path,
                                         const std::shared_ptr<RemoteReadSession> &remote_session,
                                         const std::shared_ptr<ScanMetrics> &metrics,
                                         ScanMetadataStage stage) {
	if (remote_session) return remote_session->Open(context, stage);
	return std::make_unique<LocalFile>(LocalFile::Open(context, path, metrics, stage));
}

std::string EnvironmentValue(const char *name) {
	const auto *value = std::getenv(name);
	return value == nullptr ? std::string() : std::string(value);
}

void WriteMetricsSidecar(const ScanMetrics &metrics, const std::string &path) noexcept;
void WriteMetricsV3Sidecar(const std::vector<std::shared_ptr<ScanMetrics>> &metrics,
	                       const std::string &path) noexcept;
void WriteMetricsV4Sidecar(const std::vector<std::shared_ptr<ScanMetrics>> &metrics,
	                       const std::string &path) noexcept;

class DuckomoSessionState final : public ClientContextState {
public:
	void BeginQuery(const std::string &query_id) {
		std::lock_guard<std::mutex> guard(mutex_);
		if (active_query_id_ != query_id) {
			active_query_id_ = query_id;
			pending_scans_.clear();
		}
	}

	void Publish(const std::shared_ptr<ScanMetrics> &metrics) {
		const auto snapshot = metrics->Snapshot();
		std::lock_guard<std::mutex> guard(mutex_);
		if (snapshot.query_id != active_query_id_) return;
		pending_scans_.push_back({snapshot.scan_id, snapshot.query_id, metrics->ToMetricsV4Json()});
		std::sort(pending_scans_.begin(), pending_scans_.end(),
		          [](const PublishedScan &left, const PublishedScan &right) { return left.scan_id < right.scan_id; });
		last_scans_ = pending_scans_;
		last_query_id_ = active_query_id_;
	}

	struct PublishedScan final {
		std::uint64_t scan_id;
		std::string query_id;
		std::string metrics;
	};

	std::vector<PublishedScan> LastScans() const {
		std::lock_guard<std::mutex> guard(mutex_);
		return last_scans_;
	}


private:
	mutable std::mutex mutex_;
	std::string active_query_id_;
	std::string last_query_id_;
	std::vector<PublishedScan> pending_scans_;
	std::vector<PublishedScan> last_scans_;
};

shared_ptr<DuckomoSessionState> GetDuckomoSessionState(ClientContext &context) {
	return context.registered_state->GetOrCreate<DuckomoSessionState>("duckomo.session_state");
}

// A table function's global scan state can finish before DuckDB has finished
// consuming the query (for example, when a downstream operator is interrupted
// after the final source batch). Keep a query-end observer so the sidecar and
// session profile describe the complete SQL query rather than only the scan.
class ScanMetricsQueryState final : public ClientContextState {
public:
	ScanMetricsQueryState(std::string state_key_p, std::string output_path_p, std::string output_v3_path_p,
	                      std::string output_v4_path_p,
	                      shared_ptr<DuckomoSessionState> session_p, std::string query_id_p)
	    : state_key(std::move(state_key_p)), output_path(std::move(output_path_p)),
	      output_v3_path(std::move(output_v3_path_p)), output_v4_path(std::move(output_v4_path_p)),
	      session(std::move(session_p)),
	      query_id(std::move(query_id_p)), started_at(std::chrono::steady_clock::now()) {
		session->BeginQuery(query_id);
	}

	void Add(std::shared_ptr<ScanMetrics> metrics_p) {
		std::lock_guard<std::mutex> guard(mutex);
		metrics.emplace_back(std::move(metrics_p));
	}

	std::uint64_t NextScanId() noexcept { return next_scan_id.fetch_add(1); }
	const std::string &QueryId() const noexcept { return query_id; }

	void QueryEnd(ClientContext &context, optional_ptr<ErrorData> error) override {
		std::vector<std::shared_ptr<ScanMetrics>> current_metrics;
		{
			std::lock_guard<std::mutex> guard(mutex);
			current_metrics = metrics;
		}
		const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started_at).count();
		for (const auto &current : current_metrics) {
			if (error) {
				const auto status = current->Snapshot().status;
				if (error->Type() == ExceptionType::INTERRUPT) {
					current->SetStatus(ScanStatus::Cancelled, "cancelled");
				} else if (status != ScanStatus::Failed) {
					current->SetStatus(ScanStatus::Failed, "query_error");
				}
			} else {
				// Query completion is authoritative: a valid LIMIT can stop the
				// physical scan before the selection cursor reaches EOF.
				current->SetStatus(ScanStatus::Succeeded);
			}
			current->SetElapsedMilliseconds(elapsed);
			current->FinalizeQueryMemoryAccounting(!error);
			rusage usage{};
			if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
				current->SetPeakRssBytes(static_cast<std::uint64_t>(usage.ru_maxrss));
#else
				current->SetPeakRssBytes(static_cast<std::uint64_t>(usage.ru_maxrss) * 1024);
#endif
			}
			current->PublishQueryTerminal();
			session->Publish(current);
		}
		if (!current_metrics.empty()) {
			WriteMetricsSidecar(*current_metrics.back(), output_path);
			WriteMetricsV3Sidecar(current_metrics, output_v3_path);
			WriteMetricsV4Sidecar(current_metrics, output_v4_path);
		}
		context.registered_state->Remove(state_key);
	}

private:
	std::string state_key;
	std::string output_path;
	std::string output_v3_path;
	std::string output_v4_path;
	shared_ptr<DuckomoSessionState> session;
	std::string query_id;
	std::chrono::steady_clock::time_point started_at;
	std::atomic<std::uint64_t> next_scan_id{0};
	std::mutex mutex;
	std::vector<std::shared_ptr<ScanMetrics>> metrics;
};

shared_ptr<ScanMetricsQueryState> GetScanMetricsQueryState(ClientContext &context) {
	static std::atomic<std::uint64_t> next_query_id{0};
	const auto query_text = context.GetCurrentQuery();
	const auto state_key = "duckomo.scan_metrics_query." + std::to_string(std::hash<std::string>{}(query_text));
	auto state = context.registered_state->Get<ScanMetricsQueryState>(state_key);
	if (state) return state;
	const auto query_id = std::to_string(static_cast<unsigned long long>(context.GetConnectionId())) + "-" +
	                      std::to_string(static_cast<unsigned long long>(next_query_id.fetch_add(1)));
	state = make_shared_ptr<ScanMetricsQueryState>(state_key, EnvironmentValue("DUCKOMO_METRICS_OUTPUT"),
	                                               EnvironmentValue("DUCKOMO_METRICS_V3_OUTPUT"),
	                                               EnvironmentValue("DUCKOMO_METRICS_V4_OUTPUT"),
	                                               GetDuckomoSessionState(context), query_id);
	context.registered_state->Insert(state_key, state);
	return state;
}

std::shared_ptr<ScanMetrics> CreateScanMetrics(ClientContext &context) {
	auto metrics = std::make_shared<ScanMetrics>();
	metrics->EnableQueryMemoryAccounting();
	const auto query_state = GetScanMetricsQueryState(context);
	metrics->SetQueryIdentity(query_state->QueryId(), EnvironmentValue("DUCKOMO_SCENARIO"), context.GetCurrentQuery());
	metrics->SetScanId(query_state->NextScanId());
	metrics->SetFixture(EnvironmentValue("DUCKOMO_FIXTURE_ID"), EnvironmentValue("DUCKOMO_FIXTURE_SHA256"));
	const auto identity_value = [](const char *value, const char *fallback) {
		return value != nullptr && value[0] != '\0' ? std::string(value) : std::string(fallback);
	};
	metrics->SetDependencyCommit("duckdb", identity_value(BUILD_IDENTITY.duckdb_commit,
	                                                       "08e34c447bae34eaee3723cac61f2878b6bdf787"));
	metrics->SetDependencyCommit("om-file-format", identity_value(BUILD_IDENTITY.om_commit,
	                                                              "d8855e418e2231ae8439f0c7e840fa3f93b371e3"));
	metrics->SetDependencyCommit("extension-ci-tools",
	                             identity_value(BUILD_IDENTITY.extension_ci_tools_commit,
	                                            "b777c70d30942cca5bef62d6d4fa23a13362f398"));
	metrics->SetEnvironmentValue("platform", identity_value(BUILD_IDENTITY.platform, "linux"));
	metrics->SetEnvironmentValue("build", identity_value(BUILD_IDENTITY.pair_id, "release"));
	if (BUILD_IDENTITY.pair_id[0] != '\0') {
		metrics->SetEnvironmentValue("build_id", BUILD_IDENTITY.build_id);
		metrics->SetEnvironmentValue("build_pair", BUILD_IDENTITY.pair_id);
		metrics->SetEnvironmentValue("cxx_abi", BUILD_IDENTITY.cxx_abi);
	}
	metrics->SetCachePolicyValue("application_cache", "removed");
	metrics->SetCacheRemoved();
	return metrics;
}

void RegisterScanMetricsQueryState(ClientContext &context, const std::shared_ptr<ScanMetrics> &metrics) {
	GetScanMetricsQueryState(context)->Add(metrics);
}

void WriteMetricsSidecar(const ScanMetrics &metrics) noexcept {
	WriteMetricsSidecar(metrics, EnvironmentValue("DUCKOMO_METRICS_OUTPUT"));
}

void WriteMetricsSidecar(const ScanMetrics &metrics, const std::string &path) noexcept {
	if (path.empty()) {
		return;
	}
	try {
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (output) {
			output << metrics.ToEvidenceJson() << '\n';
		}
	} catch (...) {
		// Evidence sidecars are best-effort and must never change query results.
	}
}

void WriteMetricsV3Sidecar(const std::vector<std::shared_ptr<ScanMetrics>> &metrics,
	                       const std::string &path) noexcept {
	if (path.empty()) return;
	try {
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!output) return;
		output << '[';
		for (std::size_t index = 0; index < metrics.size(); index++) {
			if (index != 0) output << ',';
			output << metrics[index]->ToMetricsV3Json();
		}
		output << "]\n";
	} catch (...) {
		// Evidence sidecars are best-effort and must never change query results.
	}
}

void WriteMetricsV4Sidecar(const std::vector<std::shared_ptr<ScanMetrics>> &metrics,
	                       const std::string &path) noexcept {
	if (path.empty()) return;
	try {
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!output) return;
		output << '[';
		for (std::size_t index = 0; index < metrics.size(); index++) {
			if (index != 0) output << ',';
			output << metrics[index]->ToMetricsV4Json();
		}
		output << "]\n";
	} catch (...) {
		// Evidence sidecars are best-effort and must never change query results.
	}
}

virtual_column_map_t GetReadOmVirtualColumns(ClientContext &, optional_ptr<FunctionData>) {
	virtual_column_map_t result;
	result.emplace(COLUMN_IDENTIFIER_EMPTY, TableColumn("", LogicalType::BOOLEAN));
	return result;
}

bool SameSchema(const BoundSchema &left, const BoundSchema &right) {
	if (left.row_count != right.row_count || left.shape != right.shape || left.crs_wkt != right.crs_wkt ||
	    left.crs_profile != right.crs_profile ||
	    left.variables.size() != right.variables.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.variables.size(); index++) {
		const auto &left_variable = left.variables[index];
		const auto &right_variable = right.variables[index];
		if (left_variable.canonical_path != right_variable.canonical_path ||
		    left_variable.column_name != right_variable.column_name ||
		    left_variable.inferred_axes != right_variable.inferred_axes || left_variable.type != right_variable.type ||
		    left_variable.time != right_variable.time || left_variable.shape != right_variable.shape ||
		    left_variable.chunk_shape != right_variable.chunk_shape ||
		    left_variable.row_count != right_variable.row_count ||
		    left_variable.metadata_offset != right_variable.metadata_offset ||
		    left_variable.metadata_size != right_variable.metadata_size) {
			return false;
		}
	}
	return true;
}

std::uint64_t ReadGridExtent(const Value &value, const std::string &field) {
	if (!value.type().IsIntegral()) {
		throw BinderException("read_om grid field '" + field + "' must be an integer value");
	}
	Value converted;
	try {
		converted = value.DefaultCastAs(LogicalType::BIGINT, true);
	} catch (const std::exception &) {
		throw BinderException("read_om grid field '" + field + "' must fit in a signed 64-bit integer");
	}
	const auto signed_value = converted.GetValue<std::int64_t>();
	if (signed_value <= 0) {
		throw BinderException("read_om grid field '" + field + "' must be positive");
	}
	return static_cast<std::uint64_t>(signed_value);
}

std::uint64_t ReadGridIndex(const Value &value, const std::string &field) {
	if (!value.type().IsIntegral()) {
		throw BinderException("read_om grid field '" + field + "' must be an integer value");
	}
	Value converted;
	try {
		converted = value.DefaultCastAs(LogicalType::BIGINT, true);
	} catch (const std::exception &) {
		throw BinderException("read_om grid field '" + field + "' must fit in a signed 64-bit integer");
	}
	const auto signed_value = converted.GetValue<std::int64_t>();
	if (signed_value < 0) throw BinderException("read_om grid field '" + field + "' must be non-negative");
	return static_cast<std::uint64_t>(signed_value);
}

double ReadGridDouble(const Value &value, const std::string &field) {
	if (!value.type().IsNumeric()) {
		throw BinderException("read_om grid field '" + field + "' must be a numeric DOUBLE-compatible value");
	}
	try {
		return value.DefaultCastAs(LogicalType::DOUBLE, true).GetValue<double>();
	} catch (const std::exception &) {
		throw BinderException("read_om grid field '" + field + "' cannot be converted to DOUBLE");
	}
}

std::string ReadGridText(const Value &value, const std::string &field) {
	if (value.type().id() != LogicalTypeId::VARCHAR) {
		throw BinderException("read_om grid field '" + field + "' must be VARCHAR");
	}
	return StringValue::Get(value);
}

using GridFieldMap = std::unordered_map<std::string, const Value *>;

GridFieldMap ReadExactGridFields(const Value &value, const std::string &path,
	                             const std::vector<std::string> &expected,
	                             const std::vector<std::string> &nullable = {}) {
	if (value.IsNull() || value.type().id() != LogicalTypeId::STRUCT) {
		throw BinderException("read_om grid field '" + path + "' must be a STRUCT");
	}
	const auto &types = StructType::GetChildTypes(value.type());
	const auto &values = StructValue::GetChildren(value);
	if (types.size() != expected.size() || values.size() != expected.size()) {
		throw BinderException("read_om grid field '" + path + "' must contain exactly the declared fields");
	}
	GridFieldMap fields;
	fields.reserve(expected.size());
	for (idx_t index = 0; index < types.size(); index++) {
		const auto &name = StructType::GetChildName(value.type(), index);
		if (std::find(expected.begin(), expected.end(), name) == expected.end() ||
		    !fields.emplace(name, &values[index]).second) {
			throw BinderException("read_om grid field '" + path + "' has an unknown or duplicate field '" + name + "'");
		}
		if (values[index].IsNull() && std::find(nullable.begin(), nullable.end(), name) == nullable.end()) {
			throw BinderException("read_om grid field '" + path + "." + name + "' must not be NULL");
		}
	}
	for (const auto &name : expected) {
		if (fields.find(name) == fields.end()) {
			throw BinderException("read_om grid field '" + path + "' is missing '" + name + "'");
		}
	}
	return fields;
}

GridStorageOrder ReadVersionOneGridOrder(const Value &value) {
	const auto order = ReadGridText(value, "layout.order");
	if (order == "separate") return GridStorageOrder::Separate;
	if (order == "x_fastest") return GridStorageOrder::LongitudeFastest;
	if (order == "y_fastest") return GridStorageOrder::LatitudeFastest;
	throw BinderException("read_om grid layout.order must be separate, x_fastest, or y_fastest");
}

std::vector<GaussianRow> ReadGaussianRows(const Value &value) {
	if (value.type().id() != LogicalTypeId::LIST ||
	    ListType::GetChildType(value.type()).id() != LogicalTypeId::STRUCT) {
		throw BinderException("read_om grid parameters.rows must be a STRUCT[] list");
	}
	std::vector<GaussianRow> rows;
	const auto &values = ListValue::GetChildren(value);
	rows.reserve(values.size());
	static const std::vector<std::string> row_fields = {"latitude", "point_count", "longitude_origin", "longitude_step"};
	for (std::size_t index = 0; index < values.size(); index++) {
		const auto prefix = "parameters.rows[" + std::to_string(index) + "]";
		const auto fields = ReadExactGridFields(values[index], prefix, row_fields);
		rows.push_back({ReadGridDouble(*fields.at("latitude"), prefix + ".latitude"),
		                ReadGridExtent(*fields.at("point_count"), prefix + ".point_count"),
		                ReadGridDouble(*fields.at("longitude_origin"), prefix + ".longitude_origin"),
		                ReadGridDouble(*fields.at("longitude_step"), prefix + ".longitude_step")});
	}
	if (rows.empty()) throw BinderException("read_om grid parameters.rows must not be empty");
	return rows;
}

std::vector<GaussianRegionSegment> ReadGaussianSegments(const Value &value) {
	if (value.IsNull()) return {};
	if (value.type().id() != LogicalTypeId::LIST ||
	    ListType::GetChildType(value.type()).id() != LogicalTypeId::STRUCT) {
		throw BinderException("read_om grid parameters.subset_segments must be NULL or STRUCT[]");
	}
	const auto &values = ListValue::GetChildren(value);
	if (values.empty()) throw BinderException("read_om grid parameters.subset_segments must be NULL or non-empty");
	std::vector<GaussianRegionSegment> segments;
	segments.reserve(values.size());
	static const std::vector<std::string> segment_fields = {"parent_row", "parent_begin", "count"};
	for (std::size_t index = 0; index < values.size(); index++) {
		const auto prefix = "parameters.subset_segments[" + std::to_string(index) + "]";
		const auto fields = ReadExactGridFields(values[index], prefix, segment_fields);
		segments.push_back({ReadGridIndex(*fields.at("parent_row"), prefix + ".parent_row"),
		                    ReadGridIndex(*fields.at("parent_begin"), prefix + ".parent_begin"),
		                    ReadGridExtent(*fields.at("count"), prefix + ".count")});
	}
	return segments;
}

GridDefinition ReadVersionOneGrid(const Value &value) {
	static const std::vector<std::string> root_fields = {"version", "type", "numeric_policy", "earth", "layout", "parameters"};
	const auto fields = ReadExactGridFields(value, "grid", root_fields);
	const auto version = ReadGridExtent(*fields.at("version"), "version");
	if (version != 1) throw BinderException("read_om grid version must be exactly 1");
	const auto type = ReadGridText(*fields.at("type"), "type");
	const auto policy_name = ReadGridText(*fields.at("numeric_policy"), "numeric_policy");
	GridNumericPolicy policy;
	if (policy_name == "float64_v1") policy = GridNumericPolicy::Float64V1;
	else if (policy_name == "openmeteo_f32_v1") policy = GridNumericPolicy::OpenMeteoF32V1;
	else throw BinderException("read_om grid numeric_policy must be float64_v1 or openmeteo_f32_v1");

	GridEarth earth;
	const auto earth_model = [&]() {
		if (fields.at("earth")->type().id() != LogicalTypeId::STRUCT) {
			throw BinderException("read_om grid earth must be a STRUCT");
		}
		const auto &earth_values = StructValue::GetChildren(*fields.at("earth"));
		for (idx_t index = 0; index < earth_values.size(); index++) {
			if (StructType::GetChildName(fields.at("earth")->type(), index) == "model") {
				return ReadGridText(earth_values[index], "earth.model");
			}
		}
		throw BinderException("read_om grid earth is missing model");
	}();
	if (earth_model == "sphere") {
		const auto earth_fields = ReadExactGridFields(*fields.at("earth"), "earth", {"model", "radius_m"});
		earth.kind = GridEarthKind::Sphere;
		earth.radius_m = ReadGridDouble(*earth_fields.at("radius_m"), "earth.radius_m");
	} else if (earth_model == "wgs84") {
		const auto earth_fields = ReadExactGridFields(*fields.at("earth"), "earth", {"model", "semi_major_m", "inverse_flattening"});
		earth.kind = GridEarthKind::Wgs84Source;
		earth.semi_major_axis_m = ReadGridDouble(*earth_fields.at("semi_major_m"), "earth.semi_major_m");
		earth.inverse_flattening = ReadGridDouble(*earth_fields.at("inverse_flattening"), "earth.inverse_flattening");
	} else {
		throw BinderException("read_om grid earth.model must be sphere or wgs84");
	}

	std::optional<GridGeometry> geometry;
	std::string coordinate_rule;
	if (type == "rotated_latlon" || type == "lambert_conformal_conic" || type == "stereographic") {
		const auto layout_fields = ReadExactGridFields(*fields.at("layout"), "layout", {"nx", "ny", "order"});
		const auto nx = ReadGridExtent(*layout_fields.at("nx"), "layout.nx");
		const auto ny = ReadGridExtent(*layout_fields.at("ny"), "layout.ny");
		const auto order = ReadVersionOneGridOrder(*layout_fields.at("order"));
		if (type == "rotated_latlon") {
			const auto parameters = ReadExactGridFields(*fields.at("parameters"), "parameters",
			    {"x0", "y0", "dx", "dy", "north_pole_latitude", "north_pole_longitude", "rotation"});
			geometry = ProjectedGrid(RotatedLatLonParameters{nx, ny,
			    ReadGridDouble(*parameters.at("x0"), "parameters.x0"),
			    ReadGridDouble(*parameters.at("y0"), "parameters.y0"),
			    ReadGridDouble(*parameters.at("dx"), "parameters.dx"),
			    ReadGridDouble(*parameters.at("dy"), "parameters.dy"),
			    ReadGridDouble(*parameters.at("north_pole_latitude"), "parameters.north_pole_latitude"),
			    ReadGridDouble(*parameters.at("north_pole_longitude"), "parameters.north_pole_longitude"),
			    ReadGridDouble(*parameters.at("rotation"), "parameters.rotation"), order});
			coordinate_rule = policy == GridNumericPolicy::OpenMeteoF32V1 ? "openmeteo_rotated_latlon_f32_v1" : "rotated_latlon_float64_v1";
		} else if (type == "lambert_conformal_conic") {
			const auto parameters = ReadExactGridFields(*fields.at("parameters"), "parameters",
			    {"x0", "y0", "dx", "dy", "central_meridian", "latitude_of_origin", "standard_parallel_1", "standard_parallel_2"});
			geometry = ProjectedGrid(LambertParameters{nx, ny,
			    ReadGridDouble(*parameters.at("x0"), "parameters.x0"),
			    ReadGridDouble(*parameters.at("y0"), "parameters.y0"),
			    ReadGridDouble(*parameters.at("dx"), "parameters.dx"),
			    ReadGridDouble(*parameters.at("dy"), "parameters.dy"),
			    ReadGridDouble(*parameters.at("central_meridian"), "parameters.central_meridian"),
			    ReadGridDouble(*parameters.at("latitude_of_origin"), "parameters.latitude_of_origin"),
			    ReadGridDouble(*parameters.at("standard_parallel_1"), "parameters.standard_parallel_1"),
			    ReadGridDouble(*parameters.at("standard_parallel_2"), "parameters.standard_parallel_2"),
			    earth.radius_m, 0, 0, order});
			coordinate_rule = policy == GridNumericPolicy::OpenMeteoF32V1 ? "openmeteo_lambert_f32_v1" : "lambert_conformal_conic_float64_v1";
		} else {
			if (earth.kind != GridEarthKind::Sphere) throw BinderException("read_om stereographic requires sphere earth");
			const auto parameters = ReadExactGridFields(*fields.at("parameters"), "parameters",
			    {"x0", "y0", "dx", "dy", "central_meridian", "latitude_of_origin", "scale_factor"});
			geometry = ProjectedGrid(StereographicParameters{nx, ny,
			    ReadGridDouble(*parameters.at("x0"), "parameters.x0"),
			    ReadGridDouble(*parameters.at("y0"), "parameters.y0"),
			    ReadGridDouble(*parameters.at("dx"), "parameters.dx"),
			    ReadGridDouble(*parameters.at("dy"), "parameters.dy"),
			    ReadGridDouble(*parameters.at("latitude_of_origin"), "parameters.latitude_of_origin"),
			    ReadGridDouble(*parameters.at("central_meridian"), "parameters.central_meridian"),
			    earth.radius_m,
			    ReadGridDouble(*parameters.at("scale_factor"), "parameters.scale_factor"), 0, 0, order});
			coordinate_rule = policy == GridNumericPolicy::OpenMeteoF32V1 ? "openmeteo_stereographic_f32_v1" : "stereographic_float64_v1";
		}
	} else if (type == "reduced_gaussian") {
		const auto layout_fields = ReadExactGridFields(*fields.at("layout"), "layout", {"order"});
		if (ReadGridText(*layout_fields.at("order"), "layout.order") != "row_major") {
			throw BinderException("read_om Gaussian layout.order must be row_major");
		}
		const auto parameters = ReadExactGridFields(*fields.at("parameters"), "parameters",
		    {"n", "latitude_rule", "rows", "subset_segments"}, {"subset_segments"});
		const auto n = ReadGridExtent(*parameters.at("n"), "parameters.n");
		const auto latitude_rule = ReadGridText(*parameters.at("latitude_rule"), "parameters.latitude_rule");
		geometry = GaussianGrid(n, latitude_rule, ReadGaussianRows(*parameters.at("rows")),
		                       ReadGaussianSegments(*parameters.at("subset_segments")));
		if (policy == GridNumericPolicy::OpenMeteoF32V1 && latitude_rule == "openmeteo_approx_v1") {
			coordinate_rule = "openmeteo_gaussian_n" + std::to_string(n) + "_f32_v1";
		} else {
			coordinate_rule = "gaussian_n" + std::to_string(n) + "_" + latitude_rule + "_v1";
		}
	} else {
		throw BinderException("read_om grid type is unknown: '" + type + "'");
	}
	if (!geometry) throw BinderException("read_om grid geometry is missing");
	return GridDefinition(std::move(coordinate_rule), earth, policy, std::move(*geometry));
}

std::vector<std::string> ReadSpatialAxes(const Value *value) {
	if (value == nullptr || value->IsNull()) {
		throw BinderException("read_om grid configuration requires spatial_axes VARCHAR[]");
	}
	if (value->type().id() != LogicalTypeId::LIST ||
	    ListType::GetChildType(value->type()).id() != LogicalTypeId::VARCHAR) {
		throw BinderException("read_om spatial_axes must be VARCHAR[]");
	}
	const auto &children = ListValue::GetChildren(*value);
	std::vector<std::string> result;
	result.reserve(children.size());
	for (const auto &child : children) {
		if (child.IsNull() || child.type().id() != LogicalTypeId::VARCHAR || StringValue::Get(child).empty()) {
			throw BinderException("read_om spatial_axes must contain non-empty, non-NULL axis names");
		}
		result.emplace_back(StringValue::Get(child));
	}
	if (result.empty()) {
		throw BinderException("read_om spatial_axes must not be empty");
	}
	return result;
}

std::string GridOrderName(GridStorageOrder order) {
	switch (order) {
	case GridStorageOrder::Separate:
		return "separate";
	case GridStorageOrder::LongitudeFastest:
		return "lon_fastest";
	case GridStorageOrder::LatitudeFastest:
		return "lat_fastest";
	}
	return "invalid";
}

struct SpatialBindConfiguration final {
	std::optional<RegularGrid> grid;
	std::optional<GridDefinition> definition;
	std::optional<GridDefinition> identity_definition;
	std::optional<SpatialLayout> layout;
	// DuckDB may copy FunctionData between binding, logical planning, and scan
	// initialization. Keep callback output in query-local shared state so the
	// planner's safe predicate copy is visible to the physical scan state.
	std::shared_ptr<SpatialPredicate> predicate = std::make_shared<SpatialPredicate>();
	std::string grid_signature;
	std::string layout_name;
	std::string source;
	std::string grid_id;
	std::string layout_id;
	std::string object_id;
	std::string evidence_level = "definition-recorded";
	std::string evidence_sample_id;
	std::string evidence_source;
	std::string evidence_build_pair;
	std::string evidence_claims;
	bool include_source = false;

	bool HasGrid() const noexcept { return grid.has_value() || definition.has_value(); }
	GridCoordinate Coordinate(std::uint64_t logical_position) const {
		if (!layout) throw ReaderError(ReaderErrorCode::InvalidShape, "spatial coordinate requested without a bound layout");
		if (definition) return layout->Coordinate(*definition, logical_position);
		if (grid) return layout->Coordinate(*grid, logical_position);
		throw ReaderError(ReaderErrorCode::InvalidShape, "spatial coordinate requested without a grid definition");
	}
	GridCoordinate Coordinate(const NativeWindow &window, std::uint64_t offset) const {
		if (!layout) throw ReaderError(ReaderErrorCode::InvalidShape, "spatial window coordinate requested without a bound layout");
		if (layout->flattened) {
			const auto point = window.SpatialAxis() == layout->point_axis
			                       ? window.Begin() + offset
			                       : window.FixedAxisIndices().at(layout->point_axis);
			if (definition && std::holds_alternative<GaussianGrid>(definition->geometry)) {
				return definition->Coordinate(NativePointPosition{point});
			}
			std::uint64_t x = 0;
			std::uint64_t y = 0;
			std::uint64_t nx = grid ? grid->Nx() : 0;
			std::uint64_t ny = grid ? grid->Ny() : 0;
			if (definition) {
				if (const auto *regular = std::get_if<RegularGrid>(&definition->geometry)) {
					nx = regular->Nx();
					ny = regular->Ny();
				} else if (const auto *projected = std::get_if<ProjectedGrid>(&definition->geometry)) {
					nx = projected->Nx();
					ny = projected->Ny();
				} else {
					throw ReaderError(ReaderErrorCode::InvalidShape,
					                  "Gaussian spatial layout must use its native point mapping");
				}
			}
			if (layout->order == GridStorageOrder::LatitudeFastest) {
				x = point / ny;
				y = point % ny;
			} else {
				x = point % nx;
				y = point / nx;
			}
			if (definition) return definition->Coordinate(NativeXYPosition{x, y});
			return grid->Coordinate(y, x);
		}
		const auto &fixed = window.FixedAxisIndices();
		const auto y = window.SpatialAxis() == layout->latitude_axis
		                   ? window.Begin() + offset
		                   : fixed.at(layout->latitude_axis);
		const auto x = window.SpatialAxis() == layout->longitude_axis
		                   ? window.Begin() + offset
		                   : fixed.at(layout->longitude_axis);
		if (definition) return definition->Coordinate(NativeXYPosition{x, y});
		if (grid) return grid->Coordinate(y, x);
		throw ReaderError(ReaderErrorCode::InvalidShape, "spatial window coordinate requested without a grid definition");
	}
};

bool SameSpatialPredicate(const SpatialPredicate &left, const SpatialPredicate &right) {
	if (left.residual_filter_retained != right.residual_filter_retained ||
	    left.has_unsupported_condition != right.has_unsupported_condition ||
	    left.fallback_reasons != right.fallback_reasons ||
	    left.necessary_conditions.size() != right.necessary_conditions.size() ||
	    left.longitude_union.size() != right.longitude_union.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.necessary_conditions.size(); index++) {
		const auto &left_condition = left.necessary_conditions[index];
		const auto &right_condition = right.necessary_conditions[index];
		if (left_condition.axis != right_condition.axis || left_condition.comparison != right_condition.comparison ||
		    left_condition.constant != right_condition.constant) {
			return false;
		}
	}
	for (std::size_t index = 0; index < left.longitude_union.size(); index++) {
		const auto &left_interval = left.longitude_union[index];
		const auto &right_interval = right.longitude_union[index];
		if (left_interval.lower != right_interval.lower || left_interval.upper != right_interval.upper ||
		    left_interval.lower_inclusive != right_interval.lower_inclusive ||
		    left_interval.upper_inclusive != right_interval.upper_inclusive) {
			return false;
		}
	}
	return true;
}

bool SameAxisPredicate(const AxisPredicate &left, const AxisPredicate &right) {
	if (left.has_unsupported_condition != right.has_unsupported_condition ||
	    left.fallback_reasons != right.fallback_reasons ||
	    left.necessary_conditions.size() != right.necessary_conditions.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.necessary_conditions.size(); index++) {
		const auto &left_condition = left.necessary_conditions[index];
		const auto &right_condition = right.necessary_conditions[index];
		if (left_condition.axis_index != right_condition.axis_index ||
		    left_condition.comparison != right_condition.comparison ||
		    !Value::NotDistinctFrom(left_condition.constant, right_condition.constant)) {
			return false;
		}
	}
	return true;
}

void DescribeSpatialConfiguration(SpatialBindConfiguration &result, ScanMetrics &metrics) {
	std::ostringstream signature;
	if (result.definition) {
		result.identity_definition = *result.definition;
	} else {
		const auto order = result.grid->Order();
		signature << std::setprecision(17) << "nx=" << result.grid->Nx() << ",ny=" << result.grid->Ny()
		          << ",lat0=" << result.grid->LatitudeOrigin() << ",lon0=" << result.grid->LongitudeOrigin()
		          << ",dlat=" << result.grid->LatitudeStep() << ",dlon=" << result.grid->LongitudeStep()
		          << ",order=" << GridOrderName(order);
		result.grid_signature = signature.str();
		if (result.layout->flattened) {
			result.layout_name = GridOrderName(order) + ":" + result.layout->axes[result.layout->point_axis];
		} else {
			result.layout_name = "separate:" + result.layout->axes[result.layout->latitude_axis] + "," +
			                     result.layout->axes[result.layout->longitude_axis];
		}
	}
	if (!result.identity_definition && result.grid) {
		result.identity_definition.emplace("regular_grid_v1", GridEarth{}, GridNumericPolicy::Float64V1, *result.grid);
	}
	if (result.identity_definition) {
		result.grid_id = GridId(*result.identity_definition);
		result.layout_id = LayoutId(*result.identity_definition, *result.layout);
		if (result.definition) {
			result.grid_signature = "grid_id=" + result.grid_id;
			result.layout_name = "layout_id=" + result.layout_id;
		}
	}
	if (result.layout->flattened) {
		if (!result.definition) result.layout_name = GridOrderName(result.layout->order) + ":" + result.layout->axes[result.layout->point_axis];
	} else {
		if (!result.definition) {
			result.layout_name = "separate:" + result.layout->axes[result.layout->latitude_axis] + "," +
			                     result.layout->axes[result.layout->longitude_axis];
		}
	}
	metrics.SetSpatialContext(result.grid_signature, result.layout_name, result.source);
	metrics.SetSpatialSelection("full", true, {}, CheckedShapeProduct(result.layout->shape));
}

std::string OpaqueObjectId(const std::string &identity_path) {
	const auto payload = std::string("duckomo-object-v1\0", 18) + identity_path;
	char digest[duckdb_mbedtls::MbedTlsWrapper::SHA256_HASH_LENGTH_BYTES];
	duckdb_mbedtls::MbedTlsWrapper::ComputeSha256Hash(payload.data(), payload.size(), digest);
	static constexpr char HEX[] = "0123456789abcdef";
	std::string result("sha256:");
	result.resize(7 + sizeof(digest) * 2);
	for (std::size_t index = 0; index < sizeof(digest); index++) {
		const auto byte = static_cast<unsigned char>(digest[index]);
		result[7 + index * 2] = HEX[byte >> 4];
		result[7 + index * 2 + 1] = HEX[byte & 0x0f];
	}
	return result;
}

LogicalType NearestSamplingType(const BoundSchema &schema) {
	child_list_t<LogicalType> variable_type;
	variable_type.emplace_back("status", LogicalType::VARCHAR);
	variable_type.emplace_back("source_count", LogicalType::UBIGINT);
	variable_type.emplace_back("valid_source_count", LogicalType::UBIGINT);
	const auto variables_type = LogicalType::MAP(LogicalType::VARCHAR, LogicalType::STRUCT(variable_type));
	child_list_t<LogicalType> fields;
	fields.emplace_back("method", LogicalType::VARCHAR);
	fields.emplace_back("algorithm", LogicalType::VARCHAR);
	fields.emplace_back("representative", LogicalType::VARCHAR);
	fields.emplace_back("spatial_version", LogicalType::VARCHAR);
	fields.emplace_back("grid_id", LogicalType::VARCHAR);
	fields.emplace_back("layout_id", LogicalType::VARCHAR);
	fields.emplace_back("distance_model", LogicalType::VARCHAR);
	fields.emplace_back("earth_radius_m", LogicalType::DOUBLE);
	fields.emplace_back("status", LogicalType::VARCHAR);
	fields.emplace_back("source_lon", LogicalType::DOUBLE);
	fields.emplace_back("source_lat", LogicalType::DOUBLE);
	fields.emplace_back("source_point_index", LogicalType::UBIGINT);
	fields.emplace_back("distance_m", LogicalType::DOUBLE);
	fields.emplace_back("axis_indices", LogicalType::LIST(LogicalType::UBIGINT));
	fields.emplace_back("variables", variables_type);
	(void)schema;
	return LogicalType::STRUCT(fields);
}

LogicalType OmSourceType() {
	return LogicalType::STRUCT({
	    {"object_id", LogicalType::VARCHAR},
	    {"object_version", LogicalType::VARCHAR},
	    {"version_strength", LogicalType::VARCHAR},
	    {"content_verified", LogicalType::BOOLEAN},
	    {"grid_id", LogicalType::VARCHAR},
	    {"layout_id", LogicalType::VARCHAR},
	    {"logical_index", LogicalType::UBIGINT},
	    {"point_index", LogicalType::UBIGINT},
	    {"parent_point_index", LogicalType::UBIGINT},
	    {"axis_indices", LogicalType::LIST(LogicalType::UBIGINT)},
	});
}

Value MakeOmSourceValue(const SpatialBindConfiguration &spatial, std::uint64_t logical_index) {
	if (!spatial.include_source || !spatial.identity_definition || !spatial.layout) {
		throw InternalException("read_om source output has no bound grid identity or layout");
	}
	std::vector<Value> axis_values;
	for (const auto index : spatial.layout->AxisIndices(logical_index)) axis_values.push_back(Value::UBIGINT(index));
	return Value::STRUCT({
	    {"object_id", Value(spatial.object_id)},
	    {"object_version", Value(LogicalType::VARCHAR)},
	    {"version_strength", Value("unverifiable")},
	    {"content_verified", Value(false)},
	    {"grid_id", Value(spatial.grid_id)},
	    {"layout_id", Value(spatial.layout_id)},
	    {"logical_index", Value::UBIGINT(logical_index)},
	    {"point_index", Value::UBIGINT(spatial.layout->LocalPointIndex(*spatial.identity_definition, logical_index))},
	    {"parent_point_index", Value::UBIGINT(spatial.layout->ParentPointIndex(*spatial.identity_definition, logical_index))},
	    {"axis_indices", Value::LIST(LogicalType::UBIGINT, std::move(axis_values))},
	});
}

void ValidateDomainBbox(const BoundSchema &schema, const VerifiedDomain &domain) {
	if (schema.crs_wkt.empty()) {
		return;
	}
	WktBboxStatus status;
	try {
		status = duckdb::duckomo::ValidateWktBbox(schema.crs_wkt, domain.grid);
	} catch (const ReaderError &) {
		throw BinderException("read_om domain '" + domain.name + "' has malformed crs_wkt BBOX");
	}
	if (status == WktBboxStatus::Absent || status == WktBboxStatus::MatchesGrid) {
		return;
	}
	throw BinderException("read_om domain '" + domain.name + "' conflicts with file crs_wkt BBOX");
}

void ValidateGridSourceCrs(const BoundSchema &schema, const GridDefinition &definition) {
	const bool reduced_gaussian = std::holds_alternative<GaussianGrid>(definition.geometry);
	const bool wgs84_source_earth = definition.earth.kind == GridEarthKind::Wgs84Source;
	std::optional<std::uint64_t> declared_gaussian_order;
	if (const auto *gaussian = std::get_if<GaussianGrid>(&definition.geometry)) {
		declared_gaussian_order = gaussian->N();
	}
	const auto status = duckdb::duckomo::CheckSourceCrsProfile(schema.crs_profile, reduced_gaussian,
	                                                            wgs84_source_earth, declared_gaussian_order);
	if (status == duckdb::duckomo::SourceCrsBindingStatus::NotSpecified ||
	    status == duckdb::duckomo::SourceCrsBindingStatus::Compatible) {
		return;
	}
	if (status == duckdb::duckomo::SourceCrsBindingStatus::Unrecognized) {
		throw BinderException("read_om cannot bind an unrecognized source crs_wkt profile to a version=1 grid");
	}
	throw BinderException("read_om source crs_wkt profile conflicts with the declared grid geometry or earth model");
}

void ValidateGridCoordinates(ClientContext &context, const GridDefinition &definition, ScanMetrics &metrics) {
	// Geometry constructors check PointCount against the signed 64-bit bound;
	// this constant-space pass is therefore bounded by the declared grid size.
	const auto count = definition.PointCount();
	const auto *projected = std::get_if<ProjectedGrid>(&definition.geometry);
	std::uint64_t evaluations = 0;
	try {
		for (std::uint64_t point = 0; point < count; point++) {
			if ((evaluations & 255U) == 0 && context.IsInterrupted()) throw InterruptException();
			NativeGridPosition position;
			if (projected != nullptr) {
				const auto order = std::visit([](const auto &parameters) { return parameters.order; }, projected->Parameters());
				if (order == GridStorageOrder::Separate) {
					position = NativeXYPosition{point % projected->Nx(), point / projected->Nx()};
				} else {
					position = NativePointPosition{point};
				}
			} else {
				position = NativePointPosition{point};
			}
			++evaluations;
			(void)definition.Coordinate(position);
		}
	} catch (...) {
		metrics.RecordCoordinatePreparation(evaluations, false);
		throw;
	}
	metrics.RecordCoordinatePreparation(evaluations, true);
}

SpatialBindConfiguration BindSpatialConfiguration(const TableFunctionBindInput &input, const BoundSchema &schema,
	                                               const AxisDeclarations &axes, ScanMetrics &metrics,
	                                               ClientContext &context, bool regions_mode) {
	SpatialBindConfiguration result;
	const auto *grid_value = GetTableFunctionNamedArgument(input, "grid");
	const auto *axes_value = GetTableFunctionNamedArgument(input, "spatial_axes");
	const auto *domain_value = GetTableFunctionNamedArgument(input, "domain");
	const bool has_grid = grid_value != nullptr && !grid_value->IsNull();
	const bool has_domain = domain_value != nullptr && !domain_value->IsNull();
	if (has_grid && has_domain) {
		throw BinderException("read_om grid and domain are mutually exclusive");
	}
	if (has_domain) {
		if (domain_value->type().id() != LogicalTypeId::VARCHAR) {
			throw BinderException("read_om domain must be a VARCHAR name");
		}
		const auto domain_name = StringValue::Get(*domain_value);
		const auto *domain = FindVerifiedDomain(domain_name);
		if (domain == nullptr) {
			const auto *registered = FindRegisteredGridDefinition(domain_name);
			if (registered == nullptr) {
				const auto message = "read_om domain is unknown or not verified: '" + domain_name + "'";
				throw BinderException(regions_mode ? "[region_reference] " + message : message);
			}
			if (!registered->domain_bindable) {
				throw BinderException("read_om domain '" + domain_name + "' is a formula identity vector, not a producer domain");
			}
			if (axes_value != nullptr && !axes_value->IsNull()) {
				throw BinderException("read_om domain does not accept an extra spatial_axes override");
			}
			if (schema.variables.size() != axes.size() || registered->expected_axis_order.empty()) {
				throw BinderException("read_om domain '" + domain_name + "' requires ordered axes for every array");
			}
			// The frozen HSURF producer profile calls its flattened point axis "lon".
			// Select only a registered alias, after metadata alignment has been checked;
			// never guess an axis from shape or relax conflicting source declarations.
			auto domain_spatial_axes = registered->expected_axis_order;
			if (domain_spatial_axes.size() == 1 && !registered->flattened_axis_alias.empty() &&
			    std::find(axes.front().begin(), axes.front().end(), domain_spatial_axes.front()) == axes.front().end()) {
				domain_spatial_axes.front() = registered->flattened_axis_alias;
			}
			for (std::size_t index = 0; index < schema.variables.size(); index++) {
				const auto &variable = schema.variables[index];
				if (axes[index].size() != variable.shape.size()) {
					throw BinderException("read_om domain '" + domain_name + "' requires complete ordered axes for every array");
				}
				for (const auto &spatial_axis : domain_spatial_axes) {
					if (std::find(axes[index].begin(), axes[index].end(), spatial_axis) == axes[index].end()) {
						throw BinderException("read_om domain '" + domain_name + "' requires spatial axis '" +
						                      spatial_axis + "' in each array's declared profile");
					}
				}
			}
			result.definition = registered->definition;
			result.evidence_level = registered->evidence_level;
			result.evidence_sample_id = registered->evidence_sample_id;
			result.evidence_source = registered->evidence_source_uri.empty()
			                             ? registered->source_path
			                             : registered->evidence_source_uri;
			result.evidence_build_pair = registered->evidence_build_pair;
			result.evidence_claims = registered->evidence_claims;
			ValidateGridSourceCrs(schema, *result.definition);
			try {
				result.layout.emplace(BindGridSpatialLayout(schema, axes, *result.definition,
				                                           domain_spatial_axes));
				const bool expected_flattened = registered->expected_layout == "flattened";
				if (result.layout->flattened != expected_flattened) {
					throw ReaderError(ReaderErrorCode::InvalidShape,
					                  "registered grid layout does not match its frozen spatial profile");
				}
				ValidateGridCoordinates(context, *result.definition, metrics);
			} catch (const ReaderError &error) {
				throw BinderException("read_om domain '" + domain_name + "' layout is invalid: " +
				                      std::string(error.what()));
			}
			result.source = "definition-recorded:domain=" + registered->name +
			                ";upstream=" + registered->upstream_commit + ";source=" + registered->source_path +
			                ";profile=" + registered->object_profile_status +
			                ";evidence_level=" + registered->evidence_level;
			DescribeSpatialConfiguration(result, metrics);
			return result;
		}
		if (!domain) {
			const auto message = "read_om domain is unknown or not verified: '" + domain_name + "'";
			throw BinderException(regions_mode ? "[region_reference] " + message : message);
		}
		if (axes_value != nullptr && !axes_value->IsNull()) {
			throw BinderException("read_om domain does not accept an extra spatial_axes override");
		}
		if (schema.variables.size() != axes.size()) {
			throw BinderException("read_om domain '" + domain_name + "' requires ordered axes for every array");
		}
		for (std::size_t index = 0; index < schema.variables.size(); index++) {
			const auto &variable = schema.variables[index];
			if (axes[index].size() != variable.shape.size() ||
			    std::find(axes[index].begin(), axes[index].end(), "lat") == axes[index].end() ||
			    std::find(axes[index].begin(), axes[index].end(), "lon") == axes[index].end()) {
				throw BinderException("read_om domain '" + domain_name +
				                      "' requires complete ordered lat/lon axes from coordinates metadata or dimensions");
			}
		}
		result.grid = domain->grid;
		try {
			result.layout.emplace(BindSpatialLayout(schema, axes, *result.grid, {"lat", "lon"}));
		} catch (const ReaderError &error) {
			throw BinderException("read_om domain '" + domain_name + "' layout is invalid: " +
			                      std::string(error.what()));
		}
		ValidateDomainBbox(schema, *domain);
		result.source = "domain:" + domain->name + ";upstream=" + domain->upstream_commit +
		                ";source=Sources/App/" + domain->source_path;
		if (!domain->sample_sha256.empty()) {
			result.source += ";sample_sha256=" + domain->sample_sha256;
		}
		DescribeSpatialConfiguration(result, metrics);
		return result;
	}
	if (!has_grid) {
		if (axes_value != nullptr && !axes_value->IsNull()) {
			throw BinderException("read_om spatial_axes requires grid or domain");
		}
		return result;
	}

	if (grid_value->type().id() != LogicalTypeId::STRUCT) {
		throw BinderException("read_om grid must be a named STRUCT");
	}
	const auto &top_field_names = StructType::GetChildTypes(grid_value->type());
	bool is_version_one = false;
	for (idx_t index = 0; index < top_field_names.size(); index++) {
		if (StructType::GetChildName(grid_value->type(), index) == "version") is_version_one = true;
	}
	if (is_version_one) {
		try {
			result.definition.emplace(ReadVersionOneGrid(*grid_value));
		} catch (const ReaderError &error) {
			throw BinderException("read_om version=1 grid is invalid: " + std::string(error.what()));
		}
		ValidateGridSourceCrs(schema, *result.definition);
		auto spatial_axes = ReadSpatialAxes(axes_value);
		try {
			result.layout.emplace(BindGridSpatialLayout(schema, axes, *result.definition, spatial_axes));
			ValidateGridCoordinates(context, *result.definition, metrics);
		} catch (const ReaderError &error) {
			throw BinderException("read_om version=1 spatial layout is invalid: " + std::string(error.what()));
		}
		result.source = "explicit";
		DescribeSpatialConfiguration(result, metrics);
		return result;
	}
	static const std::vector<std::string> required_fields = {"nx", "ny", "lat0", "lon0", "dlat", "dlon", "order"};
	const auto &field_types = StructType::GetChildTypes(grid_value->type());
	const auto &field_values = StructValue::GetChildren(*grid_value);
	if (field_types.size() != required_fields.size() || field_values.size() != required_fields.size()) {
		throw BinderException("read_om grid STRUCT must contain exactly nx, ny, lat0, lon0, dlat, dlon, and order");
	}
	std::unordered_map<std::string, const Value *> fields;
	fields.reserve(required_fields.size());
	for (idx_t index = 0; index < field_types.size(); index++) {
		const auto &name = StructType::GetChildName(grid_value->type(), index);
		if (std::find(required_fields.begin(), required_fields.end(), name) == required_fields.end() ||
		    field_values[index].IsNull() || !fields.emplace(name, &field_values[index]).second) {
			throw BinderException("read_om grid STRUCT has an unknown, duplicate, or NULL field '" + name + "'");
		}
	}
	for (const auto &field : required_fields) {
		if (fields.find(field) == fields.end()) {
			throw BinderException("read_om grid STRUCT is missing field '" + field + "'");
		}
	}
	if (fields.at("order")->type().id() != LogicalTypeId::VARCHAR) {
		throw BinderException("read_om grid field 'order' must be VARCHAR");
	}
	const auto order_name = StringValue::Get(*fields.at("order"));
	GridStorageOrder order;
	if (order_name == "separate") {
		order = GridStorageOrder::Separate;
	} else if (order_name == "lon_fastest") {
		order = GridStorageOrder::LongitudeFastest;
	} else if (order_name == "lat_fastest") {
		order = GridStorageOrder::LatitudeFastest;
	} else {
		throw BinderException("read_om grid order must be separate, lon_fastest, or lat_fastest");
	}

	try {
		result.grid.emplace(ReadGridExtent(*fields.at("nx"), "nx"), ReadGridExtent(*fields.at("ny"), "ny"),
		                    ReadGridDouble(*fields.at("lat0"), "lat0"), ReadGridDouble(*fields.at("lon0"), "lon0"),
		                    ReadGridDouble(*fields.at("dlat"), "dlat"), ReadGridDouble(*fields.at("dlon"), "dlon"), order);
	} catch (const ReaderError &error) {
		throw BinderException("read_om grid is invalid: " + std::string(error.what()));
	}
	auto spatial_axes = ReadSpatialAxes(axes_value);
	try {
		result.layout.emplace(BindSpatialLayout(schema, axes, *result.grid, spatial_axes));
	} catch (const ReaderError &error) {
		throw BinderException("read_om spatial layout is invalid: " + std::string(error.what()));
	}
	result.source = "explicit";
	DescribeSpatialConfiguration(result, metrics);
	return result;
}

struct TemporalBindConfiguration final {
	std::vector<timestamp_t> values;
	std::optional<idx_t> axis_index;
	std::uint64_t stride = 0; // Zero denotes a scalar snapshot.
	std::uint64_t axis_length = 1;
	bool regular = false;
	timestamp_t start = timestamp_t(0);
	std::int64_t step_micros = 0;

	timestamp_t Coordinate(std::uint64_t logical_position) const {
		const auto index = stride == 0 ? 0 : (logical_position / stride) % axis_length;
		if (!regular) return values.at(index);
		const __int128 value = static_cast<__int128>(start.value) + static_cast<__int128>(step_micros) * index;
		return timestamp_t(static_cast<std::int64_t>(value));
	}

	bool operator==(const TemporalBindConfiguration &other) const {
		return axis_index == other.axis_index && stride == other.stride && axis_length == other.axis_length && regular == other.regular &&
		       start == other.start && step_micros == other.step_micros && values == other.values;
	}
};

std::optional<TemporalBindConfiguration> BindTemporalConfiguration(const TableFunctionBindInput &input,
                                                                   const BoundSchema &schema,
                                                                   const AxisDeclarations &axes,
                                                                   const SemanticAxes &semantic_axes) {
	const auto *explicit_times = GetTableFunctionNamedArgument(input, "valid_times");
	const bool has_explicit = explicit_times && !explicit_times->IsNull();
	for (const auto &semantic_axis : semantic_axes) {
		if (semantic_axis.kind != SemanticAxisKind::Time) continue;
		if (has_explicit) {
			throw BinderException("read_om axes.time cannot be combined with valid_times");
		}
		TemporalBindConfiguration result;
		result.values = semantic_axis.timestamps;
		result.axis_index = semantic_axis.axis_index;
		result.stride = semantic_axis.stride;
		result.axis_length = semantic_axis.axis_length;
		result.regular = semantic_axis.regular;
		result.start = semantic_axis.timestamp_start;
		result.step_micros = semantic_axis.timestamp_step_micros;
		return result;
	}
	std::vector<timestamp_t> supplied;
	if (has_explicit) {
		for (const auto &value : ListValue::GetChildren(*explicit_times)) {
			if (value.IsNull() || !Value::IsFinite(value.GetValue<timestamp_t>())) {
				throw BinderException("read_om valid_times must contain finite, non-NULL UTC timestamps");
			}
			supplied.push_back(value.GetValue<timestamp_t>());
		}
		if (supplied.empty()) {
			throw BinderException("read_om valid_times must not be empty");
		}
	}
	std::optional<TemporalBindConfiguration> result;
	bool missing_time = false;
	for (std::size_t index = 0; index < schema.variables.size(); index++) {
		const auto &variable = schema.variables[index];
		if (!variable.time && !has_explicit) {
			missing_time = true;
			continue;
		}
		TemporalBindConfiguration current;
		if (variable.time) {
			for (const auto seconds : variable.time->epoch_seconds) {
				if (seconds > std::numeric_limits<std::int64_t>::max() / 1000000 ||
				    seconds < std::numeric_limits<std::int64_t>::min() / 1000000) {
					throw BinderException("read_om time coordinate is outside the finite TIMESTAMP range");
				}
				current.values.push_back(Timestamp::FromEpochSeconds(seconds));
			}
			if (has_explicit && current.values != supplied) {
				throw BinderException("read_om valid_times conflicts with file time metadata");
			}
		} else {
			current.values = supplied;
		}
		const auto &axis_names = axes.at(index);
		const auto time_axis = std::find(axis_names.begin(), axis_names.end(), "time");
		if (time_axis != axis_names.end()) {
			const auto axis = static_cast<std::size_t>(time_axis - axis_names.begin());
			if (current.values.size() != variable.shape.at(axis)) {
				throw BinderException("read_om time coordinate length must match the declared time axis");
			}
			current.axis_length = variable.shape.at(axis);
			current.axis_index = axis;
			current.stride = 1;
			for (std::size_t next = axis + 1; next < variable.shape.size(); next++) {
				current.stride *= variable.shape[next];
			}
		} else if (current.values.size() != 1 || (variable.time && !variable.time->scalar)) {
			throw BinderException(
			    "read_om time array requires a complete ordered time axis from coordinates or dimensions");
		}
		if (time_axis == axis_names.end()) current.axis_length = 1;
		if (result && !(current == *result)) {
			throw BinderException("read_om arrays have conflicting time coordinates");
		}
		result = std::move(current);
	}
	if (result && missing_time) {
		throw BinderException(
		    "read_om time coordinates must cover every value array; supply valid_times for missing metadata");
	}
	return result;
}

struct ReadOmBindData;
std::uint64_t AddMemoryBytes(std::uint64_t total, std::uint64_t bytes);
std::uint64_t EstimateAxisPredicateMemory(const AxisPredicate &predicate);
std::uint64_t EstimateSpatialPredicateMemory(const SpatialPredicate &predicate);
std::uint64_t EstimateBindMemory(const ReadOmBindData &data);
std::uint64_t EstimateRegionInputMemory(const RegionCollection &regions);
std::uint64_t EstimateRegionBindCopyMemory(const std::string &region_crs,
                                         const std::vector<Value> &region_geometries,
                                         const std::vector<unique_ptr<Expression>> &region_predicates);

struct ReadOmBindData final : TableFunctionData {
	ReadOmBindData(std::string path_p, BoundSchema schema_p, AxisDeclarations axes_p,
	               SpatialBindConfiguration spatial_p, std::optional<TemporalBindConfiguration> temporal_p,
	               SemanticAxes semantic_axes_p,
	               std::shared_ptr<RemoteReadSession> remote_session_p,
	               std::shared_ptr<ScanMetrics> metrics_p,
	               std::shared_ptr<ScanMemoryAccount> memory_account_p,
	               bool regions_mode_p = false,
	               std::shared_ptr<const RegionCollection> regions_p = {},
	               std::string region_crs_p = {},
	               std::vector<Value> region_geometries_p = {},
	               std::vector<unique_ptr<Expression>> region_predicates_p = {},
	               bool variable_selected_p = false, RegionSamplingOptions sampling_options_p = {},
	               std::shared_ptr<const NearestSamplingPlan> nearest_plan_p = {})
	    : path(std::move(path_p)), schema(std::move(schema_p)), axes(std::move(axes_p)), spatial(std::move(spatial_p)),
	      temporal(std::move(temporal_p)), semantic_axes(std::move(semantic_axes_p)),
	      remote_session(std::move(remote_session_p)), metrics(std::move(metrics_p)),
	      memory_account(std::move(memory_account_p)),
	      axis_predicate_memory_account(std::make_shared<ScanMemoryAccount>(metrics, ScanMemoryComponent::Selector)),
	      spatial_predicate_memory_account(std::make_shared<ScanMemoryAccount>(metrics, ScanMemoryComponent::Selector)),
	      regions_mode(regions_mode_p), regions(std::move(regions_p)), region_crs(std::move(region_crs_p)),
	      region_collection_memory_account(regions ? std::make_shared<ScanMemoryAccount>(metrics, ScanMemoryComponent::RegionInput)
	                                               : nullptr),
	      region_bind_memory_account(regions ? std::make_shared<ScanMemoryAccount>(metrics, ScanMemoryComponent::RegionInput)
	                                         : nullptr),
	      region_geometries(std::move(region_geometries_p)), region_predicates(std::move(region_predicates_p)),
	      variable_selected(variable_selected_p), sampling_options(std::move(sampling_options_p)),
	      nearest_plan(std::move(nearest_plan_p)) {
		memory_account->Set(EstimateBindMemory(*this));
		if (region_collection_memory_account) {
			const auto binding_bytes = EstimateRegionBindCopyMemory(region_crs, region_geometries, region_predicates);
			regions->ValidateOwnedBytes(binding_bytes);
			region_collection_memory_account->Set(EstimateRegionInputMemory(*regions));
			region_bind_memory_account->Set(binding_bytes);
		}
		axis_predicate_memory_account->Set(AddMemoryBytes(sizeof(*axis_predicate),
		                                                  EstimateAxisPredicateMemory(*axis_predicate)));
		spatial_predicate_memory_account->Set(EstimateSpatialPredicateMemory(*spatial.predicate));
	}

	ReadOmBindData(const ReadOmBindData &other)
	    : TableFunctionData(other), path(other.path), schema(other.schema), axes(other.axes), spatial(other.spatial),
	      temporal(other.temporal), semantic_axes(other.semantic_axes), axis_predicate(other.axis_predicate),
	      remote_session(other.remote_session), metrics(other.metrics),
	      memory_account(std::make_shared<ScanMemoryAccount>(other.metrics, ScanMemoryComponent::Bind)),
	      axis_predicate_memory_account(other.axis_predicate_memory_account),
	      spatial_predicate_memory_account(other.spatial_predicate_memory_account),
	      regions_mode(other.regions_mode), regions(other.regions), region_crs(other.region_crs),
	      region_collection_memory_account(other.region_collection_memory_account),
	      region_bind_memory_account(regions ? std::make_shared<ScanMemoryAccount>(metrics, ScanMemoryComponent::RegionInput)
	                                         : nullptr),
	      region_geometries(other.region_geometries), variable_selected(other.variable_selected),
	      sampling_options(other.sampling_options), nearest_plan(other.nearest_plan) {
		region_predicates.reserve(other.region_predicates.size());
		for (const auto &predicate : other.region_predicates) region_predicates.emplace_back(predicate->Copy());
		memory_account->Set(EstimateBindMemory(*this));
		if (region_bind_memory_account) {
			const auto binding_bytes = EstimateRegionBindCopyMemory(region_crs, region_geometries, region_predicates);
			regions->ValidateOwnedBytes(binding_bytes);
			region_bind_memory_account->Set(binding_bytes);
		}
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<ReadOmBindData>(*this);
	}

	bool Equals(const FunctionData &other_p) const override {
		const auto &other = other_p.Cast<ReadOmBindData>();
		return path == other.path && variable_selected == other.variable_selected &&
		       regions_mode == other.regions_mode && region_crs == other.region_crs &&
		       ((!regions && !other.regions) || (regions && other.regions && regions->Equals(*other.regions))) &&
		       axes == other.axes && temporal == other.temporal &&
		       semantic_axes == other.semantic_axes &&
		       spatial.grid_signature == other.spatial.grid_signature &&
		       spatial.layout_name == other.spatial.layout_name && spatial.source == other.spatial.source &&
		       spatial.include_source == other.spatial.include_source && sampling_options == other.sampling_options &&
		       ((!nearest_plan && !other.nearest_plan) ||
		        (nearest_plan && other.nearest_plan && nearest_plan->Equals(*other.nearest_plan))) &&
		       spatial.object_id == other.spatial.object_id &&
		       SameSpatialPredicate(*spatial.predicate, *other.spatial.predicate) &&
		       SameAxisPredicate(*axis_predicate, *other.axis_predicate) && SameSchema(schema, other.schema);
	}

	bool SupportStatementCache() const override {
		return false;
	}

    std::string path;
    BoundSchema schema;
    AxisDeclarations axes;
	SpatialBindConfiguration spatial;
	std::optional<TemporalBindConfiguration> temporal;
    SemanticAxes semantic_axes;
	std::shared_ptr<AxisPredicate> axis_predicate = std::make_shared<AxisPredicate>();
	std::shared_ptr<RemoteReadSession> remote_session;
	std::shared_ptr<ScanMetrics> metrics;
	std::shared_ptr<ScanMemoryAccount> memory_account;
	std::shared_ptr<ScanMemoryAccount> axis_predicate_memory_account;
	std::shared_ptr<ScanMemoryAccount> spatial_predicate_memory_account;
	bool regions_mode = false;
	std::shared_ptr<const RegionCollection> regions;
	std::string region_crs;
	std::shared_ptr<ScanMemoryAccount> region_collection_memory_account;
	std::shared_ptr<ScanMemoryAccount> region_bind_memory_account;
	std::vector<Value> region_geometries;
	std::vector<unique_ptr<Expression>> region_predicates;
	bool variable_selected = false;
	RegionSamplingOptions sampling_options;
	std::shared_ptr<const NearestSamplingPlan> nearest_plan;
};

std::uint64_t AddMemoryBytes(std::uint64_t total, std::uint64_t bytes) {
	return total > UINT64_MAX - bytes ? UINT64_MAX : total + bytes;
}

std::uint64_t VectorMemoryBytes(std::size_t capacity, std::size_t element_size) {
	if (element_size != 0 && capacity > UINT64_MAX / element_size) return UINT64_MAX;
	return static_cast<std::uint64_t>(capacity * element_size);
}

std::uint64_t StringMemoryBytes(const std::string &value) {
	const auto capacity = static_cast<std::uint64_t>(value.capacity());
	return capacity == UINT64_MAX ? UINT64_MAX : capacity + 1;
}

std::uint64_t StringVectorMemoryBytes(const std::vector<std::string> &values) {
	std::uint64_t bytes = VectorMemoryBytes(values.capacity(), sizeof(std::string));
	for (const auto &value : values) bytes = AddMemoryBytes(bytes, StringMemoryBytes(value));
	return bytes;
}

std::uint64_t EstimateSchemaMemory(const BoundSchema &schema) {
	std::uint64_t bytes = 0;
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(schema.variables.capacity(), sizeof(BoundVariable)));
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(schema.shape.capacity(), sizeof(std::uint64_t)));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(schema.crs_wkt));
	for (const auto &variable : schema.variables) {
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(variable.canonical_path));
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(variable.column_name));
		bytes = AddMemoryBytes(bytes, StringVectorMemoryBytes(variable.inferred_axes));
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(variable.shape.capacity(), sizeof(std::uint64_t)));
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(variable.chunk_shape.capacity(), sizeof(std::uint64_t)));
	}
	return bytes;
}

std::uint64_t EstimateMetadataTreeMemory(const OmMetadataTree &tree) {
	std::uint64_t bytes = sizeof(tree);
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(tree.arrays.capacity(), sizeof(MetadataVariable)));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(tree.crs_wkt));
	for (const auto &variable : tree.arrays) {
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(variable.canonical_path));
		bytes = AddMemoryBytes(bytes, StringVectorMemoryBytes(variable.inferred_axes));
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(variable.shape.capacity(), sizeof(std::uint64_t)));
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(variable.chunk_shape.capacity(), sizeof(std::uint64_t)));
	}
	return bytes;
}

std::uint64_t EstimateSemanticAxesMemory(const SemanticAxes &axes) {
	std::uint64_t bytes = VectorMemoryBytes(axes.capacity(), sizeof(SemanticAxis));
	for (const auto &axis : axes) {
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(axis.axis_name));
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(axis.level_kind));
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(axis.unit));
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(axis.timestamps.capacity(), sizeof(timestamp_t)));
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(axis.numbers.capacity(), sizeof(double)));
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(axis.durations.capacity(), sizeof(interval_t)));
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(axis.integer_members.capacity(), sizeof(std::int64_t)));
		bytes = AddMemoryBytes(bytes, StringVectorMemoryBytes(axis.text_members));
	}
	return bytes;
}

std::uint64_t EstimateAxisDeclarationsMemory(const AxisDeclarations &axes) {
	std::uint64_t bytes = VectorMemoryBytes(axes.capacity(), sizeof(std::vector<std::string>));
	for (const auto &axis : axes) bytes = AddMemoryBytes(bytes, StringVectorMemoryBytes(axis));
	return bytes;
}

std::uint64_t EstimateSpatialLayoutMemory(const std::optional<SpatialLayout> &layout) {
	if (!layout) return 0;
	std::uint64_t bytes = 0;
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(layout->shape.capacity(), sizeof(std::uint64_t)));
	bytes = AddMemoryBytes(bytes, StringVectorMemoryBytes(layout->axes));
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(layout->strides.capacity(), sizeof(std::uint64_t)));
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(layout->non_spatial_axes.capacity(), sizeof(std::uint64_t)));
	return bytes;
}

std::uint64_t EstimateSpatialPredicateMemory(const SpatialPredicate &predicate) {
	std::uint64_t bytes = sizeof(predicate);
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(predicate.necessary_conditions.capacity(), sizeof(SpatialConstraint)));
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(predicate.longitude_union.capacity(), sizeof(SpatialInterval)));
	bytes = AddMemoryBytes(bytes, StringVectorMemoryBytes(predicate.fallback_reasons));
	return bytes;
}

std::uint64_t EstimateGridDefinitionMemory(const std::optional<GridDefinition> &definition) {
	if (!definition) return 0;
	std::uint64_t bytes = 0;
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(definition->coordinate_rule_id));
	if (const auto *gaussian = std::get_if<GaussianGrid>(&definition->geometry)) {
		bytes = AddMemoryBytes(bytes, gaussian->OwnedCapacityBytes());
	}
	return bytes;
}

std::uint64_t EstimateAxisPredicateMemory(const AxisPredicate &predicate) {
	std::uint64_t bytes = 0;
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(predicate.necessary_conditions.capacity(), sizeof(AxisConstraint)));
	for (const auto &condition : predicate.necessary_conditions) {
		if (condition.constant.type().id() == LogicalTypeId::VARCHAR) {
			bytes = AddMemoryBytes(bytes, StringMemoryBytes(condition.constant.GetValue<std::string>()));
		}
	}
	bytes = AddMemoryBytes(bytes, StringVectorMemoryBytes(predicate.fallback_reasons));
	return bytes;
}

std::uint64_t EstimateSpatialBindMemory(const SpatialBindConfiguration &spatial) {
	std::uint64_t bytes = 0;
	bytes = AddMemoryBytes(bytes, EstimateSpatialLayoutMemory(spatial.layout));
	bytes = AddMemoryBytes(bytes, EstimateGridDefinitionMemory(spatial.definition));
	bytes = AddMemoryBytes(bytes, EstimateGridDefinitionMemory(spatial.identity_definition));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.grid_signature));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.layout_name));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.source));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.grid_id));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.layout_id));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.object_id));
	return bytes;
}

std::uint64_t EstimateSpatialCopyMemory(const SpatialBindConfiguration &spatial) {
	std::uint64_t bytes = EstimateSpatialLayoutMemory(spatial.layout);
	bytes = AddMemoryBytes(bytes, EstimateGridDefinitionMemory(spatial.definition));
	bytes = AddMemoryBytes(bytes, EstimateGridDefinitionMemory(spatial.identity_definition));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.grid_signature));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.layout_name));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.source));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.grid_id));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.layout_id));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.object_id));
	return bytes;
}

std::uint64_t EstimateTemporalMemory(const std::optional<TemporalBindConfiguration> &temporal) {
	if (!temporal) return 0;
	return VectorMemoryBytes(temporal->values.capacity(), sizeof(timestamp_t));
}

std::uint64_t EstimateRegionPredicateMemory(const Expression &expression) {
	if (ExpressionClassOf(expression) == ExpressionClass::BOUND_FUNCTION) {
		const auto &function = expression.Cast<BoundFunctionExpression>();
		std::uint64_t bytes = sizeof(BoundFunctionExpression);
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(function.function.name));
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(function.function.extra_info));
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(function.function.catalog_name));
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(function.function.schema_name));
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(function.function.arguments.capacity(), sizeof(LogicalType)));
		bytes = AddMemoryBytes(bytes,
		                       VectorMemoryBytes(function.children.capacity(), sizeof(unique_ptr<Expression>)));
		if (function.bind_info) bytes = AddMemoryBytes(bytes, sizeof(FunctionData));
		for (const auto &child : function.children) {
			bytes = AddMemoryBytes(bytes, EstimateRegionPredicateMemory(*child));
		}
		return bytes;
	}
	if (ExpressionClassOf(expression) == ExpressionClass::BOUND_CONSTANT) {
		// Value and expression copies share the payload charged to the region input account.
		return sizeof(BoundConstantExpression);
	}
	if (ExpressionClassOf(expression) == ExpressionClass::BOUND_REF) return sizeof(BoundReferenceExpression);
	return sizeof(Expression);
}

std::uint64_t EstimateRegionInputMemory(const RegionCollection &regions) {
	return regions.GetOwnedBytes();
}

std::uint64_t EstimateRegionBindCopyMemory(const std::string &region_crs,
	                                        const std::vector<Value> &region_geometries,
	                                        const std::vector<unique_ptr<Expression>> &region_predicates) {
	std::uint64_t bytes = StringMemoryBytes(region_crs);
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(region_geometries.capacity(), sizeof(Value)));
	bytes = AddMemoryBytes(bytes,
	                       VectorMemoryBytes(region_predicates.capacity(), sizeof(unique_ptr<Expression>)));
	for (const auto &predicate : region_predicates) {
		if (predicate) bytes = AddMemoryBytes(bytes, EstimateRegionPredicateMemory(*predicate));
	}
	return bytes;
}

std::uint64_t EstimateBindMemory(const ReadOmBindData &data) {
	std::uint64_t bytes = sizeof(data);
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(data.path));
	bytes = AddMemoryBytes(bytes, EstimateSchemaMemory(data.schema));
	bytes = AddMemoryBytes(bytes, EstimateAxisDeclarationsMemory(data.axes));
	bytes = AddMemoryBytes(bytes, EstimateSpatialBindMemory(data.spatial));
	bytes = AddMemoryBytes(bytes, EstimateTemporalMemory(data.temporal));
	bytes = AddMemoryBytes(bytes, EstimateSemanticAxesMemory(data.semantic_axes));
	bytes = AddMemoryBytes(bytes, data.nearest_plan ? data.nearest_plan->EstimatedBytes() : 0);
	return bytes;
}

std::uint64_t EstimateSpatialSelectionMemory(const SpatialSelection &selection) {
	std::uint64_t bytes = 0;
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(selection.latitude_ranges.capacity(), sizeof(AxisRange)));
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(selection.longitude_ranges.capacity(), sizeof(AxisRange)));
	bytes = AddMemoryBytes(bytes, StringVectorMemoryBytes(selection.fallback_reasons));
	return bytes;
}

std::uint64_t EstimateProjectionMemory(const ProjectionPlan &projection) {
	std::uint64_t bytes = 0;
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(projection.GetOutputSlots().capacity(), sizeof(OutputColumn)));
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(projection.GetRequiredVariableIds().capacity(), sizeof(idx_t)));
	return bytes;
}

std::uint64_t EstimateSegmentsMemory(const std::vector<BatchSegment> &segments) {
	std::uint64_t bytes = VectorMemoryBytes(segments.capacity(), sizeof(BatchSegment));
	for (const auto &segment : segments) {
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(segment.read_offset.capacity(), sizeof(std::uint64_t)));
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(segment.read_count.capacity(), sizeof(std::uint64_t)));
	}
	return bytes;
}

std::optional<std::uint64_t> CurrentThreadCpuNanoseconds() {
	timespec value{};
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0 || value.tv_sec < 0 || value.tv_nsec < 0) return std::nullopt;
	const auto seconds = static_cast<std::uint64_t>(value.tv_sec);
	if (seconds > (std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(value.tv_nsec)) / 1000000000ULL)
		return std::nullopt;
	return seconds * 1000000000ULL + static_cast<std::uint64_t>(value.tv_nsec);
}

std::uint64_t NearestSamplingScratchUpperBound(std::size_t rank, std::size_t axis_count,
                                               std::size_t variable_count, bool status_map,
                                               std::uint64_t rows, const BoundSchema &schema) {
	std::uint64_t bytes = WorstCaseBatchMappingBytes(rank, rows);
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(static_cast<std::size_t>(rows), 2 * sizeof(std::uint64_t)));
	bytes = AddMemoryBytes(bytes, sizeof(std::vector<std::vector<float>>));
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(variable_count, sizeof(std::vector<float>)));
	bytes = AddMemoryBytes(bytes, VectorMemoryBytes(static_cast<std::size_t>(rows),
	                                               variable_count * sizeof(float)));
	bytes = AddMemoryBytes(bytes, 2048 + VectorMemoryBytes(axis_count, sizeof(Value) + sizeof(std::uint64_t)));
	if (status_map) {
		const auto per_variable = 4 * sizeof(Value) + sizeof(std::string) + 128;
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(variable_count, per_variable));
		for (const auto &variable : schema.variables) {
			// batch_statuses owns a canonical path key; constructing one output-row
			// variables MAP also owns a key for each column name at peak.
			bytes = AddMemoryBytes(bytes, StringMemoryBytes(variable.canonical_path));
			bytes = AddMemoryBytes(bytes, StringMemoryBytes(variable.column_name));
		}
	}
	return bytes;
}

void ObserveComplexFilter(ClientContext &, LogicalGet &get, FunctionData *bind_data,
                           vector<unique_ptr<Expression>> &filters) {
	if (bind_data && !filters.empty()) {
		auto &data = bind_data->Cast<ReadOmBindData>();
		data.metrics->MarkFilterCallbackInvoked();
		const auto output_column_base = data.schema.variables.size() + (data.spatial.HasGrid() ? 2 : 0) +
		                               (data.temporal ? 1 : 0);
		SemanticAxis metadata_time_axis;
		const auto has_semantic_time_axis = std::any_of(
		    data.semantic_axes.begin(), data.semantic_axes.end(),
		    [](const SemanticAxis &axis) { return axis.kind == SemanticAxisKind::Time; });
		const SemanticAxis *metadata_time_axis_ptr = nullptr;
		if (data.temporal && data.temporal->axis_index && !has_semantic_time_axis) {
			metadata_time_axis.kind = SemanticAxisKind::Time;
			metadata_time_axis.axis_name = "time";
			metadata_time_axis.axis_index = *data.temporal->axis_index;
			metadata_time_axis.axis_length = data.temporal->axis_length;
			metadata_time_axis.stride = data.temporal->stride;
			metadata_time_axis.output_type = LogicalType::TIMESTAMP;
			metadata_time_axis_ptr = &metadata_time_axis;
		}
		*data.axis_predicate = ExtractAxisPredicate(get, filters, output_column_base, data.semantic_axes,
		                                           metadata_time_axis_ptr);
		if (data.sampling_options.method != RegionMethod::Nearest) {
			if (data.spatial.grid && data.spatial.layout) {
				*data.spatial.predicate = ExtractSpatialPredicate(get, filters, data.schema.variables.size());
				const auto mode = data.spatial.predicate->has_unsupported_condition ? "fallback" : "full";
				data.metrics->SetSpatialSelection(mode, data.spatial.predicate->residual_filter_retained,
				                                  data.spatial.predicate->fallback_reasons,
				                                  CheckedShapeProduct(data.spatial.layout->shape));
			} else if (data.spatial.definition) {
				*data.spatial.predicate = ExtractSpatialPredicate(get, filters, data.schema.variables.size());
				const auto has_safe_spatial_filter = !data.spatial.predicate->necessary_conditions.empty() ||
				                                     !data.spatial.predicate->longitude_union.empty();
				const auto mode = has_safe_spatial_filter ? "restricted" :
				                  data.spatial.predicate->has_unsupported_condition ? "fallback" : "full";
				data.metrics->SetSpatialSelection(mode, data.spatial.predicate->residual_filter_retained,
				                                  data.spatial.predicate->fallback_reasons,
				                                  CheckedShapeProduct(data.spatial.layout->shape));
			} else {
				data.metrics->SetSpatialSelection("fallback", true, {"spatial_mapping_unavailable_in_callback"}, 0);
			}
		}
		if (data.memory_account) data.memory_account->Set(EstimateBindMemory(data));
		if (data.axis_predicate_memory_account) {
			data.axis_predicate_memory_account->Set(AddMemoryBytes(sizeof(*data.axis_predicate),
			                                                       EstimateAxisPredicateMemory(*data.axis_predicate)));
		}
		if (data.spatial_predicate_memory_account) {
			data.spatial_predicate_memory_account->Set(EstimateSpatialPredicateMemory(*data.spatial.predicate));
		}
	}
	// Preserve every expression. DuckDB rebuilds these as ordinary filters
	// after this callback when table-function filter pushdown is disabled.
}

bool RevalidateCompleteMetadata(ClientContext &context) {
	Value setting;
	return context.TryGetCurrentSetting("duckomo_revalidate_metadata", setting) && !setting.IsNull() &&
	       setting.GetValue<bool>();
}

BoundSchema MetadataForScan(ClientContext &context, OmV3Reader &reader, const BoundSchema &bound_schema,
                            const std::string &path, bool variable_selected) {
	if (!RevalidateCompleteMetadata(context)) return bound_schema;
	ScanMemoryAccount tree_account(reader.File().Metrics(), ScanMemoryComponent::Definitions);
	auto tree = ReadMetadataTree(reader);
	tree_account.Set(EstimateMetadataTreeMemory(tree));
	// Compare the same selected view; an unchanged full file must not be
	// compared to a one-variable binding as though an array disappeared.
	if (variable_selected) {
		tree.arrays.erase(std::remove_if(tree.arrays.begin(), tree.arrays.end(), [&](const MetadataVariable &array) {
			return array.canonical_path != bound_schema.variables.front().canonical_path;
		}), tree.arrays.end());
	}
	const auto current_schema = BuildBoundSchema(tree);
	if (!SameSchema(bound_schema, current_schema)) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "OM array metadata changed between bind and scan for '" + path + "'");
	}
	return current_schema;
}

idx_t NearestSamplingColumnIndex(const ReadOmBindData &bind_data) {
	idx_t result = bind_data.schema.variables.size();
	if (bind_data.spatial.HasGrid()) result += 2;
	if (bind_data.temporal) ++result;
	for (const auto &axis : bind_data.semantic_axes) {
		if (axis.kind != SemanticAxisKind::Time) ++result;
	}
	if (bind_data.regions_mode) ++result;
	return result;
}

bool SupportsNearestSamplingExtract(const FunctionData &bind_data_p, const LogicalIndex &column_index) {
	const auto &bind_data = bind_data_p.Cast<ReadOmBindData>();
	return bind_data.regions_mode && bind_data.sampling_options.method == RegionMethod::Nearest &&
	       column_index.index != DConstants::INVALID_INDEX && column_index.index == NearestSamplingColumnIndex(bind_data);
}

ProjectionPlan BuildReadOmProjection(const ReadOmBindData &bind_data, const std::vector<column_t> &column_ids,
                                     const std::vector<ColumnIndex> &column_indexes) {
	const bool has_sampling = bind_data.regions_mode && bind_data.sampling_options.method == RegionMethod::Nearest;
	if (has_sampling) {
		return ProjectionPlan(bind_data.schema, column_indexes, bind_data.spatial.HasGrid(), bind_data.temporal.has_value(),
		                      bind_data.semantic_axes, bind_data.spatial.include_source, bind_data.regions_mode,
		                      true, true);
	}
	return ProjectionPlan(bind_data.schema, column_ids, bind_data.spatial.HasGrid(), bind_data.temporal.has_value(),
	                      bind_data.semantic_axes, bind_data.spatial.include_source, bind_data.regions_mode, false);
}

struct ReadOmGlobalState final : GlobalTableFunctionState {
	ReadOmGlobalState(ClientContext &context, const ReadOmBindData &bind_data, const std::vector<column_t> &column_ids,
	                  const std::vector<ColumnIndex> &column_indexes, std::shared_ptr<ScanMetrics> metrics_p)
	    : path(bind_data.path), schema(bind_data.schema),
	      projection(BuildReadOmProjection(bind_data, column_ids, column_indexes)),
	      spatial(bind_data.spatial), temporal(bind_data.temporal), semantic_axes(bind_data.semantic_axes),
	      axis_predicate(*bind_data.axis_predicate), regions_mode(bind_data.regions_mode), regions(bind_data.regions),
	      sampling_options(bind_data.sampling_options), nearest_plan(bind_data.nearest_plan),
	      metrics(std::move(metrics_p)),
	      memory_control_account(std::make_shared<ScanMemoryAccount>(metrics, ScanMemoryComponent::GlobalControl)),
	      memory_definition_account(std::make_shared<ScanMemoryAccount>(metrics, ScanMemoryComponent::Definitions)),
	      memory_selector_account(std::make_shared<ScanMemoryAccount>(metrics, ScanMemoryComponent::Selector)),
	      reader(make_uniq<OmV3Reader>(OpenOmReadAt(context, bind_data.path, bind_data.remote_session, metrics,
	                                               ScanMetadataStage::Scan))) {
		if (regions_mode && projection.GetRequiredVariableIds().empty()) metrics->MarkValueDecodeNotRequired();
		RefreshMemoryAccount();
		// Default scans share immutable bind metadata; no name search or copy is
		// needed after DuckDB has resolved projected columns.
		if (RevalidateCompleteMetadata(context)) {
			ScanMemoryAccount schema_account(metrics, ScanMemoryComponent::Definitions);
			auto current_schema = MetadataForScan(context, *reader, schema, path, bind_data.variable_selected);
			schema_account.Set(EstimateSchemaMemory(current_schema));
		}
		if (spatial.HasGrid() && spatial.layout) {
			selection.residual_filter_retained = spatial.predicate->residual_filter_retained;
			selection.fallback_reasons = spatial.predicate->fallback_reasons;
			const auto has_safe_spatial_filter = !spatial.predicate->necessary_conditions.empty() ||
			                                     !spatial.predicate->longitude_union.empty();
			const auto bounded_geographic_domain = spatial.definition &&
			    (!std::holds_alternative<RegularGrid>(spatial.definition->geometry));
			const auto spatial_filter_covers_domain = bounded_geographic_domain && has_safe_spatial_filter &&
			    SpatialPredicateCoversGeographicDomain(*spatial.predicate);
			spatial_preflight_active = !regions_mode && has_safe_spatial_filter && !spatial_filter_covers_domain;
			selection.mode = spatial_filter_covers_domain ? SpatialSelectionMode::Full :
			                 has_safe_spatial_filter ? SpatialSelectionMode::Restricted :
			                 spatial.predicate->has_unsupported_condition ? SpatialSelectionMode::Fallback :
			                 SpatialSelectionMode::Full;
			if (spatial_filter_covers_domain) metrics->RecordCoordinatePreparation(0, true);
			selection.candidate_rows = CheckedShapeProduct(spatial.layout->shape);
			metrics->SetSpatialSelection(SpatialSelectionModeName(selection.mode),
			                             selection.residual_filter_retained, selection.fallback_reasons,
			                             selection.candidate_rows);
		}
		selection_cursor = make_uniq<AxisSelectionCursor>(schema.shape, semantic_axes, axis_predicate);
		if (regions_mode && sampling_options.method == RegionMethod::Nearest && projection.RequiresSamplingVariables()) {
			std::vector<std::string> state_paths;
			state_paths.reserve(projection.GetRequiredVariableIds().size());
			for (const auto variable_index : projection.GetRequiredVariableIds())
				state_paths.push_back(schema.variables.at(variable_index).canonical_path);
			metrics->SetNearestSamplingStatusDependencies(state_paths);
		}
		candidate_count = selection_cursor->CandidateCount();
		if (sampling_options.method == RegionMethod::Nearest) {
			if (!nearest_plan || !spatial.layout) throw InternalException("nearest scan has no immutable region plan or layout");
			nearest_axis_cursor = make_uniq<NearestAxisCursor>(*spatial.layout, *selection_cursor);
			nearest_axis_result_count = nearest_axis_cursor->ResultCount();
			candidate_count = nearest_axis_result_count;
		}
		const bool spatial_predicate_empty = sampling_options.method != RegionMethod::Nearest &&
		                                    spatial.HasGrid() && spatial.layout &&
		                                    SpatialPredicateProvesEmpty(*spatial.predicate);
		const bool all_regions_empty = regions_mode && regions &&
		    std::none_of(regions->GetRegions().begin(), regions->GetRegions().end(),
		                 [](const Region &region) { return !region.envelope.empty; });
		if ((sampling_options.method != RegionMethod::Nearest && selection_cursor->IsEmpty()) || spatial_predicate_empty ||
		    (regions_mode && sampling_options.method != RegionMethod::Nearest &&
		     (!regions || regions->GetRegions().empty() || all_regions_empty))) {
			no_candidates = true;
			scan_completed = true;
			std::uint64_t completed_candidate_rows = 0;
			if (regions_mode && all_regions_empty && !selection_cursor->IsEmpty() && !spatial_predicate_empty) {
				const auto region_count = static_cast<std::uint64_t>(regions->GetRegions().size());
				if (region_count != 0 && candidate_count > UINT64_MAX / region_count) {
					throw ReaderError(ReaderErrorCode::ShapeOverflow,
					                  "region-record candidate count exceeds unsigned 64-bit range");
				}
				completed_candidate_rows = candidate_count * region_count;
			}
			metrics->SetCandidateRows(completed_candidate_rows);
			metrics->SetCandidateCountEvidence(completed_candidate_rows, completed_candidate_rows,
			                                   completed_candidate_rows, true);
			metrics->RecordCoordinatePreparation(0, true);
			auto empty_reasons = selection.fallback_reasons;
			for (const auto &reason : axis_predicate.fallback_reasons) {
				if (std::find(empty_reasons.begin(), empty_reasons.end(), reason) == empty_reasons.end()) {
					empty_reasons.push_back(reason);
				}
			}
			if (spatial_predicate_empty) {
				for (const auto &reason : spatial.predicate->fallback_reasons) {
					if (std::find(empty_reasons.begin(), empty_reasons.end(), reason) == empty_reasons.end()) {
						empty_reasons.push_back(reason);
					}
				}
			}
			metrics->SetSpatialSelection("empty", true, std::move(empty_reasons), 0, true);
			if (regions_mode) {
				if (sampling_options.method != RegionMethod::Nearest && completed_candidate_rows != 0) {
					for (std::size_t index = 0; index < regions->GetRegions().size(); ++index) {
						metrics->RecordRegionCandidatePairs(selection_cursor->CandidateCount());
						metrics->RecordRegionEnvelopeRejects(selection_cursor->CandidateCount());
					}
				}
				metrics->SetRegionMatchCount(sampling_options.method == RegionMethod::Nearest
				                                 ? std::optional<std::uint64_t>() : std::optional<std::uint64_t>(0),
				                             sampling_options.method == RegionMethod::Nearest
				                                 ? std::optional<bool>(false) : std::optional<bool>(true));
				metrics->MarkValueDecodeNotRequired();
			}
			metrics->SetScanComplete(true);
			RefreshMemoryAccount();
			return;
		}
		if (spatial_preflight_active && !regions_mode) {
			native_window_cursor = make_uniq<NativeWindowCursor>(
			    *spatial.layout, !projection.GetRequiredVariableIds().empty());
		} else if (spatial.HasGrid()) {
			metrics->RecordCoordinatePreparation(0, true);
		}
		candidate_count = sampling_options.method == RegionMethod::Nearest
		                      ? nearest_axis_result_count : selection_cursor->CandidateCount();
		Value configured_limit;
		std::uint64_t requested_limit = static_cast<std::uint64_t>(context.db->NumberOfThreads());
		if (context.TryGetCurrentSetting("duckomo_max_threads", configured_limit)) {
			const auto configured = configured_limit.GetValue<std::int64_t>();
			if (configured > 0) requested_limit = std::min<std::uint64_t>(requested_limit,
			                                                           static_cast<std::uint64_t>(configured));
		}
		std::uint64_t available_tasks = native_window_cursor
		                                 ? native_window_cursor->WindowUpperBound(*selection_cursor)
		                                 : (candidate_count == 0
		                                        ? std::uint64_t(1)
		                                        : candidate_count / SCAN_TASK_POSITION_WINDOW +
		                                              (candidate_count % SCAN_TASK_POSITION_WINDOW != 0));
		std::uint64_t nearest_worker_budget = UINT64_MAX;
		if (regions_mode && sampling_options.method == RegionMethod::Nearest) {
			const bool status_map = projection.RequiresSamplingVariables();
			const auto per_row = NearestSamplingScratchUpperBound(schema.shape.size(),
			                                                    spatial.layout->non_spatial_axes.size(),
			                                                    projection.GetRequiredVariableIds().size(),
			                                                    status_map, 1, schema);
			if (per_row > 512 * 1024)
				throw ReaderError(ReaderErrorCode::Allocation, "nearest minimum worker scratch exceeds 512 KiB");
			std::uint64_t low = 1;
			constexpr std::uint64_t MAX_NEAREST_TASK_ROWS = 2048;
			std::uint64_t high = MAX_NEAREST_TASK_ROWS;
			while (low < high) {
				const auto middle = low + (high - low + 1) / 2;
				if (NearestSamplingScratchUpperBound(schema.shape.size(),
				                                    spatial.layout->non_spatial_axes.size(),
				                                    projection.GetRequiredVariableIds().size(),
				                                    status_map, middle, schema) <= 512 * 1024)
					low = middle;
				else
					high = middle - 1;
			}
			nearest_batch_limit = low;
			constexpr std::uint64_t MAX_NEAREST_SCHEDULER_BYTES = 8 * 1024 * 1024;
			constexpr std::uint64_t NEAREST_SCHEDULER_CONTROL_RESERVE = 512 * 1024;
			constexpr std::uint64_t MAX_NEAREST_WORKER_SCRATCH = 512 * 1024;
			nearest_worker_budget = std::max<std::uint64_t>(
			    1, (MAX_NEAREST_SCHEDULER_BYTES - NEAREST_SCHEDULER_CONTROL_RESERVE) /
			           MAX_NEAREST_WORKER_SCRATCH);
			const auto region_count = static_cast<std::uint64_t>(regions->GetRegions().size());
			const auto per_region_tasks = candidate_count / nearest_batch_limit +
			                              (candidate_count % nearest_batch_limit != 0);
			if (region_count != 0 && per_region_tasks > UINT64_MAX / region_count)
				throw ReaderError(ReaderErrorCode::ShapeOverflow, "nearest task count exceeds unsigned 64-bit range");
			available_tasks = std::max<std::uint64_t>(1, per_region_tasks * region_count);
		}
		if (regions_mode && sampling_options.method != RegionMethod::Nearest) {
			const auto per_region_tasks = candidate_count / SCAN_TASK_POSITION_WINDOW +
			                             (candidate_count % SCAN_TASK_POSITION_WINDOW != 0);
			const auto non_empty_regions = static_cast<std::uint64_t>(std::count_if(
			    regions->GetRegions().begin(), regions->GetRegions().end(),
			    [](const Region &region) { return !region.envelope.empty; }));
			available_tasks = per_region_tasks != 0 && non_empty_regions > UINT64_MAX / per_region_tasks
			                      ? UINT64_MAX
			                      : std::max<std::uint64_t>(1, per_region_tasks * non_empty_regions);
		}
		max_threads = static_cast<idx_t>(std::max<std::uint64_t>(
		    1, std::min({requested_limit, available_tasks, nearest_worker_budget})));
		auto selection_fallback_reasons = selection.fallback_reasons;
		for (const auto &reason : axis_predicate.fallback_reasons) {
			if (std::find(selection_fallback_reasons.begin(), selection_fallback_reasons.end(), reason) ==
			    selection_fallback_reasons.end()) selection_fallback_reasons.push_back(reason);
		}
		if (selection_cursor->BudgetFallback() &&
		    std::find(selection_fallback_reasons.begin(), selection_fallback_reasons.end(),
		              "axis_interval_payload_limit") == selection_fallback_reasons.end()) {
			selection_fallback_reasons.push_back("axis_interval_payload_limit");
		}
		const auto selection_mode = selection_cursor->BudgetFallback() ? "fallback" :
		                            axis_predicate.necessary_conditions.empty() ?
		                                (selection.mode == SpatialSelectionMode::Restricted ? "restricted" :
		                                 SpatialSelectionModeName(selection.mode)) : "axis_restricted";
		std::uint64_t selection_candidate_rows = candidate_count;
		if (regions_mode) {
			const auto region_count = static_cast<std::uint64_t>(regions->GetRegions().size());
			if (region_count != 0 && candidate_count > UINT64_MAX / region_count) {
				throw ReaderError(ReaderErrorCode::ShapeOverflow,
				                  sampling_options.method == RegionMethod::Nearest
				                      ? "nearest region-axis result count exceeds unsigned 64-bit range"
				                      : "region-record candidate count exceeds unsigned 64-bit range");
			}
			selection_candidate_rows = candidate_count * region_count;
		}
		metrics->SetSpatialSelection(selection_mode,
		                             true, std::move(selection_fallback_reasons), selection_candidate_rows,
		                             candidate_count == 0);
		if (regions_mode) {
			metrics->SetCandidateCountEvidence(std::nullopt, selection_candidate_rows, 0, false);
			metrics->SetRegionMatchCount(std::nullopt, false);
		} else if (spatial_preflight_active) {
			metrics->SetCandidateCountEvidence(std::nullopt, candidate_count, 0, false);
		} else {
			metrics->SetCandidateCountEvidence(candidate_count, candidate_count, candidate_count, true);
		}
		RefreshMemoryAccount();
	}

	~ReadOmGlobalState() override {
		if (metrics && !scan_completed && !metrics_status_set && !stop_requested.load()) {
			metrics->SetStatus(ScanStatus::Failed, "incomplete_scan");
		}
	}

	idx_t MaxThreads() const override {
		return max_threads;
	}

	bool GetNextNativeWindowTask(ClientContext &context, std::vector<std::uint64_t> &positions,
	                            const ScanMemoryAccount &task_positions_account) {
		while (true) {
			NativeWindow window;
			ScanMemoryAccount window_memory(metrics, ScanMemoryComponent::Selector);
			const auto descriptor_upper_bound =
			    AddMemoryBytes(sizeof(window), OM_MAX_RANK * sizeof(std::uint64_t));
			window_memory.Set(sizeof(window), descriptor_upper_bound);
			{
				std::lock_guard<std::mutex> guard(task_mutex);
				if (stop_requested.load() || no_candidates) return false;
				if (!native_window_cursor->Next(window, *selection_cursor, [&context]() {
					    if (context.IsInterrupted()) throw InterruptException();
				    })) {
					selection_exhausted = true;
					MaybeSetScanCompleteLocked();
					return false;
				}
				ClaimTaskLocked();
			}

			// Only cursor advancement and task accounting are serialized. Coordinate
			// preflight below may visit a full native window and must stay outside
			// task_mutex so other workers can claim independent windows.
			try {
				ScanMemoryAccount coordinate_memory(metrics, ScanMemoryComponent::CoordinateBuffers);
				coordinate_memory.Set(0, sizeof(GridCoordinate) + sizeof(NativeWindowCoordinate) +
				                            sizeof(std::function<void()>));
				NativeWindowCoordinate coordinate = [this](const NativeWindow &candidate, std::uint64_t offset) {
					return spatial.Coordinate(candidate, offset);
				};
				std::function<void()> interrupt_check = [&context]() {
					if (context.IsInterrupted()) throw InterruptException();
				};
				coordinate_memory.Set(sizeof(GridCoordinate) + sizeof(coordinate) + sizeof(interrupt_check));
				const auto preflight_payload_upper_bound =
				    std::min<std::uint64_t>(MAX_SELECTOR_PAYLOAD_BYTES,
				                            MAX_SPATIAL_SELECTOR_RANGES * sizeof(AxisRange));
				window_memory.Set(AddMemoryBytes(window.EstimatedBytes(), sizeof(NativeWindowPreflight)),
				                  AddMemoryBytes(AddMemoryBytes(window.EstimatedBytes(), sizeof(NativeWindowPreflight)),
				                                 preflight_payload_upper_bound));
				std::uint64_t coordinate_evaluations = 0;
				NativeWindowPreflight preflight;
				try {
					const auto axis = window.SpatialAxis();
					const bool spatial_axis = spatial.layout->flattened
					                              ? axis == spatial.layout->point_axis
					                              : axis == spatial.layout->latitude_axis ||
					                                    axis == spatial.layout->longitude_axis;
					if (sampling_options.method == RegionMethod::Nearest) {
						preflight.whole_window = true;
						preflight.coordinate_evaluations = 0;
					} else if (spatial_axis) {
						preflight = duckdb::duckomo::PreflightNativeWindow(
						    *spatial.predicate, window, coordinate, interrupt_check, coordinate_evaluations);
					} else {
						// Faster non-spatial axes share one coordinate and can be read contiguously.
						interrupt_check();
						preflight.whole_window = MatchesSpatialPredicate(*spatial.predicate, coordinate(window, 0));
						preflight.coordinate_evaluations = coordinate_evaluations = 1;
					}
				} catch (...) {
					metrics->RecordCoordinatePreparation(coordinate_evaluations, false);
					throw;
				}
				window_memory.Set(AddMemoryBytes(
				    AddMemoryBytes(window.EstimatedBytes(), sizeof(NativeWindowPreflight)),
				    VectorMemoryBytes(preflight.ranges.capacity(), sizeof(AxisRange))));
				metrics->RecordCoordinatePreparation(preflight.coordinate_evaluations, true);
				metrics->RecordSelectionWindow(preflight.whole_window ? 1 : preflight.ranges.size(),
				                               preflight.budget_fallback);
				if (preflight.budget_fallback) {
					auto reasons = selection.fallback_reasons;
					for (const auto &reason : spatial.predicate->fallback_reasons) {
						if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) reasons.push_back(reason);
					}
					if (std::find(reasons.begin(), reasons.end(), "native_window_range_limit") == reasons.end()) {
						reasons.push_back("native_window_range_limit");
					}
					metrics->SetSpatialSelection("fallback", true, std::move(reasons), candidate_count, false);
				}

				positions.clear();
				const auto visit_offsets = [&](auto &&visitor) {
					if (preflight.whole_window) {
						for (std::uint64_t offset = 0; offset < window.Count(); offset++) {
							if (!visitor(offset)) return false;
						}
					} else {
						for (const auto &range : preflight.ranges) {
							for (std::uint64_t offset = range.begin; offset < range.end; offset++) {
								if (!visitor(offset)) return false;
							}
						}
					}
					return true;
				};
				std::uint64_t mapped_positions = 0;
				std::uint64_t selected_count = 0;
				const bool axis_filtered = selection_cursor->HasConstraints();
				bool count_selected = true;
				if (axis_filtered) {
				count_selected = visit_offsets([&](std::uint64_t offset) {
					if ((mapped_positions++ & 255U) == 0) {
						if (context.IsInterrupted()) throw InterruptException();
						if (stop_requested.load()) return false;
					}
					if (selection_cursor->Contains(window.LogicalPosition(*spatial.layout, offset))) selected_count++;
					return true;
				});
			} else if (preflight.whole_window) {
				selected_count = window.Count();
			} else {
				for (const auto &range : preflight.ranges) selected_count += range.end - range.begin;
			}
				if (count_selected && spatial_preflight_active) {
					metrics->RecordObservedCandidateRecords(selected_count);
					observed_candidate_rows.fetch_add(selected_count, std::memory_order_relaxed);
				}
				if (count_selected && selected_count != 0) {
					const auto previous_capacity_bytes =
					    VectorMemoryBytes(positions.capacity(), sizeof(std::uint64_t));
					const auto requested_capacity_bytes =
					    VectorMemoryBytes(MAX_NATIVE_WINDOW_POINTS, sizeof(std::uint64_t));
					if (selected_count > positions.capacity()) {
						task_positions_account.Set(previous_capacity_bytes,
						                          AddMemoryBytes(previous_capacity_bytes, requested_capacity_bytes));
					}
					positions.reserve(static_cast<std::size_t>(selected_count));
					const auto task_position_bytes = VectorMemoryBytes(positions.capacity(), sizeof(std::uint64_t));
					const auto task_position_upper_bound = previous_capacity_bytes == task_position_bytes
					                                           ? task_position_bytes
					                                           : AddMemoryBytes(previous_capacity_bytes,
					                                                            task_position_bytes);
					task_positions_account.Set(task_position_bytes, task_position_upper_bound);
				mapped_positions = 0;
				const auto emitted = visit_offsets([&](std::uint64_t offset) {
					if ((mapped_positions++ & 255U) == 0) {
						if (context.IsInterrupted()) throw InterruptException();
						if (stop_requested.load()) return false;
					}
					const auto position = window.LogicalPosition(*spatial.layout, offset);
					if (!axis_filtered || selection_cursor->Contains(position)) positions.push_back(position);
					return true;
				});
				if (!emitted) positions.clear();
			}
				if (stop_requested.load()) {
					positions.clear();
					RecordTaskFailed(false);
					return false;
				}
				if (positions.empty()) {
					RecordTaskCompleted();
					continue;
				}
				return true;
			} catch (const InterruptException &) {
				RecordTaskFailed(true);
				throw;
			} catch (...) {
				RecordTaskFailed(false);
				throw;
			}
		}
	}

	bool GetNextRegionTask(ClientContext &context, std::vector<std::uint64_t> &positions,
	                       const ScanMemoryAccount &task_positions_account, std::uint64_t &region_ordinal) {
	while (true) {
		std::lock_guard<std::mutex> guard(task_mutex);
		if (stop_requested.load() || no_candidates || !regions) return false;
		const auto &items = regions->GetRegions();
		while (current_region_ordinal < items.size()) {
			const auto &region = items[current_region_ordinal];
			if (!region.envelope.empty) break;
			const auto candidate_pairs = selection_cursor->CandidateCount();
			metrics->RecordRegionCandidatePairs(candidate_pairs);
			metrics->RecordRegionEnvelopeRejects(candidate_pairs);
			metrics->RecordObservedCandidateRecords(candidate_pairs);
			++current_region_ordinal;
			selection_cursor = make_uniq<AxisSelectionCursor>(schema.shape, semantic_axes, axis_predicate);
		}
		if (current_region_ordinal >= items.size()) {
			selection_exhausted = true;
			MaybeSetScanCompleteLocked();
			return false;
		}
		const auto previous_capacity_bytes = VectorMemoryBytes(positions.capacity(), sizeof(std::uint64_t));
		if (positions.capacity() < SCAN_TASK_POSITION_WINDOW) {
			const auto requested_capacity_bytes = VectorMemoryBytes(SCAN_TASK_POSITION_WINDOW, sizeof(std::uint64_t));
			task_positions_account.Set(previous_capacity_bytes,
			                           AddMemoryBytes(previous_capacity_bytes, requested_capacity_bytes));
		}
		const auto position_count = selection_cursor->Next(SCAN_TASK_POSITION_WINDOW, positions, [&context](std::uint64_t) {
			if (context.IsInterrupted()) throw InterruptException();
			return true;
		});
		const auto task_position_bytes = VectorMemoryBytes(positions.capacity(), sizeof(std::uint64_t));
		if (task_position_bytes > SCAN_TASK_POSITION_WINDOW * sizeof(std::uint64_t)) {
			throw ReaderError(ReaderErrorCode::Allocation, "region task position map exceeds the 512 KiB limit");
		}
		const auto task_position_upper_bound = previous_capacity_bytes == task_position_bytes
		                                           ? task_position_bytes
		                                           : AddMemoryBytes(previous_capacity_bytes, task_position_bytes);
		task_positions_account.Set(task_position_bytes, task_position_upper_bound);
		if (position_count == 0) {
			++current_region_ordinal;
			selection_cursor = make_uniq<AxisSelectionCursor>(schema.shape, semantic_axes, axis_predicate);
			continue;
		}
		region_ordinal = current_region_ordinal;
		ClaimTaskLocked();
		return true;
	}
	}

	bool GetNextNearestTask(ClientContext &context, std::vector<std::uint64_t> &positions,
	                        const ScanMemoryAccount &task_positions_account, std::uint64_t &region_ordinal,
	                        std::vector<std::vector<std::uint64_t>> &non_spatial_indices) {
		const auto region_count = static_cast<std::uint64_t>(regions->GetRegions().size());
		while (true) {
			NearestAxisBatch axis_batch;
			if (context.IsInterrupted()) throw InterruptException();
			{
				std::lock_guard<std::mutex> guard(task_mutex);
				if (stop_requested.load() || no_candidates) return false;
				if (current_region_ordinal >= region_count) {
					selection_exhausted = true;
					MaybeSetScanCompleteLocked();
					return false;
				}
				axis_batch = nearest_axis_cursor->Next(nearest_batch_limit);
				if (axis_batch.indices.empty() && axis_batch.exhausted) {
					++current_region_ordinal;
					nearest_axis_cursor = make_uniq<NearestAxisCursor>(*spatial.layout, *selection_cursor);
					continue;
				}
				region_ordinal = current_region_ordinal;
				ClaimTaskLocked();
			}
			try {
				positions.clear();
				positions.reserve(axis_batch.indices.size());
				for (std::size_t index = 0; index < axis_batch.indices.size(); ++index) positions.push_back(index);
				non_spatial_indices = std::move(axis_batch.indices);
				std::uint64_t bytes = VectorMemoryBytes(positions.capacity(), sizeof(std::uint64_t));
				bytes = AddMemoryBytes(bytes, VectorMemoryBytes(non_spatial_indices.capacity(), sizeof(std::vector<std::uint64_t>)));
				for (const auto &indices : non_spatial_indices)
					bytes = AddMemoryBytes(bytes, VectorMemoryBytes(indices.capacity(), sizeof(std::uint64_t)));
				if (bytes > 512 * 1024) throw ReaderError(ReaderErrorCode::Allocation, "nearest task axis batch exceeds 512 KiB");
				metrics->RecordNearestSamplingTaskRows(non_spatial_indices.size());
				task_positions_account.Set(bytes, bytes);
				return true;
			} catch (const InterruptException &) {
				RecordTaskFailed(true);
				throw;
			} catch (...) {
				RecordTaskFailed(false);
				throw;
			}
		}
	}

	bool GetNextTask(ClientContext &context, std::vector<std::uint64_t> &positions,
	                 const ScanMemoryAccount &task_positions_account, std::uint64_t &region_ordinal,
	                 std::vector<std::vector<std::uint64_t>> *non_spatial_indices = nullptr) {
		if (regions_mode && sampling_options.method == RegionMethod::Nearest) {
			if (!non_spatial_indices) throw InternalException("nearest task has no non-spatial index output");
			return GetNextNearestTask(context, positions, task_positions_account, region_ordinal, *non_spatial_indices);
		}
		if (regions_mode) return GetNextRegionTask(context, positions, task_positions_account, region_ordinal);
		if (native_window_cursor) return GetNextNativeWindowTask(context, positions, task_positions_account);
		std::lock_guard<std::mutex> guard(task_mutex);
		if (stop_requested.load() || selection_exhausted || no_candidates) return false;
		const auto previous_capacity_bytes = VectorMemoryBytes(positions.capacity(), sizeof(std::uint64_t));
		if (positions.capacity() < SCAN_TASK_POSITION_WINDOW) {
			const auto requested_capacity_bytes =
			    VectorMemoryBytes(SCAN_TASK_POSITION_WINDOW, sizeof(std::uint64_t));
			task_positions_account.Set(previous_capacity_bytes,
			                           AddMemoryBytes(previous_capacity_bytes, requested_capacity_bytes));
		}
		const auto position_count = selection_cursor->Next(SCAN_TASK_POSITION_WINDOW, positions, [&context](std::uint64_t) {
			if (context.IsInterrupted()) throw InterruptException();
			return true;
		});
		const auto task_position_bytes = VectorMemoryBytes(positions.capacity(), sizeof(std::uint64_t));
		const auto task_position_upper_bound = previous_capacity_bytes == task_position_bytes
		                                           ? task_position_bytes
		                                           : AddMemoryBytes(previous_capacity_bytes, task_position_bytes);
		task_positions_account.Set(task_position_bytes, task_position_upper_bound);
		if (position_count == 0) {
			selection_exhausted = true;
			MaybeSetScanCompleteLocked();
			return false;
		}
		ClaimTaskLocked();
		return true;
	}

	void RecordTaskCompleted() {
		std::lock_guard<std::mutex> guard(task_mutex);
		if (outstanding_tasks > 0) --outstanding_tasks;
		metrics->RecordScanTaskCompleted();
		MaybeSetScanCompleteLocked();
	}

	void RecordTaskFailed(bool cancelled) {
		std::lock_guard<std::mutex> guard(task_mutex);
		if (outstanding_tasks > 0) --outstanding_tasks;
		if (cancelled) metrics->RecordScanTaskCancelled();
		else metrics->RecordScanTaskFailed();
	}

	bool RequestStop() noexcept {
		return !stop_requested.exchange(true);
	}

private:
	void RefreshMemoryAccount() const {
		if (!memory_control_account) return;
		std::uint64_t control_bytes = sizeof(*this);
		control_bytes = AddMemoryBytes(control_bytes, EstimateSchemaMemory(schema));
		control_bytes = AddMemoryBytes(control_bytes, EstimateProjectionMemory(projection));
		control_bytes = AddMemoryBytes(control_bytes, EstimateTemporalMemory(temporal));
		control_bytes = AddMemoryBytes(control_bytes, EstimateSemanticAxesMemory(semantic_axes));
		control_bytes = AddMemoryBytes(control_bytes, sizeof(OmV3Reader));
		memory_control_account->Set(control_bytes);
		memory_definition_account->Set(EstimateSpatialCopyMemory(spatial));
		std::uint64_t selector_bytes = EstimateAxisPredicateMemory(axis_predicate);
		selector_bytes = AddMemoryBytes(selector_bytes, EstimateSpatialSelectionMemory(selection));
		if (selection_cursor) selector_bytes = AddMemoryBytes(selector_bytes, selection_cursor->EstimatedBytes());
		if (nearest_axis_cursor) selector_bytes = AddMemoryBytes(selector_bytes, sizeof(NearestAxisCursor));
		if (native_window_cursor) selector_bytes = AddMemoryBytes(selector_bytes, native_window_cursor->EstimatedBytes());
		memory_selector_account->Set(selector_bytes);
	}

	void ClaimTaskLocked() {
		++outstanding_tasks;
		metrics->RecordScanTaskCreated();
		metrics->RecordScanTaskClaimed();
	}

	void MaybeSetScanCompleteLocked() {
		if (selection_exhausted && outstanding_tasks == 0 && !stop_requested.load() && !scan_completed) {
			scan_completed = true;
			if (regions_mode && sampling_options.method != RegionMethod::Nearest) {
				const auto region_metrics = metrics->Snapshot();
				metrics->SetCandidateRows(region_metrics.candidate_pairs_observed);
				metrics->SetCandidateCountEvidence(region_metrics.candidate_pairs_observed,
				                                   candidate_count *
				                                       static_cast<std::uint64_t>(regions->GetRegions().size()),
				                                   region_metrics.candidate_pairs_observed, true);
				metrics->SetRegionMatchCount(region_metrics.matched_pairs_observed, true);
				metrics->MarkValueDecodeNotRequired();
			}
			if (spatial_preflight_active) {
				const auto exact_count = observed_candidate_rows.load(std::memory_order_relaxed);
				metrics->SetCandidateCountEvidence(exact_count, candidate_count, exact_count, true);
				metrics->SetCandidateRows(exact_count);
				if (exact_count == 0) {
					auto reasons = selection.fallback_reasons;
					for (const auto &reason : axis_predicate.fallback_reasons) {
						if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) reasons.push_back(reason);
					}
					if (selection_cursor->BudgetFallback() &&
					    std::find(reasons.begin(), reasons.end(), "axis_interval_payload_limit") == reasons.end()) {
						reasons.push_back("axis_interval_payload_limit");
					}
					metrics->SetSpatialSelection("empty", true, std::move(reasons), 0, false);
				}
			}
			if (regions_mode && sampling_options.method == RegionMethod::Nearest) {
				const auto snapshot = metrics->Snapshot();
				metrics->SetNearestSamplingResultCount(snapshot.sampling_result_rows_observed, true);
				metrics->SetCandidateCountEvidence(snapshot.sampling_result_rows_observed,
				                                   snapshot.sampling_result_upper_bound,
				                                   snapshot.sampling_result_rows_observed, true);
				metrics->SetCandidateRows(snapshot.sampling_result_rows_observed);
				metrics->SetRegionMatchCount(std::nullopt, false);
				const auto completed_snapshot = metrics->Snapshot();
				bool status_complete = completed_snapshot.sampling_status_rows_observed ==
				                      completed_snapshot.sampling_result_rows_observed;
				for (const auto &path : completed_snapshot.sampling_required_state_variables) {
					const auto found = completed_snapshot.sampling_variable_status.find(path);
					const auto rows = found == completed_snapshot.sampling_variable_status.end() ? 0 : found->second.rows_observed;
					if (rows != completed_snapshot.sampling_result_rows_observed) status_complete = false;
				}
				metrics->SetNearestSamplingStatusDependencyComplete(status_complete);
			}
			metrics->SetScanComplete(true);
		}
	}

public:

    std::string path;
    BoundSchema schema;
    ProjectionPlan projection;
    SpatialBindConfiguration spatial;
	std::optional<TemporalBindConfiguration> temporal;
	SemanticAxes semantic_axes;
	AxisPredicate axis_predicate;
	SpatialSelection selection;
	unique_ptr<AxisSelectionCursor> selection_cursor;
	unique_ptr<NearestAxisCursor> nearest_axis_cursor;
	unique_ptr<NativeWindowCursor> native_window_cursor;
	bool regions_mode = false;
	std::shared_ptr<const RegionCollection> regions;
	RegionSamplingOptions sampling_options;
	std::shared_ptr<const NearestSamplingPlan> nearest_plan;
	std::uint64_t nearest_axis_result_count = 0;
	std::uint64_t current_region_ordinal = 0; // guarded by task_mutex
    std::shared_ptr<ScanMetrics> metrics;
	    std::shared_ptr<ScanMemoryAccount> memory_control_account;
	    std::shared_ptr<ScanMemoryAccount> memory_definition_account;
	    std::shared_ptr<ScanMemoryAccount> memory_selector_account;
    unique_ptr<OmV3Reader> reader;
	mutable std::mutex task_mutex;
	std::atomic<bool> stop_requested{false};
	std::atomic<std::uint64_t> next_worker_id{0};
	bool selection_exhausted = false;
	std::uint64_t outstanding_tasks = 0; // guarded by task_mutex
	std::atomic<std::uint64_t> observed_candidate_rows{0};
	bool spatial_preflight_active = false;
	bool no_candidates = false;
	std::uint64_t candidate_count = 0;
	std::uint64_t nearest_batch_limit = 256;
	idx_t max_threads = 1;
    bool scan_completed = false;
	std::atomic<bool> metrics_status_set{false};
};

struct ReadOmLocalState final : LocalTableFunctionState {
	ReadOmLocalState(ClientContext &context, const ReadOmBindData &bind_data, const ReadOmGlobalState &global_state,
	                 std::uint64_t worker_id_p)
	    : worker_id(worker_id_p), memory_account(std::make_shared<ScanMemoryAccount>(
	          bind_data.metrics, ScanMemoryComponent::GlobalControl)),
	      task_positions_account(std::make_shared<ScanMemoryAccount>(bind_data.metrics,
	                                                               ScanMemoryComponent::TaskPositions)),
	      region_evaluator_account(std::make_shared<ScanMemoryAccount>(bind_data.metrics,
	                                                                 ScanMemoryComponent::Selector)) {
		RefreshMemoryAccount();
		if (global_state.no_candidates) {
			return;
		}
		reader = make_uniq<OmV3Reader>(OpenOmReadAt(context, bind_data.path, bind_data.remote_session,
		                                             bind_data.metrics, ScanMetadataStage::Scan));
		ScanMemoryAccount schema_account(bind_data.metrics, ScanMemoryComponent::Definitions);
		std::optional<BoundSchema> revalidated;
		if (RevalidateCompleteMetadata(context)) {
			revalidated = MetadataForScan(context, *reader, global_state.schema, bind_data.path, bind_data.variable_selected);
			schema_account.Set(EstimateSchemaMemory(*revalidated));
		}
		const auto &current_schema = revalidated ? *revalidated : global_state.schema;
		decoders.reserve(global_state.projection.GetRequiredVariableIds().size());
		for (const auto variable_index : global_state.projection.GetRequiredVariableIds()) {
			const auto &variable = current_schema.variables.at(variable_index);
			auto decoder = make_uniq<OmDecoderState>(BorrowedOmVariable(variable.metadata_owner));
			EnableOmDecoderMemoryAccounting(*decoder, bind_data.metrics);
			decoders.emplace_back(std::move(decoder));
		}
		RefreshMemoryAccount();
	}

	void RefreshMemoryAccount() const {
		std::uint64_t bytes = sizeof(*this);
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(decoders.capacity(), sizeof(unique_ptr<OmDecoderState>)));
		if (reader) bytes = AddMemoryBytes(bytes, sizeof(OmV3Reader));
		memory_account->Set(bytes);
		std::uint64_t task_bytes = VectorMemoryBytes(task_positions.capacity(), sizeof(std::uint64_t));
		task_bytes = AddMemoryBytes(task_bytes,
		                            VectorMemoryBytes(task_non_spatial_indices.capacity(), sizeof(std::vector<std::uint64_t>)));
		for (const auto &indices : task_non_spatial_indices)
			task_bytes = AddMemoryBytes(task_bytes, VectorMemoryBytes(indices.capacity(), sizeof(std::uint64_t)));
		task_positions_account->Set(task_bytes);
		region_evaluator_account->Set(region_evaluator_memory_bytes);
	}

	unique_ptr<OmV3Reader> reader;
	vector<unique_ptr<OmDecoderState>> decoders;
	std::vector<std::uint64_t> task_positions;
	std::vector<std::vector<std::uint64_t>> task_non_spatial_indices;
	idx_t task_position_offset = 0;
	std::uint64_t task_region_ordinal = DConstants::INVALID_INDEX;
	std::uint64_t active_region_ordinal = DConstants::INVALID_INDEX;
	unique_ptr<RegionPredicateEvaluator> region_evaluator;
	std::uint64_t region_evaluator_memory_bytes = 0;
	std::uint64_t worker_id;
	std::shared_ptr<ScanMemoryAccount> memory_account;
	std::shared_ptr<ScanMemoryAccount> task_positions_account;
	std::shared_ptr<ScanMemoryAccount> region_evaluator_account;
	bool registered_as_active = false;
	bool task_active = false;
};

unique_ptr<FunctionData> BindReadOmInternal(ClientContext &context, TableFunctionBindInput &input,
                                            vector<LogicalType> &return_types, TableFunctionColumnNames &names,
                                            bool regions_mode) {
	auto metrics = CreateScanMetrics(context);
	RegisterScanMetricsQueryState(context, metrics);
	if (regions_mode) metrics->SetOperation("read_om_regions");
	RegionSamplingOptions sampling_options;
	if (regions_mode) {
		sampling_options = ParseRegionSamplingOptions(GetTableFunctionNamedArgument(input, "method"),
		                                             GetTableFunctionNamedArgument(input, "max_distance_m"),
		                                             GetTableFunctionNamedArgument(input, "out_of_bounds"));
	}
	auto memory_account = std::make_shared<ScanMemoryAccount>(metrics, ScanMemoryComponent::Bind);
	std::shared_ptr<const RegionCollection> regions;
	std::string region_crs;
	if (regions_mode) {
		const auto *regions_value = GetTableFunctionNamedArgument(input, "regions");
		const auto *region_crs_value = GetTableFunctionNamedArgument(input, "region_crs");
		if (regions_value == nullptr) throw BinderException("[region_input] regions is required");
		if (region_crs_value == nullptr) throw BinderException("[region_reference] region_crs is required");
		ValidateRegionCrs(*region_crs_value);
		region_crs = StringValue::Get(*region_crs_value);
		regions = std::make_shared<RegionCollection>(RegionCollection::Build(*regions_value, [&context]() {
			if (context.IsInterrupted()) throw InterruptException();
		}));
	}
	if (input.inputs.size() != 1) {
		throw BinderException("read_om expects one constant VARCHAR path");
	}
	if (input.inputs[0].IsNull() || input.inputs[0].type().id() != LogicalTypeId::VARCHAR) {
		throw BinderException("read_om expects one constant VARCHAR path");
	}
	const auto requested_path = input.inputs[0].GetValue<std::string>();
	ScanMemoryAccount requested_path_account(metrics, ScanMemoryComponent::Bind);
	requested_path_account.Set(AddMemoryBytes(sizeof(requested_path), StringMemoryBytes(requested_path)));
	std::shared_ptr<RemoteReadSession> remote_session;
	std::string path;
	std::unique_ptr<ReadAtFile> input_file;
	if (IsSupportedRemoteOmUri(requested_path)) {
		remote_session = RemoteReadSession::Create(context, requested_path, metrics);
		path = remote_session->Path();
		input_file = remote_session->Open(context, ScanMetadataStage::Bind);
	} else {
		path = LocalFilePathFromValue(input.inputs[0]);
		metrics->SetTransportNotApplicable();
		input_file = std::make_unique<LocalFile>(LocalFile::Open(context, path, metrics, ScanMetadataStage::Bind));
	}
	OmV3Reader reader(std::move(input_file));
	ScanMemoryAccount bind_reader_account(metrics, ScanMemoryComponent::GlobalControl);
	bind_reader_account.Set(sizeof(reader));
	ScanMemoryAccount metadata_tree_account(metrics, ScanMemoryComponent::Definitions);
	std::optional<std::string> selected_variable;
	if (const auto *variable_value = GetTableFunctionNamedArgument(input, "variable")) {
		if (variable_value->IsNull() || variable_value->type().id() != LogicalTypeId::VARCHAR ||
		    variable_value->GetValue<std::string>().empty()) {
			throw BinderException("read_om variable must be a nonempty VARCHAR");
		}
		selected_variable = variable_value->GetValue<std::string>();
	}
	auto metadata_tree = selected_variable ? ReadNamedMetadataTree(reader, *selected_variable) : ReadMetadataTree(reader);
	metadata_tree_account.Set(EstimateMetadataTreeMemory(metadata_tree));
	auto schema = BuildBoundSchema(metadata_tree);
	memory_account->Set(AddMemoryBytes(
	    sizeof(ReadOmBindData), AddMemoryBytes(StringMemoryBytes(path), EstimateSchemaMemory(schema))));
	for (const auto &variable : schema.variables) {
		metrics->DeclareVariable(variable.canonical_path);
	}

	const auto *dimensions = GetTableFunctionNamedArgument(input, "dimensions");
	auto axes = ValidateAxisDeclarations(dimensions, schema);
	memory_account->Set(AddMemoryBytes(
	    AddMemoryBytes(sizeof(ReadOmBindData), StringMemoryBytes(path)),
	    AddMemoryBytes(EstimateSchemaMemory(schema), EstimateAxisDeclarationsMemory(axes))));
	auto spatial = BindSpatialConfiguration(input, schema, axes, *metrics, context, regions_mode);
	memory_account->Set(AddMemoryBytes(
	    AddMemoryBytes(sizeof(ReadOmBindData), StringMemoryBytes(path)),
	    AddMemoryBytes(AddMemoryBytes(EstimateSchemaMemory(schema), EstimateAxisDeclarationsMemory(axes)),
	                   EstimateSpatialBindMemory(spatial))));
	if (const auto *source_value = GetTableFunctionNamedArgument(input, "include_source")) {
		if (source_value->IsNull() || source_value->type().id() != LogicalTypeId::BOOLEAN) {
			throw BinderException("read_om include_source must be a non-NULL BOOLEAN");
		}
		spatial.include_source = source_value->GetValue<bool>();
	}
	if (spatial.include_source) {
		if (!spatial.HasGrid() || !spatial.identity_definition || !spatial.layout) {
			throw BinderException("read_om include_source requires a bound grid or domain");
		}
		spatial.object_id = OpaqueObjectId(remote_session ? remote_session->Uri() : path);
	}
	std::vector<std::string> spatial_axis_names;
	if (spatial.layout) {
		if (spatial.layout->flattened) {
			spatial_axis_names.emplace_back(spatial.layout->axes.at(spatial.layout->point_axis));
		} else {
			spatial_axis_names.emplace_back(spatial.layout->axes.at(spatial.layout->latitude_axis));
			spatial_axis_names.emplace_back(spatial.layout->axes.at(spatial.layout->longitude_axis));
		}
	}
	const auto *semantic_axes_value = GetTableFunctionNamedArgument(input, "axes");
	auto semantic_axes = BindSemanticAxes(semantic_axes_value, schema, axes, spatial_axis_names);
	auto temporal = BindTemporalConfiguration(input, schema, axes, semantic_axes);
	if (sampling_options.method == RegionMethod::Nearest && temporal && temporal->axis_index && spatial.layout &&
	    std::find(spatial.layout->non_spatial_axes.begin(), spatial.layout->non_spatial_axes.end(),
	              *temporal->axis_index) == spatial.layout->non_spatial_axes.end()) {
		throw BinderException("[region_sampling] temporal axis must not overlap a spatial axis for method='nearest'");
	}
	if (temporal && temporal->axis_index &&
	    std::none_of(semantic_axes.begin(), semantic_axes.end(),
	                 [](const SemanticAxis &axis) { return axis.kind == SemanticAxisKind::Time; })) {
		// Metadata or valid_times can expose a timestamp column without an
		// axes.time declaration. Keep that mapping in the selector model too, so
		// typed valid_time predicates can become logical-axis ranges. ProjectionPlan
		// skips Time semantic axes because the temporal output column is already
		// emitted from TemporalBindConfiguration.
		SemanticAxis selection_time_axis;
		selection_time_axis.kind = SemanticAxisKind::Time;
		selection_time_axis.axis_name = "time";
		selection_time_axis.axis_index = *temporal->axis_index;
		selection_time_axis.axis_length = temporal->axis_length;
		selection_time_axis.stride = temporal->stride;
		selection_time_axis.output_type = LogicalType::TIMESTAMP;
		selection_time_axis.regular = temporal->regular;
		selection_time_axis.timestamp_start = temporal->start;
		selection_time_axis.timestamp_step_micros = temporal->step_micros;
		selection_time_axis.timestamps = temporal->values;
		semantic_axes.emplace_back(std::move(selection_time_axis));
	}
	std::uint64_t staged_bind_bytes = sizeof(ReadOmBindData);
	staged_bind_bytes = AddMemoryBytes(staged_bind_bytes, StringMemoryBytes(path));
	staged_bind_bytes = AddMemoryBytes(staged_bind_bytes, EstimateSchemaMemory(schema));
	staged_bind_bytes = AddMemoryBytes(staged_bind_bytes, EstimateAxisDeclarationsMemory(axes));
	staged_bind_bytes = AddMemoryBytes(staged_bind_bytes, EstimateSpatialBindMemory(spatial));
	staged_bind_bytes = AddMemoryBytes(staged_bind_bytes, EstimateTemporalMemory(temporal));
	staged_bind_bytes = AddMemoryBytes(staged_bind_bytes, EstimateSemanticAxesMemory(semantic_axes));
	memory_account->Set(staged_bind_bytes);
	std::vector<Value> region_geometries;
	std::vector<unique_ptr<Expression>> region_predicates;
	std::shared_ptr<const NearestSamplingPlan> nearest_plan;
	if (regions_mode) {
		if (!spatial.HasGrid() || !spatial.layout) {
			throw BinderException("[region_reference] read_om_regions requires a bound grid or domain");
		}
		const auto geometry_prepare_start = std::chrono::steady_clock::now();
		RegionGeometryAdapter adapter(context);
		if (sampling_options.method == RegionMethod::Nearest) adapter.RequireNearestOperations();
		region_geometries.reserve(regions->GetRegions().size());
		region_predicates.reserve(regions->GetRegions().size());
		auto region_binding_bytes = EstimateRegionBindCopyMemory(region_crs, region_geometries, region_predicates);
		regions->ValidateOwnedBytes(region_binding_bytes);
		std::vector<NearestRegionTarget> nearest_targets;
		std::uint64_t nearest_target_prepare_ns = 0;
		if (sampling_options.method == RegionMethod::Nearest) nearest_targets.reserve(regions->GetRegions().size());
		for (idx_t ordinal = 0; ordinal < regions->GetRegions().size(); ++ordinal) {
			if (context.IsInterrupted()) throw InterruptException();
			const auto &region = regions->GetRegions()[ordinal];
			const std::string wkb(region.wkb.begin(), region.wkb.end());
			Value geometry;
			try {
				geometry = adapter.CreateGeometry(wkb);
				if (!adapter.IsValid(geometry)) {
					throw BinderException("[region_input] region ordinal %llu is not a valid Polygon or MultiPolygon",
					                     static_cast<unsigned long long>(ordinal + 1));
				}
			} catch (const BinderException &) {
				throw;
			} catch (const std::exception &error) {
				throw BinderException("[region_input] region ordinal %llu could not be validated: %s",
				                     static_cast<unsigned long long>(ordinal + 1), error.what());
			}
			if (sampling_options.method == RegionMethod::Nearest) {
				NearestRegionTarget target;
				target.empty = region.envelope.empty;
				if (!target.empty) {
					try {
						const auto target_started = std::chrono::steady_clock::now();
						target.representative = adapter.PointOnSurfaceCoordinate(geometry);
						const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
						    std::chrono::steady_clock::now() - target_started).count());
						if (nearest_target_prepare_ns > UINT64_MAX - elapsed)
							throw BinderException("[region_sampling] target preparation time exceeds unsigned 64-bit range");
						nearest_target_prepare_ns += elapsed;
					} catch (const std::exception &error) {
						throw BinderException("[region_sampling] region ordinal %llu representative point failed: %s",
						                     static_cast<unsigned long long>(ordinal + 1), error.what());
					}
				}
				nearest_targets.emplace_back(std::move(target));
			} else {
				auto predicate = adapter.BindCovers(geometry, 0, 1);
				const auto predicate_bytes = EstimateRegionPredicateMemory(*predicate);
				// Check before retaining this region's expression and geometry. The collection
				// already reserves the audited geometry estimate for every region.
				if (predicate_bytes > REGION_MAX_OWNED_BYTES - regions->ValidateOwnedBytes(region_binding_bytes)) {
					throw BinderException("[region_input_limit] owned region storage exceeds the 64 MiB collection limit");
				}
				region_binding_bytes += predicate_bytes;
				region_predicates.emplace_back(std::move(predicate));
			}
			region_geometries.emplace_back(std::move(geometry));
		}
		const auto geometry_prepare_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
		    std::chrono::steady_clock::now() - geometry_prepare_start).count());
		metrics->RecordRegionPrepareNanoseconds(geometry_prepare_ns);
		std::uint64_t input_wkb_bytes = 0;
		std::uint64_t empty_region_count = 0;
		const auto owned_input_bytes = regions->ValidateOwnedBytes(
		    EstimateRegionBindCopyMemory(region_crs, region_geometries, region_predicates));
		for (const auto &region : regions->GetRegions()) {
			input_wkb_bytes = AddMemoryBytes(input_wkb_bytes, region.wkb.size());
			if (region.envelope.empty) ++empty_region_count;
		}
		metrics->SetRegionInputMetrics(regions->GetRegions().size(), empty_region_count, input_wkb_bytes,
		                               regions->GetTotalVertices(), owned_input_bytes);
		if (sampling_options.method == RegionMethod::Nearest) {
			const auto *regular = spatial.identity_definition
			                         ? std::get_if<RegularGrid>(&spatial.identity_definition->geometry)
			                         : nullptr;
			if (regular == nullptr)
				throw BinderException("[sampling_capability] nearest requires a regular spherical longitude/latitude grid");
			NearestGridCapability capability;
			try {
				capability = QualifyNearestGrid(*spatial.identity_definition, [&context]() {
					return context.IsInterrupted();
				});
			} catch (const ReaderError &error) {
				if (error.Code() == ReaderErrorCode::Cancelled) throw InterruptException();
				throw BinderException("[sampling_capability] %s", error.what());
			}
			std::string spatial_version;
			try {
				spatial_version = adapter.SpatialVersion();
			} catch (const std::exception &error) {
				throw BinderException("[spatial_dependency] %s", error.what());
			}
			const auto locator_wall_started = std::chrono::steady_clock::now();
			const auto locator_cpu_started = CurrentThreadCpuNanoseconds();
			nearest_plan = BuildNearestSamplingPlan(
			    *regular, capability, nearest_targets, spatial_version, spatial.grid_id, spatial.layout_id,
			    sampling_options.max_distance_m, sampling_options.out_of_bounds, [&context]() {
					return context.IsInterrupted();
			    });
			const auto locator_wall_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
			    std::chrono::steady_clock::now() - locator_wall_started).count());
			const auto locator_cpu_finished = CurrentThreadCpuNanoseconds();
			std::optional<std::uint64_t> locator_cpu_ns;
			if (locator_cpu_started && locator_cpu_finished && *locator_cpu_finished >= *locator_cpu_started)
				locator_cpu_ns = *locator_cpu_finished - *locator_cpu_started;
			std::uint64_t non_spatial_results = 1;
			for (const auto axis : spatial.layout->non_spatial_axes) {
				const auto length = spatial.layout->shape.at(axis);
				if (length != 0 && non_spatial_results > static_cast<std::uint64_t>(INT64_MAX) / length)
					throw BinderException("[region_sampling] region-axis result count exceeds signed 64-bit range");
				non_spatial_results *= length;
			}
			if (spatial.layout->non_spatial_axes.empty()) non_spatial_results = 1;
			const auto region_count = static_cast<std::uint64_t>(regions->GetRegions().size());
			if (region_count != 0 && non_spatial_results > static_cast<std::uint64_t>(INT64_MAX) / region_count)
				throw BinderException("[region_sampling] region-axis result count exceeds signed 64-bit range");
			metrics->SetNearestSamplingIdentity("nearest", "sphere_haversine_f64_v1", "point_on_surface_v1",
			                                    spatial_version, spatial.grid_id, spatial.layout_id, "grid_sphere",
			                                    capability.earth_radius_m, region_count,
			                                    non_spatial_results * region_count, nearest_target_prepare_ns);
			std::uint64_t coordinate_comparisons = 0;
			for (const auto &entry : nearest_plan->regions) {
				if (coordinate_comparisons > UINT64_MAX - entry.location.coordinate_comparisons)
					throw BinderException("[region_sampling] coordinate comparison count exceeds unsigned 64-bit range");
				coordinate_comparisons += entry.location.coordinate_comparisons;
			}
			metrics->RecordNearestSamplingLocation(coordinate_comparisons, locator_wall_ns, locator_cpu_ns);
		}
	}

	const auto semantic_column_count = static_cast<std::size_t>(std::count_if(
	    semantic_axes.begin(), semantic_axes.end(),
	    [](const SemanticAxis &axis) { return axis.kind != SemanticAxisKind::Time; }));
	const bool has_sampling_column = regions_mode && sampling_options.method == RegionMethod::Nearest;
	return_types.reserve(return_types.size() + schema.variables.size() + (spatial.HasGrid() ? 2 : 0) +
	                     (temporal ? 1 : 0) + semantic_column_count + (regions_mode ? 1 : 0) +
	                     (has_sampling_column ? 1 : 0) + (spatial.include_source ? 1 : 0));
	names.reserve(names.size() + schema.variables.size() + (spatial.HasGrid() ? 2 : 0) +
	              (temporal ? 1 : 0) + semantic_column_count + (regions_mode ? 1 : 0) +
	              (has_sampling_column ? 1 : 0) + (spatial.include_source ? 1 : 0));
	for (const auto &variable : schema.variables) {
		return_types.emplace_back(variable.type);
		names.emplace_back(variable.column_name);
	}
	if (spatial.HasGrid()) {
		AppendSpatialOutputColumns(schema, return_types, names);
	}
	if (temporal) {
		for (const auto &name : names) {
			if (StringUtil::CIEquals(TableFunctionColumnNameString(name), "valid_time")) {
				throw BinderException("read_om valid_time column name conflicts with a source array");
			}
		}
		return_types.emplace_back(LogicalType::TIMESTAMP);
		names.emplace_back("valid_time");
	}
	for (const auto &semantic_axis : semantic_axes) {
		if (semantic_axis.kind == SemanticAxisKind::Time) continue;
		const auto name = SemanticOutputName(semantic_axis.kind);
		for (const auto &existing_name : names) {
			if (StringUtil::CIEquals(TableFunctionColumnNameString(existing_name), name)) {
				throw BinderException("read_om " + name + " column name conflicts with existing column '" +
				                      existing_name + "'");
			}
		}
		return_types.emplace_back(semantic_axis.output_type);
		names.emplace_back(name);
	}
	if (regions_mode) {
		for (const auto &existing_name : names) {
			if (StringUtil::CIEquals(TableFunctionColumnNameString(existing_name), "region_id")) {
				throw BinderException("[region_input] region_id column name conflicts with existing column '%s'",
				                     existing_name);
			}
		}
		return_types.emplace_back(LogicalType::VARCHAR);
		names.emplace_back("region_id");
	}
	if (has_sampling_column) {
		for (const auto &existing_name : names) {
			if (StringUtil::CIEquals(TableFunctionColumnNameString(existing_name), "sampling"))
				throw BinderException("[region_sampling] sampling column name conflicts with source array '%s'", existing_name);
		}
		return_types.emplace_back(NearestSamplingType(schema));
		names.emplace_back("sampling");
	}
	if (spatial.include_source) {
		for (const auto &existing_name : names) {
			if (StringUtil::CIEquals(TableFunctionColumnNameString(existing_name), "om_source")) {
				throw BinderException("read_om om_source column name conflicts with source array '" + existing_name + "'");
			}
		}
		return_types.emplace_back(OmSourceType());
		names.emplace_back("om_source");
	}
	auto bind_data = make_uniq<ReadOmBindData>(path, std::move(schema), std::move(axes), std::move(spatial),
	                                          std::move(temporal), std::move(semantic_axes),
	                                          std::move(remote_session), metrics, memory_account, regions_mode,
	                                          std::move(regions), std::move(region_crs),
	                                          std::move(region_geometries), std::move(region_predicates), selected_variable.has_value(),
	                                          sampling_options, std::move(nearest_plan));
	return bind_data;
}

unique_ptr<FunctionData> BindReadOm(ClientContext &context, TableFunctionBindInput &input,
                                    vector<LogicalType> &return_types, TableFunctionColumnNames &names) {
	return BindReadOmInternal(context, input, return_types, names, false);
}

unique_ptr<FunctionData> BindReadOmRegions(ClientContext &context, TableFunctionBindInput &input,
                                            vector<LogicalType> &return_types, TableFunctionColumnNames &names) {
	return BindReadOmInternal(context, input, return_types, names, true);
}

struct GridInfoBindData final : TableFunctionData {
	GridInfoBindData(GridInfoDocument document_p, std::shared_ptr<ScanMetrics> metrics_p)
	    : document(std::move(document_p)), metrics(std::move(metrics_p)),
	      memory_account(std::make_shared<ScanMemoryAccount>(metrics, ScanMemoryComponent::Bind)) {
		RefreshMemoryAccount();
	}

	GridInfoBindData(const GridInfoBindData &other)
	    : TableFunctionData(other), document(other.document), metrics(other.metrics),
	      memory_account(std::make_shared<ScanMemoryAccount>(metrics, ScanMemoryComponent::Bind)) {
		RefreshMemoryAccount();
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<GridInfoBindData>(*this);
	}

	bool Equals(const FunctionData &other_p) const override {
		const auto &other = other_p.Cast<GridInfoBindData>();
		return document.grid_id == other.document.grid_id && document.object_id == other.document.object_id &&
		       document.layout_json == other.document.layout_json;
	}

	bool SupportStatementCache() const override {
		return false;
	}

	void RefreshMemoryAccount() const {
		std::uint64_t bytes = sizeof(*this);
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(document.grid_id));
		if (document.parent_grid_id) bytes = AddMemoryBytes(bytes, StringMemoryBytes(*document.parent_grid_id));
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(document.grid_type));
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(document.definition_json));
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(document.layout_json));
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(document.crs_json));
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(document.capabilities_json));
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(document.provenance_json));
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(document.object_id));
		if (document.object_version) bytes = AddMemoryBytes(bytes, StringMemoryBytes(*document.object_version));
		bytes = AddMemoryBytes(bytes, StringMemoryBytes(document.version_strength));
		memory_account->Set(bytes);
	}

	GridInfoDocument document;
	std::shared_ptr<ScanMetrics> metrics;
	std::shared_ptr<ScanMemoryAccount> memory_account;
};

struct GridInfoGlobalState final : GlobalTableFunctionState {
	explicit GridInfoGlobalState(std::shared_ptr<ScanMetrics> metrics)
	    : memory_account(std::make_shared<ScanMemoryAccount>(std::move(metrics), ScanMemoryComponent::GlobalControl)) {
		memory_account->Set(sizeof(*this));
	}
	std::shared_ptr<ScanMemoryAccount> memory_account;
	bool emitted = false;
};

Value JsonValue(std::string json) {
	return ValueWithLogicalType(Value(std::move(json)), LogicalType::JSON());
}

unique_ptr<FunctionData> BindGridInfo(ClientContext &context, TableFunctionBindInput &input,
                                      vector<LogicalType> &return_types, TableFunctionColumnNames &names) {
	// Reuse read_om's complete metadata, CRS, dimensions, grid/domain, and axis
	// binder. Its bind path reads metadata and coordinate evidence only; values
	// are not initialized or decoded by this one-row descriptor function.
	vector<LogicalType> read_types;
	TableFunctionColumnNames read_names;
	auto common_data = BindReadOm(context, input, read_types, read_names);
	if (!common_data) throw InternalException("om_grid_info common binder returned no bind data");
	const auto &read = common_data->Cast<ReadOmBindData>();
	if (!read.spatial.HasGrid() || !read.spatial.identity_definition || !read.spatial.layout) {
		throw BinderException("om_grid_info requires a bound grid or domain");
	}
	read.metrics->SetOperation("grid_info");
	auto document = DescribeGrid(*read.spatial.identity_definition, *read.spatial.layout, read.schema,
	                             read.spatial.source,
	                             OpaqueObjectId(read.remote_session ? read.remote_session->Uri() : read.path), std::nullopt,
	                             "unverifiable", false, read.spatial.evidence_level,
	                             read.spatial.evidence_sample_id, read.spatial.evidence_source,
	                             read.spatial.evidence_build_pair, read.spatial.evidence_claims);

	return_types = {LogicalType::INTEGER, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR,
	                LogicalType::JSON(), LogicalType::JSON(), LogicalType::JSON(), LogicalType::JSON(),
	                LogicalType::JSON(), LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR,
	                LogicalType::BOOLEAN};
	names = {"descriptor_version", "grid_id", "parent_grid_id", "grid_type", "definition", "layout", "crs",
	         "capabilities", "provenance", "object_id", "object_version", "version_strength", "content_verified"};
	return make_uniq<GridInfoBindData>(std::move(document), read.metrics);
}

unique_ptr<GlobalTableFunctionState> InitGridInfo(ClientContext &, TableFunctionInitInput &input) {
	if (!input.bind_data) throw InternalException("om_grid_info was initialized without bind data");
	const auto &bind_data = input.bind_data->Cast<GridInfoBindData>();
	return make_uniq<GridInfoGlobalState>(bind_data.metrics);
}

void ScanGridInfo(ClientContext &, TableFunctionInput &input, DataChunk &output) {
	if (!input.bind_data || !input.global_state) {
		throw InternalException("om_grid_info was initialized without bind or global state");
	}
	auto &bind_data = input.bind_data->Cast<GridInfoBindData>();
	auto &state = input.global_state->Cast<GridInfoGlobalState>();
	if (state.emitted) return;
	const auto &document = bind_data.document;
	ScanMemoryAccount output_value_account(bind_data.metrics, ScanMemoryComponent::GlobalControl);
	const auto largest_string = std::max({StringMemoryBytes(document.grid_id),
	                                      document.parent_grid_id ? StringMemoryBytes(*document.parent_grid_id) : 0,
	                                      StringMemoryBytes(document.grid_type),
	                                      StringMemoryBytes(document.definition_json),
	                                      StringMemoryBytes(document.layout_json),
	                                      StringMemoryBytes(document.crs_json),
	                                      StringMemoryBytes(document.capabilities_json),
	                                      StringMemoryBytes(document.provenance_json),
	                                      StringMemoryBytes(document.object_id),
	                                      document.object_version ? StringMemoryBytes(*document.object_version) : 0,
	                                      StringMemoryBytes(document.version_strength)});
	output_value_account.Set(AddMemoryBytes(sizeof(Value) * 2, AddMemoryBytes(largest_string, largest_string)));
	output.SetValue(0, 0, Value::INTEGER(1));
	output.SetValue(1, 0, Value(document.grid_id));
	output.SetValue(2, 0, document.parent_grid_id ? Value(*document.parent_grid_id) : Value(LogicalType::VARCHAR));
	output.SetValue(3, 0, Value(document.grid_type));
	output.SetValue(4, 0, JsonValue(document.definition_json));
	output.SetValue(5, 0, JsonValue(document.layout_json));
	output.SetValue(6, 0, JsonValue(document.crs_json));
	output.SetValue(7, 0, JsonValue(document.capabilities_json));
	output.SetValue(8, 0, JsonValue(document.provenance_json));
	output.SetValue(9, 0, Value(document.object_id));
	output.SetValue(10, 0, document.object_version ? Value(*document.object_version) : Value(LogicalType::VARCHAR));
	output.SetValue(11, 0, Value(document.version_strength));
	output.SetValue(12, 0, Value::BOOLEAN(document.content_verified));
	output.SetCardinality(1);
	state.emitted = true;
}

unique_ptr<GlobalTableFunctionState> InitReadOm(ClientContext &context, TableFunctionInitInput &input) {
	if (!input.bind_data) {
		throw InternalException("read_om was initialized without bind data");
	}
	const auto &bind_data = input.bind_data->Cast<ReadOmBindData>();
	return make_uniq<ReadOmGlobalState>(context, bind_data, input.column_ids, input.column_indexes, bind_data.metrics);
}

unique_ptr<LocalTableFunctionState> InitReadOmLocal(ExecutionContext &context, TableFunctionInitInput &input,
                                                    GlobalTableFunctionState *global_state) {
	if (!input.bind_data || !global_state) {
		throw InternalException("read_om worker was initialized without bind or global state");
	}
	auto &global = global_state->Cast<ReadOmGlobalState>();
	try {
		const auto worker_id = global.next_worker_id.fetch_add(1);
		return make_uniq<ReadOmLocalState>(context.client, input.bind_data->Cast<ReadOmBindData>(), global, worker_id);
	} catch (const ReaderError &error) {
		if (global.RequestStop() && global.metrics) {
			global.metrics->SetStatus(ScanStatus::Failed,
			                          "reader_error_" + std::to_string(static_cast<unsigned int>(error.Code())));
			global.metrics_status_set = true;
		}
		throw;
	} catch (const std::exception &) {
		if (global.RequestStop() && global.metrics) {
			global.metrics->SetStatus(ScanStatus::Failed, "worker_init_error");
			global.metrics_status_set = true;
		}
		throw;
	}
}

bool RegionEnvelopeContains(const RegionEnvelope &envelope, const GridCoordinate &coordinate) {
	return !envelope.empty && coordinate.longitude >= envelope.min_longitude &&
	       coordinate.longitude <= envelope.max_longitude && coordinate.latitude >= envelope.min_latitude &&
	       coordinate.latitude <= envelope.max_latitude;
}

void FilterRegionCandidates(ClientContext &context, ReadOmGlobalState &state, ReadOmLocalState &local,
	                        const ReadOmBindData &bind_data, std::vector<std::uint64_t> &logical_positions) {
	if (local.task_region_ordinal >= bind_data.region_predicates.size()) {
		throw InternalException("read_om_regions task refers to an unknown region ordinal");
	}
	if (local.active_region_ordinal != local.task_region_ordinal) {
		local.region_evaluator.reset();
		local.region_evaluator_memory_bytes = 0;
		local.RefreshMemoryAccount();
		local.region_evaluator = make_uniq<RegionPredicateEvaluator>(
		    context, *bind_data.region_predicates[local.task_region_ordinal]);
		local.active_region_ordinal = local.task_region_ordinal;
		local.region_evaluator_memory_bytes = AddMemoryBytes(
		    sizeof(RegionPredicateEvaluator),
		    EstimateRegionPredicateMemory(*bind_data.region_predicates[local.task_region_ordinal]));
		local.RefreshMemoryAccount();
	}
	const auto &region = bind_data.regions->GetRegions()[local.task_region_ordinal];
	std::vector<std::uint64_t> matches;
	if (logical_positions.size() > STANDARD_VECTOR_SIZE) {
		throw InternalException("read_om_regions geometry scratch exceeds one output vector");
	}
	matches.reserve(logical_positions.size());
	DataChunk coordinates;
	coordinates.Initialize(context, {LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE)},
	                       REGION_GEOMETRY_BATCH_SIZE);
	SelectionVector selected(REGION_GEOMETRY_BATCH_SIZE);
	std::array<std::uint64_t, REGION_GEOMETRY_BATCH_SIZE> candidate_positions{};
	coordinates.SetCardinality(REGION_GEOMETRY_BATCH_SIZE);
	const auto coordinate_allocation_bytes = coordinates.GetAllocationSize();
	coordinates.SetCardinality(0);
	std::uint64_t scratch_bytes = sizeof(matches);
	scratch_bytes = AddMemoryBytes(scratch_bytes, VectorMemoryBytes(matches.capacity(), sizeof(std::uint64_t)));
	scratch_bytes = AddMemoryBytes(scratch_bytes, sizeof(coordinates));
	scratch_bytes = AddMemoryBytes(scratch_bytes, coordinate_allocation_bytes);
	scratch_bytes = AddMemoryBytes(scratch_bytes, sizeof(selected));
	scratch_bytes = AddMemoryBytes(scratch_bytes, VectorMemoryBytes(REGION_GEOMETRY_BATCH_SIZE, sizeof(idx_t)));
	scratch_bytes = AddMemoryBytes(scratch_bytes, sizeof(candidate_positions));
	if (scratch_bytes > MAX_REGION_GEOMETRY_SCRATCH_BYTES) {
		throw ReaderError(ReaderErrorCode::Allocation, "region geometry scratch exceeds the 64 KiB worker limit");
	}
	ScanMemoryAccount scratch_account(state.metrics, ScanMemoryComponent::Selector);
	scratch_account.Set(scratch_bytes);
	idx_t coordinate_count = 0;
	std::uint64_t envelope_rejects = 0;
	state.metrics->RecordRegionCandidatePairs(logical_positions.size());
	state.metrics->RecordObservedCandidateRecords(logical_positions.size());
	auto evaluate = [&]() {
		if (coordinate_count == 0) return;
		coordinates.SetCardinality(coordinate_count);
		const auto started = std::chrono::steady_clock::now();
		idx_t match_count = 0;
		bool interrupted = false;
		try {
			match_count = local.region_evaluator->Select(coordinates, selected);
			interrupted = context.IsInterrupted();
		} catch (...) {
			const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
			    std::chrono::steady_clock::now() - started).count();
			state.metrics->RecordRegionGeometryBatch(coordinate_count, 0,
			                                         static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)));
			throw;
		}
		const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
		    std::chrono::steady_clock::now() - started).count();
		state.metrics->RecordRegionGeometryBatch(coordinate_count, match_count,
		                                         static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)));
		if (interrupted) throw InterruptException();
		for (idx_t i = 0; i < match_count; ++i) matches.push_back(candidate_positions[selected.get_index(i)]);
		coordinates.Reset();
		coordinate_count = 0;
	};
	std::uint64_t coordinate_evaluations = 0;
	try {
		for (const auto logical_position : logical_positions) {
			if (context.IsInterrupted()) throw InterruptException();
			const auto coordinate = state.spatial.Coordinate(logical_position);
			++coordinate_evaluations;
			if (!RegionEnvelopeContains(region.envelope, coordinate)) {
				++envelope_rejects;
				continue;
			}
			if (!MatchesSpatialPredicate(*state.spatial.predicate, coordinate)) {
				continue;
			}
			coordinates.SetValue(0, coordinate_count, Value::DOUBLE(coordinate.longitude));
			coordinates.SetValue(1, coordinate_count, Value::DOUBLE(coordinate.latitude));
			candidate_positions[coordinate_count] = logical_position;
			if (++coordinate_count == REGION_GEOMETRY_BATCH_SIZE) evaluate();
		}
		evaluate();
	} catch (...) {
		state.metrics->RecordCoordinatePreparation(coordinate_evaluations, false);
		state.metrics->RecordRegionEnvelopeRejects(envelope_rejects);
		throw;
	}
	state.metrics->RecordCoordinatePreparation(coordinate_evaluations, true);
	state.metrics->RecordRegionEnvelopeRejects(envelope_rejects);
	logical_positions = std::move(matches);
}

const char *NearestGeometryStatusName(GeometryStatus status) {
	switch (status) {
	case GeometryStatus::Ok: return "ok";
	case GeometryStatus::EmptyGeometry: return "empty_geometry";
	case GeometryStatus::OutsideDomain: return "outside_domain";
	case GeometryStatus::BeyondMaxDistance: return "beyond_max_distance";
	}
	throw InternalException("unknown nearest geometry status");
}

std::uint64_t NearestLogicalIndex(const SpatialLayout &layout, const std::vector<std::uint64_t> &non_spatial,
                                  const NearestSourcePoint &source) {
	return layout.SourcePosition(non_spatial, source.x, source.y, source.point_index).logical_index;
}

std::uint64_t NearestAxisIndex(const SpatialLayout &layout, const std::vector<std::uint64_t> &non_spatial,
                               idx_t axis) {
	const auto found = std::find(layout.non_spatial_axes.begin(), layout.non_spatial_axes.end(), axis);
	if (found == layout.non_spatial_axes.end()) throw InternalException("nearest semantic axis is spatial or unbound");
	return non_spatial.at(static_cast<std::size_t>(found - layout.non_spatial_axes.begin()));
}

void ScanNearestBatch(ClientContext &context, ReadOmGlobalState &state, ReadOmLocalState &local,
                      const ReadOmBindData &bind_data, DataChunk &output, idx_t count) {
	if (!state.nearest_plan || !state.spatial.layout ||
	    local.task_region_ordinal >= state.nearest_plan->regions.size()) {
		throw InternalException("nearest output has no bound region target");
	}
	const auto &layout = *state.spatial.layout;
	const auto &entry = state.nearest_plan->regions[local.task_region_ordinal];
	const auto &required_variables = state.projection.GetRequiredVariableIds();
	const auto &slots = state.projection.GetOutputSlots();
	if (output.data.size() != slots.size() || local.decoders.size() != required_variables.size())
		throw InternalException("nearest output column count does not match its projection plan");
	const auto task_offset = local.task_position_offset;
	if (task_offset + count > local.task_non_spatial_indices.size())
		throw InternalException("nearest non-spatial axis batch is shorter than its task positions");

	ScanMemoryAccount worker_scratch(state.metrics, ScanMemoryComponent::Coalescing);
	const auto scratch_bound = NearestSamplingScratchUpperBound(
	    state.schema.shape.size(), layout.non_spatial_axes.size(), required_variables.size(),
	    state.projection.RequiresSamplingVariables(), count, state.schema);
	worker_scratch.Set(scratch_bound, scratch_bound);
	if (scratch_bound > 512 * 1024)
		throw ReaderError(ReaderErrorCode::Allocation, "nearest worker scratch exceeds 512 KiB after checked allocation planning");
	const auto no_source = std::numeric_limits<std::uint64_t>::max();
	std::vector<std::uint64_t> source_logical(count, no_source);
	std::vector<std::uint64_t> unique_sources;
	unique_sources.reserve(count);
	for (idx_t row = 0; row < count; ++row) {
		if ((row & 255U) == 0 && context.IsInterrupted()) throw InterruptException();
		if (entry.status == GeometryStatus::Ok && entry.location.source) {
			source_logical[row] = NearestLogicalIndex(layout, local.task_non_spatial_indices[task_offset + row],
			                                          *entry.location.source);
			unique_sources.push_back(source_logical[row]);
		}
	}
	const auto source_rows_requested = static_cast<std::uint64_t>(
	    std::count_if(source_logical.begin(), source_logical.end(), [no_source](std::uint64_t logical) {
		    return logical != no_source;
	    }));
	std::sort(unique_sources.begin(), unique_sources.end());
	unique_sources.erase(std::unique(unique_sources.begin(), unique_sources.end()), unique_sources.end());
	state.metrics->RecordNearestSamplingSources(source_rows_requested, unique_sources.size());
	const auto segments = required_variables.empty() ? std::vector<BatchSegment>()
	                                                : BuildSelectedBatchSegments(state.schema.shape, unique_sources);
	std::vector<std::vector<float>> decoded(required_variables.size());
	for (idx_t required_index = 0; required_index < required_variables.size(); ++required_index) {
		decoded[required_index].resize(unique_sources.size());
		const auto variable_index = required_variables[required_index];
		const auto &variable = state.schema.variables.at(variable_index);
		for (const auto &segment : segments) {
			if (context.IsInterrupted()) throw InterruptException();
			const auto bytes = segment.count * sizeof(float);
			std::vector<std::uint64_t> cube_offset(segment.read_count.size(), 0);
			local.reader->DecodeSelection(*local.decoders[required_index], variable.canonical_path,
			                              segment.read_offset, segment.read_count,
			                              cube_offset, segment.read_count,
			                              decoded[required_index].data() + segment.batch_offset, bytes);
		}
	}
	auto decoded_value = [&](idx_t variable_index, std::uint64_t logical) -> Value {
		const auto required = std::find(required_variables.begin(), required_variables.end(), variable_index);
		if (required == required_variables.end()) throw InternalException("nearest variable dependency was not decoded");
		const auto required_index = static_cast<idx_t>(required - required_variables.begin());
		const auto found = std::lower_bound(unique_sources.begin(), unique_sources.end(), logical);
		if (found == unique_sources.end() || *found != logical) throw InternalException("nearest source is missing from decoded mapping");
		const auto value = decoded[required_index].at(static_cast<std::size_t>(found - unique_sources.begin()));
		if (std::isnan(value)) return Value();
		return Value::FLOAT(value);
	};
	std::map<std::string, ScanSamplingVariableStatusMetrics> batch_statuses;
	if (state.projection.RequiresSamplingVariables()) {
		for (const auto variable_index : required_variables) {
			auto &status = batch_statuses[state.schema.variables.at(variable_index).canonical_path];
			status.rows_observed = count;
			for (idx_t row = 0; row < count; ++row) {
				if ((row & 255U) == 0 && context.IsInterrupted()) throw InterruptException();
				if (entry.status != GeometryStatus::Ok || source_logical[row] == no_source) {
					switch (entry.status) {
					case GeometryStatus::EmptyGeometry: ++status.empty_geometry; break;
					case GeometryStatus::OutsideDomain: ++status.outside_domain; break;
					case GeometryStatus::BeyondMaxDistance: ++status.beyond_max_distance; break;
					case GeometryStatus::Ok: throw InternalException("valid nearest geometry has no source for status accounting");
					}
				} else if (decoded_value(variable_index, source_logical[row]).IsNull()) {
					++status.source_null;
				} else {
					++status.ok;
				}
			}
		}
	}
	auto nearest_sampling_fields = [&](idx_t row, bool include_variables) {
		const bool sampled = entry.status == GeometryStatus::Ok && source_logical[row] != no_source;
		const auto &source = entry.location.source;
		std::vector<Value> axis_indices;
		axis_indices.reserve(layout.non_spatial_axes.size());
		for (std::size_t axis_position = 0; axis_position < layout.non_spatial_axes.size(); ++axis_position)
			axis_indices.emplace_back(Value::UBIGINT(local.task_non_spatial_indices[task_offset + row].at(axis_position)));
		std::vector<Value> variable_keys;
		std::vector<Value> variable_values;
		if (include_variables) {
			variable_keys.reserve(state.schema.variables.size());
			variable_values.reserve(state.schema.variables.size());
			for (idx_t variable_index = 0; variable_index < state.schema.variables.size(); ++variable_index) {
				std::string variable_status = NearestGeometryStatusName(entry.status);
				std::uint64_t source_count = 0;
				std::uint64_t valid_source_count = 0;
				if (sampled) {
					const auto value = decoded_value(variable_index, source_logical[row]);
					if (value.IsNull()) variable_status = "source_null";
					else {
						variable_status = "ok";
						valid_source_count = 1;
					}
					source_count = 1;
				}
				variable_keys.emplace_back(state.schema.variables[variable_index].column_name);
				variable_values.emplace_back(Value::STRUCT({
				    {"status", Value(variable_status)},
				    {"source_count", Value::UBIGINT(source_count)},
				    {"valid_source_count", Value::UBIGINT(valid_source_count)},
				}));
			}
		}
		return std::vector<Value>{
		    Value("nearest"), Value("sphere_haversine_f64_v1"), Value("point_on_surface_v1"),
		    Value(state.nearest_plan->spatial_version), Value(state.nearest_plan->grid_id),
		    Value(state.nearest_plan->layout_id), Value("grid_sphere"), Value::DOUBLE(state.nearest_plan->earth_radius_m),
		    Value(NearestGeometryStatusName(entry.status)),
		    source ? Value::DOUBLE(source->longitude) : Value(),
		    source ? Value::DOUBLE(source->latitude) : Value(),
		    source ? Value::UBIGINT(source->point_index) : Value(),
		    source ? Value::DOUBLE(source->distance_m) : Value(),
		    Value::LIST(LogicalType::UBIGINT, std::move(axis_indices)),
		    Value::MAP(LogicalType::VARCHAR,
		               LogicalType::STRUCT({{"status", LogicalType::VARCHAR},
		                                    {"source_count", LogicalType::UBIGINT},
		                                    {"valid_source_count", LogicalType::UBIGINT}}),
		               std::move(variable_keys), std::move(variable_values)),
		};
	};
	auto nearest_sampling_value = [&](idx_t row) {
		const auto fields = nearest_sampling_fields(row, true);
		return Value::STRUCT({
		    {"method", fields[0]}, {"algorithm", fields[1]}, {"representative", fields[2]},
		    {"spatial_version", fields[3]}, {"grid_id", fields[4]}, {"layout_id", fields[5]},
		    {"distance_model", fields[6]}, {"earth_radius_m", fields[7]}, {"status", fields[8]},
		    {"source_lon", fields[9]}, {"source_lat", fields[10]}, {"source_point_index", fields[11]},
		    {"distance_m", fields[12]}, {"axis_indices", fields[13]}, {"variables", fields[14]},
		});
	};

	for (idx_t output_column = 0; output_column < slots.size(); ++output_column) {
		const auto &slot = slots[output_column];
		for (idx_t row = 0; row < count; ++row) {
			if ((row & 255U) == 0 && context.IsInterrupted()) throw InterruptException();
			const auto &indices = local.task_non_spatial_indices[task_offset + row];
			switch (slot.kind) {
			case OutputColumnKind::Value: {
				if (slot.required_variable_index >= required_variables.size())
					throw InternalException("nearest value output refers to an unknown decoder");
				const auto value = source_logical[row] == no_source
				                      ? Value() : decoded_value(required_variables[slot.required_variable_index], source_logical[row]);
				output.data[output_column].SetValue(row, value);
				break;
			}
			case OutputColumnKind::Latitude:
				output.data[output_column].SetValue(row, entry.has_representative
				                                              ? Value::DOUBLE(entry.representative.latitude) : Value());
				break;
			case OutputColumnKind::Longitude:
				output.data[output_column].SetValue(row, entry.has_representative
				                                              ? Value::DOUBLE(entry.representative.longitude) : Value());
				break;
			case OutputColumnKind::ValidTime: {
				if (!state.temporal) throw InternalException("nearest valid_time has no bound temporal coordinate");
				const auto time_position = state.temporal->axis_index
				                               ? NearestAxisIndex(layout, indices, *state.temporal->axis_index) *
				                                     state.temporal->stride
				                               : 0;
				output.data[output_column].SetValue(row, Value::TIMESTAMP(state.temporal->Coordinate(time_position)));
				break;
			}
			case OutputColumnKind::SemanticCoordinate: {
				if (slot.semantic_axis_index >= state.semantic_axes.size())
					throw InternalException("nearest semantic coordinate is not bound");
				const auto &axis = state.semantic_axes[slot.semantic_axis_index];
				const auto axis_index = NearestAxisIndex(layout, indices, axis.axis_index);
				const auto logical = axis_index * axis.stride;
				output.data[output_column].SetValue(row, axis.CoordinateValue(logical));
				break;
			}
			case OutputColumnKind::RegionId:
				output.data[output_column].SetValue(row, Value(bind_data.regions->GetRegions()[local.task_region_ordinal].id));
				break;
			case OutputColumnKind::Sampling:
				output.data[output_column].SetValue(row, nearest_sampling_value(row));
				break;
			case OutputColumnKind::SamplingField: {
				const auto fields = nearest_sampling_fields(row, slot.sampling_field_index == 14);
				output.data[output_column].SetValue(row, fields.at(static_cast<std::size_t>(slot.sampling_field_index)));
				break;
			}
			case OutputColumnKind::Source:
				output.data[output_column].SetValue(row, source_logical[row] == no_source
				                                              ? Value() : MakeOmSourceValue(state.spatial, source_logical[row]));
				break;
			case OutputColumnKind::Cardinality:
				output.data[output_column].SetValue(row, Value::BOOLEAN(true));
				break;
			}
		}
	}
	local.task_position_offset += count;
	state.metrics->RecordScannedRows(count);
	output.SetCardinality(count);
	if (!batch_statuses.empty()) state.metrics->RecordNearestSamplingStatuses(batch_statuses, count);
	state.metrics->RecordNearestSamplingRows(count);
	if (!local.registered_as_active) {
		state.metrics->RecordWorkerActive(local.worker_id);
		local.registered_as_active = true;
	}
}

void ScanReadOmImpl(ClientContext &context, ReadOmGlobalState &state, ReadOmLocalState &local,
	                const ReadOmBindData &bind_data, DataChunk &output) {
	output.SetCardinality(0);
	if (state.stop_requested.load() || context.IsInterrupted()) {
		throw InterruptException();
	}
	while (true) {
	while (local.task_position_offset >= local.task_positions.size()) {
		if (local.task_active) {
			state.RecordTaskCompleted();
			local.task_active = false;
		}
		if (!state.GetNextTask(context, local.task_positions, *local.task_positions_account,
		                       local.task_region_ordinal,
	                       state.sampling_options.method == RegionMethod::Nearest ? &local.task_non_spatial_indices : nullptr)) {
			local.RefreshMemoryAccount();
			return;
		}
		local.RefreshMemoryAccount();
		local.task_position_offset = 0;
		local.task_active = true;
	}
	auto candidate_count = std::min<idx_t>(STANDARD_VECTOR_SIZE,
	                                       local.task_positions.size() - local.task_position_offset);
	idx_t count = candidate_count;
	const auto &required_variables = state.projection.GetRequiredVariableIds();
	if (!required_variables.empty()) {
		count = std::min<idx_t>(count, static_cast<idx_t>(MaxBatchPositionCount(state.schema.shape.size())));
		candidate_count = count;
	}
	if (state.sampling_options.method == RegionMethod::Nearest) {
		struct WorkerExecution final {
			ScanMetrics &metrics;
			std::uint64_t worker_id;
			WorkerExecution(ScanMetrics &metrics_p, std::uint64_t worker_id_p)
			    : metrics(metrics_p), worker_id(worker_id_p) { metrics.BeginWorkerExecution(worker_id); }
			~WorkerExecution() { metrics.EndWorkerExecution(worker_id); }
		} worker_execution(*state.metrics, local.worker_id);
		ScanNearestBatch(context, state, local, bind_data, output, candidate_count);
		return;
	}
	struct WorkerExecution final {
		ScanMetrics &metrics;
		std::uint64_t worker_id;
		WorkerExecution(ScanMetrics &metrics_p, std::uint64_t worker_id_p)
		    : metrics(metrics_p), worker_id(worker_id_p) { metrics.BeginWorkerExecution(worker_id); }
		~WorkerExecution() { metrics.EndWorkerExecution(worker_id); }
	} worker_execution(*state.metrics, local.worker_id);
	ScanMemoryAccount batch_account(state.metrics, ScanMemoryComponent::BatchSegments);
	const auto initial_batch_upper_bound = required_variables.empty()
	                                           ? AddMemoryBytes(sizeof(std::vector<std::uint64_t>) +
	                                                               sizeof(std::vector<BatchSegment>),
	                                                           VectorMemoryBytes(count, sizeof(std::uint64_t)))
	                                           : WorstCaseBatchMappingBytes(state.schema.shape.size(), count);
	batch_account.Set(0, initial_batch_upper_bound);
	std::vector<std::uint64_t> logical_positions(
	    local.task_positions.begin() + local.task_position_offset,
	    local.task_positions.begin() + local.task_position_offset + candidate_count);
	if (state.regions_mode) {
		auto charge_candidate_positions = [&]() {
			const auto bytes = AddMemoryBytes(sizeof(logical_positions),
			                                 VectorMemoryBytes(logical_positions.capacity(), sizeof(std::uint64_t)));
			batch_account.Set(bytes, std::max(bytes, initial_batch_upper_bound));
		};
		charge_candidate_positions();
		FilterRegionCandidates(context, state, local, bind_data, logical_positions);
		charge_candidate_positions();
		count = static_cast<idx_t>(logical_positions.size());
		if (count == 0) {
			local.task_position_offset += candidate_count;
			continue;
		}
	}
	const auto &slots = state.projection.GetOutputSlots();
	const auto segments = required_variables.empty() ? std::vector<BatchSegment>()
	                                                : BuildSelectedBatchSegments(state.schema.shape, logical_positions);
	auto refresh_batch_memory = [&](std::uint64_t temporary_bytes, std::uint64_t temporary_upper_bound_bytes) {
		std::uint64_t bytes = sizeof(logical_positions) + sizeof(segments);
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(logical_positions.capacity(), sizeof(std::uint64_t)));
		bytes = AddMemoryBytes(bytes, EstimateSegmentsMemory(segments));
		bytes = AddMemoryBytes(bytes, temporary_bytes);
		const auto mapping_upper_bound = required_variables.empty()
		                                    ? AddMemoryBytes(sizeof(logical_positions) + sizeof(segments),
		                                                     VectorMemoryBytes(logical_positions.capacity(),
		                                                                       sizeof(std::uint64_t)))
		                                    : WorstCaseBatchMappingBytes(state.schema.shape.size(),
		                                                                 logical_positions.size());
		const auto upper_bound = AddMemoryBytes(mapping_upper_bound, temporary_upper_bound_bytes);
		batch_account.Set(bytes, upper_bound);
	};
	refresh_batch_memory(0, 0);
	if (output.data.size() != slots.size() || local.decoders.size() != required_variables.size()) {
		throw InternalException("read_om output column count does not match its projection plan");
	}

    for (std::size_t output_column = 0; output_column < slots.size(); output_column++) {
		if (slots[output_column].kind == OutputColumnKind::ValidTime) {
			auto *values = MutableVectorData<timestamp_t>(output.data[output_column]);
			for (std::uint64_t row = 0; row < count; row++) {
				values[row] = state.temporal->Coordinate(logical_positions[row]);
			}
			MutableVectorValidity(output.data[output_column]).SetAllValid(static_cast<idx_t>(count));
			continue;
		}
		if (slots[output_column].kind == OutputColumnKind::Latitude ||
		    slots[output_column].kind == OutputColumnKind::Longitude) {
			if (!state.spatial.HasGrid() || !state.spatial.layout) {
				throw InternalException("read_om projected a spatial column without a bound grid");
			}
			auto *values = MutableVectorData<double>(output.data[output_column]);
			for (std::uint64_t row = 0; row < count; row++) {
				const auto logical_index = logical_positions[row];
				const auto coordinate = state.spatial.Coordinate(logical_index);
				values[row] = slots[output_column].kind == OutputColumnKind::Latitude ? coordinate.latitude
				                                                                            : coordinate.longitude;
			}
			state.metrics->RecordCoordinateEvaluations(count);
			MutableVectorValidity(output.data[output_column]).SetAllValid(static_cast<idx_t>(count));
			continue;
		}
		if (slots[output_column].kind == OutputColumnKind::SemanticCoordinate) {
			if (slots[output_column].semantic_axis_index >= state.semantic_axes.size()) {
				throw InternalException("read_om projected an unknown semantic coordinate");
			}
			const auto &semantic_axis = state.semantic_axes[slots[output_column].semantic_axis_index];
			for (std::uint64_t row = 0; row < count; row++) {
				output.data[output_column].SetValue(row, semantic_axis.CoordinateValue(logical_positions[row]));
			}
			continue;
		}
		if (slots[output_column].kind == OutputColumnKind::Source) {
			for (std::uint64_t row = 0; row < count; row++) {
				output.data[output_column].SetValue(row,
				    MakeOmSourceValue(state.spatial, logical_positions[row]));
			}
			continue;
		}
		if (slots[output_column].kind == OutputColumnKind::RegionId) {
			if (!state.regions_mode || local.task_region_ordinal >= bind_data.regions->GetRegions().size()) {
				throw InternalException("read_om projected region_id without a bound region task");
			}
			const auto &region_id = bind_data.regions->GetRegions()[local.task_region_ordinal].id;
			for (idx_t row = 0; row < count; ++row) output.data[output_column].SetValue(row, Value(region_id));
			continue;
		}
		if (slots[output_column].kind != OutputColumnKind::Cardinality) {
            continue;
        }
        auto *values = MutableVectorData<bool>(output.data[output_column]);
        std::fill_n(values, static_cast<idx_t>(count), true);
        MutableVectorValidity(output.data[output_column]).SetAllValid(static_cast<idx_t>(count));
    }

    for (std::size_t required_index = 0; required_index < required_variables.size(); required_index++) {
        idx_t primary_output = DConstants::INVALID_INDEX;
        for (idx_t output_column = 0; output_column < slots.size(); output_column++) {
			if (slots[output_column].kind == OutputColumnKind::Value &&
			    slots[output_column].required_variable_index == required_index) {
				primary_output = output_column;
                break;
			}
		}
        if (primary_output == DConstants::INVALID_INDEX) {
            throw InternalException("read_om projection plan has an unreferenced required variable");
        }
        auto *values = MutableVectorData<float>(output.data[primary_output]);
        auto &validity = MutableVectorValidity(output.data[primary_output]);
        validity.SetAllValid(static_cast<idx_t>(count));
        for (const auto &segment : segments) {
            if (context.IsInterrupted()) {
                throw InterruptException();
            }
			const auto cube_offset_upper_bound =
			    AddMemoryBytes(sizeof(std::vector<std::uint64_t>),
			                   VectorMemoryBytes(segment.read_count.size(), sizeof(std::uint64_t)));
			refresh_batch_memory(0, cube_offset_upper_bound);
			std::vector<std::uint64_t> cube_offset(segment.read_count.size(), 0);
			const auto cube_offset_bytes = AddMemoryBytes(sizeof(cube_offset),
			                                             VectorMemoryBytes(cube_offset.capacity(),
			                                                               sizeof(std::uint64_t)));
			refresh_batch_memory(cube_offset_bytes, cube_offset_bytes);
            const auto variable_index = required_variables[required_index];
            const auto &variable = state.schema.variables.at(variable_index);
			local.reader->DecodeSelection(*local.decoders[required_index], variable.canonical_path,
                                          segment.read_offset, segment.read_count,
                                          cube_offset, segment.read_count, values + segment.batch_offset,
                                          segment.count * sizeof(float));
			for (std::uint64_t index = 0; index < segment.count; index++) {
				const auto batch_index = segment.batch_offset + index;
				if (std::isnan(values[batch_index])) {
					validity.SetInvalid(static_cast<idx_t>(batch_index));
				}
			}
		}
		refresh_batch_memory(0, 0);

        for (idx_t output_column = 0; output_column < slots.size(); output_column++) {
			if (output_column == primary_output || slots[output_column].kind != OutputColumnKind::Value ||
			    slots[output_column].required_variable_index != required_index) {
				continue;
			}
			auto *duplicate_values = MutableVectorData<float>(output.data[output_column]);
            std::memcpy(duplicate_values, values, static_cast<std::size_t>(count) * sizeof(float));
            auto &duplicate_validity = MutableVectorValidity(output.data[output_column]);
            duplicate_validity.SetAllValid(static_cast<idx_t>(count));
            for (idx_t row = 0; row < count; row++) {
                if (!validity.RowIsValid(row)) {
                    duplicate_validity.SetInvalid(row);
                }
            }
        }
    }
	if (context.IsInterrupted()) {
		throw InterruptException();
	}

	// Commit both the visible cardinality and shared range only after every
	// array has decoded successfully. Exceptions leave a zero-row chunk.
	local.task_position_offset += candidate_count;
	state.metrics->RecordScannedRows(count);
	output.SetCardinality(static_cast<idx_t>(count));
	if (!local.registered_as_active) {
		state.metrics->RecordWorkerActive(local.worker_id);
		local.registered_as_active = true;
	}
	return;
	}
}

void ScanReadOm(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	if (!input.global_state || !input.bind_data) {
		throw InternalException("read_om scan has no bind or global state");
	}
	auto &state = input.global_state->Cast<ReadOmGlobalState>();
	if (!input.local_state) {
		throw InternalException("read_om scan has no local worker state");
	}
	auto &local = input.local_state->Cast<ReadOmLocalState>();
	try {
		ScanReadOmImpl(context, state, local, input.bind_data->Cast<ReadOmBindData>(), output);
	} catch (const InterruptException &) {
		const auto first_stop = state.RequestStop();
		if (local.task_active) {
			state.RecordTaskFailed(true);
			local.task_active = false;
		}
		if (first_stop && state.metrics) {
			state.metrics->SetStatus(ScanStatus::Cancelled, "cancelled");
			state.metrics_status_set = true;
		}
		throw;
	} catch (const ReaderError &error) {
		const auto first_stop = state.RequestStop();
		if (local.task_active) {
			state.RecordTaskFailed(error.Code() == ReaderErrorCode::Cancelled);
			local.task_active = false;
		}
		if (first_stop && state.metrics) {
			if (error.Code() == ReaderErrorCode::Cancelled) {
				state.metrics->SetStatus(ScanStatus::Cancelled, "cancelled");
			} else {
				state.metrics->SetStatus(ScanStatus::Failed,
				                         "reader_error_" + std::to_string(static_cast<unsigned int>(error.Code())));
			}
			state.metrics_status_set = true;
		}
		throw;
	} catch (const std::exception &) {
		const auto first_stop = state.RequestStop();
		if (local.task_active) {
			state.RecordTaskFailed(false);
			local.task_active = false;
		}
		if (first_stop && state.metrics) {
			state.metrics->SetStatus(ScanStatus::Failed, "scan_error");
			state.metrics_status_set = true;
		}
		throw;
	}
}

struct LastScanMetricsBindData final : FunctionData {
	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<LastScanMetricsBindData>();
	}
	bool Equals(const FunctionData &other_p) const override {
		(void)other_p.Cast<LastScanMetricsBindData>();
		return true;
	}
};

struct LastScanMetricsGlobalState final : GlobalTableFunctionState {
	std::vector<DuckomoSessionState::PublishedScan> rows;
	idx_t offset = 0;
};

unique_ptr<FunctionData> BindLastScanMetrics(ClientContext &context, TableFunctionBindInput &,
                                             vector<LogicalType> &return_types, TableFunctionColumnNames &names) {
	return_types.emplace_back(LogicalType::VARCHAR);
	names.emplace_back("query_id");
	return_types.emplace_back(LogicalType::UBIGINT);
	names.emplace_back("scan_id");
	return_types.emplace_back(LogicalType::VARCHAR);
	names.emplace_back("metrics");
	return make_uniq<LastScanMetricsBindData>();
}

unique_ptr<GlobalTableFunctionState> InitLastScanMetrics(ClientContext &context, TableFunctionInitInput &) {
	auto result = make_uniq<LastScanMetricsGlobalState>();
	result->rows = GetDuckomoSessionState(context)->LastScans();
	return result;
}

void ScanLastMetrics(ClientContext &, TableFunctionInput &input, DataChunk &output) {
	if (!input.bind_data || !input.global_state) {
		throw InternalException("duckomo_last_scan_metrics was initialized without state");
	}
	auto &state = input.global_state->Cast<LastScanMetricsGlobalState>();
	const auto &rows = state.rows;
	idx_t count = 0;
	while (state.offset < rows.size() && count < STANDARD_VECTOR_SIZE) {
		const auto &row = rows[state.offset++];
		output.SetValue(0, count, Value(row.query_id));
		output.SetValue(1, count, Value::UBIGINT(row.scan_id));
		output.SetValue(2, count, Value(row.metrics));
		++count;
	}
	output.SetCardinality(count);
}

} // namespace

TableFunction GetReadOmFunction() {
	TableFunction function("read_om", {LogicalType::VARCHAR}, ScanReadOm, BindReadOm, InitReadOm, InitReadOmLocal);
	AddTableFunctionOption(function, "dimensions", LogicalType::ANY);
	AddTableFunctionOption(function, "grid", LogicalType::ANY);
	AddTableFunctionOption(function, "spatial_axes", LogicalType::LIST(LogicalType::VARCHAR));
	AddTableFunctionOption(function, "domain", LogicalType::VARCHAR);
	AddTableFunctionOption(function, "include_source", LogicalType::BOOLEAN);
	AddTableFunctionOption(function, "valid_times", LogicalType::LIST(LogicalType::TIMESTAMP));
	AddTableFunctionOption(function, "axes", LogicalType::ANY);
	AddTableFunctionOption(function, "variable", LogicalType::VARCHAR);
	function.get_virtual_columns = GetReadOmVirtualColumns;
    function.projection_pushdown = true;
	function.filter_pushdown = false;
	function.filter_prune = false;
	function.pushdown_complex_filter = ObserveComplexFilter;
	function.order_preservation_type = OrderPreservationType::NO_ORDER;
	return function;
}

TableFunction GetReadOmRegionsFunction() {
	TableFunction function("read_om_regions", {LogicalType::VARCHAR}, ScanReadOm, BindReadOmRegions,
	                       InitReadOm, InitReadOmLocal);
	AddTableFunctionOption(function, "regions", LogicalType::ANY);
	AddTableFunctionOption(function, "region_crs", LogicalType::ANY);
	AddTableFunctionOption(function, "method", LogicalType::ANY);
	AddTableFunctionOption(function, "max_distance_m", LogicalType::ANY);
	AddTableFunctionOption(function, "out_of_bounds", LogicalType::ANY);
	AddTableFunctionOption(function, "dimensions", LogicalType::ANY);
	AddTableFunctionOption(function, "grid", LogicalType::ANY);
	AddTableFunctionOption(function, "spatial_axes", LogicalType::LIST(LogicalType::VARCHAR));
	AddTableFunctionOption(function, "domain", LogicalType::VARCHAR);
	AddTableFunctionOption(function, "include_source", LogicalType::BOOLEAN);
	AddTableFunctionOption(function, "valid_times", LogicalType::LIST(LogicalType::TIMESTAMP));
	AddTableFunctionOption(function, "axes", LogicalType::ANY);
	AddTableFunctionOption(function, "variable", LogicalType::VARCHAR);
	function.get_virtual_columns = GetReadOmVirtualColumns;
	function.projection_pushdown = true;
	function.filter_pushdown = false;
	function.filter_prune = false;
	function.pushdown_complex_filter = ObserveComplexFilter;
	function.supports_pushdown_extract = SupportsNearestSamplingExtract;
	function.order_preservation_type = OrderPreservationType::NO_ORDER;
	return function;
}

TableFunction GetGridInfoFunction() {
	TableFunction function("om_grid_info", {LogicalType::VARCHAR}, ScanGridInfo, BindGridInfo, InitGridInfo);
	AddTableFunctionOption(function, "dimensions", LogicalType::ANY);
	AddTableFunctionOption(function, "grid", LogicalType::ANY);
	AddTableFunctionOption(function, "spatial_axes", LogicalType::LIST(LogicalType::VARCHAR));
	AddTableFunctionOption(function, "domain", LogicalType::VARCHAR);
	AddTableFunctionOption(function, "valid_times", LogicalType::LIST(LogicalType::TIMESTAMP));
	AddTableFunctionOption(function, "axes", LogicalType::ANY);
	AddTableFunctionOption(function, "variable", LogicalType::VARCHAR);
	function.projection_pushdown = false;
	function.filter_pushdown = false;
	function.filter_prune = false;
	return function;
}

TableFunction GetLastScanMetricsFunction() {
	return TableFunction("duckomo_last_scan_metrics", {}, ScanLastMetrics, BindLastScanMetrics,
	                     InitLastScanMetrics);
}

} // namespace duckomo
} // namespace duckdb
