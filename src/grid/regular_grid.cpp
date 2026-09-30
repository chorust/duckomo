#include "duckomo/regular_grid.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "duckomo/om_reader.hpp"

namespace duckdb {
namespace duckomo {
namespace {

constexpr std::uint64_t MAX_GRID_POINTS = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

void ValidateFinite(const char *name, double value) {
	if (!std::isfinite(value)) {
		throw ReaderError(ReaderErrorCode::InvalidShape, std::string("regular grid ") + name + " must be finite");
	}
}

double RawCoordinate(double origin, double step, std::uint64_t index, const char *name) {
	// Keep the multiply and add explicit. This is also the formula used by the
	// fixed upstream reference for this feature.
	const double displacement = static_cast<double>(index) * step;
	const double result = origin + displacement;
	if (!std::isfinite(displacement) || !std::isfinite(result)) {
		throw ReaderError(ReaderErrorCode::InvalidShape,
		                  std::string("regular grid ") + name + " coordinate overflows DOUBLE");
	}
	return result;
}

double NormalizeLongitude(double longitude) {
	// Reduce first so adding a normalization offset cannot lose precision for
	// large but finite unwrapped longitudes.
	double normalized = std::fmod(longitude, 360.0);
	if (normalized < -180.0) {
		normalized += 360.0;
	} else if (normalized >= 180.0) {
		normalized -= 360.0;
	}
	return normalized;
}

} // namespace

RegularGrid::RegularGrid(std::uint64_t nx, std::uint64_t ny, double lat0, double lon0, double dlat, double dlon,
                         GridStorageOrder order, bool allow_out_of_range_latitude)
    : nx_(nx), ny_(ny), point_count_(0), lat0_(lat0), lon0_(lon0), dlat_(dlat), dlon_(dlon), order_(order),
      allow_out_of_range_latitude_(allow_out_of_range_latitude) {
	if (nx_ == 0 || ny_ == 0) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "regular grid nx and ny must be positive");
	}
	if (nx_ > MAX_GRID_POINTS / ny_) {
		throw ReaderError(ReaderErrorCode::ShapeOverflow, "regular grid point count exceeds the signed 64-bit limit");
	}
	point_count_ = nx_ * ny_;
	ValidateFinite("lat0", lat0_);
	ValidateFinite("lon0", lon0_);
	ValidateFinite("dlat", dlat_);
	ValidateFinite("dlon", dlon_);
	if (dlat_ == 0.0 || dlon_ == 0.0) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "regular grid dlat and dlon must be non-zero");
	}
	if (order_ != GridStorageOrder::Separate && order_ != GridStorageOrder::LongitudeFastest &&
	    order_ != GridStorageOrder::LatitudeFastest) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "regular grid storage order is invalid");
	}

	const auto last_y = ny_ - 1;
	const auto first_lat = RawCoordinate(lat0_, dlat_, 0, "latitude");
	const auto last_lat = RawCoordinate(lat0_, dlat_, last_y, "latitude");
	const auto lat_min = std::min(first_lat, last_lat);
	const auto lat_max = std::max(first_lat, last_lat);
	if (!allow_out_of_range_latitude_ && (lat_min < -90.0 || lat_max > 90.0)) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "regular grid latitude values must lie within [-90, 90]");
	}
	(void)RawCoordinate(lon0_, dlon_, nx_ - 1, "longitude");
}

std::uint64_t RegularGrid::Nx() const noexcept {
	return nx_;
}

std::uint64_t RegularGrid::Ny() const noexcept {
	return ny_;
}

std::uint64_t RegularGrid::PointCount() const noexcept {
	return point_count_;
}

GridStorageOrder RegularGrid::Order() const noexcept {
	return order_;
}

double RegularGrid::LatitudeOrigin() const noexcept {
	return lat0_;
}

double RegularGrid::LongitudeOrigin() const noexcept {
	return lon0_;
}

double RegularGrid::LatitudeStep() const noexcept {
	return dlat_;
}

double RegularGrid::LongitudeStep() const noexcept {
	return dlon_;
}

GridCoordinate RegularGrid::Coordinate(std::uint64_t y, std::uint64_t x) const {
	if (y >= ny_ || x >= nx_) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "regular grid point index is outside the grid");
	}
	const auto latitude = RawCoordinate(lat0_, dlat_, y, "latitude");
	if (!allow_out_of_range_latitude_ && (latitude < -90.0 || latitude > 90.0)) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "regular grid latitude values must lie within [-90, 90]");
	}
	const auto longitude = NormalizeLongitude(RawCoordinate(lon0_, dlon_, x, "longitude"));
	if (!std::isfinite(longitude)) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "regular grid normalized longitude is not finite");
	}
	return {latitude, longitude};
}

} // namespace duckomo
} // namespace duckdb
