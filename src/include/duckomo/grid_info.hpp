#pragma once

#include <optional>
#include <string>

#include "duckomo/grid_definition.hpp"
#include "duckomo/schema.hpp"
#include "duckomo/spatial_layout.hpp"

namespace duckdb {
namespace duckomo {

struct GridInfoDocument final {
	std::string grid_id;
	std::optional<std::string> parent_grid_id;
	std::string grid_type;
	std::string definition_json;
	std::string layout_json;
	std::string crs_json;
	std::string capabilities_json;
	std::string provenance_json;
	std::string object_id;
	std::optional<std::string> object_version;
	std::string version_strength;
	bool content_verified = false;
};

GridInfoDocument DescribeGrid(const GridDefinition &definition, const SpatialLayout &layout,
	                          const BoundSchema &schema, const std::string &source,
	                          const std::string &object_id,
	                          const std::optional<std::string> &object_version,
	                          const std::string &version_strength, bool content_verified,
	                          const std::string &evidence_level, const std::string &evidence_sample_id,
	                          const std::string &evidence_source, const std::string &evidence_build_pair,
	                          const std::string &evidence_claims);

} // namespace duckomo
} // namespace duckdb
