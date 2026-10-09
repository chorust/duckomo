#pragma once

#include <array>
#include <optional>
#include <string>

#include "duckomo/metadata.hpp"
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

enum class SourceCrsBindingStatus : std::uint8_t { NotSpecified, Compatible, Unrecognized, ConflictsWithGrid };

// Classifies closed source CRS profiles. BBOX remains diagnostic; a recognized
// ECMWF Gaussian O-order remark is retained and checked against the definition.
SourceCrsProfile ClassifySourceCrsProfile(const std::string &wkt);

// Checks a classified source profile against the declared geometry and earth
// model. A recognized but incompatible source profile is distinct from an
// unrecognized WKT profile so callers can report the conflict precisely.
SourceCrsBindingStatus CheckSourceCrsProfile(const SourceCrsProfile &profile, bool reduced_gaussian,
                                             bool wgs84_source_earth,
                                             std::optional<std::uint64_t> declared_gaussian_order);

} // namespace duckomo
} // namespace duckdb
