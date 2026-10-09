#include "duckomo/spatial_selection.hpp"

#include <algorithm>
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
	for (std::size_t axis = 0; axis < layout.axes.size(); axis++) {
		layout.axes[axis] = "axis_" + std::to_string(axis);
	}
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

void TestNativeWindowDescriptorChecksExtentAndPayload() {
	const auto window = NativeWindow::Create(1, 2, MAX_NATIVE_WINDOW_POINTS, {4, 70000, 3}, {1, 0, 2});
	Require(window.SpatialAxis() == 1 && window.Begin() == 2 && window.End() == 65538,
	        "native window preserves a checked half-open scanline range");
	Require(window.Count() == MAX_NATIVE_WINDOW_POINTS &&
	            window.EstimatedBytes() <= MAX_SELECTOR_PAYLOAD_BYTES,
	        "native window length and selector payload respect their fixed budgets");
	bool rejected = false;
	try {
		(void)NativeWindow::Create(1, 2, MAX_NATIVE_WINDOW_POINTS + 1, {4, 70000, 3}, {1, 0, 2});
	} catch (const ReaderError &error) {
		rejected = error.Code() == ReaderErrorCode::InvalidSelection;
	}
	Require(rejected, "native windows reject a length above 65536 before copying axis indices");
	rejected = false;
	try {
		(void)NativeWindow::Create(1, 69999, 2, {4, 70000, 3}, {1, 0, 2});
	} catch (const ReaderError &error) {
		rejected = error.Code() == ReaderErrorCode::InvalidSelection;
	}
	Require(rejected, "native windows reject axis-end overflow");
	rejected = false;
	try {
		(void)NativeWindow::Create(1, 2, 1, {4, 70000, 3}, {1, 0});
	} catch (const ReaderError &error) {
		rejected = error.Code() == ReaderErrorCode::InvalidShape;
	}
	Require(rejected, "native windows reject a fixed-index vector with the wrong rank");
}

void TestNativeWindowCursorUsesFastestSpatialAxisAndRealStrides() {
	auto interleaved = SeparateLayout({2, 3, 2, 4}, 1, 3);
	NativeWindowCursor cursor(interleaved);
	Require(cursor.SpatialAxis() == 3 && cursor.WindowUpperBound() == 12,
	        "separate layout windows use the spatial axis with the smallest source stride");
	std::vector<std::uint64_t> positions;
	NativeWindow window;
	while (cursor.Next(window)) {
		Require(window.Count() == 4, "short native axis emits one bounded window per fixed-axis tuple");
		std::uint64_t prior = 0;
		for (std::uint64_t offset = 0; offset < window.Count(); offset++) {
			const auto position = window.LogicalPosition(interleaved, offset);
			if (offset != 0) Require(position > prior, "each window maps to strictly increasing source positions");
			prior = position;
			positions.push_back(position);
		}
	}
	std::sort(positions.begin(), positions.end());
	Require(positions.size() == 48, "native descriptors cover every logical source position once");
	for (std::uint64_t position = 0; position < positions.size(); position++) {
		Require(positions[position] == position, "interleaved axes map windows through actual row-major strides");
	}

	SpatialLayout trailing_axis;
	trailing_axis.shape = {3, 2, 4};
	trailing_axis.axes = {"lat", "lon", "time"};
	trailing_axis.strides = {8, 4, 1};
	trailing_axis.latitude_axis = 0;
	trailing_axis.longitude_axis = 1;
	NativeWindowCursor trailing_cursor(trailing_axis);
	Require(trailing_cursor.SpatialAxis() == 1 && trailing_cursor.WindowUpperBound() == 12,
	        "fastest spatial axis is selected even when another axis is physically trailing");
	positions.clear();
	while (trailing_cursor.Next(window)) {
		for (std::uint64_t offset = 0; offset < window.Count(); offset++) {
			positions.push_back(window.LogicalPosition(trailing_axis, offset));
		}
	}
	std::sort(positions.begin(), positions.end());
	Require(positions.size() == 24, "non-spatial trailing positions are retained by every window");
	for (std::uint64_t position = 0; position < positions.size(); position++) {
		Require(positions[position] == position, "interleaved native windows never skip or duplicate a row");
	}
}

void TestNativeWindowCursorSplitsLongSpatialAxes() {
	SpatialLayout layout;
	layout.shape = {MAX_NATIVE_WINDOW_POINTS * 2 + 1};
	layout.axes = {"point"};
	layout.strides = {1};
	layout.flattened = true;
	layout.point_axis = 0;
	NativeWindowCursor cursor(layout);
	Require(cursor.WindowUpperBound() == 3, "window upper bound rounds a long native axis up by bounded chunks");
	NativeWindow window;
	const std::vector<std::uint64_t> expected_counts = {MAX_NATIVE_WINDOW_POINTS, MAX_NATIVE_WINDOW_POINTS, 1};
	for (const auto expected : expected_counts) {
		Require(cursor.Next(window), "long axis emits every expected descriptor");
		Require(window.Count() == expected, "long-axis descriptors never exceed 65536 points");
	}
	Require(!cursor.Next(window), "long-axis cursor reports exhaustion after its last descriptor");
}

void TestSpatialIntervalsWidenAtTheSharedBudget() {
	constexpr std::uint64_t nx = 70000;
	RegularGrid grid(nx, 1, 0.0, 0.0, 1.0, 180.0, GridStorageOrder::LongitudeFastest);
	SpatialLayout layout;
	layout.shape = {nx};
	layout.axes = {"point"};
	layout.strides = {1};
	layout.flattened = true;
	layout.point_axis = 0;
	layout.order = GridStorageOrder::LongitudeFastest;
	SpatialPredicate predicate;
	predicate.necessary_conditions = {{SpatialAxis::Longitude, SpatialComparison::Equal, 0.0}};
	const auto selection = BuildSpatialSelection(grid, layout, predicate);
	Require(selection.budget_fallback && selection.residual_filter_retained,
	        "fragmented longitude selection widens while retaining the engine filter");
	Require(selection.mode == SpatialSelectionMode::Full && selection.candidate_rows == nx,
	        "budget fallback reports the complete conservative candidate range");
	Require(std::find(selection.fallback_reasons.begin(), selection.fallback_reasons.end(),
	                   "axis_interval_payload_limit") != selection.fallback_reasons.end(),
	        "interval budget fallback has a stable diagnostic reason");
}

void TestLongitudeIntervalUnionAndSeam() {
	RegularGrid grid(3, 2, 10.0, 179.0, 1.0, 1.0, GridStorageOrder::Separate);
	const auto layout = SeparateLayout({2, 3}, 0, 1);
	SpatialPredicate predicate;
	predicate.longitude_union = {{179.0, 180.0, true, false}, {-180.0, -179.0, true, false}};
	const auto selection = BuildSpatialSelection(grid, layout, predicate);
	Require(selection.mode == SpatialSelectionMode::Restricted && selection.candidate_rows == 4,
	        "two normalized longitude intervals select only their seam-adjacent columns");
	SpatialBatchCursor cursor(grid, layout, selection);
	SpatialBatch batch;
	std::vector<std::uint64_t> positions;
	while (cursor.Next(8, batch)) {
		positions.insert(positions.end(), batch.logical_positions.begin(), batch.logical_positions.end());
	}
	Require(positions == std::vector<std::uint64_t>({0, 1, 3, 4}),
	        "longitude union preserves row-major source positions and excludes the strict upper boundary");
}

void TestSpatialPredicateContradictionsAreProvenWithoutCoordinateTraversal() {
	SpatialPredicate predicate;
	predicate.necessary_conditions = {{SpatialAxis::Latitude, SpatialComparison::Greater, 11.0},
	                                  {SpatialAxis::Latitude, SpatialComparison::LessEqual, 11.0}};
	Require(SpatialPredicateProvesEmpty(predicate),
	        "crossing strict and inclusive latitude bounds prove an empty candidate set");

	predicate.necessary_conditions = {{SpatialAxis::Latitude, SpatialComparison::GreaterEqual, 11.0},
	                                  {SpatialAxis::Latitude, SpatialComparison::LessEqual, 11.0}};
	Require(!SpatialPredicateProvesEmpty(predicate), "a closed singleton latitude interval remains possible");

	predicate.necessary_conditions = {{SpatialAxis::Longitude, SpatialComparison::Greater, 180.0}};
	Require(SpatialPredicateProvesEmpty(predicate),
	        "normalized longitude domain proves predicates strictly beyond 180 degrees empty");

	predicate.necessary_conditions.clear();
	predicate.longitude_union = {{200.0, 210.0, true, true}, {220.0, 230.0, true, true}};
	Require(SpatialPredicateProvesEmpty(predicate),
	        "a safe longitude union outside the normalized domain proves an empty candidate set");

	predicate.longitude_union = {{-180.0, -170.0, true, false}, {170.0, 180.0, true, false}};
	Require(!SpatialPredicateProvesEmpty(predicate), "a seam-spanning longitude union remains possible");
	predicate.necessary_conditions = {{SpatialAxis::Longitude, SpatialComparison::GreaterEqual, 10.0},
	                                  {SpatialAxis::Longitude, SpatialComparison::LessEqual, 160.0}};
	Require(SpatialPredicateProvesEmpty(predicate),
	        "necessary longitude bounds are intersected with both safe union branches");
}

void TestSpatialPredicateDetectsOnlyProvenFullDomainConditions() {
	SpatialPredicate predicate;
	predicate.necessary_conditions = {{SpatialAxis::Latitude, SpatialComparison::GreaterEqual, -90.0},
	                                  {SpatialAxis::Latitude, SpatialComparison::LessEqual, 90.0},
	                                  {SpatialAxis::Longitude, SpatialComparison::GreaterEqual, -180.0},
	                                  {SpatialAxis::Longitude, SpatialComparison::Less, 180.0}};
	Require(SpatialPredicateCoversGeographicDomain(predicate),
	        "closed latitude and half-open longitude bounds cover the guaranteed geographic domain");

	predicate.necessary_conditions.front().comparison = SpatialComparison::Greater;
	Require(!SpatialPredicateCoversGeographicDomain(predicate),
	        "strict latitude bounds do not claim coverage of a valid domain endpoint");

	predicate.necessary_conditions = {{SpatialAxis::Latitude, SpatialComparison::Greater, -91.0},
	                                  {SpatialAxis::Latitude, SpatialComparison::Less, 91.0},
	                                  {SpatialAxis::Longitude, SpatialComparison::GreaterEqual, -181.0},
	                                  {SpatialAxis::Longitude, SpatialComparison::Less, 181.0}};
	Require(SpatialPredicateCoversGeographicDomain(predicate),
	        "bounds beyond the guaranteed domain safely cover all output coordinates");

	predicate.necessary_conditions.clear();
	predicate.longitude_union = {{-180.0, 0.0, true, false}, {0.0, 180.0, true, false}};
	Require(SpatialPredicateCoversGeographicDomain(predicate),
	        "adjacent longitude intervals cover the full normalized domain when one includes their shared point");
	predicate.longitude_union[1].lower_inclusive = false;
	Require(!SpatialPredicateCoversGeographicDomain(predicate),
	        "a missing shared longitude endpoint prevents a full-domain shortcut");
}

void TestFlattenedSelectorRangeBudgetBoundary() {
	auto make_layout = [](std::uint64_t point_count) {
		SpatialLayout layout;
		layout.shape = {point_count};
		layout.axes = {"point"};
		layout.strides = {1};
		layout.flattened = true;
		layout.point_axis = 0;
		layout.order = GridStorageOrder::LongitudeFastest;
		return layout;
	};
	auto make_predicate = [] {
		SpatialPredicate predicate;
		predicate.necessary_conditions = {{SpatialAxis::Longitude, SpatialComparison::Equal, 0.0}};
		return predicate;
	};
	auto collect_positions = [](SpatialBatchCursor &cursor) {
		SpatialBatch batch;
		std::vector<std::uint64_t> positions;
		while (cursor.Next(STANDARD_VECTOR_SIZE, batch)) {
			positions.insert(positions.end(), batch.logical_positions.begin(), batch.logical_positions.end());
		}
		return positions;
	};

	RegularGrid exact_grid(4, MAX_SPATIAL_SELECTOR_RANGES, 0.0, 0.0,
	                       89.0 / static_cast<double>(MAX_SPATIAL_SELECTOR_RANGES - 1), 1.0,
	                       GridStorageOrder::LongitudeFastest);
	const auto exact_layout = make_layout(exact_grid.PointCount());
	auto exact_selection = BuildSpatialSelection(exact_grid, exact_layout, make_predicate());
	SpatialBatchCursor exact_cursor(exact_grid, exact_layout, exact_selection);
	const auto exact_positions = collect_positions(exact_cursor);
	Require(exact_cursor.Selection().mode == SpatialSelectionMode::Restricted &&
	            !exact_cursor.Selection().budget_fallback,
	        "exactly 4096 flattened source ranges remain selectable");
	Require(exact_positions.size() == MAX_SPATIAL_SELECTOR_RANGES,
	        "4096-range selection emits only matching source positions");
	for (std::uint64_t row = 0; row < MAX_SPATIAL_SELECTOR_RANGES; row++) {
		Require(exact_positions[row] == row * exact_grid.Nx(),
		        "bounded flattened selection preserves one longitude-zero position per row");
	}

	RegularGrid over_grid(4, MAX_SPATIAL_SELECTOR_RANGES + 1, 0.0, 0.0,
	                      89.0 / static_cast<double>(MAX_SPATIAL_SELECTOR_RANGES), 1.0,
	                      GridStorageOrder::LongitudeFastest);
	const auto over_layout = make_layout(over_grid.PointCount());
	auto over_selection = BuildSpatialSelection(over_grid, over_layout, make_predicate());
	SpatialBatchCursor over_cursor(over_grid, over_layout, over_selection);
	Require(over_cursor.Selection().mode == SpatialSelectionMode::Fallback &&
	            over_cursor.Selection().budget_fallback &&
	            over_cursor.Selection().candidate_rows == over_grid.PointCount(),
	        "4097 flattened source ranges widen before selector storage exceeds budget");
	Require(std::find(over_cursor.Selection().fallback_reasons.begin(),
	                   over_cursor.Selection().fallback_reasons.end(), "flattened_spatial_range_limit") !=
	            over_cursor.Selection().fallback_reasons.end(),
	        "flattened selector budget fallback has a stable diagnostic");
	const auto widened_positions = collect_positions(over_cursor);
	Require(widened_positions.size() == over_grid.PointCount() &&
	            widened_positions.front() == 0 && widened_positions.back() == over_grid.PointCount() - 1,
	        "widened flattened selection conservatively emits every source position for residual filtering");
}

} // namespace

int main() {
	try {
		TestRestrictedCursorPreservesPositionsAndSegments();
		TestNegativeStepAndStrictBoundaries();
		TestRestrictedCursorSkipsExcludedMultidimensionalRanges();
		TestEmptyAndFlattenedLayouts();
		TestLayoutProductOverflowFailsClosed();
		TestNativeWindowDescriptorChecksExtentAndPayload();
		TestNativeWindowCursorUsesFastestSpatialAxisAndRealStrides();
		TestNativeWindowCursorSplitsLongSpatialAxes();
		TestSpatialIntervalsWidenAtTheSharedBudget();
		TestLongitudeIntervalUnionAndSeam();
		TestSpatialPredicateContradictionsAreProvenWithoutCoordinateTraversal();
		TestSpatialPredicateDetectsOnlyProvenFullDomainConditions();
		TestFlattenedSelectorRangeBudgetBoundary();
		std::cout << "spatial selection checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "spatial selection checks failed: " << error.what() << '\n';
		return 1;
	}
}
