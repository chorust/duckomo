#include "duckomo/grid_selection.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "duckomo/gaussian_grid.hpp"
#include "duckomo/grid_definition.hpp"
#include "duckomo/spatial_layout.hpp"

namespace {
using namespace duckdb::duckomo;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

SpatialLayout FlattenedLayout(std::uint64_t count) {
	SpatialLayout layout;
	layout.shape = {count};
	layout.axes = {"point"};
	layout.strides = {1};
	layout.point_axis = 0;
	layout.flattened = true;
	layout.order = GridStorageOrder::LongitudeFastest;
	return layout;
}

bool Matches(const SpatialPredicate &predicate, const GridCoordinate &point) {
	for (const auto &condition : predicate.necessary_conditions) {
		const auto value = condition.axis == SpatialAxis::Latitude ? point.latitude : point.longitude;
		switch (condition.comparison) {
		case SpatialComparison::Equal: if (value != condition.constant) return false; break;
		case SpatialComparison::Less: if (!(value < condition.constant)) return false; break;
		case SpatialComparison::LessEqual: if (!(value <= condition.constant)) return false; break;
		case SpatialComparison::Greater: if (!(value > condition.constant)) return false; break;
		case SpatialComparison::GreaterEqual: if (!(value >= condition.constant)) return false; break;
		}
	}
	if (!predicate.longitude_union.empty()) {
		bool matched = false;
		for (const auto &interval : predicate.longitude_union) {
			const bool lower = interval.lower_inclusive ? point.longitude >= interval.lower : point.longitude > interval.lower;
			const bool upper = interval.upper_inclusive ? point.longitude <= interval.upper : point.longitude < interval.upper;
			matched = matched || (lower && upper);
		}
		if (!matched) return false;
	}
	return true;
}

bool Contains(const std::vector<AxisRange> &ranges, std::uint64_t point) {
	for (const auto &range : ranges) {
		if (point >= range.begin && point < range.end) return true;
	}
	return false;
}

void TestProjectedCurveAndStrictNextafterBoundaries() {
	constexpr std::uint64_t NX = 200;
	constexpr std::uint64_t NY = 60;
	constexpr std::uint64_t COUNT = NX * NY;
	const GridDefinition definition("rotated_test_v1", GridEarth{}, GridNumericPolicy::Float64V1,
	    ProjectedGrid(RotatedLatLonParameters{NX, NY, -180.0, -87.0, 360.0 / NX, 174.0 / (NY - 1), 34.0, 18.0, 180.0,
	                                          GridStorageOrder::LongitudeFastest}));
	auto layout = FlattenedLayout(COUNT);
	layout.geometry = SpatialLayoutGeometry::Projected;
	const auto window = NativeWindow::Create(0, 0, COUNT, layout.shape, {0});
	GridCoordinate target;
	bool found_seam_point = false;
	for (std::uint64_t point = 0; point < COUNT; point++) {
		const auto coordinate = layout.Coordinate(definition, point);
		if (coordinate.longitude >= 170.0 || coordinate.longitude <= -170.0) {
			target = coordinate;
			found_seam_point = true;
			break;
		}
	}
	Require(found_seam_point, "global rotated fixture contains a point adjacent to the longitude seam");
	const auto lower_edge = std::nextafter(target.latitude, -INFINITY);
	SpatialPredicate predicate;
	predicate.necessary_conditions = {{SpatialAxis::Latitude, SpatialComparison::Greater, lower_edge},
	                                  {SpatialAxis::Latitude, SpatialComparison::LessEqual, target.latitude}};
	predicate.longitude_union = {{170.0, 180.0, true, false}, {-180.0, -170.0, true, false}};
	std::uint64_t evaluations = 0;
	auto preflight = PreflightNativeWindow(predicate, window,
	    [&definition, &layout](const NativeWindow &candidate, std::uint64_t offset) {
		    return layout.Coordinate(definition, candidate.Begin() + offset);
	    }, {}, evaluations);
	Require(evaluations == preflight.coordinate_evaluations,
	        "curve preflight coordinate evaluation totals agree");
	Require(!preflight.budget_fallback && !preflight.whole_window,
	        "a normal curved-grid seam selection remains represented by exact windows");
	std::uint64_t exact_matches = 0;
	for (std::uint64_t point = 0; point < COUNT; point++) {
		const auto coordinate = definition.Coordinate(NativePointPosition{point});
		const bool expected = Matches(predicate, coordinate);
		const bool candidate = Contains(preflight.ranges, point);
		Require(!expected || candidate, "curved preflight never excludes a point accepted by exact coordinates");
		Require(expected == candidate, "representable curved selection contains no false positives");
		if (expected) exact_matches++;
	}
	Require(exact_matches > 0, "the rotated fixture selects an exact nextafter boundary at the seam");

	const std::uint64_t middle_point = (NY / 2) * NX + NX / 2;
	const auto middle = layout.Coordinate(definition, middle_point);
	SpatialPredicate middle_window;
	middle_window.necessary_conditions = {
	    {SpatialAxis::Latitude, SpatialComparison::GreaterEqual, std::nextafter(middle.latitude, -INFINITY)},
	    {SpatialAxis::Latitude, SpatialComparison::LessEqual, std::nextafter(middle.latitude, INFINITY)},
	    {SpatialAxis::Longitude, SpatialComparison::GreaterEqual, std::nextafter(middle.longitude, -INFINITY)},
	    {SpatialAxis::Longitude, SpatialComparison::LessEqual, std::nextafter(middle.longitude, INFINITY)}};
	const std::vector<std::uint64_t> corners = {0, NX - 1, (NY - 1) * NX, COUNT - 1};
	for (const auto corner : corners) {
		Require(!Matches(middle_window, layout.Coordinate(definition, corner)),
		        "all four projected domain corners fall outside a narrow interior coordinate window");
	}
	Require(Matches(middle_window, middle), "an interior projected point intersects the narrow coordinate window");
	evaluations = 0;
	const auto interior_preflight = PreflightNativeWindow(middle_window, window,
	    [&definition, &layout](const NativeWindow &candidate, std::uint64_t offset) {
		    return layout.Coordinate(definition, candidate.Begin() + offset);
	    }, {}, evaluations);
	Require(Contains(interior_preflight.ranges, middle_point),
	        "projected native selection preserves a middle intersection missed by domain corners");
	for (std::uint64_t point = 0; point < COUNT; point++) {
		Require(Contains(interior_preflight.ranges, point) == Matches(middle_window, layout.Coordinate(definition, point)),
		        "projected interior selection agrees with exact coordinates for every native position");
	}
}

void TestGaussianRegionMapsLocalOffsets() {
	const GridDefinition definition("gaussian_region_test_v1", GridEarth{GridEarthKind::Wgs84Source},
	    GridNumericPolicy::Float64V1,
	    GaussianGrid(1, "explicit_v1", {{60.0, 6, 0.0, 60.0}, {-60.0, 6, 0.0, 60.0}},
	                 {{0, 3, 3}, {1, 0, 6}, {0, 0, 3}}));
	auto layout = FlattenedLayout(12);
	layout.geometry = SpatialLayoutGeometry::Gaussian;
	const auto window = NativeWindow::Create(0, 0, 12, layout.shape, {0});
	SpatialPredicate predicate;
	predicate.necessary_conditions = {{SpatialAxis::Latitude, SpatialComparison::GreaterEqual, 60.0}};
	predicate.longitude_union = {{-180.0, -119.0, true, true}, {119.0, 180.0, true, true}};
	std::uint64_t evaluations = 0;
	const auto preflight = PreflightNativeWindow(predicate, window,
	    [&definition, &layout](const NativeWindow &candidate, std::uint64_t offset) {
		    return layout.Coordinate(definition, candidate.Begin() + offset);
	    }, {}, evaluations);
	std::vector<std::uint64_t> selected;
	for (std::uint64_t point = 0; point < 12; point++) {
		const auto coordinate = definition.Coordinate(NativePointPosition{point});
		const bool expected = Matches(predicate, coordinate);
		Require(expected == Contains(preflight.ranges, point),
		        "Gaussian region selection uses local order while coordinates resolve parent segments");
		if (expected) selected.push_back(point);
	}
	Require(selected == std::vector<std::uint64_t>({0, 1, 11}),
	        "Gaussian seam selection follows the declared non-contiguous local segments");
}

NativeWindowPreflight AlternatingSelection(std::uint64_t count, std::uint64_t &evaluations) {
	auto layout = FlattenedLayout(count);
	const auto window = NativeWindow::Create(0, 0, count, layout.shape, {0});
	SpatialPredicate predicate;
	predicate.necessary_conditions = {{SpatialAxis::Latitude, SpatialComparison::Equal, 1.0}};
	return PreflightNativeWindow(predicate, window,
	    [](const NativeWindow &, std::uint64_t offset) {
		    return GridCoordinate{offset % 2 == 0 ? 1.0 : 0.0, 0.0};
	    }, {}, evaluations);
}

void TestRangeBudgetAndCancellation() {
	std::uint64_t evaluations = 0;
	auto exact = AlternatingSelection(8192, evaluations);
	Require(!exact.whole_window && !exact.budget_fallback && exact.ranges.size() == 4096,
	        "exactly 4096 native intervals fit the per-window selector budget");
	evaluations = 0;
	auto overflow = AlternatingSelection(8194, evaluations);
	Require(overflow.whole_window && overflow.budget_fallback && overflow.ranges.empty(),
	        "4097 native intervals widen the complete window before any selection is emitted");
	Require(evaluations == 8194,
	        "overflow widens immediately after the bounded range-count pass");

	auto layout = FlattenedLayout(2000);
	const auto window = NativeWindow::Create(0, 0, 2000, layout.shape, {0});
	SpatialPredicate predicate;
	predicate.necessary_conditions = {{SpatialAxis::Latitude, SpatialComparison::Greater, -1.0}};
	std::uint64_t cancellation_evaluations = 0;
	std::uint64_t checks = 0;
	bool cancelled = false;
	try {
		(void)PreflightNativeWindow(predicate, window,
		    [](const NativeWindow &, std::uint64_t) { return GridCoordinate{0.0, 0.0}; },
		    [&checks]() { if (++checks == 2) throw std::runtime_error("cancelled"); }, cancellation_evaluations);
	} catch (const std::runtime_error &error) {
		cancelled = std::string(error.what()) == "cancelled";
	}
	Require(cancelled && cancellation_evaluations == 256,
	        "coordinate preflight checks cancellation every 256 evaluations");
}

void TestSinglePointSeparateLayoutUsesDeterministicTieBreak() {
	SpatialLayout layout;
	layout.shape = {1, 1};
	layout.axes = {"latitude", "longitude"};
	layout.strides = {1, 1};
	layout.latitude_axis = 0;
	layout.longitude_axis = 1;
	NativeWindowCursor cursor(layout);
	Require(cursor.SpatialAxis() == 1,
	        "equal fastest-axis strides on a 1x1 separate grid use the deterministic longitude tie-break");
	NativeWindow window;
	Require(cursor.Next(window) && window.Count() == 1 && window.LogicalPosition(layout, 0) == 0,
	        "single-point layout produces its one source position exactly once");
	Require(!cursor.Next(window), "single-point native window cursor exhausts cleanly");
}

} // namespace

int main() {
	try {
		TestProjectedCurveAndStrictNextafterBoundaries();
		TestGaussianRegionMapsLocalOffsets();
		TestRangeBudgetAndCancellation();
		TestSinglePointSeparateLayoutUsesDeterministicTieBreak();
		std::cout << "grid selection checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "grid selection checks failed: " << error.what() << '\n';
		return 1;
	}
}
