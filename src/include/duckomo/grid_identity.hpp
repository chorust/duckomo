#pragma once

#include <optional>
#include <string>

#include "duckomo/grid_definition.hpp"

namespace duckdb {
namespace duckomo {

struct SpatialLayout;

// Canonical encodings are binary, length framed, and independent of JSON or
// host byte order. Provenance and domain names deliberately stay outside them.
std::string CanonicalGridDefinition(const GridDefinition &definition);
std::string GridId(const GridDefinition &definition);
std::optional<std::string> ParentGridId(const GridDefinition &definition);
std::string LayoutId(const GridDefinition &definition, const SpatialLayout &layout);

} // namespace duckomo
} // namespace duckdb
