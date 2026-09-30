#include "duckomo/spatial_selection.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace duckdb::duckomo;

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

SpatialLayout SeparateLayout(std::vector<std::uint64_t> shape, std::uint64_t latitude_axis,
	                           std::uint64_t longitude_axis) {
	SpatialLayout layout;
	layout.shape = std::move(shape);
	layout.axes.resize(layout.shape.size());
	layout.strides.resize(layout.shape.size(), 1);
	for (std::size_t reverse = layout.shape.size(); reverse > 1; reverse--) {
		layout.strides[reverse - 2] = layout.strides[reverse - 1] * layout.shape[reverse - 1];
	}
	layout.latitude_axis = latitude_axis;
	layout.longitude_axis = longitude_axis;
	layout.order = GridStorageOrder::Separate;
	layout.non_spatial_axes.clear();
	for (std::uint64_t axis = 0; axis < layout.shape.size(); axis++) {
		if (axis != latitude_axis && axis != longitude_axis) {
			layout.non_spatial_axes.push_back(axis);
		}
	}
	return layout;
}

void TestRestrictedCursorPreservesPositionsAndSegments() {
	RegularGrid grid(3, 2, 10.0, 100.0, 1.0, 2.0, GridStorageOrder::Separate);
	auto layout = SeparateLayout({2, 3, 3}, 0, 2);
	SpatialPredicate predicate;
	predicate.necessary_conditions = {{SpatialAxis::Latitude, SpatialComparison::Equal, 11.0},
	                                  {SpatialAxis::Longitude, SpatialComparison::GreaterEqual, 102.0},
	                                  {SpatialAxis::Longitude, SpatialComparison::LessEqual, 104.0}};
	auto selection = BuildSpatialSelection(grid, layout, predicate);
	Require(selection.mode == SpatialSelectionMode::Restricted, "coordinate conditions produce restricted selection");
	Require(selection.candidate_rows == 6, "candidate rows include every extra-axis position");

	SpatialBatchCursor cursor(grid, layout, selection);
	SpatialBatch batch;
	std::vector<std::uint64_t> positions;
	std::size_t segment_count = 0;
	while (cursor.Next(4, batch)) {
		Require(batch.logical_positions.size() <= 4, "cursor respects caller's bounded vector size");
		for (const auto &segment : batch.read_segments) {
			Require(segment.count > 0, "cursor never emits a zero-count OM read segment");
			Require(segment.read_count.back() == segment.count, "each read segment stays contiguous on the last axis");
			segment_count++;
		}
		positions.insert(positions.end(), batch.logical_positions.begin(), batch.logical_positions.end());
	}
	Require(positions == std::vector<std::uint64_t>({10, 11, 13, 14, 16, 17}),
	        "cursor emits every selected source position once and in source order");
	Require(segment_count == 3, "gaps in selected positions split OM reads into legal contiguous segments");
	Require(cursor.Exhausted(), "cursor reports exhaustion after the final selected position");
}

void TestNegativeStepAndStrictBoundaries() {
	RegularGrid grid(3, 2, 11.0, 104.0, -1.0, -2.0, GridStorageOrder::Separate);
	auto layout = SeparateLayout({2, 2, 3}, 0, 2);
	SpatialPredicate predicate;
	predicate.necessary_conditions = {{SpatialAxis::Latitude, SpatialComparison::Less, 11.0},
	                                  {SpatialAxis::Longitude, SpatialComparison::Greater, 100.0}};
	auto selection = BuildSpatialSelection(grid, layout, predicate);
	Require(selection.candidate_rows == 4, "strict comparisons are exact for negative-step axes and extra rows");
	SpatialBatchCursor cursor(grid, layout, selection);
	SpatialBatch batch;
	std::vector<std::uint64_t> positions;
	while (cursor.Next(8, batch)) {
		positions.insert(positions.end(), batch.logical_positions.begin(), batch.logical_positions.end());
	}
	Require(positions == std::vector<std::uint64_t>({6, 7, 9, 10}), "negative axes map to exact source positions");
}

void TestRestrictedCursorSkipsExcludedMultidimensionalRanges() {
	RegularGrid grid(3, 2, 10.0, 100.0, 1.0, 2.0, GridStorageOrder::Separate);
	auto layout = SeparateLayout({2, 3, 1000000000}, 0, 1);
	SpatialPredicate predicate;
	predicate.necessary_conditions = {{SpatialAxis::Latitude, SpatialComparison::Equal, 11.0},
	                                  {SpatialAxis::Longitude, SpatialComparison::Equal, 104.0}};
	auto selection = BuildSpatialSelection(grid, layout, predicate);
	Require(selection.candidate_rows == 1000000000,
	        "every time position remains a candidate for the selected spatial cell");

	SpatialBatchCursor cursor(grid, layout, std::move(selection));
	SpatialBatch batch;
	Require(cursor.Next(3, batch), "restricted cursor yields the first selected time positions");
	Require(batch.logical_positions == std::vector<std::uint64_t>({5000000000, 5000000001, 5000000002}),
	        "cursor jumps over excluded lat/lon cells and preserves trailing time order");
	Require(batch.read_segments.size() == 1 && batch.read_segments.front().count == 3,
	        "contiguous selected time positions remain one OM read segment");
}

void TestEmptyAndFlattenedLayouts() {
	RegularGrid grid(3, 2, 10.0, 179.0, 1.0, 1.0, GridStorageOrder::LongitudeFastest);
	SpatialLayout flat;
	flat.shape = {2, 6};
	flat.axes = {"sample", "point"};
	flat.strides = {6, 1};
	flat.flattened = true;
	flat.point_axis = 1;
	flat.order = GridStorageOrder::LongitudeFastest;
	flat.non_spatial_axes = {0};
	SpatialPredicate seam;
	seam.necessary_conditions = {{SpatialAxis::Longitude, SpatialComparison::GreaterEqual, 179.0}};
	auto selected = BuildSpatialSelection(grid, flat, seam);
	Require(selected.candidate_rows == 4, "seam-adjacent longitude selection preserves each extra sample");
	SpatialBatchCursor cursor(grid, flat, selected);
	SpatialBatch batch;
	std::vector<std::uint64_t> positions;
	while (cursor.Next(3, batch)) {
		for (const auto &segment : batch.read_segments) {
			Require(segment.count > 0, "seam selections never create empty decoder ranges");
		}
		positions.insert(positions.end(), batch.logical_positions.begin(), batch.logical_positions.end());
	}
	Require(positions == std::vector<std::uint64_t>({0, 3, 6, 9}), "flattened seam selection preserves repeated extra-axis positions");

	SpatialPredicate none;
	none.necessary_conditions = {{SpatialAxis::Latitude, SpatialComparison::Greater, 90.0}};
	selected = BuildSpatialSelection(grid, flat, none);
	Require(selected.mode == SpatialSelectionMode::Empty && selected.candidate_rows == 0,
	        "nonmatching bounds become a zero-row selection");
	SpatialBatchCursor empty_cursor(grid, flat, selected);
	Require(!empty_cursor.Next(8, batch) && batch.read_segments.empty(), "empty cursor exhausts without a decoder segment");

	RegularGrid latitude_fast_grid(3, 2, 10.0, 100.0, 1.0, 2.0, GridStorageOrder::LatitudeFastest);
	SpatialLayout latitude_fast;
	latitude_fast.shape = {6};
	latitude_fast.axes = {"point"};
	latitude_fast.strides = {1};
	latitude_fast.flattened = true;
	latitude_fast.point_axis = 0;
	latitude_fast.order = GridStorageOrder::LatitudeFastest;
	SpatialPredicate latitude_fast_predicate;
	latitude_fast_predicate.necessary_conditions = {
	    {SpatialAxis::Latitude, SpatialComparison::Equal, 11.0},
	    {SpatialAxis::Longitude, SpatialComparison::GreaterEqual, 102.0}};
	selected = BuildSpatialSelection(latitude_fast_grid, latitude_fast, latitude_fast_predicate);
	SpatialBatchCursor latitude_fast_cursor(latitude_fast_grid, latitude_fast, selected);
	positions.clear();
	while (latitude_fast_cursor.Next(2, batch)) {
		positions.insert(positions.end(), batch.logical_positions.begin(), batch.logical_positions.end());
	}
	Require(positions == std::vector<std::uint64_t>({3, 5}), "lat_fastest flattened layout maps to exact source positions");

	auto trailing_extra = SeparateLayout({2, 3, 2}, 0, 1);
	SpatialPredicate trailing_predicate;
	trailing_predicate.necessary_conditions = {{SpatialAxis::Latitude, SpatialComparison::Equal, 11.0},
	                                          {SpatialAxis::Longitude, SpatialComparison::Equal, 102.0}};
	selected = BuildSpatialSelection(RegularGrid(3, 2, 10.0, 100.0, 1.0, 2.0, GridStorageOrder::Separate),
	                                 trailing_extra, trailing_predicate);
	SpatialBatchCursor trailing_cursor(
	    RegularGrid(3, 2, 10.0, 100.0, 1.0, 2.0, GridStorageOrder::Separate), trailing_extra, selected);
	positions.clear();
	while (trailing_cursor.Next(4, batch)) {
		positions.insert(positions.end(), batch.logical_positions.begin(), batch.logical_positions.end());
	}
	Require(positions == std::vector<std::uint64_t>({8, 9}), "last-axis extra data is preserved per selected cell");
}

void TestLayoutProductOverflowFailsClosed() {
	RegularGrid grid(1, 1, 0.0, 0.0, 1.0, 1.0, GridStorageOrder::Separate);
	auto layout = SeparateLayout({static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), 2, 1}, 1, 2);
	SpatialPredicate predicate;
	try {
		(void)BuildSpatialSelection(grid, layout, predicate);
	} catch (const std::exception &) {
		return;
	}
	throw std::runtime_error("oversized logical shape must reject before cursor construction");
}

} // namespace

int main() {
	try {
		TestRestrictedCursorPreservesPositionsAndSegments();
		TestNegativeStepAndStrictBoundaries();
		TestRestrictedCursorSkipsExcludedMultidimensionalRanges();
		TestEmptyAndFlattenedLayouts();
		TestLayoutProductOverflowFailsClosed();
		std::cout << "spatial selection checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "spatial selection checks failed: " << error.what() << '\n';
		return 1;
	}
}
