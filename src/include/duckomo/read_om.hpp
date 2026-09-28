#pragma once

#include "duckdb/function/table_function.hpp"

namespace duckdb {
namespace duckomo {

// Full metadata-driven OM scanner. The dimensions map is validated during
// binding; output columns are the metadata arrays themselves.
TableFunction GetReadOmFunction();

} // namespace duckomo
} // namespace duckdb
