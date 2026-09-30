#pragma once

#include <cstdint>

namespace duckdb {
namespace duckomo {

enum class GridStorageOrder : std::uint8_t { Separate, LongitudeFastest, LatitudeFastest };

struct GridCoordinate final {
	double latitude = 0;
	double longitude = 0;
};

// A checked regular grid. Construction validates extents, finite arithmetic,
// and latitude bounds before the definition is used to scan data.
class RegularGrid final {
public:
	RegularGrid(std::uint64_t nx, std::uint64_t ny, double lat0, double lon0, double dlat, double dlon,
	            GridStorageOrder order, bool allow_out_of_range_latitude = false);

	std::uint64_t Nx() const noexcept;
	std::uint64_t Ny() const noexcept;
	std::uint64_t PointCount() const noexcept;
	GridStorageOrder Order() const noexcept;
	double LatitudeOrigin() const noexcept;
	double LongitudeOrigin() const noexcept;
	double LatitudeStep() const noexcept;
	double LongitudeStep() const noexcept;
	GridCoordinate Coordinate(std::uint64_t y, std::uint64_t x) const;

private:
	std::uint64_t nx_;
	std::uint64_t ny_;
	std::uint64_t point_count_;
	double lat0_;
	double lon0_;
	double dlat_;
	double dlon_;
	GridStorageOrder order_;
	bool allow_out_of_range_latitude_;
};

} // namespace duckomo
} // namespace duckdb
