#include "duckomo/read_om.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckomo/batch.hpp"
#include "duckomo/axis_filter.hpp"
#include "duckomo/axis_selection.hpp"
#include "duckomo/domain_bbox.hpp"
#include "duckomo/dimensions.hpp"
#include "duckomo/domain_registry.hpp"
#include "duckomo/local_file.hpp"
#include "duckomo/metrics.hpp"
#include "duckomo/metadata.hpp"
#include "duckomo/projection.hpp"
#include "duckomo/range_cache.hpp"
#include "duckomo/remote_file.hpp"
#include "duckomo/reader.hpp"
#include "duckomo/regular_grid.hpp"
#include "duckomo/schema.hpp"
#include "duckomo/semantic_axes.hpp"
#include "duckomo/spatial_filter.hpp"
#include "duckomo/spatial_layout.hpp"
#include "duckomo/spatial_selection.hpp"

namespace duckdb {
namespace duckomo {

namespace {

constexpr idx_t SCAN_TASK_POSITION_WINDOW = 65536;

std::unique_ptr<ReadAtFile> OpenOmReadAt(ClientContext &context, const std::string &path,
                                         const std::shared_ptr<RemoteReadSession> &remote_session,
                                         const std::shared_ptr<ScanMetrics> &metrics,
                                         ScanMetadataStage stage, RangeCache *cache) {
	if (remote_session) return remote_session->Open(context, stage, cache);
	return std::make_unique<LocalFile>(LocalFile::Open(context, path, metrics, stage));
}

std::string EnvironmentValue(const char *name) {
	const auto *value = std::getenv(name);
	return value == nullptr ? std::string() : std::string(value);
}

void WriteMetricsSidecar(const ScanMetrics &metrics, const std::string &path) noexcept;
void WriteMetricsV3Sidecar(const std::vector<std::shared_ptr<ScanMetrics>> &metrics,
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
		pending_scans_.push_back({snapshot.scan_id, snapshot.query_id, metrics->ToMetricsV3Json()});
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

	RangeCache &Cache() noexcept { return cache_; }

private:
	mutable std::mutex mutex_;
	std::string active_query_id_;
	std::string last_query_id_;
	std::vector<PublishedScan> pending_scans_;
	std::vector<PublishedScan> last_scans_;
	RangeCache cache_;
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
	                      shared_ptr<DuckomoSessionState> session_p, std::string query_id_p)
	    : state_key(std::move(state_key_p)), output_path(std::move(output_path_p)),
	      output_v3_path(std::move(output_v3_path_p)), session(std::move(session_p)),
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
			session->Publish(current);
		}
		if (!current_metrics.empty()) {
			WriteMetricsSidecar(*current_metrics.back(), output_path);
			WriteMetricsV3Sidecar(current_metrics, output_v3_path);
		}
		context.registered_state->Remove(state_key);
	}

private:
	std::string state_key;
	std::string output_path;
	std::string output_v3_path;
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
	metrics->SetDependencyCommit("duckdb", "08e34c447bae34eaee3723cac61f2878b6bdf787");
	metrics->SetDependencyCommit("om-file-format", "d8855e418e2231ae8439f0c7e840fa3f93b371e3");
	metrics->SetDependencyCommit("extension-ci-tools", "b777c70d30942cca5bef62d6d4fa23a13362f398");
	metrics->SetEnvironmentValue("platform", "linux");
	metrics->SetEnvironmentValue("build", "release");
	metrics->SetCachePolicyValue("application_cache", "disabled");
	const auto cache = GetDuckomoSessionState(context)->Cache().Stats();
	metrics->SetCacheEvictionBaseline(cache.evictions);
	metrics->SetCacheState(cache.enabled, cache.capacity_bytes, cache.accounted_bytes,
	                       cache.peak_charged_bytes, cache.control_bytes, cache.evictions,
	                       "not_applicable", "unverified");
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

virtual_column_map_t GetReadOmVirtualColumns(ClientContext &, optional_ptr<FunctionData>) {
	virtual_column_map_t result;
	result.emplace(COLUMN_IDENTIFIER_EMPTY, TableColumn("", LogicalType::BOOLEAN));
	return result;
}

bool SameSchema(const BoundSchema &left, const BoundSchema &right) {
	if (left.row_count != right.row_count || left.shape != right.shape || left.crs_wkt != right.crs_wkt ||
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

const Value *GetNamedValue(const TableFunctionBindInput &input, const char *name) {
	const auto entry = input.named_parameters.find(name);
	return entry == input.named_parameters.end() ? nullptr : &entry->second;
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
	std::optional<SpatialLayout> layout;
	// DuckDB may copy FunctionData between binding, logical planning, and scan
	// initialization. Keep callback output in query-local shared state so the
	// planner's safe predicate copy is visible to the physical scan state.
	std::shared_ptr<SpatialPredicate> predicate = std::make_shared<SpatialPredicate>();
	std::string grid_signature;
	std::string layout_name;
	std::string source;
};

bool SameSpatialPredicate(const SpatialPredicate &left, const SpatialPredicate &right) {
	if (left.residual_filter_retained != right.residual_filter_retained ||
	    left.has_unsupported_condition != right.has_unsupported_condition ||
	    left.fallback_reasons != right.fallback_reasons ||
	    left.necessary_conditions.size() != right.necessary_conditions.size()) {
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
	const auto order = result.grid->Order();
	std::ostringstream signature;
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
	metrics.SetSpatialContext(result.grid_signature, result.layout_name, result.source);
	metrics.SetSpatialSelection("full", true, {}, CheckedShapeProduct(result.layout->shape));
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

SpatialBindConfiguration BindSpatialConfiguration(const TableFunctionBindInput &input, const BoundSchema &schema,
	                                               const AxisDeclarations &axes, ScanMetrics &metrics) {
	SpatialBindConfiguration result;
	const auto *grid_value = GetNamedValue(input, "grid");
	const auto *axes_value = GetNamedValue(input, "spatial_axes");
	const auto *domain_value = GetNamedValue(input, "domain");
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
		if (!domain) {
			throw BinderException("read_om domain is unknown or not verified: '" + domain_name + "'");
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
		return stride == other.stride && axis_length == other.axis_length && regular == other.regular &&
		       start == other.start && step_micros == other.step_micros && values == other.values;
	}
};

std::optional<TemporalBindConfiguration> BindTemporalConfiguration(const TableFunctionBindInput &input,
                                                                   const BoundSchema &schema,
                                                                   const AxisDeclarations &axes,
                                                                   const SemanticAxes &semantic_axes) {
	const auto *explicit_times = GetNamedValue(input, "valid_times");
	const bool has_explicit = explicit_times && !explicit_times->IsNull();
	for (const auto &semantic_axis : semantic_axes) {
		if (semantic_axis.kind != SemanticAxisKind::Time) continue;
		if (has_explicit) {
			throw BinderException("read_om axes.time cannot be combined with valid_times");
		}
		TemporalBindConfiguration result;
		result.values = semantic_axis.timestamps;
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
			if (value.IsNull() || !Timestamp::IsFinite(value.GetValue<timestamp_t>())) {
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

struct ReadOmBindData final : TableFunctionData {
	ReadOmBindData(std::string path_p, BoundSchema schema_p, AxisDeclarations axes_p,
	               SpatialBindConfiguration spatial_p, std::optional<TemporalBindConfiguration> temporal_p,
	               SemanticAxes semantic_axes_p,
	               std::shared_ptr<RemoteReadSession> remote_session_p,
	               std::shared_ptr<ScanMetrics> metrics_p,
	               std::shared_ptr<ScanMemoryAccount> memory_account_p)
	    : path(std::move(path_p)), schema(std::move(schema_p)), axes(std::move(axes_p)), spatial(std::move(spatial_p)),
	      temporal(std::move(temporal_p)), semantic_axes(std::move(semantic_axes_p)),
	      remote_session(std::move(remote_session_p)), metrics(std::move(metrics_p)),
	      memory_account(std::move(memory_account_p)) {
	}

	ReadOmBindData(const ReadOmBindData &other)
	    : TableFunctionData(other), path(other.path), schema(other.schema), axes(other.axes), spatial(other.spatial),
	      temporal(other.temporal), semantic_axes(other.semantic_axes), axis_predicate(other.axis_predicate),
	      remote_session(other.remote_session), metrics(other.metrics), memory_account(other.memory_account) {
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<ReadOmBindData>(*this);
	}

	bool Equals(const FunctionData &other_p) const override {
		const auto &other = other_p.Cast<ReadOmBindData>();
		return path == other.path && axes == other.axes && temporal == other.temporal &&
		       semantic_axes == other.semantic_axes &&
		       spatial.grid_signature == other.spatial.grid_signature &&
		       spatial.layout_name == other.spatial.layout_name && spatial.source == other.spatial.source &&
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
};

std::uint64_t AddMemoryBytes(std::uint64_t total, std::uint64_t bytes) {
	return total > UINT64_MAX - bytes ? UINT64_MAX : total + bytes;
}

std::uint64_t VectorMemoryBytes(std::size_t capacity, std::size_t element_size) {
	if (element_size != 0 && capacity > UINT64_MAX / element_size) return UINT64_MAX;
	return static_cast<std::uint64_t>(capacity * element_size);
}

std::uint64_t StringMemoryBytes(const std::string &value) {
	return static_cast<std::uint64_t>(value.size());
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
	bytes = AddMemoryBytes(bytes, StringVectorMemoryBytes(predicate.fallback_reasons));
	return bytes;
}

std::uint64_t EstimateAxisPredicateMemory(const AxisPredicate &predicate) {
	std::uint64_t bytes = sizeof(predicate);
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
	bytes = AddMemoryBytes(bytes, EstimateSpatialPredicateMemory(*spatial.predicate));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.grid_signature));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.layout_name));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.source));
	return bytes;
}

std::uint64_t EstimateSpatialCopyMemory(const SpatialBindConfiguration &spatial) {
	std::uint64_t bytes = EstimateSpatialLayoutMemory(spatial.layout);
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.grid_signature));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.layout_name));
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(spatial.source));
	return bytes;
}

std::uint64_t EstimateTemporalMemory(const std::optional<TemporalBindConfiguration> &temporal) {
	if (!temporal) return 0;
	return VectorMemoryBytes(temporal->values.capacity(), sizeof(timestamp_t));
}

std::uint64_t EstimateBindMemory(const ReadOmBindData &data) {
	std::uint64_t bytes = sizeof(data);
	bytes = AddMemoryBytes(bytes, StringMemoryBytes(data.path));
	bytes = AddMemoryBytes(bytes, EstimateSchemaMemory(data.schema));
	bytes = AddMemoryBytes(bytes, EstimateAxisDeclarationsMemory(data.axes));
	bytes = AddMemoryBytes(bytes, EstimateSpatialBindMemory(data.spatial));
	bytes = AddMemoryBytes(bytes, EstimateTemporalMemory(data.temporal));
	bytes = AddMemoryBytes(bytes, EstimateSemanticAxesMemory(data.semantic_axes));
	bytes = AddMemoryBytes(bytes, EstimateAxisPredicateMemory(*data.axis_predicate));
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

std::uint64_t EstimateSpatialBatchMemory(const SpatialBatch &batch) {
	std::uint64_t bytes = VectorMemoryBytes(batch.logical_positions.capacity(), sizeof(std::uint64_t));
	return AddMemoryBytes(bytes, EstimateSegmentsMemory(batch.read_segments));
}

void ObserveComplexFilter(ClientContext &, LogicalGet &get, FunctionData *bind_data,
                           vector<unique_ptr<Expression>> &filters) {
	if (bind_data && !filters.empty()) {
		auto &data = bind_data->Cast<ReadOmBindData>();
		data.metrics->MarkFilterCallbackInvoked();
		const auto output_column_base = data.schema.variables.size() + (data.spatial.grid ? 2 : 0) +
		                               (data.temporal ? 1 : 0);
		*data.axis_predicate = ExtractAxisPredicate(get, filters, output_column_base, data.semantic_axes);
		if (data.spatial.grid && data.spatial.layout) {
			*data.spatial.predicate = ExtractSpatialPredicate(get, filters, data.schema.variables.size());
			const auto selection = BuildSpatialSelection(*data.spatial.grid, *data.spatial.layout,
			                                              *data.spatial.predicate);
			data.metrics->SetSpatialSelection(SpatialSelectionModeName(selection.mode),
			                                  selection.residual_filter_retained, selection.fallback_reasons,
			                                  selection.candidate_rows, false);
		} else {
			data.metrics->SetSpatialSelection("fallback", true, {"spatial_mapping_unavailable_in_callback"}, 0);
		}
		if (data.memory_account) data.memory_account->Set(EstimateBindMemory(data));
	}
	// Preserve every expression. DuckDB rebuilds these as ordinary filters
	// after this callback when table-function filter pushdown is disabled.
}

struct ReadOmGlobalState final : GlobalTableFunctionState {
	ReadOmGlobalState(ClientContext &context, const ReadOmBindData &bind_data, const std::vector<column_t> &column_ids,
	                  std::shared_ptr<ScanMetrics> metrics_p)
	    : path(bind_data.path), schema(bind_data.schema),
	      projection(schema, column_ids, bind_data.spatial.grid.has_value(), bind_data.temporal.has_value(),
	                 bind_data.semantic_axes),
	      spatial(bind_data.spatial), temporal(bind_data.temporal), semantic_axes(bind_data.semantic_axes),
	      axis_predicate(*bind_data.axis_predicate), metrics(std::move(metrics_p)),
	      memory_account(std::make_shared<ScanMemoryAccount>(metrics)),
	      reader(make_uniq<OmV3Reader>(OpenOmReadAt(context, bind_data.path, bind_data.remote_session, metrics,
	                                               ScanMetadataStage::Scan,
	                                               &GetDuckomoSessionState(context)->Cache()))) {
		// The file may have changed since binding. Revalidate its complete tree
		// before constructing decoder state, while keeping binding metadata-only.
		auto current_schema = BuildBoundSchema(ReadMetadataTree(*reader));
		if (!SameSchema(schema, current_schema)) {
			throw ReaderError(ReaderErrorCode::InvalidMetadata,
			                  "OM array metadata changed between bind and scan for '" + path + "'");
		}
		if (spatial.grid && spatial.layout) {
			selection = BuildSpatialSelection(*spatial.grid, *spatial.layout, *spatial.predicate);
			metrics->SetSpatialSelection(SpatialSelectionModeName(selection.mode),
			                             selection.residual_filter_retained, selection.fallback_reasons,
			                             selection.candidate_rows, false);
		if (selection.mode == SpatialSelectionMode::Empty) {
			no_candidates = true;
			scan_completed = true;
			metrics->SetScanComplete(true);
			RefreshMemoryAccount();
			return;
			}
		}
		selection_cursor = make_uniq<AxisSelectionCursor>(schema.shape, semantic_axes, axis_predicate);
		if (selection_cursor->IsEmpty()) {
			no_candidates = true;
			scan_completed = true;
			metrics->SetScanComplete(true);
			RefreshMemoryAccount();
			return;
		}
		if (selection.mode == SpatialSelectionMode::Restricted &&
		    selection.candidate_rows <= selection_cursor->CandidateCount()) {
			spatial_cursor = make_uniq<SpatialBatchCursor>(*spatial.grid, *spatial.layout, selection);
		}
		candidate_count = selection_cursor->CandidateCount();
		if (spatial.grid && spatial.layout) {
			candidate_count = std::min(candidate_count, selection.candidate_rows);
			// The precise intersection cardinality is not known without exhausting
			// the lazy cursor. Avoid creating idle workers for sparse mixed queries.
			if (!axis_predicate.necessary_conditions.empty()) {
				candidate_count = std::min<std::uint64_t>(candidate_count, SCAN_TASK_POSITION_WINDOW);
			}
		}
		Value configured_limit;
		std::uint64_t requested_limit = static_cast<std::uint64_t>(context.db->NumberOfThreads());
		if (context.TryGetCurrentSetting("duckomo_max_threads", configured_limit)) {
			const auto configured = configured_limit.GetValue<std::int64_t>();
			if (configured > 0) requested_limit = std::min<std::uint64_t>(requested_limit,
			                                                           static_cast<std::uint64_t>(configured));
		}
		const auto available_tasks = candidate_count == 0
		                                 ? std::uint64_t(1)
		                                 : candidate_count / SCAN_TASK_POSITION_WINDOW +
		                                       (candidate_count % SCAN_TASK_POSITION_WINDOW != 0);
		max_threads = static_cast<idx_t>(std::max<std::uint64_t>(1, std::min(requested_limit, available_tasks)));
		metrics->SetSpatialSelection(axis_predicate.necessary_conditions.empty() ?
		                                 (selection.mode == SpatialSelectionMode::Restricted ? "restricted" :
		                                  SpatialSelectionModeName(selection.mode)) : "axis_restricted",
		                             true, axis_predicate.fallback_reasons, candidate_count,
		                             candidate_count == 0);
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

	bool GetNextTask(ClientContext &context, std::vector<std::uint64_t> &positions) {
		std::lock_guard<std::mutex> guard(task_mutex);
		if (stop_requested.load() || selection_exhausted || no_candidates) return false;
		if (spatial_cursor) {
			positions.clear();
			SpatialBatch batch;
			ScanMemoryAccount batch_account(metrics);
			const auto prior_position_capacity = positions.capacity();
			while (positions.size() < SCAN_TASK_POSITION_WINDOW && !spatial_cursor->Exhausted()) {
				if (context.IsInterrupted()) throw InterruptException();
				if (!spatial_cursor->Next(std::min<idx_t>(STANDARD_VECTOR_SIZE,
				                                         SCAN_TASK_POSITION_WINDOW - positions.size()), batch,
				                          [&context]() { if (context.IsInterrupted()) throw InterruptException(); })) break;
				for (const auto position : batch.logical_positions) {
					if (selection_cursor->Contains(position)) positions.push_back(position);
				}
				const auto position_growth = positions.capacity() > prior_position_capacity
				                                 ? VectorMemoryBytes(positions.capacity() - prior_position_capacity,
				                                                     sizeof(std::uint64_t))
				                                 : 0;
				batch_account.Set(AddMemoryBytes(position_growth, EstimateSpatialBatchMemory(batch)));
			}
			if (positions.empty()) {
				selection_exhausted = true;
				MaybeSetScanCompleteLocked();
				return false;
			}
			ClaimTaskLocked();
			return true;
		}
		const auto include_spatial = [this, &context](std::uint64_t logical_position) {
			if (context.IsInterrupted()) throw InterruptException();
			if (!spatial.grid || !spatial.layout) return true;
			const auto coordinate = spatial.layout->Coordinate(*spatial.grid, logical_position);
			for (const auto &constraint : spatial.predicate->necessary_conditions) {
				const auto value = constraint.axis == SpatialAxis::Latitude ? coordinate.latitude : coordinate.longitude;
				switch (constraint.comparison) {
				case SpatialComparison::Equal: if (value != constraint.constant) return false; break;
				case SpatialComparison::Less: if (!(value < constraint.constant)) return false; break;
				case SpatialComparison::LessEqual: if (!(value <= constraint.constant)) return false; break;
				case SpatialComparison::Greater: if (!(value > constraint.constant)) return false; break;
				case SpatialComparison::GreaterEqual: if (!(value >= constraint.constant)) return false; break;
				}
			}
			return true;
		};
		const auto position_count = selection_cursor->Next(SCAN_TASK_POSITION_WINDOW, positions, include_spatial);
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
		if (!memory_account) return;
		std::uint64_t bytes = sizeof(*this);
		bytes = AddMemoryBytes(bytes, EstimateSchemaMemory(schema));
		bytes = AddMemoryBytes(bytes, EstimateProjectionMemory(projection));
		bytes = AddMemoryBytes(bytes, EstimateSpatialCopyMemory(spatial));
		bytes = AddMemoryBytes(bytes, EstimateTemporalMemory(temporal));
		bytes = AddMemoryBytes(bytes, EstimateSemanticAxesMemory(semantic_axes));
		bytes = AddMemoryBytes(bytes, EstimateAxisPredicateMemory(axis_predicate));
		bytes = AddMemoryBytes(bytes, EstimateSpatialSelectionMemory(selection));
		if (selection_cursor) bytes = AddMemoryBytes(bytes, selection_cursor->EstimatedBytes());
		if (spatial_cursor) bytes = AddMemoryBytes(bytes, spatial_cursor->EstimatedBytes());
		bytes = AddMemoryBytes(bytes, sizeof(OmV3Reader));
		memory_account->Set(bytes);
	}

	void ClaimTaskLocked() {
		++outstanding_tasks;
		metrics->RecordScanTaskCreated();
		metrics->RecordScanTaskClaimed();
	}

	void MaybeSetScanCompleteLocked() {
		if (selection_exhausted && outstanding_tasks == 0 && !stop_requested.load() && !scan_completed) {
			scan_completed = true;
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
	unique_ptr<SpatialBatchCursor> spatial_cursor;
    std::shared_ptr<ScanMetrics> metrics;
    std::shared_ptr<ScanMemoryAccount> memory_account;
    unique_ptr<OmV3Reader> reader;
	mutable std::mutex task_mutex;
	std::atomic<bool> stop_requested{false};
	std::atomic<std::uint64_t> next_worker_id{0};
	bool selection_exhausted = false;
	std::uint64_t outstanding_tasks = 0; // guarded by task_mutex
	bool no_candidates = false;
	std::uint64_t candidate_count = 0;
	idx_t max_threads = 1;
    bool scan_completed = false;
	std::atomic<bool> metrics_status_set{false};
};

struct ReadOmLocalState final : LocalTableFunctionState {
	ReadOmLocalState(ClientContext &context, const ReadOmBindData &bind_data, const ReadOmGlobalState &global_state,
	                 std::uint64_t worker_id_p)
	    : worker_id(worker_id_p), memory_account(std::make_shared<ScanMemoryAccount>(bind_data.metrics)) {
		if (global_state.no_candidates) {
			RefreshMemoryAccount();
			return;
		}
		reader = make_uniq<OmV3Reader>(OpenOmReadAt(context, bind_data.path, bind_data.remote_session,
		                                             bind_data.metrics, ScanMetadataStage::Scan,
		                                             &GetDuckomoSessionState(context)->Cache()));
		const auto current_schema = BuildBoundSchema(ReadMetadataTree(*reader));
		if (!SameSchema(global_state.schema, current_schema)) {
			throw ReaderError(ReaderErrorCode::InvalidMetadata,
			                  "OM array metadata changed while opening a scan worker for '" + bind_data.path + "'");
		}
		decoders.reserve(global_state.projection.GetRequiredVariableIds().size());
		for (const auto variable_index : global_state.projection.GetRequiredVariableIds()) {
			const auto &variable = current_schema.variables.at(variable_index);
			decoders.emplace_back(make_uniq<OmDecoderState>(BorrowedOmVariable(variable.metadata_owner)));
		}
		RefreshMemoryAccount();
	}

	void RefreshMemoryAccount() const {
		std::uint64_t bytes = sizeof(*this);
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(decoders.capacity(), sizeof(unique_ptr<OmDecoderState>)));
		bytes = AddMemoryBytes(bytes, VectorMemoryBytes(task_positions.capacity(), sizeof(std::uint64_t)));
		if (reader) bytes = AddMemoryBytes(bytes, sizeof(OmV3Reader));
		memory_account->Set(bytes);
	}

	unique_ptr<OmV3Reader> reader;
	vector<unique_ptr<OmDecoderState>> decoders;
	std::vector<std::uint64_t> task_positions;
	idx_t task_position_offset = 0;
	std::uint64_t worker_id;
	std::shared_ptr<ScanMemoryAccount> memory_account;
	bool registered_as_active = false;
	bool task_active = false;
};

unique_ptr<FunctionData> BindReadOm(ClientContext &context, TableFunctionBindInput &input,
                                    vector<LogicalType> &return_types, vector<std::string> &names) {
	auto metrics = CreateScanMetrics(context);
	RegisterScanMetricsQueryState(context, metrics);
	auto memory_account = std::make_shared<ScanMemoryAccount>(metrics);
	if (input.inputs.size() != 1) {
		throw BinderException("read_om expects one constant VARCHAR path");
	}
	if (input.inputs[0].IsNull() || input.inputs[0].type().id() != LogicalTypeId::VARCHAR) {
		throw BinderException("read_om expects one constant VARCHAR path");
	}
	const auto requested_path = input.inputs[0].GetValue<std::string>();
	std::shared_ptr<RemoteReadSession> remote_session;
	std::string path;
	std::unique_ptr<ReadAtFile> input_file;
	if (IsSupportedRemoteOmUri(requested_path)) {
		remote_session = RemoteReadSession::Create(context, requested_path, metrics);
		path = remote_session->Path();
		input_file = remote_session->Open(context, ScanMetadataStage::Bind,
		                                 &GetDuckomoSessionState(context)->Cache());
	} else {
		path = LocalFilePathFromValue(input.inputs[0]);
		metrics->SetTransportAvailable(false);
		const auto cache = GetDuckomoSessionState(context)->Cache().Stats();
		metrics->SetCacheState(cache.enabled, cache.capacity_bytes, cache.accounted_bytes,
		                       cache.peak_charged_bytes, cache.control_bytes, cache.evictions,
		                       "local_source", "not_applicable");
		input_file = std::make_unique<LocalFile>(
		    LocalFile::Open(context, path, metrics, ScanMetadataStage::Bind));
	}
	OmV3Reader reader(std::move(input_file));
	auto schema = BuildBoundSchema(ReadMetadataTree(reader));
	for (const auto &variable : schema.variables) {
		metrics->DeclareVariable(variable.canonical_path);
	}

	const auto dimensions_entry = input.named_parameters.find("dimensions");
	const auto *dimensions = dimensions_entry == input.named_parameters.end() ? nullptr : &dimensions_entry->second;
	auto axes = ValidateAxisDeclarations(dimensions, schema);
	auto spatial = BindSpatialConfiguration(input, schema, axes, *metrics);
	std::vector<std::string> spatial_axis_names;
	if (spatial.layout) {
		if (spatial.layout->flattened) {
			spatial_axis_names.emplace_back(spatial.layout->axes.at(spatial.layout->point_axis));
		} else {
			spatial_axis_names.emplace_back(spatial.layout->axes.at(spatial.layout->latitude_axis));
			spatial_axis_names.emplace_back(spatial.layout->axes.at(spatial.layout->longitude_axis));
		}
	}
	const auto *semantic_axes_value = GetNamedValue(input, "axes");
	auto semantic_axes = BindSemanticAxes(semantic_axes_value, schema, axes, spatial_axis_names);
	auto temporal = BindTemporalConfiguration(input, schema, axes, semantic_axes);

	const auto semantic_column_count = static_cast<std::size_t>(std::count_if(
	    semantic_axes.begin(), semantic_axes.end(),
	    [](const SemanticAxis &axis) { return axis.kind != SemanticAxisKind::Time; }));
	return_types.reserve(return_types.size() + schema.variables.size() + (spatial.grid ? 2 : 0) +
	                     (temporal ? 1 : 0) + semantic_column_count);
	names.reserve(names.size() + schema.variables.size() + (spatial.grid ? 2 : 0) +
	              (temporal ? 1 : 0) + semantic_column_count);
	for (const auto &variable : schema.variables) {
		return_types.emplace_back(variable.type);
		names.emplace_back(variable.column_name);
	}
	if (spatial.grid) {
		AppendSpatialOutputColumns(schema, return_types, names);
	}
	if (temporal) {
		for (const auto &name : names) {
			if (StringUtil::CIEquals(name, "valid_time")) {
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
			if (StringUtil::CIEquals(existing_name, name)) {
				throw BinderException("read_om " + name + " column name conflicts with existing column '" +
				                      existing_name + "'");
			}
		}
		return_types.emplace_back(semantic_axis.output_type);
		names.emplace_back(name);
	}
	auto bind_data = make_uniq<ReadOmBindData>(path, std::move(schema), std::move(axes), std::move(spatial),
	                                          std::move(temporal), std::move(semantic_axes),
	                                          std::move(remote_session), metrics, memory_account);
	memory_account->Set(EstimateBindMemory(bind_data->Cast<ReadOmBindData>()));
	return bind_data;
}

unique_ptr<GlobalTableFunctionState> InitReadOm(ClientContext &context, TableFunctionInitInput &input) {
	if (!input.bind_data) {
		throw InternalException("read_om was initialized without bind data");
	}
	const auto &bind_data = input.bind_data->Cast<ReadOmBindData>();
	return make_uniq<ReadOmGlobalState>(context, bind_data, input.column_ids, bind_data.metrics);
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

void ScanReadOmImpl(ClientContext &context, ReadOmGlobalState &state, ReadOmLocalState &local, DataChunk &output) {
	output.SetCardinality(0);
	if (state.stop_requested.load() || context.IsInterrupted()) {
		throw InterruptException();
	}
	while (local.task_position_offset >= local.task_positions.size()) {
		if (local.task_active) {
			state.RecordTaskCompleted();
			local.task_active = false;
		}
		if (!state.GetNextTask(context, local.task_positions)) {
			local.RefreshMemoryAccount();
			return;
		}
		local.RefreshMemoryAccount();
		local.task_position_offset = 0;
		local.task_active = true;
	}
	const auto count = std::min<idx_t>(STANDARD_VECTOR_SIZE,
	                                  local.task_positions.size() - local.task_position_offset);
	struct WorkerExecution final {
		ScanMetrics &metrics;
		std::uint64_t worker_id;
		WorkerExecution(ScanMetrics &metrics_p, std::uint64_t worker_id_p)
		    : metrics(metrics_p), worker_id(worker_id_p) { metrics.BeginWorkerExecution(worker_id); }
		~WorkerExecution() { metrics.EndWorkerExecution(worker_id); }
	} worker_execution(*state.metrics, local.worker_id);
	std::vector<std::uint64_t> logical_positions(
	    local.task_positions.begin() + local.task_position_offset,
	    local.task_positions.begin() + local.task_position_offset + count);
	const auto segments = BuildSelectedBatchSegments(state.schema.shape, logical_positions);
	ScanMemoryAccount batch_account(state.metrics);
	auto refresh_batch_memory = [&]() {
		batch_account.Set(AddMemoryBytes(VectorMemoryBytes(logical_positions.capacity(), sizeof(std::uint64_t)),
		                                  EstimateSegmentsMemory(segments)));
	};
	refresh_batch_memory();
	const auto &slots = state.projection.GetOutputSlots();
	const auto &required_variables = state.projection.GetRequiredVariableIds();
	if (output.data.size() != slots.size() || local.decoders.size() != required_variables.size()) {
		throw InternalException("read_om output column count does not match its projection plan");
	}

    for (std::size_t output_column = 0; output_column < slots.size(); output_column++) {
		if (slots[output_column].kind == OutputColumnKind::ValidTime) {
			auto *values = FlatVector::GetData<timestamp_t>(output.data[output_column]);
			for (std::uint64_t row = 0; row < count; row++) {
				values[row] = state.temporal->Coordinate(logical_positions[row]);
			}
			FlatVector::Validity(output.data[output_column]).SetAllValid(static_cast<idx_t>(count));
			continue;
		}
		if (slots[output_column].kind == OutputColumnKind::Latitude ||
		    slots[output_column].kind == OutputColumnKind::Longitude) {
			if (!state.spatial.grid || !state.spatial.layout) {
				throw InternalException("read_om projected a spatial column without a bound grid");
			}
			auto *values = FlatVector::GetData<double>(output.data[output_column]);
			for (std::uint64_t row = 0; row < count; row++) {
				const auto logical_index = logical_positions[row];
				const auto coordinate = state.spatial.layout->Coordinate(*state.spatial.grid, logical_index);
				values[row] = slots[output_column].kind == OutputColumnKind::Latitude ? coordinate.latitude
				                                                                            : coordinate.longitude;
			}
			FlatVector::Validity(output.data[output_column]).SetAllValid(static_cast<idx_t>(count));
			continue;
		}
		if (slots[output_column].kind == OutputColumnKind::SemanticCoordinate) {
			if (slots[output_column].semantic_axis_index >= state.semantic_axes.size()) {
				throw InternalException("read_om projected an unknown semantic coordinate");
			}
			const auto &semantic_axis = state.semantic_axes[slots[output_column].semantic_axis_index];
			for (std::uint64_t row = 0; row < count; row++) {
				const auto logical_index = logical_positions[row];
				output.data[output_column].SetValue(row, semantic_axis.CoordinateValue(logical_index));
			}
			continue;
		}
		if (slots[output_column].kind != OutputColumnKind::Cardinality) {
            continue;
        }
        auto *values = FlatVector::GetData<bool>(output.data[output_column]);
        std::fill_n(values, static_cast<idx_t>(count), true);
        FlatVector::Validity(output.data[output_column]).SetAllValid(static_cast<idx_t>(count));
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
        auto *values = FlatVector::GetData<float>(output.data[primary_output]);
        auto &validity = FlatVector::Validity(output.data[primary_output]);
        validity.SetAllValid(static_cast<idx_t>(count));
        for (const auto &segment : segments) {
            if (context.IsInterrupted()) {
                throw InterruptException();
            }
			std::vector<std::uint64_t> cube_offset(segment.read_count.size(), 0);
			refresh_batch_memory();
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

        for (idx_t output_column = 0; output_column < slots.size(); output_column++) {
			if (output_column == primary_output || slots[output_column].kind != OutputColumnKind::Value ||
			    slots[output_column].required_variable_index != required_index) {
				continue;
			}
			auto *duplicate_values = FlatVector::GetData<float>(output.data[output_column]);
            std::memcpy(duplicate_values, values, static_cast<std::size_t>(count) * sizeof(float));
            auto &duplicate_validity = FlatVector::Validity(output.data[output_column]);
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
	local.task_position_offset += count;
	state.metrics->RecordScannedRows(count);
	output.SetCardinality(static_cast<idx_t>(count));
	if (!local.registered_as_active) {
		state.metrics->RecordWorkerActive(local.worker_id);
		local.registered_as_active = true;
	}
}

void ScanReadOm(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	if (!input.global_state) {
		throw InternalException("read_om scan has no global state");
	}
	auto &state = input.global_state->Cast<ReadOmGlobalState>();
	if (!input.local_state) {
		throw InternalException("read_om scan has no local worker state");
	}
	auto &local = input.local_state->Cast<ReadOmLocalState>();
	try {
		ScanReadOmImpl(context, state, local, output);
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
                                             vector<LogicalType> &return_types, vector<std::string> &names) {
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

struct ClearCacheGlobalState final : GlobalTableFunctionState {
	RangeCacheClearResult result;
	bool emitted = false;
};

unique_ptr<FunctionData> BindClearCache(ClientContext &, TableFunctionBindInput &,
                                        vector<LogicalType> &return_types, vector<std::string> &names) {
	return_types.emplace_back(LogicalType::UBIGINT);
	names.emplace_back("cleared_entries");
	return_types.emplace_back(LogicalType::UBIGINT);
	names.emplace_back("cleared_bytes");
	return nullptr;
}

unique_ptr<GlobalTableFunctionState> InitClearCache(ClientContext &context, TableFunctionInitInput &) {
	auto result = make_uniq<ClearCacheGlobalState>();
	result->result = GetDuckomoSessionState(context)->Cache().Clear();
	return result;
}

void ScanClearCache(ClientContext &, TableFunctionInput &input, DataChunk &output) {
	if (!input.global_state) throw InternalException("duckomo_clear_cache was initialized without state");
	auto &state = input.global_state->Cast<ClearCacheGlobalState>();
	if (state.emitted) return;
	output.SetValue(0, 0, Value::UBIGINT(state.result.entries));
	output.SetValue(1, 0, Value::UBIGINT(state.result.accounted_bytes));
	output.SetCardinality(1);
	state.emitted = true;
}

} // namespace

TableFunction GetReadOmFunction() {
	TableFunction function("read_om", {LogicalType::VARCHAR}, ScanReadOm, BindReadOm, InitReadOm, InitReadOmLocal);
	function.named_parameters["dimensions"] = LogicalType::MAP(LogicalType::VARCHAR, LogicalType::LIST(LogicalType::VARCHAR));
	function.named_parameters["grid"] = LogicalType::ANY;
	function.named_parameters["spatial_axes"] = LogicalType::LIST(LogicalType::VARCHAR);
	function.named_parameters["domain"] = LogicalType::VARCHAR;
	function.named_parameters["valid_times"] = LogicalType::LIST(LogicalType::TIMESTAMP);
	function.named_parameters["axes"] = LogicalType::ANY;
	function.get_virtual_columns = GetReadOmVirtualColumns;
    function.projection_pushdown = true;
	function.filter_pushdown = false;
	function.filter_prune = false;
	function.pushdown_complex_filter = ObserveComplexFilter;
	function.order_preservation_type = OrderPreservationType::NO_ORDER;
	return function;
}

TableFunction GetLastScanMetricsFunction() {
	return TableFunction("duckomo_last_scan_metrics", {}, ScanLastMetrics, BindLastScanMetrics,
	                     InitLastScanMetrics);
}

TableFunction GetClearCacheFunction() {
	return TableFunction("duckomo_clear_cache", {}, ScanClearCache, BindClearCache, InitClearCache);
}

void SetDuckomoCacheEnabled(ClientContext &context, bool enabled) {
	GetDuckomoSessionState(context)->Cache().SetEnabled(enabled);
}

void SetDuckomoCacheCapacity(ClientContext &context, std::uint64_t capacity_bytes) {
	GetDuckomoSessionState(context)->Cache().SetCapacity(capacity_bytes);
}

} // namespace duckomo
} // namespace duckdb
