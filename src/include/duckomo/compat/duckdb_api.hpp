#pragma once

#include <string>
#include <vector>

#include "duckdb/function/table_function.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#if defined(__has_include) && __has_include("duckdb/common/vector/flat_vector.hpp")
#include "duckdb/common/vector/flat_vector.hpp"
#else
#include "duckdb/common/types/vector.hpp"
#endif

#if defined(__has_include)
#if __has_include("duckdb/main/capi_v2/extension_api_v2.hpp")
#include "duckdb/common/identifier.hpp"
#define DUCKOMO_DUCKDB_API_GENERATION 2
#else
#define DUCKOMO_DUCKDB_API_GENERATION 1
#endif
#else
#define DUCKOMO_DUCKDB_API_GENERATION 1
#endif

namespace duckdb {
class BoundConstantExpression;
class BoundConjunctionExpression;
class BoundCastExpression;
namespace duckomo {

#if DUCKOMO_DUCKDB_API_GENERATION >= 2
using TableFunctionColumnName = Identifier;
inline std::string IdentifierNameString(const TableFunctionColumnName &name) {
	return name.GetIdentifierName();
}
inline std::string TableFunctionColumnNameString(const TableFunctionColumnName &name) {
	return IdentifierNameString(name);
}
#else
using TableFunctionColumnName = std::string;
inline std::string IdentifierNameString(const TableFunctionColumnName &name) {
	return name;
}
inline const std::string &TableFunctionColumnNameString(const TableFunctionColumnName &name) {
	return name;
}
#endif

using TableFunctionColumnNames = vector<TableFunctionColumnName>;

void AddTableFunctionOption(TableFunction &function, std::string name, LogicalType type);
const Value *GetTableFunctionNamedArgument(const TableFunctionBindInput &input, const std::string &name);
Value ValueWithLogicalType(Value value, const LogicalType &type);
ValidityMask &MutableVectorValidity(Vector &vector);
template <class T>
T *MutableVectorData(Vector &vector) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return FlatVector::GetDataMutable<T>(vector);
#else
	return FlatVector::GetData<T>(vector);
#endif
}
ExpressionClass ExpressionClassOf(const Expression &expression);
ExpressionType ExpressionTypeOf(const Expression &expression);
const LogicalType &ExpressionReturnType(const Expression &expression);
const Value &BoundConstantValue(const BoundConstantExpression &expression);
const vector<unique_ptr<Expression>> &BoundConjunctionChildren(const BoundConjunctionExpression &expression);
bool IsBoundCastExpression(const Expression &expression);
const Expression &BoundCastChild(const Expression &expression);
bool IsBoundComparisonExpression(const Expression &expression);
const Expression &BoundComparisonLeft(const Expression &expression);
const Expression &BoundComparisonRight(const Expression &expression);
bool IsBoundBetweenExpression(const Expression &expression);
const Expression &BoundBetweenInput(const Expression &expression);
const Expression &BoundBetweenLower(const Expression &expression);
const Expression &BoundBetweenUpper(const Expression &expression);
bool BoundBetweenLowerInclusive(const Expression &expression);
bool BoundBetweenUpperInclusive(const Expression &expression);
ColumnBinding BoundColumnBinding(const BoundColumnRefExpression &expression);
idx_t BoundColumnDepth(const BoundColumnRefExpression &expression);


} // namespace duckomo
} // namespace duckdb
