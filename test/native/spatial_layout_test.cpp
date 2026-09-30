#include "duckomo/spatial_layout.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "duckomo/dimensions.hpp"
#include "duckomo/om_reader.hpp"

namespace {
using namespace duckdb::duckomo;
using duckdb::LogicalType;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

void RequireInvalid(const std::function<void()> &action, const std::string &message) {
	try { action(); } catch (const ReaderError &) { return; }
	throw std::runtime_error(message + ": expected ReaderError");
}

BoundSchema MakeSchema(std::vector<std::uint64_t> shape, std::vector<std::string> axes) {
	BoundSchema schema;
	schema.shape = std::move(shape);
	schema.row_count = 1;
	for (const auto length : schema.shape) schema.row_count *= length;
	BoundVariable variable;
	variable.canonical_path = "/value";
	variable.column_name = "value";
	variable.type = LogicalType::FLOAT;
	variable.shape = schema.shape;
	variable.row_count = schema.row_count;
	variable.inferred_axes = std::move(axes);
	schema.variables.push_back(std::move(variable));
	return schema;
}

void TestSeparateAxesAtArbitraryPositions() {
	RegularGrid grid(3, 2, 10, 100, 1, 2, GridStorageOrder::Separate);
	auto schema = MakeSchema({4, 2, 3}, {"member", "lat", "lon"});
	auto layout = BindSpatialLayout(schema, {{"member", "lat", "lon"}}, grid, {"lat", "lon"});
	Require(layout.latitude_axis == 1 && layout.longitude_axis == 2, "spatial axes map by identity, not shape position");
	Require(layout.strides == std::vector<std::uint64_t>({6, 3, 1}), "row-major strides preserve fastest final axis");
	Require(layout.non_spatial_axes == std::vector<std::uint64_t>({0}), "member axis remains non-spatial");

	auto swapped = MakeSchema({3, 4, 2}, {"lon", "member", "lat"});
	auto swapped_layout = BindSpatialLayout(swapped, {{"lon", "member", "lat"}}, grid, {"lat", "lon"});
	Require(swapped_layout.latitude_axis == 2 && swapped_layout.longitude_axis == 0,
	        "spatial axis meaning is independent of storage order");
	Require(swapped_layout.GridIndices(grid, 1) == std::make_pair<std::uint64_t, std::uint64_t>(1, 0),
	        "reordered spatial axes map logical row one to latitude one, longitude zero");
	Require(swapped_layout.GridIndices(grid, 2) == std::make_pair<std::uint64_t, std::uint64_t>(0, 0),
	        "an extra middle axis advances without changing spatial coordinates");
	Require(swapped_layout.GridIndices(grid, 8) == std::make_pair<std::uint64_t, std::uint64_t>(0, 1),
	        "longitude at the first file axis advances in row-major source order");
	const auto position = swapped_layout.AxisIndices(8);
	Require(position == std::vector<std::uint64_t>({1, 0, 0}), "logical positions decode to row-major file axes");
	RequireInvalid([&] { (void)swapped_layout.AxisIndices(swapped.row_count); }, "row count is outside layout");
}

void TestFlattenedLayouts() {
	RegularGrid lon_fast(3, 2, 10, 100, 1, 2, GridStorageOrder::LongitudeFastest);
	auto flat_schema = MakeSchema({2, 6}, {"sample", "point"});
	auto flat = BindSpatialLayout(flat_schema, {{"sample", "point"}}, lon_fast, {"point"});
	Require(flat.flattened && flat.point_axis == 1 && flat.non_spatial_axes == std::vector<std::uint64_t>({0}),
	        "flattened point axis leaves sample axis intact");
	Require(flat.GridIndices(lon_fast, 5) == std::make_pair<std::uint64_t, std::uint64_t>(1, 2),
	        "lon_fastest uses s=y*nx+x");
	Require(flat.GridIndices(lon_fast, 6) == std::make_pair<std::uint64_t, std::uint64_t>(0, 0),
	        "each extra sample traverses the full spatial grid independently");

	RegularGrid lat_fast(3, 2, 10, 100, 1, 2, GridStorageOrder::LatitudeFastest);
	auto lat_flat = BindSpatialLayout(flat_schema, {{"sample", "point"}}, lat_fast, {"point"});
	Require(lat_flat.order == GridStorageOrder::LatitudeFastest, "flattened storage order is explicit");
	Require(lat_flat.GridIndices(lat_fast, 1) == std::make_pair<std::uint64_t, std::uint64_t>(1, 0),
	        "lat_fastest uses s=x*ny+y");
	Require(lat_flat.GridIndices(lat_fast, 2) == std::make_pair<std::uint64_t, std::uint64_t>(0, 1),
	        "lat_fastest advances longitude after the latitude run");
	const auto coordinate = lat_flat.Coordinate(lat_fast, 2);
	Require(coordinate.latitude == 10 && coordinate.longitude == 102,
	        "logical positions reuse the regular-grid coordinate formula");
}

void TestExtraAxisPositionsAndBatchBoundary() {
	RegularGrid grid(3, 2, -30, 170, 0.5, 1, GridStorageOrder::Separate);
	auto trailing_extra_axis = MakeSchema({2, 3, 4}, {"lat", "lon", "member"});
	auto trailing_layout = BindSpatialLayout(trailing_extra_axis, {{"lat", "lon", "member"}}, grid, {"lat", "lon"});
	Require(trailing_layout.non_spatial_axes == std::vector<std::uint64_t>({2}),
	        "an extra final axis retains its own logical positions");
	Require(trailing_layout.GridIndices(grid, 0) == std::make_pair<std::uint64_t, std::uint64_t>(0, 0) &&
	            trailing_layout.GridIndices(grid, 1) == std::make_pair<std::uint64_t, std::uint64_t>(0, 0) &&
	            trailing_layout.GridIndices(grid, 4) == std::make_pair<std::uint64_t, std::uint64_t>(0, 1),
	        "each trailing member value maps to its source point without broadcasting");

	// Exercise a source range that straddles multiple STANDARD_VECTOR_SIZE
	// boundaries. The expected values are derived from explicit row/column
	// arithmetic, independent of SpatialLayout's cursor conversion.
	auto large_schema = MakeSchema({73, 61}, {"lat", "lon"});
	RegularGrid large_grid(61, 73, -36, -90, 1, 1, GridStorageOrder::Separate);
	auto large_layout = BindSpatialLayout(large_schema, {{"lat", "lon"}}, large_grid, {"lat", "lon"});
	for (std::uint64_t logical = 2040; logical < 2060; logical++) {
		const auto expected_y = logical / 61;
		const auto expected_x = logical % 61;
		const auto expected = std::make_pair(expected_y, expected_x);
		Require(large_layout.GridIndices(large_grid, logical) == expected,
		        "cross-batch logical positions preserve row-major grid coordinates");
		const auto coordinate = large_layout.Coordinate(large_grid, logical);
		Require(coordinate.latitude == -36.0 + static_cast<double>(expected_y) &&
		            coordinate.longitude == -90.0 + static_cast<double>(expected_x),
		        "cross-batch coordinates match an independent scalar reference");
	}
}

void TestRejectAmbiguousLayout() {
	RegularGrid grid(3, 2, 10, 100, 1, 2, GridStorageOrder::Separate);
	auto schema = MakeSchema({2, 3}, {});
	RequireInvalid([&] { BindSpatialLayout(schema, {{"", ""}}, grid, {"lat", "lon"}); },
	               "shape without axis identities is rejected");
	RequireInvalid([&] { BindSpatialLayout(schema, {{"row", "column"}}, grid, {"row", "missing"}); },
	               "unknown spatial axis is rejected");
	RequireInvalid([&] {
		BindSpatialLayout(schema, {{"row", "column"}}, grid, {"row", "row"});
	}, "spatial axes cannot be repeated");
	auto wrong_shape = MakeSchema({6}, {"point"});
	RequireInvalid([&] { BindSpatialLayout(wrong_shape, {{"point"}}, grid, {"point"}); },
	               "flattened product mismatch is rejected");
}

} // namespace

int main() {
	try {
		TestSeparateAxesAtArbitraryPositions();
		TestFlattenedLayouts();
		TestExtraAxisPositionsAndBatchBoundary();
		TestRejectAmbiguousLayout();
		std::cout << "spatial layout checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "spatial layout checks failed: " << error.what() << '\n';
		return 1;
	}
}
