#include "duckomo/compat/duckdb_api.hpp"

#include "duckdb/common/constants.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/planner/operator/logical_empty_result.hpp"
#include "duckdb/planner/operator/logical_filter.hpp"
#include "duckdb/planner/operator/logical_projection.hpp"
#include "duckdb/planner/operator/logical_set_operation.hpp"
#include "duckdb/planner/binder.hpp"
#include "duckdb/planner/expression/bound_between_expression.hpp"
#include "duckdb/planner/expression/bound_cast_expression.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/expression/bound_comparison_expression.hpp"
#include "duckdb/planner/expression/bound_conjunction_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"

#include <utility>

namespace duckdb {
namespace duckomo {

ReadOmBoundTableFunction BindReadOmFunctionCall(const TableFunction &function,
                                                const TableFunctionBindInput &input) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	BoundTableFunction bound_function(function);
	bound_function.SetCallArguments(input.inputs, input.named_parameters);
	return bound_function;
#else
	(void)input;
	return function;
#endif
}

unique_ptr<LogicalOperator> CreateReadOmLogicalGet(idx_t bind_index, ReadOmBoundTableFunction function,
                                                   unique_ptr<FunctionData> bind_data,
                                                   const std::vector<LogicalType> &return_types,
                                                   const std::vector<std::string> &return_names,
                                                   virtual_column_map_t virtual_columns) {
	vector<LogicalType> duckdb_types(return_types.begin(), return_types.end());
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	vector<Identifier> duckdb_names;
	duckdb_names.reserve(return_names.size());
	for (const auto &name : return_names) duckdb_names.emplace_back(name);
	auto get = make_uniq<LogicalGet>(TableIndex(bind_index), std::move(function), std::move(bind_data),
	                                 std::move(duckdb_types), std::move(duckdb_names), std::move(virtual_columns));
	for (idx_t column = 0; column < return_types.size(); column++) get->AddColumnId(column);
	return get;
#else
	vector<string> duckdb_names(return_names.begin(), return_names.end());
	auto get = make_uniq<LogicalGet>(bind_index, std::move(function), std::move(bind_data), std::move(duckdb_types),
	                                 std::move(duckdb_names), std::move(virtual_columns));
	for (idx_t column = 0; column < return_types.size(); column++) get->AddColumnId(column);
	return get;
#endif
}

idx_t ReadOmLogicalGetTableIndex(const LogicalGet &get) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return get.table_index.index;
#else
	return get.table_index;
#endif
}

idx_t ReadOmColumnBindingTableIndex(const ColumnBinding &binding) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return binding.table_index.index;
#else
	return binding.table_index;
#endif
}

unique_ptr<LogicalOperator> CreateReadOmSeriesCardinalityProjection(unique_ptr<LogicalOperator> child,
                                                                   idx_t projection_index,
                                                                   idx_t child_table_index,
                                                                   idx_t input_column_count) {
	if (!child) throw InternalException("read_om_series cardinality projection has no child operator");
	child->ResolveOperatorTypes();
	if (child->types.size() != input_column_count) {
		throw InternalException("read_om_series cardinality projection input has an unexpected column count");
	}

	vector<unique_ptr<Expression>> expressions;
	expressions.reserve(input_column_count + 1);
	expressions.emplace_back(make_uniq<BoundConstantExpression>(Value::BOOLEAN(true)));
	for (idx_t column = 0; column < input_column_count; column++) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
		const ColumnBinding binding{TableIndex(child_table_index), ProjectionIndex(column)};
#else
		const ColumnBinding binding(child_table_index, column);
#endif
		expressions.emplace_back(make_uniq<BoundColumnRefExpression>(child->types[column], binding));
	}
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	auto projection = make_uniq<LogicalProjection>(TableIndex(projection_index), std::move(expressions));
#else
	auto projection = make_uniq<LogicalProjection>(projection_index, std::move(expressions));
#endif
	projection->children.emplace_back(std::move(child));
	return projection;
}

unique_ptr<LogicalOperator> CreateReadOmSeriesOutputProjection(unique_ptr<LogicalOperator> child,
                                                               idx_t output_table_index,
                                                               idx_t child_table_index,
                                                               idx_t output_column_count) {
	if (!child) throw InternalException("read_om_series output projection has no child operator");
	child->ResolveOperatorTypes();
	if (child->types.size() != output_column_count + 1 || child->types.front() != LogicalType::BOOLEAN) {
		throw InternalException("read_om_series internal cardinality column does not match its output schema");
	}

	vector<unique_ptr<Expression>> expressions;
	expressions.reserve(output_column_count);
	for (idx_t column = 0; column < output_column_count; column++) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
		const ColumnBinding binding{TableIndex(child_table_index), ProjectionIndex(column + 1)};
#else
		const ColumnBinding binding(child_table_index, column + 1);
#endif
		expressions.emplace_back(make_uniq<BoundColumnRefExpression>(child->types[column + 1], binding));
	}
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	auto projection = make_uniq<LogicalProjection>(TableIndex(output_table_index), std::move(expressions));
#else
	auto projection = make_uniq<LogicalProjection>(output_table_index, std::move(expressions));
#endif
	projection->children.emplace_back(std::move(child));
	return projection;
}

idx_t GenerateReadOmTableIndex(Binder &binder) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return binder.GenerateTableIndex().index;
#else
	return binder.GenerateTableIndex();
#endif
}

unique_ptr<LogicalOperator> CreateReadOmUnionOperator(
    idx_t bind_index, idx_t column_count, std::vector<unique_ptr<LogicalOperator>> children) {
	vector<unique_ptr<LogicalOperator>> duckdb_children;
	duckdb_children.reserve(children.size());
	for (auto &child : children) duckdb_children.emplace_back(std::move(child));
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return make_uniq<LogicalSetOperation>(TableIndex(bind_index), column_count, std::move(duckdb_children),
	                                      LogicalOperatorType::LOGICAL_UNION, true);
#else
	return make_uniq<LogicalSetOperation>(bind_index, column_count, std::move(duckdb_children),
	                                      LogicalOperatorType::LOGICAL_UNION, true);
#endif
}

unique_ptr<LogicalOperator> CreateReadOmEmptyResult(idx_t bind_index,
                                                    const std::vector<LogicalType> &return_types) {
	vector<LogicalType> duckdb_types(return_types.begin(), return_types.end());
	vector<ColumnBinding> bindings;
	bindings.reserve(return_types.size());
	for (idx_t column = 0; column < return_types.size(); column++) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
		bindings.emplace_back(TableIndex(bind_index), ProjectionIndex(column));
#else
		bindings.emplace_back(bind_index, column);
#endif
	}
	return make_uniq<LogicalEmptyResult>(std::move(duckdb_types), std::move(bindings));
}

unique_ptr<LogicalOperator> CreateReadOmTimestampWindowFilter(unique_ptr<LogicalOperator> child,
                                                              idx_t table_index, idx_t column_index,
                                                              std::int64_t start_microseconds,
                                                              std::int64_t end_microseconds) {
	if (!child) {
		throw InternalException("read_om_series timestamp filter has no child operator");
	}
	// LogicalGet stores returned_types separately until its logical types are resolved.
	child->ResolveOperatorTypes();
	if (column_index >= child->types.size() || child->types[column_index] != LogicalType(LogicalTypeId::TIMESTAMP)) {
		throw InternalException("read_om_series valid_time filter has no TIMESTAMP child binding");
	}
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	const ColumnBinding binding{TableIndex(table_index), ProjectionIndex(column_index)};
#else
	const ColumnBinding binding(table_index, column_index);
#endif
	auto filter = make_uniq<LogicalFilter>();
	filter->children.emplace_back(std::move(child));
	auto column = make_uniq<BoundColumnRefExpression>(LogicalType(LogicalTypeId::TIMESTAMP), binding);
	auto lower = make_uniq<BoundConstantExpression>(Value::TIMESTAMP(timestamp_t(start_microseconds)));
	auto upper_column = make_uniq<BoundColumnRefExpression>(LogicalType(LogicalTypeId::TIMESTAMP), binding);
	auto upper = make_uniq<BoundConstantExpression>(Value::TIMESTAMP(timestamp_t(end_microseconds)));
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	filter->expressions.emplace_back(BoundComparisonExpression::Create(
	    ExpressionType::COMPARE_GREATERTHANOREQUALTO, std::move(column), std::move(lower)));
	filter->expressions.emplace_back(BoundComparisonExpression::Create(
	    ExpressionType::COMPARE_LESSTHANOREQUALTO, std::move(upper_column), std::move(upper)));
#else
	filter->expressions.emplace_back(make_uniq<BoundComparisonExpression>(
	    ExpressionType::COMPARE_GREATERTHANOREQUALTO, std::move(column), std::move(lower)));
	filter->expressions.emplace_back(make_uniq<BoundComparisonExpression>(
	    ExpressionType::COMPARE_LESSTHANOREQUALTO, std::move(upper_column), std::move(upper)));
#endif
	return filter;
}

void AddTableFunctionOption(TableFunction &function, std::string name, LogicalType type) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	auto &signature = function.GetSignature();
	auto add_option = [&](TypedKwargs &options) { options.Add(Identifier(name), std::move(type)); };
	if (signature.GetTypedKwargs()) {
		signature.ExtendTypedKwargs(add_option);
	} else {
		signature.WithTypedKwargs("options", add_option);
	}
#else
	function.named_parameters[std::move(name)] = std::move(type);
#endif
}

const Value *GetTableFunctionNamedArgument(const TableFunctionBindInput &input, const std::string &name) {
	for (const auto &entry : input.named_parameters) {
		if (StringUtil::CIEquals(TableFunctionColumnNameString(entry.first), name)) return &entry.second;
	}
	return nullptr;
}

void RemoveTableFunctionNamedArgument(TableFunctionNamedArguments &parameters, const std::string &name) {
	for (auto entry = parameters.begin(); entry != parameters.end();) {
		if (StringUtil::CIEquals(TableFunctionColumnNameString(entry->first), name)) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
			parameters.erase(entry);
			entry = parameters.begin();
#else
			entry = parameters.erase(entry);
#endif
		} else {
			++entry;
		}
	}
}

void SetTableFunctionNamedArgument(TableFunctionNamedArguments &parameters, std::string name, Value value) {
	RemoveTableFunctionNamedArgument(parameters, name);
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	parameters.insert(Identifier(std::move(name)), std::move(value));
#else
	parameters.emplace(std::move(name), std::move(value));
#endif
}

Value ValueWithLogicalType(Value value, const LogicalType &type) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return value.WithType(type);
#else
	value.Reinterpret(type);
	return value;
#endif
}

ValidityMask &MutableVectorValidity(Vector &vector) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return FlatVector::ValidityMutable(vector);
#else
	return FlatVector::Validity(vector);
#endif
}

ExpressionClass ExpressionClassOf(const Expression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return expression.GetExpressionClass();
#else
	return expression.expression_class;
#endif
}

ExpressionType ExpressionTypeOf(const Expression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return expression.GetExpressionType();
#else
	return expression.type;
#endif
}

const LogicalType &ExpressionReturnType(const Expression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return expression.GetReturnType();
#else
	return expression.return_type;
#endif
}

const Value &BoundConstantValue(const BoundConstantExpression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return expression.GetValue();
#else
	return expression.value;
#endif
}

const vector<unique_ptr<Expression>> &BoundConjunctionChildren(const BoundConjunctionExpression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return expression.GetChildren();
#else
	return expression.children;
#endif
}

bool IsBoundCastExpression(const Expression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return BoundCastExpression::IsCast(expression);
#else
	return ExpressionClassOf(expression) == ExpressionClass::BOUND_CAST;
#endif
}

const Expression &BoundCastChild(const Expression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return BoundCastExpression::Child(expression.Cast<BoundFunctionExpression>());
#else
	return *expression.Cast<BoundCastExpression>().child;
#endif
}

bool IsBoundComparisonExpression(const Expression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return BoundComparisonExpression::IsComparison(expression);
#else
	return ExpressionClassOf(expression) == ExpressionClass::BOUND_COMPARISON;
#endif
}

const Expression &BoundComparisonLeft(const Expression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return BoundComparisonExpression::Left(expression.Cast<BoundFunctionExpression>());
#else
	return *expression.Cast<BoundComparisonExpression>().left;
#endif
}

const Expression &BoundComparisonRight(const Expression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return BoundComparisonExpression::Right(expression.Cast<BoundFunctionExpression>());
#else
	return *expression.Cast<BoundComparisonExpression>().right;
#endif
}

bool IsBoundBetweenExpression(const Expression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	if (ExpressionClassOf(expression) != ExpressionClass::BOUND_FUNCTION ||
	    ExpressionTypeOf(expression) != ExpressionType::COMPARE_BETWEEN) {
		return false;
	}
	return BoundBetweenExpression::HasValidBindData(expression.Cast<BoundFunctionExpression>());
#else
	return ExpressionClassOf(expression) == ExpressionClass::BOUND_BETWEEN &&
	       ExpressionTypeOf(expression) == ExpressionType::COMPARE_BETWEEN;
#endif
}

const Expression &BoundBetweenInput(const Expression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return BoundBetweenExpression::Input(expression.Cast<BoundFunctionExpression>());
#else
	return *expression.Cast<BoundBetweenExpression>().input;
#endif
}

const Expression &BoundBetweenLower(const Expression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return BoundBetweenExpression::LowerBound(expression.Cast<BoundFunctionExpression>());
#else
	return *expression.Cast<BoundBetweenExpression>().lower;
#endif
}

const Expression &BoundBetweenUpper(const Expression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return BoundBetweenExpression::UpperBound(expression.Cast<BoundFunctionExpression>());
#else
	return *expression.Cast<BoundBetweenExpression>().upper;
#endif
}

bool BoundBetweenLowerInclusive(const Expression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return BoundBetweenExpression::LowerInclusive(expression.Cast<BoundFunctionExpression>());
#else
	return expression.Cast<BoundBetweenExpression>().lower_inclusive;
#endif
}

bool BoundBetweenUpperInclusive(const Expression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return BoundBetweenExpression::UpperInclusive(expression.Cast<BoundFunctionExpression>());
#else
	return expression.Cast<BoundBetweenExpression>().upper_inclusive;
#endif
}

ColumnBinding BoundColumnBinding(const BoundColumnRefExpression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return expression.Binding();
#else
	return expression.binding;
#endif
}

idx_t BoundColumnDepth(const BoundColumnRefExpression &expression) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return expression.Depth();
#else
	return expression.depth;
#endif
}

} // namespace duckomo
} // namespace duckdb
