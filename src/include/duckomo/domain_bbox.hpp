#pragma once

#include <array>
#include <optional>
#include <string>

#include "duckomo/regular_grid.hpp"

namespace duckdb {
namespace duckomo {

enum class WktBboxStatus { Absent, MatchesGrid, ConflictsWithGrid };

// Returns the first WKT BBOX clause, accepting WKT whitespace around its
// brackets and comma-separated coordinates. Malformed clauses throw.
std::optional<std::array<double, 4>> ParseWktBbox(const std::string &wkt);

// Compares an optional WKT BBOX with the registered regular grid. Malformed
// clauses throw ReaderError; absent BBOX metadata is reported separately.
WktBboxStatus ValidateWktBbox(const std::string &wkt, const RegularGrid &grid);

} // namespace duckomo
} // namespace duckdb
