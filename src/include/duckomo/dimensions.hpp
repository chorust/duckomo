#pragma once

#include <string>
#include <vector>

#include "duckdb/common/types/value.hpp"
#include "duckomo/schema.hpp"

namespace duckdb {
namespace duckomo {

using AxisDeclarations = std::vector<std::vector<std::string>>;

// Validates the optional dimensions MAP against the complete schema. A single
// variable may omit the map; multiple variables require explicit equal axes.
AxisDeclarations ValidateAxisDeclarations(const Value *dimensions, const BoundSchema &schema);

} // namespace duckomo
} // namespace duckdb
