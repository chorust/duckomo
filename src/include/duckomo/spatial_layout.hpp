#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "duckomo/dimensions.hpp"
#include "duckomo/grid_definition.hpp"
#include "duckomo/regular_grid.hpp"
#include "duckomo/schema.hpp"

namespace duckdb {
namespace duckomo {

enum class SpatialLayoutGeometry : std::uint8_t { Regular, Projected, Gaussian };

// Maps a verified ordered OM axis identity to the latitude/longitude points.
// Extra axes remain ordinary logical positions and receive no inferred meaning.
struct SpatialLayout final {
	std::vector<std::uint64_t> shape;
	std::vector<std::string> axes;
	std::vector<std::uint64_t> strides;
	std::vector<std::uint64_t> non_spatial_axes;
	std::uint64_t latitude_axis = 0;
	std::uint64_t longitude_axis = 0;
	std::uint64_t point_axis = 0;
	SpatialLayoutGeometry geometry = SpatialLayoutGeometry::Regular;
	bool flattened = false;
	GridStorageOrder order = GridStorageOrder::Separate;

	// Convert a row-major source position to each declared file-axis index.
	// No inferred labels are attached to non-spatial axes.
	std::vector<std::uint64_t> AxisIndices(std::uint64_t logical_index) const;

	// Map the source position to (y, x) in the grid. Repeated geographic
	// coordinates remain separate source positions when the grid wraps.
	std::pair<std::uint64_t, std::uint64_t> GridIndices(const RegularGrid &grid,
	                                                   std::uint64_t logical_index) const;
	GridCoordinate Coordinate(const RegularGrid &grid, std::uint64_t logical_index) const;
	NativeGridPosition NativePosition(const GridDefinition &definition, std::uint64_t logical_index) const;
	GridCoordinate Coordinate(const GridDefinition &definition, std::uint64_t logical_index) const;
	std::uint64_t LocalPointIndex(const GridDefinition &definition, std::uint64_t logical_index) const;
	std::uint64_t ParentPointIndex(const GridDefinition &definition, std::uint64_t logical_index) const;
};

SpatialLayout BindSpatialLayout(const BoundSchema &schema, const AxisDeclarations &axis_declarations,
                                const RegularGrid &grid, const std::vector<std::string> &spatial_axes);
SpatialLayout BindGridSpatialLayout(const BoundSchema &schema, const AxisDeclarations &axis_declarations,
                                    const GridDefinition &grid, const std::vector<std::string> &spatial_axes);

} // namespace duckomo
} // namespace duckdb
