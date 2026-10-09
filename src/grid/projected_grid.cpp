#include "duckomo/projected_grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <type_traits>

#include "duckomo/om_reader.hpp"

namespace duckdb {
namespace duckomo {
namespace {

constexpr std::uint64_t MAX_GRID_POINTS = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
constexpr double PI64 = 3.141592653589793238462643383279502884;
constexpr float PI32 = 3.14159265358979323846F;

void ValidateFinite(const char *name, double value) {
	if (!std::isfinite(value)) {
		throw ReaderError(ReaderErrorCode::InvalidShape, std::string("projected grid ") + name + " must be finite");
	}
}

std::uint64_t CheckedPointCount(std::uint64_t nx, std::uint64_t ny) {
	if (nx == 0 || ny == 0) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "projected grid nx and ny must be positive");
	}
	if (nx > MAX_GRID_POINTS / ny) {
		throw ReaderError(ReaderErrorCode::ShapeOverflow, "projected grid point count exceeds the signed 64-bit limit");
	}
	return nx * ny;
}

void ValidateOrder(GridStorageOrder order) {
	if (order != GridStorageOrder::Separate && order != GridStorageOrder::LongitudeFastest &&
	    order != GridStorageOrder::LatitudeFastest) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "projected grid storage order is invalid");
	}
}

double NormalizeLongitude(double longitude) {
	double normalized = std::fmod(longitude, 360.0);
	if (normalized < -180.0) normalized += 360.0;
	if (normalized >= 180.0) normalized -= 360.0;
	return normalized;
}

float NormalizeLongitude(float longitude) {
	float normalized = std::fmod(longitude, 360.0F);
	if (normalized < -180.0F) normalized += 360.0F;
	if (normalized >= 180.0F) normalized -= 360.0F;
	return normalized;
}

double DegreesToRadians(double degrees) { return degrees * PI64 / 180.0; }
float DegreesToRadians(float degrees) { return degrees * PI32 / 180.0F; }

double GridCoordinateValue(double origin, double step, std::uint64_t index, const char *field) {
	const double displacement = static_cast<double>(index) * step;
	const double value = origin + displacement;
	if (!std::isfinite(displacement) || !std::isfinite(value)) {
		throw ReaderError(ReaderErrorCode::InvalidShape, std::string("projected grid ") + field + " overflows DOUBLE");
	}
	return value;
}

float GridCoordinateValue(float origin, float step, std::uint64_t index, const char *field) {
	const float displacement = static_cast<float>(index) * step;
	const float value = origin + displacement;
	if (!std::isfinite(displacement) || !std::isfinite(value)) {
		throw ReaderError(ReaderErrorCode::InvalidShape, std::string("projected grid ") + field + " overflows FLOAT");
	}
	return value;
}

template <class T> void ValidateProjectedBase(T const &p) {
	CheckedPointCount(p.nx, p.ny);
	ValidateOrder(p.order);
	ValidateFinite("x0", p.x0);
	ValidateFinite("y0", p.y0);
	ValidateFinite("dx", p.dx);
	ValidateFinite("dy", p.dy);
	if (p.dx == 0 || p.dy == 0) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "projected grid dx and dy must be non-zero");
	}
	(void)GridCoordinateValue(p.x0, p.dx, p.nx - 1, "x");
	(void)GridCoordinateValue(p.y0, p.dy, p.ny - 1, "y");
}

template <class T> void ValidateProjected(T const &p) { ValidateProjectedBase(p); }

template <> void ValidateProjected(RotatedLatLonParameters const &p) {
	ValidateProjectedBase(p);
	ValidateFinite("north_pole_latitude", p.north_pole_latitude);
	ValidateFinite("north_pole_longitude", p.north_pole_longitude);
	ValidateFinite("rotation", p.rotation);
	if (p.north_pole_latitude < -90 || p.north_pole_latitude > 90) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "rotated grid north-pole latitude must lie within [-90, 90]");
	}
	const double first = GridCoordinateValue(p.y0, p.dy, 0, "latitude");
	const double last = GridCoordinateValue(p.y0, p.dy, p.ny - 1, "latitude");
	if (std::min(first, last) < -90.0 || std::max(first, last) > 90.0) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "rotated grid native latitudes must lie within [-90, 90]");
	}
}

template <> void ValidateProjected(LambertParameters const &p) {
	ValidateProjectedBase(p);
	ValidateFinite("longitude_of_false_origin", p.longitude_of_false_origin);
	ValidateFinite("latitude_of_false_origin", p.latitude_of_false_origin);
	ValidateFinite("standard_parallel_1", p.standard_parallel_1);
	ValidateFinite("standard_parallel_2", p.standard_parallel_2);
	ValidateFinite("radius_m", p.radius_m);
	ValidateFinite("false_easting_m", p.false_easting_m);
	ValidateFinite("false_northing_m", p.false_northing_m);
	if (p.radius_m <= 0 || p.latitude_of_false_origin <= -90 || p.latitude_of_false_origin >= 90 ||
	    std::abs(p.standard_parallel_1) >= 90 || std::abs(p.standard_parallel_2) >= 90) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "Lambert radius and latitude parameters are outside their valid domain");
	}
	const double phi1 = DegreesToRadians(p.standard_parallel_1);
	const double phi2 = DegreesToRadians(p.standard_parallel_2);
	const double n = p.standard_parallel_1 == p.standard_parallel_2
	                      ? std::sin(phi1)
	                      : std::log(std::cos(phi1) / std::cos(phi2)) /
	                            std::log(std::tan(PI64 / 4 + phi2 / 2) / std::tan(PI64 / 4 + phi1 / 2));
	if (!std::isfinite(n) || n == 0) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "Lambert standard parallels produce a degenerate cone");
	}
}

template <> void ValidateProjected(StereographicParameters const &p) {
	ValidateProjectedBase(p);
	ValidateFinite("latitude_of_origin", p.latitude_of_origin);
	ValidateFinite("longitude_of_origin", p.longitude_of_origin);
	ValidateFinite("radius_m", p.radius_m);
	ValidateFinite("scale_factor", p.scale_factor);
	ValidateFinite("false_easting_m", p.false_easting_m);
	ValidateFinite("false_northing_m", p.false_northing_m);
	if (std::abs(p.latitude_of_origin) > 90 || p.radius_m <= 0 || p.scale_factor <= 0) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "stereographic radius, scale, or latitude is outside its valid domain");
	}
}

NativeXYPosition ResolvePosition(const NativeGridPosition &position, std::uint64_t nx, std::uint64_t ny,
	                            GridStorageOrder order) {
	if (const auto *xy = std::get_if<NativeXYPosition>(&position)) {
		if (xy->x >= nx || xy->y >= ny) {
			throw ReaderError(ReaderErrorCode::InvalidSelection, "native projected x/y position is outside the grid");
		}
		return {xy->x, xy->y};
	}
	const auto point = std::get<NativePointPosition>(position).point;
	if (point >= nx * ny || order == GridStorageOrder::Separate) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "linear position requires a flattened projected grid and a valid point index");
	}
	if (order == GridStorageOrder::LongitudeFastest) return {point % nx, point / nx};
	return {point / ny, point % ny};
}

GridCoordinate RotatedCoordinate(const RotatedLatLonParameters &p, std::uint64_t x, std::uint64_t y,
	                             GridNumericPolicy policy) {
	if (policy == GridNumericPolicy::OpenMeteoF32V1) {
		const float pi = PI32;
		const float theta_degrees = 90.0F + static_cast<float>(p.north_pole_latitude);
		const float theta = DegreesToRadians(theta_degrees);
		const float phi = DegreesToRadians(static_cast<float>(p.north_pole_longitude));
		const float native_x = GridCoordinateValue(static_cast<float>(p.x0), static_cast<float>(p.dx), x, "x");
		const float offset = static_cast<float>(p.rotation) - 180.0F;
		const float source_x = native_x + offset;
		const float native_y = GridCoordinateValue(static_cast<float>(p.y0), static_cast<float>(p.dy), y, "y");
		const float lon = DegreesToRadians(source_x);
		const float lat = DegreesToRadians(native_y);
		const float cos_theta = std::cos(theta);
		const float sin_theta = std::sin(theta);
		const float cos_phi = std::cos(phi);
		const float sin_phi = std::sin(phi);
		const float sin_lat = std::sin(lat);
		const float cos_lat = std::cos(lat);
		const float sin_lon = std::sin(lon);
		const float cos_lon = std::cos(lon);
		const float first = cos_theta * sin_lat;
		const float second = cos_lon * sin_theta * cos_lat;
		const float latitude_radians = -std::asin(std::clamp(first - second, -1.0F, 1.0F));
		const float longitude_numerator = sin_lon;
		const float longitude_denominator = std::tan(lat) * sin_theta + cos_lon * cos_theta;
		const float longitude_radians = -(std::atan2(longitude_numerator, longitude_denominator) - phi);
		const float latitude = latitude_radians * 180.0F / pi;
		const float longitude = NormalizeLongitude(longitude_radians * 180.0F / pi);
		if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90 || latitude > 90) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "rotated grid produced a non-finite geographic coordinate");
		}
		return {latitude, longitude};
	}

	const double native_latitude = GridCoordinateValue(p.y0, p.dy, y, "latitude");
	const double native_longitude = GridCoordinateValue(p.x0, p.dx, x, "longitude") + p.rotation;
	if (native_latitude < -90 || native_latitude > 90) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "rotated grid native latitude is outside [-90, 90]");
	}
	const double pole_lat = DegreesToRadians(p.north_pole_latitude);
	const double pole_lon = DegreesToRadians(p.north_pole_longitude);
	const double lat = DegreesToRadians(native_latitude);
	const double lon = DegreesToRadians(native_longitude);
	const double cp = std::cos(pole_lat);
	const double sp = std::sin(pole_lat);
	const double cl = std::cos(pole_lon);
	const double sl = std::sin(pole_lon);
	const double c = std::cos(lat);
	const double vx = c * std::cos(lon) * sp * cl + c * std::sin(lon) * (-sl) + std::sin(lat) * cp * cl;
	const double vy = c * std::cos(lon) * sp * sl + c * std::sin(lon) * cl + std::sin(lat) * cp * sl;
	const double vz = -c * std::cos(lon) * cp + std::sin(lat) * sp;
	const double latitude = std::asin(std::clamp(vz, -1.0, 1.0)) * 180.0 / PI64;
	const double longitude = std::hypot(vx, vy) <= 1e-15 ? 0.0 : NormalizeLongitude(std::atan2(vy, vx) * 180.0 / PI64);
	if (!std::isfinite(latitude) || !std::isfinite(longitude)) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "rotated grid produced a non-finite geographic coordinate");
	}
	return {latitude, longitude};
}

struct LambertDerived64 final { double n, f, rho0, lambda0; };
LambertDerived64 DeriveLambert(const LambertParameters &p) {
	const double phi1 = DegreesToRadians(p.standard_parallel_1);
	const double phi2 = DegreesToRadians(p.standard_parallel_2);
	const double phi0 = DegreesToRadians(p.latitude_of_false_origin);
	const double lambda0 = DegreesToRadians(p.longitude_of_false_origin);
	const double n = p.standard_parallel_1 == p.standard_parallel_2
	                      ? std::sin(phi1)
	                      : std::log(std::cos(phi1) / std::cos(phi2)) /
	                            std::log(std::tan(PI64 / 4 + phi2 / 2) / std::tan(PI64 / 4 + phi1 / 2));
	const double f = std::cos(phi1) * std::pow(std::tan(PI64 / 4 + phi1 / 2), n) / n;
	const double rho0 = f / std::pow(std::tan(PI64 / 4 + phi0 / 2), n);
	if (!std::isfinite(n) || !std::isfinite(f) || !std::isfinite(rho0) || n == 0) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "Lambert grid parameters do not define a finite inverse transform");
	}
	return {n, f, rho0, lambda0};
}

GridCoordinate LambertCoordinate(const LambertParameters &p, std::uint64_t x, std::uint64_t y,
	                             GridNumericPolicy policy) {
	if (policy == GridNumericPolicy::OpenMeteoF32V1) {
		const float pi = PI32;
		const float phi1_degrees = static_cast<float>(p.standard_parallel_1);
		const float phi2_degrees = static_cast<float>(p.standard_parallel_2);
		const float phi0_degrees = static_cast<float>(p.latitude_of_false_origin);
		const float lambda0_degrees = static_cast<float>(p.longitude_of_false_origin);
		const float phi1 = DegreesToRadians(phi1_degrees);
		const float phi2 = DegreesToRadians(phi2_degrees);
		const float phi0 = DegreesToRadians(phi0_degrees);
		const float lambda0 = DegreesToRadians(lambda0_degrees);
		const float n = phi1_degrees == phi2_degrees
		                    ? std::sin(phi1)
		                    : std::log(std::cos(phi1) / std::cos(phi2)) /
		                          std::log(std::tan(pi / 4.0F + phi2 / 2.0F) / std::tan(pi / 4.0F + phi1 / 2.0F));
		const float f = (std::cos(phi1) * std::pow(std::tan(pi / 4.0F + phi1 / 2.0F), n)) / n;
		const float rho0 = f / std::pow(std::tan(pi / 4.0F + phi0 / 2.0F), n);
		const float radius = static_cast<float>(p.radius_m);
		const float x_native = GridCoordinateValue(static_cast<float>(p.x0), static_cast<float>(p.dx), x, "x");
		const float y_native = GridCoordinateValue(static_cast<float>(p.y0), static_cast<float>(p.dy), y, "y");
		const float x_scaled = (x_native - static_cast<float>(p.false_easting_m)) / radius;
		const float y_scaled = (y_native - static_cast<float>(p.false_northing_m)) / radius;
		const float theta = n >= 0 ? std::atan2(x_scaled, rho0 - y_scaled)
		                           : std::atan2(-1.0F * x_scaled, y_scaled - rho0);
		const float apex_distance = std::hypot(x_scaled, rho0 - y_scaled);
		const float apex_scale = std::max({1.0F, std::abs(x_scaled), std::abs(y_scaled), std::abs(rho0)});
		if (apex_distance <= 8.0F * std::numeric_limits<float>::epsilon() * apex_scale) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "Lambert grid coordinate lies at an undefined projection singularity");
		}
		const float rho = (n > 0 ? 1.0F : -1.0F) * apex_distance;
		const float ratio = f / rho;
		if (!std::isfinite(ratio) || ratio <= 0) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "Lambert grid coordinate lies at an undefined projection singularity");
		}
		const float phi_rad = 2.0F * std::atan(std::pow(ratio, 1.0F / n)) - pi / 2.0F;
		const float lambda_rad = lambda0 + theta / n;
		const float latitude = phi_rad * 180.0F / pi;
		const float longitude = NormalizeLongitude(lambda_rad * 180.0F / pi);
		if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90 || latitude > 90) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "Lambert grid produced a non-finite geographic coordinate");
		}
		return {latitude, longitude};
	}

	const auto derived = DeriveLambert(p);
	const double x_native = GridCoordinateValue(p.x0, p.dx, x, "x") - p.false_easting_m;
	const double y_native = GridCoordinateValue(p.y0, p.dy, y, "y") - p.false_northing_m;
	const double x_scaled = x_native / p.radius_m;
	const double y_scaled = y_native / p.radius_m;
	const double theta = derived.n >= 0 ? std::atan2(x_scaled, derived.rho0 - y_scaled)
	                                    : std::atan2(-x_scaled, y_scaled - derived.rho0);
	const double apex_distance = std::hypot(x_scaled, derived.rho0 - y_scaled);
	const double apex_scale = std::max({1.0, std::abs(x_scaled), std::abs(y_scaled), std::abs(derived.rho0)});
	if (apex_distance <= 8.0 * std::numeric_limits<double>::epsilon() * apex_scale) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "Lambert grid coordinate lies at an undefined projection singularity");
	}
	const double rho = std::copysign(apex_distance, derived.n);
	const double ratio = derived.f / rho;
	if (!std::isfinite(ratio) || ratio <= 0) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "Lambert grid coordinate lies at an undefined projection singularity");
	}
	const double phi = 2.0 * std::atan(std::pow(ratio, 1.0 / derived.n)) - PI64 / 2.0;
	const double lambda = derived.lambda0 + theta / derived.n;
	const double latitude = phi * 180.0 / PI64;
	const double longitude = NormalizeLongitude(lambda * 180.0 / PI64);
	if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90 || latitude > 90) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "Lambert grid produced a non-finite geographic coordinate");
	}
	return {latitude, longitude};
}

GridCoordinate StereographicCoordinate(const StereographicParameters &p, std::uint64_t x, std::uint64_t y,
	                                   GridNumericPolicy policy) {
	if (policy == GridNumericPolicy::OpenMeteoF32V1) {
		const float pi = PI32;
		const float center_lat = static_cast<float>(p.latitude_of_origin);
		const float center_lon = static_cast<float>(p.longitude_of_origin);
		const float radius = static_cast<float>(p.radius_m);
		const float scale = static_cast<float>(p.scale_factor);
		const float lambda0 = DegreesToRadians(center_lon);
		const float phi1 = DegreesToRadians(center_lat);
		const float sin_phi1 = std::sin(phi1);
		const float cos_phi1 = std::cos(phi1);
		const float x_native = GridCoordinateValue(static_cast<float>(p.x0), static_cast<float>(p.dx), x, "x");
		const float y_native = GridCoordinateValue(static_cast<float>(p.y0), static_cast<float>(p.dy), y, "y");
		const float east = x_native - static_cast<float>(p.false_easting_m);
		const float north = y_native - static_cast<float>(p.false_northing_m);
		const float projected_radius = std::sqrt(east * east + north * north);
		float latitude = center_lat;
		float longitude = NormalizeLongitude(center_lon);
		if (projected_radius != 0.0F) {
			const float c = 2.0F * std::atan2(projected_radius, 2.0F * radius * scale);
			const float sin_c = std::sin(c);
			const float cos_c = std::cos(c);
			const float latitude_arg = cos_c * sin_phi1 + (north * sin_c * cos_phi1) / projected_radius;
			const float phi = std::asin(std::clamp(latitude_arg, -1.0F, 1.0F));
			const float lambda = lambda0 + std::atan2(east * sin_c,
			                                            projected_radius * cos_phi1 * cos_c - north * sin_phi1 * sin_c);
			latitude = phi * 180.0F / pi;
			longitude = NormalizeLongitude(lambda * 180.0F / pi);
		}
		if (!std::isfinite(latitude) || !std::isfinite(longitude)) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "stereographic grid produced a non-finite geographic coordinate");
		}
		return {latitude, longitude};
	}

	const double lambda0 = DegreesToRadians(p.longitude_of_origin);
	const double phi1 = DegreesToRadians(p.latitude_of_origin);
	const double east = GridCoordinateValue(p.x0, p.dx, x, "x") - p.false_easting_m;
	const double north = GridCoordinateValue(p.y0, p.dy, y, "y") - p.false_northing_m;
	const double rho = std::hypot(east, north);
	if (rho == 0.0) return {p.latitude_of_origin, NormalizeLongitude(p.longitude_of_origin)};
	const double c = 2.0 * std::atan2(rho, 2.0 * p.radius_m * p.scale_factor);
	const double sin_c = std::sin(c);
	const double cos_c = std::cos(c);
	const double latitude_arg = cos_c * std::sin(phi1) + north * sin_c * std::cos(phi1) / rho;
	const double latitude = std::asin(std::clamp(latitude_arg, -1.0, 1.0)) * 180.0 / PI64;
	const double longitude = NormalizeLongitude((lambda0 + std::atan2(
	                                                  east * sin_c,
	                                                  rho * std::cos(phi1) * cos_c - north * std::sin(phi1) * sin_c)) *
	                                              180.0 / PI64);
	if (!std::isfinite(latitude) || !std::isfinite(longitude)) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "stereographic grid produced a non-finite geographic coordinate");
	}
	return {latitude, longitude};
}

} // namespace

ProjectedGrid::ProjectedGrid(ProjectedParameters parameters) : parameters_(std::move(parameters)) {
	std::visit([&](const auto &p) {
		ValidateProjected(p);
		nx_ = p.nx;
		ny_ = p.ny;
	}, parameters_);
	unit_ = std::holds_alternative<RotatedLatLonParameters>(parameters_) ? NativeCoordinateUnit::Degrees
	                                                                   : NativeCoordinateUnit::Metres;
}

std::uint64_t ProjectedGrid::Nx() const noexcept { return nx_; }
std::uint64_t ProjectedGrid::Ny() const noexcept { return ny_; }
std::uint64_t ProjectedGrid::PointCount() const noexcept { return nx_ * ny_; }
NativeCoordinateUnit ProjectedGrid::CoordinateUnit() const noexcept { return unit_; }
const ProjectedParameters &ProjectedGrid::Parameters() const noexcept { return parameters_; }

GridCoordinate ProjectedGrid::Coordinate(const NativeGridPosition &position, GridNumericPolicy numeric_policy) const {
	const auto order = std::visit([](const auto &p) { return p.order; }, parameters_);
	const auto xy = ResolvePosition(position, nx_, ny_, order);
	return CoordinateAt(xy.x, xy.y, numeric_policy);
}

GridCoordinate ProjectedGrid::CoordinateAt(std::uint64_t x, std::uint64_t y, GridNumericPolicy numeric_policy) const {
	if (x >= nx_ || y >= ny_) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "native projected x/y position is outside the grid");
	}
	return std::visit([&](const auto &p) {
		using T = std::decay_t<decltype(p)>;
		if constexpr (std::is_same_v<T, RotatedLatLonParameters>) {
			return RotatedCoordinate(p, x, y, numeric_policy);
		} else if constexpr (std::is_same_v<T, LambertParameters>) {
			return LambertCoordinate(p, x, y, numeric_policy);
		} else {
			return StereographicCoordinate(p, x, y, numeric_policy);
		}
	}, parameters_);
}

} // namespace duckomo
} // namespace duckdb
