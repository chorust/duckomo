#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "duckdb/common/types/value.hpp"
#include "duckdb/common/types.hpp"
#include "duckomo/axis_selection.hpp"
#include "duckomo/nearest_sampling.hpp"
#include "duckomo/spatial_layout.hpp"

namespace duckdb {
namespace duckomo {

enum class RegionMethod : std::uint8_t { Points, Nearest };
enum class OutOfBoundsPolicy : std::uint8_t { Null, Error };
enum class GeometryStatus : std::uint8_t { Ok, EmptyGeometry, OutsideDomain, BeyondMaxDistance };

struct RegionSamplingOptions final {
	RegionMethod method = RegionMethod::Points;
	std::optional<double> max_distance_m;
	OutOfBoundsPolicy out_of_bounds = OutOfBoundsPolicy::Null;
	bool max_distance_explicit = false;
	bool out_of_bounds_explicit = false;
	bool operator==(const RegionSamplingOptions &other) const noexcept {
		return method == other.method && max_distance_m == other.max_distance_m && out_of_bounds == other.out_of_bounds &&
		       max_distance_explicit == other.max_distance_explicit && out_of_bounds_explicit == other.out_of_bounds_explicit;
	}
};

struct RegionSamplingSource final {
	NearestSourcePoint point;
	std::uint64_t logical_index = 0;
	std::vector<std::uint64_t> axis_indices;
};

struct NearestRegionTarget final {
	bool empty = false;
	GridCoordinate representative;
};

struct NearestRegionPlanEntry final {
	GeometryStatus status = GeometryStatus::EmptyGeometry;
	bool has_representative = false;
	GridCoordinate representative;
	NearestLocation location;
};

struct NearestSamplingPlan final {
	std::string spatial_version;
	std::string grid_id;
	std::string layout_id;
	double earth_radius_m = 0;
	std::vector<NearestRegionPlanEntry> regions;

	std::uint64_t EstimatedBytes() const noexcept;
	bool Equals(const NearestSamplingPlan &other) const noexcept;
};

std::shared_ptr<const NearestSamplingPlan> BuildNearestSamplingPlan(
    const RegularGrid &grid, const NearestGridCapability &capability, const std::vector<NearestRegionTarget> &targets,
    std::string spatial_version, std::string grid_id, std::string layout_id,
    const std::optional<double> &max_distance_m, OutOfBoundsPolicy out_of_bounds,
    const NearestCancellationCheck &cancelled = {});

// Reconstruct an original row-major logical position from source x/y and the
// complete non-spatial axis position vector. Flattened point identity is kept
// separate from the multidimensional source position.
std::uint64_t SpatialLayoutLogicalIndex(const SpatialLayout &layout, const std::vector<std::uint64_t> &non_spatial_indices,
                                        std::uint64_t x, std::uint64_t y, std::uint64_t point_index);

struct NearestAxisBatch final {
	std::vector<std::vector<std::uint64_t>> indices;
	bool exhausted = false;
};

// Iterates only non-spatial axes. Spatial dimensions are omitted from the
// cursor domain and original-axis order is preserved in each output vector.
class NearestAxisCursor final {
public:
	using AxisFilter = std::function<bool(std::size_t, std::uint64_t)>;
	NearestAxisCursor(const SpatialLayout &layout, AxisFilter include = {});
	NearestAxisCursor(const SpatialLayout &layout, const AxisSelectionCursor &selection);
	bool IsExhausted() const noexcept;
	std::uint64_t ResultCount() const noexcept;
	NearestAxisBatch Next(std::uint64_t limit);

private:
	std::vector<std::uint64_t> axes;
	std::vector<std::uint64_t> lengths;
	std::vector<std::uint64_t> current;
	std::vector<std::optional<std::uint64_t>> next_selected;
	const AxisSelectionCursor *selection = nullptr;
	AxisFilter include;
	std::uint64_t result_count = 0;
	bool exhausted = false;
	bool started = false;
	bool filtered_exhausted = false;
	bool AdvanceSelected();
};

// Parse the method-specific options after retrieving their bound constant
// values. Presence is retained so explicit defaults can be rejected for points.
RegionSamplingOptions ParseRegionSamplingOptions(const Value *method, const Value *max_distance_m,
                                                const Value *out_of_bounds);

} // namespace duckomo
} // namespace duckdb
