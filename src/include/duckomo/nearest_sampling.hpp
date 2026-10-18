#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "duckomo/grid_definition.hpp"
#include "duckomo/om_reader.hpp"

namespace duckdb {
namespace duckomo {

enum class NearestCoverageKind : std::uint8_t { GlobalSphere, LatitudeBand, Regional };

struct NearestGridCapability final {
	NearestCoverageKind coverage = NearestCoverageKind::Regional;
	bool periodic_longitude = false;
	double earth_radius_m = 6371229.0;
	double unwrapped_longitude_first = 0;
	double unwrapped_longitude_last = 0;
	double latitude_first = 0;
	double latitude_last = 0;
};

struct NearestSourcePoint final {
	std::uint64_t x = 0;
	std::uint64_t y = 0;
	std::uint64_t point_index = 0;
	double longitude = 0;
	double latitude = 0;
	double distance_m = 0;
};

struct NearestLocation final {
	bool inside_domain = false;
	double target_longitude = 0;
	double target_latitude = 0;
	std::optional<NearestSourcePoint> source;
	std::uint64_t coordinate_comparisons = 0;
};

using NearestCancellationCheck = std::function<bool()>;

// Qualifies a regular sphere grid and records the exact coverage interval.
// Rejects coordinate collapse, duplicate normalized meridians, unsupported
// earth models, and invalid global-period declarations.
NearestGridCapability QualifyNearestGrid(const GridDefinition &definition,
                                          const NearestCancellationCheck &cancelled = {});

// Lifts a periodic longitude or checks the original unwrapped closed interval.
// No half-cell expansion or seam inferred from a normalized bounding box.
bool NearestTargetInsideDomain(const RegularGrid &grid, const NearestGridCapability &capability,
                               double target_longitude, double target_latitude,
                               double *lifted_longitude = nullptr);

// Fixed binary64 numerical kernel, independently specified by contract.
double SphereHaversineF64V1(double target_longitude, double target_latitude,
                            double source_longitude, double source_latitude, double radius_m);

// Exhaustive O(1)-scratch grid locator. Cancellation is polled at least once
// per 256 coordinate comparisons. Coverage is checked before distance work.
NearestLocation LocateNearestRegularGrid(const RegularGrid &grid, const NearestGridCapability &capability,
                                         double target_longitude, double target_latitude,
                                         const NearestCancellationCheck &cancelled = {});

} // namespace duckomo
} // namespace duckdb
