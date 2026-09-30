#include "duckomo/read_om.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <limits>
#include <optional>
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
#include "duckdb/planner/column_binding.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckomo/batch.hpp"
#include "duckomo/domain_bbox.hpp"
#include "duckomo/dimensions.hpp"
#include "duckomo/domain_registry.hpp"
#include "duckomo/local_file.hpp"
#include "duckomo/metrics.hpp"
#include "duckomo/metadata.hpp"
#include "duckomo/projection.hpp"
#include "duckomo/reader.hpp"
#include "duckomo/regular_grid.hpp"
#include "duckomo/schema.hpp"
#include "duckomo/spatial_filter.hpp"
#include "duckomo/spatial_layout.hpp"
#include "duckomo/spatial_selection.hpp"

namespace duckdb {
namespace duckomo {

namespace {

std::string EnvironmentValue(const char *name) {
	const auto *value = std::getenv(name);
	return value == nullptr ? std::string() : std::string(value);
}

void WriteMetricsSidecar(const ScanMetrics &metrics, const std::string &path) noexcept;

// A table function's global scan state can finish before DuckDB has finished
// consuming the query (for example, when a downstream operator is interrupted
// after the final source batch). Keep a query-end observer so the sidecar
// status describes the complete SQL query rather than only the scanner.
class ScanMetricsQueryState final : public ClientContextState {
public:
	ScanMetricsQueryState(std::string state_key_p, std::string output_path_p, std::shared_ptr<ScanMetrics> metrics_p)
	    : state_key(std::move(state_key_p)), output_path(std::move(output_path_p)), metrics(std::move(metrics_p)) {
	}

	void QueryEnd(ClientContext &context, optional_ptr<ErrorData> error) override {
		if (error) {
			const auto current = metrics->Snapshot().status;
			if (error->Type() == ExceptionType::INTERRUPT) {
				metrics->SetStatus(ScanStatus::Cancelled, "cancelled");
			} else if (current != ScanStatus::Failed) {
				metrics->SetStatus(ScanStatus::Failed, "query_error");
			}
		} else {
			// A successful query can stop before the scan reaches EOF (for
			// example, when LIMIT has enough rows). Query completion is the
			// authoritative success signal; the scan destructor may have marked
			// this case as incomplete_scan already.
			metrics->SetStatus(ScanStatus::Succeeded);
		}
		WriteMetricsSidecar(*metrics, output_path);
		context.registered_state->Remove(state_key);
	}

private:
	std::string state_key;
	std::string output_path;
	std::shared_ptr<ScanMetrics> metrics;
};

std::shared_ptr<ScanMetrics> CreateScanMetrics(ClientContext &context) {
	static std::atomic<std::uint64_t> next_query_id{0};
	auto metrics = std::make_shared<ScanMetrics>();
	const auto query_id = std::to_string(static_cast<unsigned long long>(context.GetConnectionId())) + "-" +
	                      std::to_string(static_cast<unsigned long long>(next_query_id.fetch_add(1)));
	metrics->SetQueryIdentity(query_id, EnvironmentValue("DUCKOMO_SCENARIO"), context.GetCurrentQuery());
	metrics->SetFixture(EnvironmentValue("DUCKOMO_FIXTURE_ID"), EnvironmentValue("DUCKOMO_FIXTURE_SHA256"));
	metrics->SetDependencyCommit("duckdb", "08e34c447bae34eaee3723cac61f2878b6bdf787");
	metrics->SetDependencyCommit("om-file-format", "d8855e418e2231ae8439f0c7e840fa3f93b371e3");
	metrics->SetDependencyCommit("extension-ci-tools", "b777c70d30942cca5bef62d6d4fa23a13362f398");
	metrics->SetEnvironmentValue("platform", "linux");
	metrics->SetEnvironmentValue("build", "release");
	metrics->SetCachePolicyValue("application_cache", "disabled");
	return metrics;
}

void RegisterScanMetricsQueryState(ClientContext &context, const std::shared_ptr<ScanMetrics> &metrics) {
	const auto output_path = EnvironmentValue("DUCKOMO_METRICS_OUTPUT");
	if (!output_path.empty()) {
		const auto state_key = "duckomo.scan_metrics." + metrics->Snapshot().query_id;
		context.registered_state->Insert(state_key,
		                                make_shared_ptr<ScanMetricsQueryState>(state_key, output_path, metrics));
	}
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
	bool operator==(const TemporalBindConfiguration &other) const {
		return stride == other.stride && values == other.values;
	}
};

std::optional<TemporalBindConfiguration> BindTemporalConfiguration(const TableFunctionBindInput &input,
                                                                   const BoundSchema &schema,
                                                                   const AxisDeclarations &axes) {
	const auto *explicit_times = GetNamedValue(input, "valid_times");
	const bool has_explicit = explicit_times && !explicit_times->IsNull();
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
			current.stride = 1;
			for (std::size_t next = axis + 1; next < variable.shape.size(); next++) {
				current.stride *= variable.shape[next];
			}
		} else if (current.values.size() != 1 || (variable.time && !variable.time->scalar)) {
			throw BinderException(
			    "read_om time array requires a complete ordered time axis from coordinates or dimensions");
		}
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
	               std::shared_ptr<ScanMetrics> metrics_p)
	    : path(std::move(path_p)), schema(std::move(schema_p)), axes(std::move(axes_p)), spatial(std::move(spatial_p)),
	      temporal(std::move(temporal_p)), metrics(std::move(metrics_p)) {
	}

	ReadOmBindData(const ReadOmBindData &other)
	    : TableFunctionData(other), path(other.path), schema(other.schema), axes(other.axes), spatial(other.spatial),
	      temporal(other.temporal), metrics(other.metrics) {
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<ReadOmBindData>(*this);
	}

	bool Equals(const FunctionData &other_p) const override {
		const auto &other = other_p.Cast<ReadOmBindData>();
		return path == other.path && axes == other.axes && temporal == other.temporal &&
		       spatial.grid_signature == other.spatial.grid_signature &&
		       spatial.layout_name == other.spatial.layout_name && spatial.source == other.spatial.source &&
		       SameSpatialPredicate(*spatial.predicate, *other.spatial.predicate) && SameSchema(schema, other.schema);
	}

	bool SupportStatementCache() const override {
		return false;
	}

    std::string path;
    BoundSchema schema;
    AxisDeclarations axes;
    SpatialBindConfiguration spatial;
	std::optional<TemporalBindConfiguration> temporal;
	std::shared_ptr<ScanMetrics> metrics;
};

void ObserveComplexFilter(ClientContext &, LogicalGet &get, FunctionData *bind_data,
                           vector<unique_ptr<Expression>> &filters) {
	if (bind_data && !filters.empty()) {
		auto &data = bind_data->Cast<ReadOmBindData>();
		data.metrics->MarkFilterCallbackInvoked();
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
	}
	// Preserve every expression. DuckDB rebuilds these as ordinary filters
	// after this callback when table-function filter pushdown is disabled.
}

struct ReadOmGlobalState final : GlobalTableFunctionState {
	ReadOmGlobalState(ClientContext &context, const ReadOmBindData &bind_data, const std::vector<column_t> &column_ids,
	                  std::shared_ptr<ScanMetrics> metrics_p)
	    : path(bind_data.path), schema(bind_data.schema),
	      projection(schema, column_ids, bind_data.spatial.grid.has_value(), bind_data.temporal.has_value()),
	      spatial(bind_data.spatial), temporal(bind_data.temporal), metrics(std::move(metrics_p)),
	      reader(make_uniq<OmV3Reader>(LocalFile::Open(context, bind_data.path, metrics, ScanMetadataStage::Scan))) {
		// The file may have changed since binding. Revalidate its complete tree
		// before constructing decoder state, while keeping binding metadata-only.
		auto current_schema = BuildBoundSchema(ReadMetadataTree(*reader));
		if (!SameSchema(schema, current_schema)) {
			throw ReaderError(ReaderErrorCode::InvalidMetadata,
			                  "OM array metadata changed between bind and scan for local file '" + path + "'");
		}
		if (spatial.grid && spatial.layout) {
			selection = BuildSpatialSelection(*spatial.grid, *spatial.layout, *spatial.predicate);
			metrics->SetSpatialSelection(SpatialSelectionModeName(selection.mode),
			                             selection.residual_filter_retained, selection.fallback_reasons,
			                             selection.candidate_rows, false);
			spatial_cursor = make_uniq<SpatialBatchCursor>(*spatial.grid, *spatial.layout, selection);
			if (selection.mode == SpatialSelectionMode::Empty) {
				return;
			}
		}

        decoders.reserve(projection.GetRequiredVariableIds().size());
        for (const auto variable_index : projection.GetRequiredVariableIds()) {
            const auto &variable = current_schema.variables.at(variable_index);
            decoders.emplace_back(make_uniq<OmDecoderState>(BorrowedOmVariable(variable.metadata_owner)));
        }
	}

	~ReadOmGlobalState() override {
        if (!metrics) {
            return;
        }
        if (!scan_completed && !metrics_status_set) {
            metrics->SetStatus(ScanStatus::Failed, "incomplete_scan");
        }
        WriteMetricsSidecar(*metrics);
    }

    std::string path;
    BoundSchema schema;
    ProjectionPlan projection;
    SpatialBindConfiguration spatial;
	std::optional<TemporalBindConfiguration> temporal;
	SpatialSelection selection;
    unique_ptr<SpatialBatchCursor> spatial_cursor;
    std::shared_ptr<ScanMetrics> metrics;
    std::uint64_t next_linear_index = 0;
    unique_ptr<OmV3Reader> reader;
    vector<unique_ptr<OmDecoderState>> decoders;
    bool scan_completed = false;
    bool metrics_status_set = false;
};

unique_ptr<FunctionData> BindReadOm(ClientContext &context, TableFunctionBindInput &input,
                                    vector<LogicalType> &return_types, vector<std::string> &names) {
	auto metrics = CreateScanMetrics(context);
	RegisterScanMetricsQueryState(context, metrics);
	if (input.inputs.size() != 1) {
		throw BinderException("read_om expects one constant VARCHAR path");
	}
	const auto path = LocalFilePathFromValue(input.inputs[0]);
	OmV3Reader reader(LocalFile::Open(context, path, metrics, ScanMetadataStage::Bind));
	auto schema = BuildBoundSchema(ReadMetadataTree(reader));
	for (const auto &variable : schema.variables) {
		metrics->DeclareVariable(variable.canonical_path);
	}

	const auto dimensions_entry = input.named_parameters.find("dimensions");
	const auto *dimensions = dimensions_entry == input.named_parameters.end() ? nullptr : &dimensions_entry->second;
	auto axes = ValidateAxisDeclarations(dimensions, schema);
	auto spatial = BindSpatialConfiguration(input, schema, axes, *metrics);
	auto temporal = BindTemporalConfiguration(input, schema, axes);

	return_types.reserve(return_types.size() + schema.variables.size() + (spatial.grid ? 2 : 0) + (temporal ? 1 : 0));
	names.reserve(names.size() + schema.variables.size() + (spatial.grid ? 2 : 0) + (temporal ? 1 : 0));
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
	return make_uniq<ReadOmBindData>(path, std::move(schema), std::move(axes), std::move(spatial), std::move(temporal),
	                                 std::move(metrics));
}

unique_ptr<GlobalTableFunctionState> InitReadOm(ClientContext &context, TableFunctionInitInput &input) {
	if (!input.bind_data) {
		throw InternalException("read_om was initialized without bind data");
	}
	const auto &bind_data = input.bind_data->Cast<ReadOmBindData>();
	return make_uniq<ReadOmGlobalState>(context, bind_data, input.column_ids, bind_data.metrics);
}

void ScanReadOmImpl(ClientContext &context, ReadOmGlobalState &state, DataChunk &output) {
	output.SetCardinality(0);
	std::uint64_t count = 0;
	std::vector<BatchSegment> segments;
	std::vector<std::uint64_t> logical_positions;
	if (state.spatial_cursor) {
		SpatialBatch batch;
		if (!state.spatial_cursor->Next(STANDARD_VECTOR_SIZE, batch, [&context] {
				if (context.IsInterrupted()) {
					throw InterruptException();
				}
			})) {
			state.scan_completed = true;
			if (state.metrics) {
				state.metrics->SetStatus(ScanStatus::Succeeded);
				state.metrics_status_set = true;
			}
			return;
		}
		count = batch.logical_positions.size();
		logical_positions = std::move(batch.logical_positions);
		segments = std::move(batch.read_segments);
	} else {
		if (state.next_linear_index >= state.schema.row_count) {
			state.scan_completed = true;
			if (state.metrics) {
				state.metrics->SetStatus(ScanStatus::Succeeded);
				state.metrics_status_set = true;
			}
			return;
		}
		count = std::min<std::uint64_t>(state.schema.row_count - state.next_linear_index,
		                                static_cast<std::uint64_t>(STANDARD_VECTOR_SIZE));
		segments = BuildBatchSegments(state.schema.shape, state.next_linear_index, count);
	}
    const auto &slots = state.projection.GetOutputSlots();
    const auto &required_variables = state.projection.GetRequiredVariableIds();
    if (output.data.size() != slots.size() || state.decoders.size() != required_variables.size()) {
        throw InternalException("read_om output column count does not match its projection plan");
	}

    for (std::size_t output_column = 0; output_column < slots.size(); output_column++) {
		if (slots[output_column].is_valid_time) {
			auto *values = FlatVector::GetData<timestamp_t>(output.data[output_column]);
			const auto &time = *state.temporal;
			for (std::uint64_t row = 0; row < count; row++) {
				const auto position = state.spatial_cursor ? logical_positions[row] : state.next_linear_index + row;
				const auto time_index = time.stride == 0 ? 0 : (position / time.stride) % time.values.size();
				values[row] = time.values[time_index];
			}
			FlatVector::Validity(output.data[output_column]).SetAllValid(static_cast<idx_t>(count));
			continue;
		}
		if (slots[output_column].is_latitude || slots[output_column].is_longitude) {
			if (!state.spatial.grid || !state.spatial.layout) {
				throw InternalException("read_om projected a spatial column without a bound grid");
			}
			auto *values = FlatVector::GetData<double>(output.data[output_column]);
			for (std::uint64_t row = 0; row < count; row++) {
				const auto logical_index = state.spatial_cursor ? logical_positions[row] : state.next_linear_index + row;
				const auto coordinate = state.spatial.layout->Coordinate(*state.spatial.grid, logical_index);
				values[row] = slots[output_column].is_latitude ? coordinate.latitude : coordinate.longitude;
			}
			FlatVector::Validity(output.data[output_column]).SetAllValid(static_cast<idx_t>(count));
			continue;
		}
		if (!slots[output_column].is_cardinality) {
            continue;
        }
        auto *values = FlatVector::GetData<bool>(output.data[output_column]);
        std::fill_n(values, static_cast<idx_t>(count), true);
        FlatVector::Validity(output.data[output_column]).SetAllValid(static_cast<idx_t>(count));
    }

    for (std::size_t required_index = 0; required_index < required_variables.size(); required_index++) {
        idx_t primary_output = DConstants::INVALID_INDEX;
        for (idx_t output_column = 0; output_column < slots.size(); output_column++) {
			if (!slots[output_column].is_cardinality && !slots[output_column].is_latitude &&
			    !slots[output_column].is_longitude && !slots[output_column].is_valid_time &&
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
            const auto variable_index = required_variables[required_index];
            const auto &variable = state.schema.variables.at(variable_index);
            state.reader->DecodeSelection(*state.decoders[required_index], variable.canonical_path,
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
			if (output_column == primary_output || slots[output_column].is_cardinality ||
			    slots[output_column].is_latitude || slots[output_column].is_longitude ||
			    slots[output_column].is_valid_time || slots[output_column].required_variable_index != required_index) {
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
	if (!state.spatial_cursor) {
		state.next_linear_index += count;
	}
	output.SetCardinality(static_cast<idx_t>(count));
}

void ScanReadOm(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	if (!input.global_state) {
		throw InternalException("read_om scan has no global state");
	}
	auto &state = input.global_state->Cast<ReadOmGlobalState>();
	try {
		ScanReadOmImpl(context, state, output);
	} catch (const InterruptException &) {
		if (state.metrics) {
			state.metrics->SetStatus(ScanStatus::Cancelled, "cancelled");
			state.metrics_status_set = true;
		}
		throw;
	} catch (const ReaderError &error) {
		if (state.metrics) {
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
		if (state.metrics) {
			state.metrics->SetStatus(ScanStatus::Failed, "scan_error");
			state.metrics_status_set = true;
		}
		throw;
	}
}

} // namespace

TableFunction GetReadOmFunction() {
	TableFunction function("read_om", {LogicalType::VARCHAR}, ScanReadOm, BindReadOm, InitReadOm);
	function.named_parameters["dimensions"] = LogicalType::MAP(LogicalType::VARCHAR, LogicalType::LIST(LogicalType::VARCHAR));
	function.named_parameters["grid"] = LogicalType::ANY;
	function.named_parameters["spatial_axes"] = LogicalType::LIST(LogicalType::VARCHAR);
	function.named_parameters["domain"] = LogicalType::VARCHAR;
	function.named_parameters["valid_times"] = LogicalType::LIST(LogicalType::TIMESTAMP);
	function.get_virtual_columns = GetReadOmVirtualColumns;
    function.projection_pushdown = true;
	function.filter_pushdown = false;
	function.filter_prune = false;
	function.pushdown_complex_filter = ObserveComplexFilter;
	function.order_preservation_type = OrderPreservationType::INSERTION_ORDER;
	return function;
}

} // namespace duckomo
} // namespace duckdb
