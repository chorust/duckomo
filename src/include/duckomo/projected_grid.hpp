#pragma once

#include <cstdint>
#include <variant>

#include "duckomo/regular_grid.hpp"

namespace duckdb {
namespace duckomo {

enum class GridNumericPolicy : std::uint8_t { Float64V1, OpenMeteoF32V1 };
enum class NativeCoordinateUnit : std::uint8_t { Degrees, Metres };

struct NativeXYPosition final {
	std::uint64_t x = 0;
	std::uint64_t y = 0;
};

struct NativePointPosition final {
	std::uint64_t point = 0;
};

using NativeGridPosition = std::variant<NativeXYPosition, NativePointPosition>;

struct RotatedLatLonParameters final {
	std::uint64_t nx = 0;
	std::uint64_t ny = 0;
	double x0 = 0;
	double y0 = 0;
	double dx = 0;
	double dy = 0;
	double north_pole_latitude = 90;
	double north_pole_longitude = 0;
	double rotation = 0;
	GridStorageOrder order = GridStorageOrder::Separate;
};

struct LambertParameters final {
	std::uint64_t nx = 0;
	std::uint64_t ny = 0;
	double x0 = 0;
	double y0 = 0;
	double dx = 0;
	double dy = 0;
	double longitude_of_false_origin = 0;
	double latitude_of_false_origin = 0;
	double standard_parallel_1 = 0;
	double standard_parallel_2 = 0;
	double radius_m = 0;
	double false_easting_m = 0;
	double false_northing_m = 0;
	GridStorageOrder order = GridStorageOrder::Separate;
};

struct StereographicParameters final {
	std::uint64_t nx = 0;
	std::uint64_t ny = 0;
	double x0 = 0;
	double y0 = 0;
	double dx = 0;
	double dy = 0;
	double latitude_of_origin = 0;
	double longitude_of_origin = 0;
	double radius_m = 0;
	double scale_factor = 1;
	double false_easting_m = 0;
	double false_northing_m = 0;
	GridStorageOrder order = GridStorageOrder::Separate;
};

using ProjectedParameters = std::variant<RotatedLatLonParameters, LambertParameters, StereographicParameters>;

class ProjectedGrid final {
public:
	explicit ProjectedGrid(ProjectedParameters parameters);

	std::uint64_t Nx() const noexcept;
	std::uint64_t Ny() const noexcept;
	std::uint64_t PointCount() const noexcept;
	NativeCoordinateUnit CoordinateUnit() const noexcept;
	const ProjectedParameters &Parameters() const noexcept;
	GridCoordinate Coordinate(const NativeGridPosition &position, GridNumericPolicy numeric_policy) const;
	GridCoordinate CoordinateAt(std::uint64_t x, std::uint64_t y, GridNumericPolicy numeric_policy) const;

private:
	ProjectedParameters parameters_;
	std::uint64_t nx_ = 0;
	std::uint64_t ny_ = 0;
	NativeCoordinateUnit unit_ = NativeCoordinateUnit::Degrees;
};

} // namespace duckomo
} // namespace duckdb
