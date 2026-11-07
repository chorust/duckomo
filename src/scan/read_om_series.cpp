#include "duckomo/read_om.hpp"
#include "duckomo/read_om_binding.hpp"
#include "duckomo/series_plan.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "duckdb/common/exception.hpp"
#include "duckdb/common/exception/binder_exception.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/planner/binder.hpp"
#include "duckdb/planner/logical_operator.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include "duckomo/om_reader.hpp"
#include "duckomo/schema.hpp"

namespace duckdb {
namespace duckomo {
namespace {

struct BoundSeriesCandidate final {
	SeriesChunkCandidate plan;
	ReadOmChildBinding child;
};

bool ReadBooleanOption(const TableFunctionBindInput &input, const std::string &name, bool default_value) {
	const auto *value = GetTableFunctionNamedArgument(input, name);
	if (value == nullptr) return default_value;
	if (value->IsNull() || value->type().id() != LogicalTypeId::BOOLEAN) {
		throw BinderException("read_om_series " + name + " must be a non-NULL BOOLEAN");
	}
	return value->GetValue<bool>();
}

std::optional<std::string> ReadVariableOption(const TableFunctionBindInput &input) {
	const auto *value = GetTableFunctionNamedArgument(input, "variable");
	if (value == nullptr) return std::nullopt;
	if (value->IsNull() || value->type().id() != LogicalTypeId::VARCHAR || StringValue::Get(*value).empty()) {
		throw BinderException("read_om_series variable must be a nonempty VARCHAR");
	}
	return StringValue::Get(*value);
}

void ValidateSeriesSpatialPresence(const TableFunctionBindInput &input) {
	const auto *grid = GetTableFunctionNamedArgument(input, "grid");
	const auto *domain = GetTableFunctionNamedArgument(input, "domain");
	const auto *spatial_axes = GetTableFunctionNamedArgument(input, "spatial_axes");
	const bool has_grid = grid != nullptr;
	const bool has_domain = domain != nullptr;
	if (has_grid == has_domain) {
		throw BinderException("read_om_series requires exactly one explicit grid or domain");
	}
	if ((has_grid && grid->IsNull()) || (has_domain && domain->IsNull())) {
		throw BinderException("read_om_series grid/domain must be non-NULL");
	}
	if (has_grid && (spatial_axes == nullptr || spatial_axes->IsNull())) {
		throw BinderException("read_om_series explicit grid requires spatial_axes");
	}
	if (has_domain && spatial_axes != nullptr && !spatial_axes->IsNull()) {
		throw BinderException("read_om_series domain does not accept spatial_axes");
	}
}

struct ChildInputStorage final {
	vector<Value> inputs;
	TableFunctionNamedArguments parameters;
	unique_ptr<ReadOmBoundTableFunction> function;
};

ChildInputStorage MakeChildInput(const TableFunctionBindInput &series_input, const TableFunction &read_function,
                                 const std::string &uri, std::int64_t origin_microseconds,
                                 std::int64_t step_microseconds, bool root_array) {
	ChildInputStorage result;
	result.inputs.emplace_back(uri);
	result.parameters = series_input.named_parameters;
	for (const auto *name : {"model", "chunk_time_length", "time_step", "valid_times", "start", "end", "strict", "axes"}) {
		RemoveTableFunctionNamedArgument(result.parameters, name);
	}
	SetTableFunctionNamedArgument(result.parameters, "axes", MakeSeriesTimeAxes(origin_microseconds, step_microseconds));
	if (root_array && GetTableFunctionNamedArgument(series_input, "variable") == nullptr) {
		SetTableFunctionNamedArgument(result.parameters, "variable", Value("/"));
	}
	result.function = make_uniq<ReadOmBoundTableFunction>(read_function);
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	result.function->SetCallArguments(result.inputs, result.parameters);
#endif
	return result;
}

unique_ptr<LogicalOperator> BuildBalancedUnion(std::vector<unique_ptr<LogicalOperator>> &leaves,
                                               std::size_t begin, std::size_t end, idx_t bind_index,
                                               idx_t column_count, Binder &binder, bool root) {
	if (end - begin == 1) return std::move(leaves[begin]);
	const auto middle = begin + (end - begin) / 2;
	auto top = BuildBalancedUnion(leaves, begin, middle, bind_index, column_count, binder, false);
	auto bottom = BuildBalancedUnion(leaves, middle, end, bind_index, column_count, binder, false);
	std::vector<unique_ptr<LogicalOperator>> children;
	children.emplace_back(std::move(top));
	children.emplace_back(std::move(bottom));
	const auto table_index = root ? bind_index : GenerateReadOmTableIndex(binder);
	return CreateReadOmUnionOperator(table_index, column_count, std::move(children));
}

unique_ptr<LogicalOperator> BindReadOmSeriesInternal(ClientContext &context, TableFunctionBindInput &input,
                                                     idx_t bind_index, TableFunctionColumnNames &return_names) {
	if (!input.binder) throw InternalException("read_om_series requires the active DuckDB binder");
	if (context.IsInterrupted()) throw InterruptException();

	const auto variable = ReadVariableOption(input);
	const auto include_source = ReadBooleanOption(input, "include_source", false);
	const auto strict = ReadBooleanOption(input, "strict", false);
	ValidateSeriesSpatialPresence(input);

	const auto temporal = ReadSeriesTemporalLayout(input);
	auto plan = BindSeriesPlan(input, temporal);
	const auto *dimensions = GetTableFunctionNamedArgument(input, "dimensions");
	const auto declared = BuildDeclaredSeriesSchema(dimensions, include_source, variable);
	ValidateReadOmSeriesSpatialDeclaration(context, input, declared);
	for (const auto &name : declared.column_names) return_names.emplace_back(name);

	const bool root_array = declared.arrays.size() == 1 && declared.arrays.front().canonical_path == "/";
	TableFunction read_function = GetReadOmFunction();
	std::vector<BoundSeriesCandidate> selected;
	selected.reserve(plan.candidates.size());
	std::optional<ReadOmBindingSignature> reference;
	for (const auto &candidate : plan.candidates) {
		if (context.IsInterrupted()) throw InterruptException();
		auto child_storage = MakeChildInput(input, read_function, candidate.uri, candidate.origin_microseconds,
		                                    temporal.step_microseconds, root_array);
		TableFunctionBindInput child_input(child_storage.inputs, child_storage.parameters, input.input_table_types,
		                                  input.input_table_names, input.info, input.binder, *child_storage.function,
		                                  input.ref);
		std::optional<ReadOmChildBinding> child;
		try {
			// The retained series plan reopens this bind's session at scan time. Do not
			// turn a post-bind disappearance into a fresh initial-open skip by forcing
			// the parent statement to rebind on each execution.
			child.emplace(BindReadOmChild(context, child_input, false));
		} catch (const ReaderError &error) {
			const bool confirmed_initial_absence = error.Category() == ReaderErrorCategory::ObjectNotFound &&
			                                      error.Stage() == ReaderErrorStage::InitialOpen;
			if (!confirmed_initial_absence || strict) throw;
			continue;
		}
		ValidateSeriesChildSignature(declared, child->signature, temporal.chunk_time_length,
		                             reference ? &*reference : nullptr);
		if (!reference) reference = child->signature;
		// Presence and metadata remain bind-time obligations for every candidate,
		// including one whose requested time interval contains no slots. Do not
		// retain a slotless leaf: it contributes no rows and must decode no values.
		if (candidate.slots.empty) continue;
		selected.emplace_back(BoundSeriesCandidate{candidate, std::move(*child)});
	}
	if (selected.empty()) return CreateReadOmEmptyResult(bind_index, declared.column_types);

	const auto time_column = std::find(declared.column_names.begin(), declared.column_names.end(), "valid_time");
	if (time_column == declared.column_names.end()) {
		throw InternalException("read_om_series declared output has no valid_time column");
	}
	const auto time_column_index = static_cast<idx_t>(time_column - declared.column_names.begin());
	std::vector<unique_ptr<LogicalOperator>> leaves;
	std::vector<idx_t> leaf_projection_indices;
	leaves.reserve(selected.size());
	leaf_projection_indices.reserve(selected.size());
	for (std::size_t index = 0; index < selected.size(); index++) {
		auto &candidate = selected[index];
		const auto table_index = GenerateReadOmTableIndex(*input.binder);
		auto leaf = std::move(candidate.child).CreateLogicalGet(table_index);
		leaf = CreateReadOmTimestampWindowFilter(std::move(leaf), table_index, time_column_index,
		                                         plan.window.start_microseconds, plan.window.end_microseconds);
		const auto projection_index = GenerateReadOmTableIndex(*input.binder);
		leaf = CreateReadOmSeriesCardinalityProjection(std::move(leaf), projection_index, table_index,
		                                               declared.column_types.size());
		leaf_projection_indices.emplace_back(projection_index);
		leaves.emplace_back(std::move(leaf));
	}

	idx_t internal_table_index;
	unique_ptr<LogicalOperator> internal;
	if (leaves.size() == 1) {
		internal_table_index = leaf_projection_indices.front();
		internal = std::move(leaves.front());
	} else {
		internal_table_index = GenerateReadOmTableIndex(*input.binder);
		internal = BuildBalancedUnion(leaves, 0, leaves.size(), internal_table_index,
		                               declared.column_types.size() + 1, *input.binder, true);
	}
	return CreateReadOmSeriesOutputProjection(std::move(internal), bind_index, internal_table_index,
	                                          declared.column_types.size());
}

#if DUCKOMO_DUCKDB_API_GENERATION >= 2
unique_ptr<LogicalOperator> BindReadOmSeries(ClientContext &context, TableFunctionBindInput &input,
                                             TableIndex bind_index, vector<Identifier> &return_names) {
	return BindReadOmSeriesInternal(context, input, bind_index.index,
	                                reinterpret_cast<TableFunctionColumnNames &>(return_names));
}
#else
unique_ptr<LogicalOperator> BindReadOmSeries(ClientContext &context, TableFunctionBindInput &input,
                                             idx_t bind_index, TableFunctionColumnNames &return_names) {
	return BindReadOmSeriesInternal(context, input, bind_index, return_names);
}
#endif

} // namespace

TableFunction GetReadOmSeriesFunction() {
	TableFunction function("read_om_series", {LogicalType::VARCHAR}, nullptr);
	AddTableFunctionOption(function, "model", LogicalType::VARCHAR);
	AddTableFunctionOption(function, "chunk_time_length", LogicalType::BIGINT);
	AddTableFunctionOption(function, "time_step", LogicalType::INTERVAL);
	AddTableFunctionOption(function, "valid_times", LogicalType::ANY);
	AddTableFunctionOption(function, "start", LogicalType::ANY);
	AddTableFunctionOption(function, "end", LogicalType::ANY);
	AddTableFunctionOption(function, "dimensions", LogicalType::ANY);
	AddTableFunctionOption(function, "grid", LogicalType::ANY);
	AddTableFunctionOption(function, "spatial_axes", LogicalType::LIST(LogicalType::VARCHAR));
	AddTableFunctionOption(function, "domain", LogicalType::VARCHAR);
	AddTableFunctionOption(function, "strict", LogicalType::BOOLEAN);
	AddTableFunctionOption(function, "include_source", LogicalType::BOOLEAN);
	AddTableFunctionOption(function, "variable", LogicalType::VARCHAR);
	AddTableFunctionOption(function, "axes", LogicalType::ANY);
	function.bind_operator = BindReadOmSeries;
	return function;
}

} // namespace duckomo
} // namespace duckdb
