#include "duckomo/spatial_layout.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

#include "duckomo/om_reader.hpp"

namespace duckdb {
namespace duckomo {
namespace {

std::uint64_t CheckedProduct(std::uint64_t left, std::uint64_t right) {
	if (right != 0 && left > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / right) {
		throw ReaderError(ReaderErrorCode::ShapeOverflow, "regular grid layout point count exceeds signed 64-bit limit");
	}
	return left * right;
}

std::uint64_t CheckedLayoutProduct(const std::vector<std::uint64_t> &shape) {
	if (shape.empty()) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "spatial layout shape must have positive rank");
	}
	std::uint64_t product = 1;
	for (const auto length : shape) {
		if (length == 0) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "spatial layout axis lengths must be positive");
		}
		product = CheckedProduct(product, length);
	}
	return product;
}

std::uint64_t FindAxis(const std::vector<std::string> &axes, const std::string &name) {
	const auto found = std::find(axes.begin(), axes.end(), name);
	if (found == axes.end()) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "spatial axis '" + name + "' is not present in dimensions metadata");
	}
	return static_cast<std::uint64_t>(std::distance(axes.begin(), found));
}

void ValidateAxisMetadata(const SpatialLayout &layout) {
	if (layout.axes.size() != layout.shape.size() || layout.strides.size() != layout.shape.size()) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "spatial layout axes, shape, and strides must have the same rank");
	}
	(void)CheckedLayoutProduct(layout.shape);
	std::uint64_t expected_stride = 1;
	for (std::size_t reverse = layout.shape.size(); reverse > 0; reverse--) {
		const auto axis = reverse - 1;
		if (layout.axes[axis].empty()) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "spatial layout axis identities must not be empty");
		}
		if (layout.strides[axis] != expected_stride) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "spatial layout strides do not match the row-major source shape");
		}
		for (std::size_t prior = 0; prior < axis; prior++) {
			if (layout.axes[prior] == layout.axes[axis]) {
				throw ReaderError(ReaderErrorCode::InvalidShape, "spatial layout axis identities must be unique");
			}
		}
		expected_stride = CheckedProduct(expected_stride, layout.shape[axis]);
	}
}

} // namespace

std::vector<std::uint64_t> SpatialLayout::AxisIndices(std::uint64_t logical_index) const {
	ValidateAxisMetadata(*this);
	if (CheckedLayoutProduct(shape) <= logical_index) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "logical row index is outside the spatial layout");
	}
	std::vector<std::uint64_t> result(shape.size());
	for (std::size_t axis = 0; axis < shape.size(); axis++) {
		result[axis] = (logical_index / strides[axis]) % shape[axis];
	}
	return result;
}

std::pair<std::uint64_t, std::uint64_t> SpatialLayout::GridIndices(const RegularGrid &grid,
	                                                               std::uint64_t logical_index) const {
	if (grid.Order() != order) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "regular grid order does not match the bound spatial layout");
	}
	const auto indices = AxisIndices(logical_index);
	if (flattened) {
		if (point_axis >= indices.size() || shape[point_axis] != grid.PointCount()) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "flattened spatial point axis no longer matches the grid");
		}
		const auto point = indices[point_axis];
		if (order == GridStorageOrder::LongitudeFastest) {
			return {point / grid.Nx(), point % grid.Nx()};
		}
		if (order == GridStorageOrder::LatitudeFastest) {
			return {point % grid.Ny(), point / grid.Ny()};
		}
		throw ReaderError(ReaderErrorCode::InvalidShape, "flattened spatial layout requires an explicit storage order");
	}
	if (latitude_axis >= indices.size() || longitude_axis >= indices.size() ||
	    shape[latitude_axis] != grid.Ny() || shape[longitude_axis] != grid.Nx()) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "separate spatial axes no longer match the grid");
	}
	return {indices[latitude_axis], indices[longitude_axis]};
}

GridCoordinate SpatialLayout::Coordinate(const RegularGrid &grid, std::uint64_t logical_index) const {
	const auto [y, x] = GridIndices(grid, logical_index);
	return grid.Coordinate(y, x);
}

NativeGridPosition SpatialLayout::NativePosition(const GridDefinition &definition, std::uint64_t logical_index) const {
	const auto indices = AxisIndices(logical_index);
	if (geometry == SpatialLayoutGeometry::Gaussian) {
		const auto *gaussian = std::get_if<GaussianGrid>(&definition.geometry);
		if (gaussian == nullptr || point_axis >= indices.size() || shape[point_axis] != gaussian->PointCount()) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "Gaussian spatial layout does not match its grid definition");
		}
		return NativePointPosition{indices[point_axis]};
	}
	if (geometry == SpatialLayoutGeometry::Projected) {
		const auto *projected = std::get_if<ProjectedGrid>(&definition.geometry);
		if (projected == nullptr) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "projected spatial layout does not match its grid definition");
		}
		const auto order = std::visit([](const auto &parameters) { return parameters.order; }, projected->Parameters());
		if (flattened) {
			if (order == GridStorageOrder::Separate || point_axis >= indices.size() ||
			    shape[point_axis] != projected->PointCount()) {
				throw ReaderError(ReaderErrorCode::InvalidShape, "flattened projected layout has no point axis");
			}
			return NativePointPosition{indices[point_axis]};
		}
		if (order != GridStorageOrder::Separate || latitude_axis >= indices.size() || longitude_axis >= indices.size() ||
		    shape[latitude_axis] != projected->Ny() || shape[longitude_axis] != projected->Nx()) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "separate projected layout has no y/x axes");
		}
		return NativeXYPosition{indices[longitude_axis], indices[latitude_axis]};
	}
	const auto *regular = std::get_if<RegularGrid>(&definition.geometry);
	if (regular == nullptr) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "regular spatial layout does not match its grid definition");
	}
	const auto [y, x] = GridIndices(*regular, logical_index);
	return NativeXYPosition{x, y};
}

GridCoordinate SpatialLayout::Coordinate(const GridDefinition &definition, std::uint64_t logical_index) const {
	return definition.Coordinate(NativePosition(definition, logical_index));
}

std::uint64_t SpatialLayout::LocalPointIndex(const GridDefinition &definition, std::uint64_t logical_index) const {
	const auto position = NativePosition(definition, logical_index);
	if (const auto *point = std::get_if<NativePointPosition>(&position)) return point->point;
	const auto xy = std::get<NativeXYPosition>(position);
	const auto &geometry = definition.geometry;
	if (const auto *regular = std::get_if<RegularGrid>(&geometry)) {
		return regular->Order() == GridStorageOrder::LatitudeFastest ? xy.x * regular->Ny() + xy.y
		                                                           : xy.y * regular->Nx() + xy.x;
	}
	const auto &projected = std::get<ProjectedGrid>(geometry);
	const auto order = std::visit([](const auto &parameters) { return parameters.order; }, projected.Parameters());
	return order == GridStorageOrder::LatitudeFastest ? xy.x * projected.Ny() + xy.y
	                                                  : xy.y * projected.Nx() + xy.x;
}

std::uint64_t SpatialLayout::ParentPointIndex(const GridDefinition &definition, std::uint64_t logical_index) const {
	const auto local = LocalPointIndex(definition, logical_index);
	if (const auto *gaussian = std::get_if<GaussianGrid>(&definition.geometry)) {
		return gaussian->ParentPointIndex(local);
	}
	return local;
}

SpatialLayout BindSpatialLayout(const BoundSchema &schema, const AxisDeclarations &axis_declarations,
                                const RegularGrid &grid, const std::vector<std::string> &spatial_axes) {
	if (schema.variables.empty() || axis_declarations.size() != schema.variables.size()) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "spatial mapping requires validated axes for every value array");
	}
	if (axis_declarations.front().size() != schema.shape.size()) {
		throw ReaderError(ReaderErrorCode::InvalidShape,
		                  "spatial mapping requires complete axis identities; shape alone is insufficient");
	}
	if (CheckedLayoutProduct(schema.shape) != schema.row_count) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "spatial layout shape does not match the validated row count");
	}
	if (std::any_of(axis_declarations.begin(), axis_declarations.end(),
	                [&](const auto &axes) { return axes != axis_declarations.front(); })) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "value arrays have different ordered axis identities");
	}
	for (const auto &variable : schema.variables) {
		if (variable.shape != schema.shape || variable.row_count != schema.row_count) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "spatial mapping requires aligned value array shapes");
		}
	}

	SpatialLayout result;
	result.shape = schema.shape;
	result.axes = axis_declarations.front();
	result.geometry = SpatialLayoutGeometry::Regular;
	result.order = grid.Order();
	result.strides.resize(result.shape.size(), 1);
	for (std::size_t axis = 0; axis < result.axes.size(); axis++) {
		if (result.axes[axis].empty()) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "spatial axis identities must not be empty");
		}
		for (std::size_t prior = 0; prior < axis; prior++) {
			if (result.axes[prior] == result.axes[axis]) {
				throw ReaderError(ReaderErrorCode::InvalidShape, "spatial mapping contains a duplicate axis identity");
			}
		}
	}
	for (std::size_t reverse = result.shape.size(); reverse > 1; reverse--) {
		const auto axis = reverse - 2;
		result.strides[axis] = CheckedProduct(result.strides[axis + 1], result.shape[axis + 1]);
	}

	if (grid.Order() == GridStorageOrder::Separate) {
		if (spatial_axes.size() != 2 || spatial_axes[0] == spatial_axes[1]) {
			throw ReaderError(ReaderErrorCode::InvalidShape,
			                  "separate spatial layout requires distinct [latitude_axis, longitude_axis]");
		}
		result.latitude_axis = FindAxis(result.axes, spatial_axes[0]);
		result.longitude_axis = FindAxis(result.axes, spatial_axes[1]);
		if (result.shape[result.latitude_axis] != grid.Ny() || result.shape[result.longitude_axis] != grid.Nx()) {
			throw ReaderError(ReaderErrorCode::InvalidShape,
			                  "separate spatial axis lengths do not match regular grid ny/nx");
		}
		result.non_spatial_axes.reserve(result.shape.size() - 2);
		for (std::uint64_t axis = 0; axis < result.shape.size(); axis++) {
			if (axis != result.latitude_axis && axis != result.longitude_axis) {
				result.non_spatial_axes.push_back(axis);
			}
		}
	} else {
		if (spatial_axes.size() != 1) {
			throw ReaderError(ReaderErrorCode::InvalidShape,
			                  "flattened spatial layout requires exactly one spatial point axis");
		}
		result.flattened = true;
		result.point_axis = FindAxis(result.axes, spatial_axes.front());
		if (result.shape[result.point_axis] != grid.PointCount()) {
			throw ReaderError(ReaderErrorCode::InvalidShape,
			                  "flattened spatial axis length does not match nx multiplied by ny");
		}
		for (std::uint64_t axis = 0; axis < result.shape.size(); axis++) {
			if (axis != result.point_axis) {
				result.non_spatial_axes.push_back(axis);
			}
		}
	}
	return result;
}

SpatialLayout BindGridSpatialLayout(const BoundSchema &schema, const AxisDeclarations &axis_declarations,
	                                const GridDefinition &grid, const std::vector<std::string> &spatial_axes) {
	if (const auto *regular = std::get_if<RegularGrid>(&grid.geometry)) {
		return BindSpatialLayout(schema, axis_declarations, *regular, spatial_axes);
	}
	if (schema.variables.empty() || axis_declarations.size() != schema.variables.size() ||
	    axis_declarations.front().size() != schema.shape.size()) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "grid mapping requires validated axes for every value array");
	}
	if (CheckedLayoutProduct(schema.shape) != schema.row_count ||
	    std::any_of(axis_declarations.begin(), axis_declarations.end(),
	                [&](const auto &axes) { return axes != axis_declarations.front(); })) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "grid mapping requires one validated source shape and ordered axis profile");
	}
	for (const auto &variable : schema.variables) {
		if (variable.shape != schema.shape || variable.row_count != schema.row_count) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "grid mapping requires aligned value array shapes");
		}
	}

	SpatialLayout result;
	result.shape = schema.shape;
	result.axes = axis_declarations.front();
	result.strides.resize(result.shape.size(), 1);
	for (std::size_t reverse = result.shape.size(); reverse > 1; reverse--) {
		const auto axis = reverse - 2;
		result.strides[axis] = CheckedProduct(result.strides[axis + 1], result.shape[axis + 1]);
	}
	for (std::size_t axis = 0; axis < result.axes.size(); axis++) {
		if (result.axes[axis].empty() ||
		    std::find(result.axes.begin(), result.axes.begin() + static_cast<std::ptrdiff_t>(axis), result.axes[axis]) !=
		        result.axes.begin() + static_cast<std::ptrdiff_t>(axis)) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "grid mapping axis identities must be non-empty and unique");
		}
	}

	if (const auto *projected = std::get_if<ProjectedGrid>(&grid.geometry)) {
		result.geometry = SpatialLayoutGeometry::Projected;
		const auto order = std::visit([](const auto &parameters) { return parameters.order; }, projected->Parameters());
		result.order = order;
		if (order == GridStorageOrder::Separate) {
			if (spatial_axes.size() != 2 || spatial_axes[0] == spatial_axes[1]) {
				throw ReaderError(ReaderErrorCode::InvalidShape, "separate projected layout requires ordered [y_axis, x_axis]");
			}
			result.latitude_axis = FindAxis(result.axes, spatial_axes[0]);
			result.longitude_axis = FindAxis(result.axes, spatial_axes[1]);
			if (result.shape[result.latitude_axis] != projected->Ny() || result.shape[result.longitude_axis] != projected->Nx()) {
				throw ReaderError(ReaderErrorCode::InvalidShape, "projected y/x axis lengths do not match declared ny/nx");
			}
			for (std::uint64_t axis = 0; axis < result.shape.size(); axis++) {
				if (axis != result.latitude_axis && axis != result.longitude_axis) result.non_spatial_axes.push_back(axis);
			}
		} else {
			if (spatial_axes.size() != 1) {
				throw ReaderError(ReaderErrorCode::InvalidShape, "flattened projected layout requires one ordered point axis");
			}
			result.flattened = true;
			result.point_axis = FindAxis(result.axes, spatial_axes.front());
			if (result.shape[result.point_axis] != projected->PointCount()) {
				throw ReaderError(ReaderErrorCode::InvalidShape, "projected point axis length does not match nx*ny");
			}
			for (std::uint64_t axis = 0; axis < result.shape.size(); axis++) {
				if (axis != result.point_axis) result.non_spatial_axes.push_back(axis);
			}
		}
		return result;
	}

	const auto &gaussian = std::get<GaussianGrid>(grid.geometry);
	result.geometry = SpatialLayoutGeometry::Gaussian;
	result.flattened = true;
	if (spatial_axes.size() != 1) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "Gaussian layout requires exactly one point axis");
	}
	result.point_axis = FindAxis(result.axes, spatial_axes.front());
	if (result.shape[result.point_axis] != gaussian.PointCount()) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "Gaussian point axis length does not match the full or local grid point count");
	}
	for (std::uint64_t axis = 0; axis < result.shape.size(); axis++) {
		if (axis != result.point_axis) result.non_spatial_axes.push_back(axis);
	}
	return result;
}

} // namespace duckomo
} // namespace duckdb
