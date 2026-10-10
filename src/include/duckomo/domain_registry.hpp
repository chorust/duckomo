#pragma once

#include <string>
#include <vector>

#include "duckomo/grid_definition.hpp"
#include "duckomo/regular_grid.hpp"

namespace duckdb {
namespace duckomo {

// Source-derived regular grids keyed by Open-Meteo's domain prefix under
// data/, data_run/, or data_spatial/. Binding validates each file's shape
// and ordered axes; files without coordinates need explicit dimensions.
struct VerifiedDomain final {
	std::string name;
	RegularGrid grid;
	std::string upstream_commit;
	std::string sample_sha256;
	std::string source_path;
};

// Closed, source-derived version-one definitions generated from the pinned
// manifest. The expected profile is retained separately from the geometry;
// it is not evidence that a producer object has been inspected.
struct RegisteredGridDefinition final {
	std::string name;
	std::string kind;
	GridDefinition definition;
	std::string upstream_commit;
	std::string source_path;
	std::string grid_id;
	std::string parent_grid_id;
	std::string expected_layout;
	std::vector<std::string> expected_axis_order;
	std::string object_profile_status;
	std::string evidence_level;
	std::string evidence_sample_id;
	std::string evidence_source_uri;
	std::string evidence_build_pair;
	std::string evidence_claims;
	std::string parent_definition;
	bool domain_bindable = true;
	std::string flattened_axis_alias;
};

const VerifiedDomain *FindVerifiedDomain(const std::string &name);
const RegisteredGridDefinition *FindRegisteredGridDefinition(const std::string &name);

} // namespace duckomo
} // namespace duckdb
