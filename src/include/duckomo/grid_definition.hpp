#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "duckomo/gaussian_grid.hpp"
#include "duckomo/regular_grid.hpp"

namespace duckdb {
namespace duckomo {

enum class GridEarthKind : std::uint8_t { Sphere, Wgs84Source };

struct GridEarth final {
	GridEarthKind kind = GridEarthKind::Sphere;
	double radius_m = 6371229.0;
	double semi_major_axis_m = 6378137.0;
	double inverse_flattening = 298.257223563;
};

using GridGeometry = std::variant<RegularGrid, ProjectedGrid, GaussianGrid>;

struct GridDefinition final {
	std::uint32_t version = 1;
	std::string coordinate_rule_id;
	GridEarth earth;
	GridNumericPolicy numeric_policy = GridNumericPolicy::Float64V1;
	GridGeometry geometry;

	GridDefinition(std::string coordinate_rule_id, GridEarth earth, GridNumericPolicy numeric_policy,
	               GridGeometry geometry);
	std::uint64_t PointCount() const noexcept;
	GridCoordinate Coordinate(const NativeGridPosition &position) const;
};

} // namespace duckomo
} // namespace duckdb
