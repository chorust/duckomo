#include "duckomo/grid_definition.hpp"

#include <cmath>
#include <type_traits>

#include "duckomo/om_reader.hpp"

namespace duckdb {
namespace duckomo {

GridDefinition::GridDefinition(std::string coordinate_rule_id_p, GridEarth earth_p, GridNumericPolicy numeric_policy_p,
	                           GridGeometry geometry_p)
	: coordinate_rule_id(std::move(coordinate_rule_id_p)), earth(earth_p), numeric_policy(numeric_policy_p),
	  geometry(std::move(geometry_p)) {
	if (coordinate_rule_id.empty()) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "grid coordinate_rule_id must not be empty");
	}
	if (numeric_policy != GridNumericPolicy::Float64V1 && numeric_policy != GridNumericPolicy::OpenMeteoF32V1) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "grid numeric policy is not supported");
	}
	if (earth.kind == GridEarthKind::Sphere) {
		if (!std::isfinite(earth.radius_m) || earth.radius_m <= 0) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "spherical grid earth radius must be finite and positive");
		}
	} else if (earth.kind == GridEarthKind::Wgs84Source) {
		if (!std::isfinite(earth.semi_major_axis_m) || !std::isfinite(earth.inverse_flattening) ||
		    earth.semi_major_axis_m != 6378137.0 || earth.inverse_flattening != 298.257223563) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "WGS84 source grid earth parameters are invalid");
		}
	} else {
		throw ReaderError(ReaderErrorCode::InvalidShape, "WGS84 source grid earth parameters are invalid");
	}
	if (std::holds_alternative<ProjectedGrid>(geometry) && earth.kind != GridEarthKind::Sphere) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "projected grids in this version require a spherical earth model");
	}
}

std::uint64_t GridDefinition::PointCount() const noexcept {
	return std::visit([](const auto &grid) { return grid.PointCount(); }, geometry);
}

GridCoordinate GridDefinition::Coordinate(const NativeGridPosition &position) const {
	return std::visit([&](const auto &grid) -> GridCoordinate {
		using T = std::decay_t<decltype(grid)>;
		if constexpr (std::is_same_v<T, ProjectedGrid>) {
			return grid.Coordinate(position, numeric_policy);
		} else if constexpr (std::is_same_v<T, GaussianGrid>) {
			const auto *point = std::get_if<NativePointPosition>(&position);
			if (point == nullptr) {
				throw ReaderError(ReaderErrorCode::InvalidSelection, "Gaussian grid requires a linear point position");
			}
			return grid.Coordinate(point->point, numeric_policy);
		} else {
			if (const auto *xy = std::get_if<NativeXYPosition>(&position)) {
				return grid.Coordinate(xy->y, xy->x);
			}
			const auto point = std::get<NativePointPosition>(position).point;
			if (point >= grid.PointCount() || grid.Order() == GridStorageOrder::Separate) {
				throw ReaderError(ReaderErrorCode::InvalidSelection, "regular grid linear position requires a flattened storage order");
			}
			const auto x = grid.Order() == GridStorageOrder::LongitudeFastest ? point % grid.Nx() : point / grid.Ny();
			const auto y = grid.Order() == GridStorageOrder::LongitudeFastest ? point / grid.Nx() : point % grid.Ny();
			return grid.Coordinate(y, x);
		}
	}, geometry);
}

} // namespace duckomo
} // namespace duckdb
