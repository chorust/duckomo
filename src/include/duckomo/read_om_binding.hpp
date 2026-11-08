#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "duckdb/function/table_function.hpp"
#include "duckdb/planner/logical_operator.hpp"
#include "duckdb/common/table_column.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include "duckomo/schema.hpp"

namespace duckdb {
class ClientContext;

namespace duckomo {

struct ReadOmArrayBindingSignature final {
	std::string canonical_path;
	std::string column_name;
	LogicalType type = LogicalType::FLOAT;
	std::vector<std::uint64_t> shape;
	std::vector<std::string> dimension_names;
};

// Stable logical schema and query-owned identity facts from one ordinary
// read_om bind. Physical compression chunk shape is intentionally omitted.
struct ReadOmBindingSignature final {
	std::vector<ReadOmArrayBindingSignature> arrays;
	std::vector<LogicalType> column_types;
	std::vector<std::string> column_names;
	std::string grid_id;
	std::string layout_id;
	std::string object_id;
	bool include_source = false;
};

// Owns the real read_om bind and its callbacks so a series binder can attach
// that existing leaf directly to a logical plan without reopening the object.
struct ReadOmChildBinding final {
	ReadOmBoundTableFunction function;
	unique_ptr<FunctionData> bind_data;
	ReadOmBindingSignature signature;
	virtual_column_map_t virtual_columns;

	unique_ptr<LogicalOperator> CreateLogicalGet(idx_t bind_index) &&;
};

// Series plans can retain the bound child and reopen its same object identity at
// scan time instead of forcing a fresh candidate bind on statement execution.
ReadOmChildBinding BindReadOmChild(ClientContext &context, TableFunctionBindInput &input,
                                   bool propagate_rebind_requirement = true);
// Parses a series spatial declaration and checks axis identities against its
// explicit dimensions without opening an object. Shape and CRS reconciliation
// remain the responsibility of each ordinary read_om child bind.
void ValidateReadOmSeriesSpatialDeclaration(ClientContext &context, const TableFunctionBindInput &input,
                                            const DeclaredSeriesSchema &declared);
ReadOmBindingSignature ReadOmBindingSignatureFrom(const FunctionData &bind_data);

// Validates one ordinary read_om child against the declaration-only schema,
// the required temporal extent and (when present) the first child signature.
void ValidateSeriesChildSignature(const DeclaredSeriesSchema &declared,
                                  const ReadOmBindingSignature &child,
                                  std::int64_t chunk_time_length,
                                  const ReadOmBindingSignature *reference = nullptr);

} // namespace duckomo
} // namespace duckdb
