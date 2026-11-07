#pragma once

#include <cstdint>
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
class LogicalGet;
class LogicalOperator;
class Binder;
namespace duckomo {

#if DUCKOMO_DUCKDB_API_GENERATION >= 2
using TableFunctionColumnName = Identifier;
using TableFunctionNamedArguments = named_argument_map_t;
inline std::string IdentifierNameString(const TableFunctionColumnName &name) {
	return name.GetIdentifierName();
}
inline std::string TableFunctionColumnNameString(const TableFunctionColumnName &name) {
	return IdentifierNameString(name);
}
#else
using TableFunctionColumnName = std::string;
using TableFunctionNamedArguments = named_parameter_map_t;
inline std::string IdentifierNameString(const TableFunctionColumnName &name) {
	return name;
}
inline const std::string &TableFunctionColumnNameString(const TableFunctionColumnName &name) {
	return name;
}
#endif

using TableFunctionColumnNames = vector<TableFunctionColumnName>;

#if DUCKOMO_DUCKDB_API_GENERATION >= 2
using ReadOmBoundTableFunction = BoundTableFunction;
#else
using ReadOmBoundTableFunction = TableFunction;
#endif
ReadOmBoundTableFunction BindReadOmFunctionCall(const TableFunction &function,
                                                const TableFunctionBindInput &input);
unique_ptr<LogicalOperator> CreateReadOmLogicalGet(idx_t bind_index, ReadOmBoundTableFunction function,
                                                   unique_ptr<FunctionData> bind_data,
                                                   const std::vector<LogicalType> &return_types,
                                                   const std::vector<std::string> &return_names,
                                                   virtual_column_map_t virtual_columns);
idx_t ReadOmLogicalGetTableIndex(const LogicalGet &get);
idx_t ReadOmColumnBindingTableIndex(const ColumnBinding &binding);
unique_ptr<LogicalOperator> CreateReadOmUnionOperator(
    idx_t bind_index, idx_t column_count, std::vector<unique_ptr<LogicalOperator>> children);
unique_ptr<LogicalOperator> CreateReadOmSeriesCardinalityProjection(unique_ptr<LogicalOperator> child,
                                                                   idx_t projection_index,
                                                                   idx_t child_table_index,
                                                                   idx_t input_column_count);
unique_ptr<LogicalOperator> CreateReadOmSeriesOutputProjection(unique_ptr<LogicalOperator> child,
                                                               idx_t output_table_index,
                                                               idx_t child_table_index,
                                                               idx_t output_column_count);
idx_t GenerateReadOmTableIndex(Binder &binder);
unique_ptr<LogicalOperator> CreateReadOmEmptyResult(idx_t bind_index,
                                                    const std::vector<LogicalType> &return_types);
unique_ptr<LogicalOperator> CreateReadOmTimestampWindowFilter(unique_ptr<LogicalOperator> child,
                                                              idx_t table_index, idx_t column_index,
                                                              std::int64_t start_microseconds,
                                                              std::int64_t end_microseconds);

void AddTableFunctionOption(TableFunction &function, std::string name, LogicalType type);
const Value *GetTableFunctionNamedArgument(const TableFunctionBindInput &input, const std::string &name);
void RemoveTableFunctionNamedArgument(TableFunctionNamedArguments &parameters, const std::string &name);
void SetTableFunctionNamedArgument(TableFunctionNamedArguments &parameters, std::string name, Value value);
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
//! Sets the logical cardinality of an output chunk and, on 2.0, resizes its child vectors to match. 1.x vectors
//! have no size of their own, so there it only sets the chunk cardinality.
inline void SetOutputCardinality(DataChunk &chunk, idx_t count) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	chunk.SetChildCardinality(count);
#else
	chunk.SetCardinality(count);
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
