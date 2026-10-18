#include "duckomo/nearest_sampling.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace duckdb {
namespace duckomo {
namespace {
constexpr double PI_F64 = 3.141592653589793238462643383279502884;
constexpr double DEG_TO_RAD = PI_F64 / 180.0;
constexpr double FULL_CIRCLE = 360.0;
constexpr std::uint64_t CANCELLATION_INTERVAL = 256;
constexpr std::uint64_t MAX_EXACT_BINARY64_INTEGER = std::uint64_t(1) << 53;

[[noreturn]] void CapabilityError(const std::string &message) {
	throw ReaderError(ReaderErrorCode::InvalidShape, "[sampling_capability] " + message);
}

void RequireFinite(const char *name, double value) {
	if (!std::isfinite(value)) CapabilityError(std::string(name) + " must be finite");
}

double RawCoordinate(double origin, double step, std::uint64_t index) {
	const double displacement = static_cast<double>(index) * step;
	const double coordinate = origin + displacement;
	if (!std::isfinite(displacement) || !std::isfinite(coordinate))
		CapabilityError("regular axis coordinate is non-finite");
	return coordinate;
}

double NormalizeLongitude(double longitude) {
	double normalized = std::fmod(longitude, FULL_CIRCLE);
	if (normalized < -180.0) normalized += FULL_CIRCLE;
	else if (normalized >= 180.0) normalized -= FULL_CIRCLE;
	return normalized;
}

bool SameNormalizedLongitude(double left, double right) {
	return NormalizeLongitude(left) == NormalizeLongitude(right);
}

std::uint64_t PointIndex(const RegularGrid &grid, std::uint64_t x, std::uint64_t y) {
	if (grid.Order() == GridStorageOrder::LatitudeFastest) return x * grid.Ny() + y;
	return y * grid.Nx() + x;
}

} // namespace

NearestGridCapability QualifyNearestGrid(const GridDefinition &definition,
                                          const NearestCancellationCheck &cancelled) {
	if (definition.version != 1) CapabilityError("unsupported grid definition version");
	if (definition.earth.kind != GridEarthKind::Sphere) CapabilityError("nearest requires a spherical earth definition");
	RequireFinite("earth radius", definition.earth.radius_m);
	if (definition.earth.radius_m <= 0.0) CapabilityError("earth radius must be positive");
	const auto *grid = std::get_if<RegularGrid>(&definition.geometry);
	if (grid == nullptr) CapabilityError("nearest supports regular longitude/latitude grids only");
	if (grid->AllowsOutOfRangeLatitude()) CapabilityError("out-of-range latitude grids are not supported");
	if (grid->Nx() == 0 || grid->Ny() == 0) CapabilityError("grid dimensions must be positive");
	if (grid->Nx() - 1 > MAX_EXACT_BINARY64_INTEGER || grid->Ny() - 1 > MAX_EXACT_BINARY64_INTEGER)
		CapabilityError("grid axis index exceeds the exact binary64 integer range");

	NearestGridCapability capability;
	capability.earth_radius_m = definition.earth.radius_m;
	capability.unwrapped_longitude_first = RawCoordinate(grid->LongitudeOrigin(), grid->LongitudeStep(), 0);
	capability.unwrapped_longitude_last = RawCoordinate(grid->LongitudeOrigin(), grid->LongitudeStep(), grid->Nx() - 1);
	capability.latitude_first = RawCoordinate(grid->LatitudeOrigin(), grid->LatitudeStep(), 0);
	capability.latitude_last = RawCoordinate(grid->LatitudeOrigin(), grid->LatitudeStep(), grid->Ny() - 1);
	if (capability.latitude_first < -90.0 || capability.latitude_first > 90.0 ||
	    capability.latitude_last < -90.0 || capability.latitude_last > 90.0)
		CapabilityError("source latitude endpoints must lie in [-90,90]");
	if (grid->Ny() > 1 &&
	    (grid->LatitudeStep() > 0 ? capability.latitude_last <= capability.latitude_first
	                               : capability.latitude_last >= capability.latitude_first))
		CapabilityError("latitude axis must be strictly monotonic");
	if (grid->Nx() > 1 &&
	    (grid->LongitudeStep() > 0 ? capability.unwrapped_longitude_last <= capability.unwrapped_longitude_first
	                                : capability.unwrapped_longitude_last >= capability.unwrapped_longitude_first))
		CapabilityError("unwrapped longitude axis must be strictly monotonic");

	// The source axes are affine in binary64, so strict endpoint monotonicity
	// plus one interior pass detects rounding collapse without a coordinate LUT.
	double prior = capability.latitude_first;
	for (std::uint64_t y = 1; y < grid->Ny(); ++y) {
		if (((y - 1) % CANCELLATION_INTERVAL) == 0 && cancelled && cancelled())
			throw ReaderError(ReaderErrorCode::Cancelled, "[sampling_capability] grid capability check was cancelled");
		const auto current = RawCoordinate(grid->LatitudeOrigin(), grid->LatitudeStep(), y);
		if ((grid->LatitudeStep() > 0 && current <= prior) || (grid->LatitudeStep() < 0 && current >= prior))
			CapabilityError("latitude coordinates collapse in binary64");
		prior = current;
	}
	prior = capability.unwrapped_longitude_first;
	for (std::uint64_t x = 1; x < grid->Nx(); ++x) {
		if (((x - 1) % CANCELLATION_INTERVAL) == 0 && cancelled && cancelled())
			throw ReaderError(ReaderErrorCode::Cancelled, "[sampling_capability] grid capability check was cancelled");
		const auto current = RawCoordinate(grid->LongitudeOrigin(), grid->LongitudeStep(), x);
		if ((grid->LongitudeStep() > 0 && current <= prior) || (grid->LongitudeStep() < 0 && current >= prior))
			CapabilityError("unwrapped longitude coordinates collapse in binary64");
		if (SameNormalizedLongitude(prior, current)) CapabilityError("normalized source longitudes are not unique");
		prior = current;
	}
	const double absolute_step = std::abs(grid->LongitudeStep());
	// nx steps determine exact periodic closure; only the first-to-last coordinate
	// span determines whether a regional axis wraps more than once.
	const double longitude_period = grid->Nx() > 1 ? static_cast<double>(grid->Nx()) * absolute_step : 0.0;
	if (!std::isfinite(longitude_period)) CapabilityError("longitude period calculation is non-finite");
	const double endpoint_span = std::abs(capability.unwrapped_longitude_last - capability.unwrapped_longitude_first);
	const bool full_period = grid->Nx() >= 2 && longitude_period == FULL_CIRCLE && endpoint_span < FULL_CIRCLE;
	if (endpoint_span >= FULL_CIRCLE) CapabilityError("longitude axis wraps more than once");
	capability.periodic_longitude = full_period;
	if (capability.periodic_longitude) {
		capability.coverage = std::min(capability.latitude_first, capability.latitude_last) == -90.0 &&
		                              std::max(capability.latitude_first, capability.latitude_last) == 90.0
		                          ? NearestCoverageKind::GlobalSphere
		                          : NearestCoverageKind::LatitudeBand;
	} else {
		capability.coverage = NearestCoverageKind::Regional;
	}
	return capability;
}

bool NearestTargetInsideDomain(const RegularGrid &grid, const NearestGridCapability &capability,
                               double target_longitude, double target_latitude, double *lifted_longitude) {
	if (!std::isfinite(target_longitude) || !std::isfinite(target_latitude) || target_latitude <= -90.0 || target_latitude >= 90.0)
		return false;
	const auto lat_min = std::min(capability.latitude_first, capability.latitude_last);
	const auto lat_max = std::max(capability.latitude_first, capability.latitude_last);
	if (target_latitude < lat_min || target_latitude > lat_max) return false;
	if (capability.periodic_longitude) {
		const auto normalized = NormalizeLongitude(target_longitude);
		if (lifted_longitude != nullptr) *lifted_longitude = normalized;
		return true;
	}
	if (grid.Nx() == 1) {
		const auto normalized = NormalizeLongitude(target_longitude);
		if (normalized != NormalizeLongitude(capability.unwrapped_longitude_first)) return false;
		if (lifted_longitude != nullptr) *lifted_longitude = normalized;
		return true;
	}
	const auto longitude_span = std::abs(static_cast<long double>(capability.unwrapped_longitude_last) -
	                                     static_cast<long double>(capability.unwrapped_longitude_first));
	const auto normalized_start = static_cast<long double>(NormalizeLongitude(capability.unwrapped_longitude_first));
	const auto normalized_target = NormalizeLongitude(target_longitude);
	auto directed_offset = grid.LongitudeStep() > 0.0
	                           ? static_cast<long double>(normalized_target) - normalized_start
	                           : normalized_start - static_cast<long double>(normalized_target);
	directed_offset = std::fmod(directed_offset, static_cast<long double>(FULL_CIRCLE));
	if (directed_offset < 0.0L) directed_offset += static_cast<long double>(FULL_CIRCLE);
	if (!std::isfinite(directed_offset) || directed_offset > longitude_span) return false;
	// A normalized target is distance-equivalent to its unique lift. Keeping the
	// reduced value avoids reconstructing a small offset atop a huge longitude.
	if (lifted_longitude != nullptr) *lifted_longitude = normalized_target;
	return true;
}

double SphereHaversineF64V1(double target_longitude, double target_latitude,
                            double source_longitude, double source_latitude, double radius_m) {
	if (!std::isfinite(target_longitude) || !std::isfinite(target_latitude) ||
	    !std::isfinite(source_longitude) || !std::isfinite(source_latitude) ||
	    !std::isfinite(radius_m) || radius_m <= 0.0)
		throw ReaderError(ReaderErrorCode::InvalidShape, "[sampling_capability] distance inputs must be finite and radius positive");
	if (target_latitude == source_latitude && SameNormalizedLongitude(target_longitude, source_longitude)) return 0.0;
	double central_angle;
	if (source_latitude == 90.0) {
		central_angle = (90.0 - target_latitude) * DEG_TO_RAD;
	} else if (source_latitude == -90.0) {
		central_angle = (90.0 + target_latitude) * DEG_TO_RAD;
	} else {
		const double phi_t = target_latitude * DEG_TO_RAD;
		const double phi_s = source_latitude * DEG_TO_RAD;
		const double delta_phi = phi_s - phi_t;
		const double delta_lon_degrees = std::remainder(source_longitude - target_longitude, FULL_CIRCLE);
		const double delta_lambda = delta_lon_degrees * DEG_TO_RAD;
		const double s1 = std::sin(delta_phi / 2.0);
		const double s2 = std::sin(delta_lambda / 2.0);
		double a = (s1 * s1) + ((std::cos(phi_t) * std::cos(phi_s)) * (s2 * s2));
		a = std::min(1.0, std::max(0.0, a));
		central_angle = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
	}
	const auto distance = radius_m * central_angle;
	if (!std::isfinite(distance)) throw ReaderError(ReaderErrorCode::InvalidShape, "[region_sampling] spherical distance is non-finite");
	return distance;
}

NearestLocation LocateNearestRegularGrid(const RegularGrid &grid, const NearestGridCapability &capability,
                                         double target_longitude, double target_latitude,
                                         const NearestCancellationCheck &cancelled) {
	NearestLocation result;
	result.target_longitude = target_longitude;
	result.target_latitude = target_latitude;
	double lifted_longitude = 0;
	if (!NearestTargetInsideDomain(grid, capability, target_longitude, target_latitude, &lifted_longitude)) return result;
	result.inside_domain = true;
	for (std::uint64_t y = 0; y < grid.Ny(); ++y) {
		const auto source_latitude = grid.Coordinate(y, 0).latitude;
		for (std::uint64_t x = 0; x < grid.Nx(); ++x) {
			if ((result.coordinate_comparisons % CANCELLATION_INTERVAL) == 0 && cancelled && cancelled())
				throw ReaderError(ReaderErrorCode::Cancelled, "[region_sampling] nearest locator was cancelled");
			const auto coordinate = grid.Coordinate(y, x);
			const auto distance = SphereHaversineF64V1(lifted_longitude, target_latitude,
			                                           coordinate.longitude, source_latitude, capability.earth_radius_m);
			const auto point_index = PointIndex(grid, x, y);
			if (!result.source || distance < result.source->distance_m ||
			    (distance == result.source->distance_m && point_index < result.source->point_index)) {
				result.source = NearestSourcePoint{x, y, point_index, coordinate.longitude, source_latitude, distance};
			}
			++result.coordinate_comparisons;
		}
	}
	return result;
}

} // namespace duckomo
} // namespace duckdb
