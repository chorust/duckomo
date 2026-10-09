#include "duckomo/compat/duckdb_api.hpp"

#include "duckdb/common/constants.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/planner/expression/bound_between_expression.hpp"
#include "duckdb/planner/expression/bound_cast_expression.hpp"
#include "duckdb/planner/expression/bound_comparison_expression.hpp"
#include "duckdb/planner/expression/bound_conjunction_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"

#include <utility>

namespace duckdb {
namespace duckomo {

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
