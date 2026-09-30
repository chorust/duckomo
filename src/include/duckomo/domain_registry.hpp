#pragma once

#include <string>

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

const VerifiedDomain *FindVerifiedDomain(const std::string &name);

} // namespace duckomo
} // namespace duckdb
