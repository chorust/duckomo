#pragma once

#include <cstdint>

#include "duckdb/function/table_function.hpp"

namespace duckdb {
class ClientContext;
namespace duckomo {

// Full metadata-driven OM scanner. The dimensions map is validated during
// binding; output columns are the metadata arrays themselves.
TableFunction GetReadOmFunction();
TableFunction GetGridInfoFunction();
TableFunction GetLastScanMetricsFunction();

} // namespace duckomo
} // namespace duckdb
