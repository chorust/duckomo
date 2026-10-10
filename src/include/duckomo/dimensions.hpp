#pragma once

#include <string>
#include <vector>

#include "duckdb/common/types/value.hpp"
#include "duckomo/schema.hpp"

namespace duckdb {
namespace duckomo {

using AxisDeclarations = std::vector<std::vector<std::string>>;

// Validates shared VARCHAR[] or per-variable MAP declarations against every
// array. Omission uses ordered coordinates metadata (optional for one array).
AxisDeclarations ValidateAxisDeclarations(const Value *dimensions, const BoundSchema &schema);

} // namespace duckomo
} // namespace duckdb
