#include "duckomo/axis_selection.hpp"
#include "duckomo/spatial_selection.hpp"
#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &) {
}
} // namespace duckdb

namespace {

using namespace duckdb;
using namespace duckdb::duckomo;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

SemanticAxis TimeAxis() {
	SemanticAxis axis;
	axis.kind = SemanticAxisKind::Time;
	axis.axis_name = "time";
	axis.axis_index = 0;
	axis.axis_length = 2;
	axis.stride = 3;
	axis.output_type = LogicalType::TIMESTAMP;
	axis.timestamps = {timestamp_t(0), timestamp_t(3600LL * 1000000)};
	return axis;
}

SemanticAxis MemberAxis() {
	SemanticAxis axis;
	axis.kind = SemanticAxisKind::Member;
	axis.axis_name = "member";
	axis.axis_index = 1;
	axis.axis_length = 3;
	axis.stride = 1;
	axis.output_type = LogicalType::BIGINT;
	axis.integer_members = {10, 20, 20};
	return axis;
}

void VerifyCombinedPredicateAndSegments() {
	const SemanticAxes axes = {TimeAxis(), MemberAxis()};
	AxisPredicate predicate;
	predicate.necessary_conditions.push_back({0, AxisComparison::GreaterEqual,
	                                         Value::TIMESTAMP(timestamp_t(3600LL * 1000000))});
	predicate.necessary_conditions.push_back({1, AxisComparison::Equal, Value::BIGINT(20)});
	AxisSelectionCursor cursor({2, 3}, axes, predicate);
	Require(cursor.HasConstraints() && !cursor.IsEmpty(), "combined predicate should be non-empty");
	Require(cursor.Contains(4) && cursor.Contains(5) && !cursor.Contains(3),
	        "membership checks should recognize selected positions independently of traversal");
	std::vector<std::uint64_t> positions;
	Require(cursor.Next(1, positions) == 1 && positions == std::vector<std::uint64_t>{4},
	        "first bounded cursor batch should preserve row-major ordinal 4");
	Require(cursor.Next(2, positions) == 1 && positions == std::vector<std::uint64_t>{5},
	        "duplicate member coordinates should retain their distinct source positions");
	Require(cursor.Next(2, positions) == 0 && cursor.IsExhausted(), "cursor should exhaust exactly once");
	Require(cursor.Contains(4), "membership checks should still work after cursor exhaustion");

	const auto segments = BuildSelectedBatchSegments({2, 3}, {1, 2, 4, 5});
	Require(segments.size() == 2, "sparse row positions should form two contiguous read segments");
	Require(segments[0].linear_index == 1 && segments[0].count == 2 && segments[0].batch_offset == 0,
	        "first selected read segment should preserve output offsets");
	Require(segments[1].linear_index == 4 && segments[1].count == 2 && segments[1].batch_offset == 2,
	        "second selected read segment should preserve output offsets");
}

void VerifyContradictionBecomesEmpty() {
	AxisPredicate predicate;
	predicate.necessary_conditions.push_back({1, AxisComparison::Equal, Value::BIGINT(99)});
	AxisSelectionCursor cursor({2, 3}, {TimeAxis(), MemberAxis()}, predicate);
	Require(cursor.HasConstraints() && cursor.IsEmpty(), "unmatched equality should prove an empty selection");
	std::vector<std::uint64_t> positions;
	Require(cursor.Next(8, positions) == 0, "empty selection must emit no positions");
}

void VerifyUnconstrainedAxesCoverEachPositionOnce() {
	AxisSelectionCursor cursor({2, 3}, {TimeAxis(), MemberAxis()}, {});
	std::vector<std::uint64_t> positions;
	std::vector<std::uint64_t> all;
	while (cursor.Next(2, positions) != 0) all.insert(all.end(), positions.begin(), positions.end());
	Require(all == std::vector<std::uint64_t>({0, 1, 2, 3, 4, 5}),
	        "unconstrained cursor should cover every row-major position once");
}

SemanticAxis FragmentedLevelAxis(idx_t axis_index, std::uint64_t length) {
	SemanticAxis axis;
	axis.kind = SemanticAxisKind::Level;
	axis.axis_name = axis_index == 0 ? "level_a" : "level_b";
	axis.axis_index = axis_index;
	axis.axis_length = length;
	axis.stride = axis_index == 0 ? length : 1;
	axis.output_type = LogicalType::DOUBLE;
	axis.numbers.reserve(static_cast<std::size_t>(length));
	for (std::uint64_t index = 0; index < length; index++) axis.numbers.push_back(index % 2 == 0 ? 0.0 : 1.0);
	return axis;
}

void VerifyGlobalIntervalBudgetWidensWithoutDroppingRows() {
	constexpr std::uint64_t axis_length = 40000;
	const SemanticAxes axes = {FragmentedLevelAxis(0, axis_length), FragmentedLevelAxis(1, axis_length)};
	AxisPredicate predicate;
	predicate.necessary_conditions.push_back({0, AxisComparison::Equal, Value::DOUBLE(1.0)});
	predicate.necessary_conditions.push_back({1, AxisComparison::Equal, Value::DOUBLE(1.0)});
	AxisSelectionCursor cursor({axis_length, axis_length}, axes, predicate);
	Require(cursor.BudgetFallback(), "a second fragmented axis widens when the shared interval budget is exhausted");
	Require(cursor.IntervalPayloadBytes() <= MAX_SELECTION_INTERVAL_BYTES,
	        "retained semantic intervals stay within the shared payload budget");
	Require(cursor.CandidateCount() == (axis_length / 2) * axis_length,
	        "widening a later axis keeps a conservative candidate superset");
	Require(cursor.Contains(axis_length), "the first matching source position remains selected");
	Require(cursor.Contains(axis_length + 1), "the widened axis retains candidates for residual WHERE evaluation");
	Require(!cursor.Contains(0), "the first axis predicate remains applied after widening the second axis");
	std::vector<std::uint64_t> positions;
	Require(cursor.Next(1, positions) == 1 && positions == std::vector<std::uint64_t>{axis_length},
	        "cursor traversal remains source ordered after interval widening");
	try {
		(void)cursor.Next(MAX_NATIVE_WINDOW_POINTS + 1, positions);
	} catch (const ReaderError &error) {
		Require(error.Code() == ReaderErrorCode::InvalidSelection, "oversized axis batches fail with InvalidSelection");
		return;
	}
	throw std::runtime_error("axis cursor must reject a batch beyond STANDARD_VECTOR_SIZE");
}

void VerifySelectedSegmentsRejectInvalidPositions() {
	try {
		(void)BuildSelectedBatchSegments({2, 3}, {2, 1});
	} catch (const ReaderError &error) {
		Require(error.Code() == ReaderErrorCode::InvalidSelection, "unsorted positions fail with InvalidSelection");
		return;
	}
	throw std::runtime_error("batch segment builder must reject unsorted source positions");
}

void VerifyRankEightBatchMappingsStayWithinBudget() {
	const std::vector<std::uint64_t> shape = {1, 1, 1, 1, 1, 1, 1, 1024};
	const auto limit = MaxBatchPositionCount(shape.size());
	Require(limit > 0 && limit < STANDARD_VECTOR_SIZE,
	        "rank-eight fragmented batches shrink below the standard vector size");
	Require(WorstCaseBatchMappingBytes(shape.size(), limit) <= MAX_BATCH_MAPPING_BYTES,
	        "the configured rank-eight batch bound fits its 64 KiB payload budget");
	Require(WorstCaseBatchMappingBytes(shape.size(), limit + 1) > MAX_BATCH_MAPPING_BYTES,
	        "the rank-eight batch limit is maximal for its worst-case mapping bound");

	std::vector<std::uint64_t> positions;
	positions.reserve(limit);
	for (std::uint64_t index = 0; index < limit; index++) positions.push_back(index * 2);
	const auto segments = BuildSelectedBatchSegments(shape, positions);
	Require(segments.size() == limit,
	        "alternating high-rank positions produce one bounded decode segment per position");
	Require(segments.front().linear_index == 0 && segments.front().batch_offset == 0 &&
	             segments.back().linear_index == (limit - 1) * 2 && segments.back().batch_offset == limit - 1,
	        "bounded segments retain source positions and output mappings");

	positions.push_back(limit * 2);
	try {
		(void)BuildSelectedBatchSegments(shape, positions);
	} catch (const ReaderError &error) {
		Require(error.Code() == ReaderErrorCode::InvalidSelection,
		        "an oversized mapping fails before allocating its segments");
		return;
	}
	throw std::runtime_error("an over-budget batch mapping must be rejected before segment allocation");
}

void VerifyNativeWindowScheduleUsesSelectedFixedAxes() {
	SemanticAxis time = TimeAxis();
	time.stride = 18;
	SemanticAxis member = MemberAxis();
	member.axis_index = 2;
	const SemanticAxes axes = {time, member};
	AxisPredicate predicate;
	predicate.necessary_conditions.push_back({0, AxisComparison::GreaterEqual,
	                                         Value::TIMESTAMP(timestamp_t(3600LL * 1000000))});
	predicate.necessary_conditions.push_back({2, AxisComparison::Equal, Value::BIGINT(20)});
	AxisSelectionCursor axis_selection({2, 6, 3}, axes, predicate);

	SpatialLayout layout;
	layout.shape = {2, 6, 3};
	layout.axes = {"time", "point", "member"};
	layout.strides = {18, 3, 1};
	layout.non_spatial_axes = {0, 2};
	layout.point_axis = 1;
	layout.flattened = true;
	NativeWindowCursor windows(layout);
	Require(windows.WindowUpperBound() == 6 && windows.WindowUpperBound(axis_selection) == 2,
	        "selected semantic coordinates bound task concurrency by eligible native windows");

	NativeWindow window;
	std::vector<std::uint64_t> positions;
	while (windows.Next(window, axis_selection)) {
		Require(window.FixedAxisIndices()[0] == 1,
		        "window cursor jumps directly to the selected time index");
		Require(window.FixedAxisIndices()[2] == 1 || window.FixedAxisIndices()[2] == 2,
		        "duplicate semantic member values retain both original source positions");
		for (std::uint64_t offset = 0; offset < window.Count(); offset++) {
			const auto logical_position = window.LogicalPosition(layout, offset);
			Require(axis_selection.Contains(logical_position),
			        "fixed-axis combinations emitted by the window cursor satisfy semantic selection");
			positions.push_back(logical_position);
		}
	}
	std::sort(positions.begin(), positions.end());
	Require(positions == std::vector<std::uint64_t>({19, 20, 22, 23, 25, 26, 28, 29, 31, 32, 34, 35}),
	        "window scheduling preserves interleaved source positions without duplicates");
}

void VerifyValueWindowsReadFasterNonSpatialAxesContiguously() {
	for (const bool flattened : {true, false}) {
		SpatialLayout layout;
		layout.shape = flattened ? std::vector<std::uint64_t>{6, 4} : std::vector<std::uint64_t>{2, 3, 4};
		layout.strides = flattened ? std::vector<std::uint64_t>{4, 1} : std::vector<std::uint64_t>{12, 4, 1};
		layout.axes = flattened ? std::vector<std::string>{"point", "time"}
		                        : std::vector<std::string>{"y", "x", "time"};
		layout.point_axis = 0;
		layout.latitude_axis = 0;
		layout.longitude_axis = 1;
		layout.flattened = flattened;
		layout.non_spatial_axes = {layout.shape.size() - 1};
		AxisSelectionCursor selection(layout.shape, {}, {});
		NativeWindowCursor windows(layout, true);
		Require(windows.SpatialAxis() == layout.shape.size() - 1 && windows.WindowUpperBound(selection) == 6,
		        "value windows vary the fastest storage axis even when it is non-spatial");
		NativeWindow window;
		std::uint64_t expected = 0;
		while (windows.Next(window, selection)) {
			std::vector<std::uint64_t> positions;
			for (std::uint64_t offset = 0; offset < window.Count(); offset++) {
				const auto position = window.LogicalPosition(layout, offset);
				Require(position == expected++, "time windows preserve contiguous source positions without duplicates");
				positions.push_back(position);
			}
			const auto segments = BuildSelectedBatchSegments(layout.shape, positions);
			Require(segments.size() == 1 && segments[0].count == 4,
			        "one spatial point's time coordinates form one contiguous decoder selection");
		}
		Require(expected == 24, "contiguous time windows cover every source position");
	}
}

void VerifySinglePointCreatesOneWindowPerTimeIndex() {
	SemanticAxis time;
	time.kind = SemanticAxisKind::Time;
	time.axis_name = "time";
	time.axis_index = 0;
	time.axis_length = 4;
	time.stride = 1;
	time.output_type = LogicalType::TIMESTAMP;
	time.timestamps = {timestamp_t(0), timestamp_t(3600LL * 1000000), timestamp_t(7200LL * 1000000),
	                   timestamp_t(10800LL * 1000000)};

	SpatialLayout layout;
	layout.shape = {4, 1};
	layout.axes = {"time", "point"};
	layout.strides = {1, 1};
	layout.non_spatial_axes = {0};
	layout.point_axis = 1;
	layout.geometry = SpatialLayoutGeometry::Gaussian;
	layout.flattened = true;
	AxisSelectionCursor axis_selection(layout.shape, {time}, {});
	NativeWindowCursor windows(layout);
	Require(windows.WindowUpperBound() == 4 && windows.WindowUpperBound(axis_selection) == 4,
	        "a [point=1,time=4] source has four eligible native windows");

	NativeWindow window;
	for (std::uint64_t time_index = 0; time_index < 4; time_index++) {
		Require(windows.Next(window, axis_selection), "each time coordinate should schedule its point window");
		Require(window.Count() == 1 && window.FixedAxisIndices()[0] == time_index,
		        "single-point windows preserve their independent time positions");
		Require(window.LogicalPosition(layout, 0) == time_index,
		        "single-point windows retain the row-major logical source position");
	}
	Require(!windows.Next(window, axis_selection), "single-point time windows exhaust after four positions");
}

} // namespace

int main() {
	try {
		VerifyCombinedPredicateAndSegments();
		VerifyContradictionBecomesEmpty();
		VerifyUnconstrainedAxesCoverEachPositionOnce();
		VerifyGlobalIntervalBudgetWidensWithoutDroppingRows();
		VerifySelectedSegmentsRejectInvalidPositions();
		VerifyRankEightBatchMappingsStayWithinBudget();
		VerifyNativeWindowScheduleUsesSelectedFixedAxes();
		VerifyValueWindowsReadFasterNonSpatialAxesContiguously();
		VerifySinglePointCreatesOneWindowPerTimeIndex();
		std::cout << "semantic axis selection checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
