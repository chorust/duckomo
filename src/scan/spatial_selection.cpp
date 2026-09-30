#include "duckomo/spatial_selection.hpp"

#include <algorithm>
#include <limits>
#include <numeric>

#include "duckomo/om_reader.hpp"

namespace duckdb {
namespace duckomo {
namespace {

std::uint64_t CountRanges(const std::vector<AxisRange> &ranges) {
	std::uint64_t count = 0;
	for (const auto &range : ranges) {
		if (range.begin >= range.end) {
			continue;
		}
		const auto length = range.end - range.begin;
		if (count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - length) {
			throw ReaderError(ReaderErrorCode::ShapeOverflow, "spatial axis selection exceeds the signed 64-bit limit");
		}
		count += length;
	}
	return count;
}

bool MatchesConstraints(double coordinate, SpatialAxis axis, const std::vector<SpatialConstraint> &constraints) {
	for (const auto &constraint : constraints) {
		if (constraint.axis != axis) {
			continue;
		}
		switch (constraint.comparison) {
		case SpatialComparison::Equal:
			if (!(coordinate == constraint.constant)) return false;
			break;
		case SpatialComparison::Less:
			if (!(coordinate < constraint.constant)) return false;
			break;
		case SpatialComparison::LessEqual:
			if (!(coordinate <= constraint.constant)) return false;
			break;
		case SpatialComparison::Greater:
			if (!(coordinate > constraint.constant)) return false;
			break;
		case SpatialComparison::GreaterEqual:
			if (!(coordinate >= constraint.constant)) return false;
			break;
		}
	}
	return true;
}

template <class COORDINATE>
std::vector<AxisRange> BuildAxisRanges(std::uint64_t extent, const std::vector<SpatialConstraint> &constraints,
	                                  SpatialAxis axis, COORDINATE coordinate) {
	std::vector<AxisRange> ranges;
	if (constraints.empty()) {
		ranges.push_back({0, extent});
		return ranges;
	}
	bool in_range = false;
	std::uint64_t range_begin = 0;
	for (std::uint64_t index = 0; index < extent; index++) {
		const bool matches = MatchesConstraints(coordinate(index), axis, constraints);
		if (matches && !in_range) {
			range_begin = index;
			in_range = true;
		} else if (!matches && in_range) {
			ranges.push_back({range_begin, index});
			in_range = false;
		}
	}
	if (in_range) {
		ranges.push_back({range_begin, extent});
	}
	return ranges;
}

void AppendMergedRange(std::vector<AxisRange> &ranges, std::uint64_t begin, std::uint64_t end) {
	if (begin >= end) {
		return;
	}
	if (!ranges.empty() && begin <= ranges.back().end) {
		ranges.back().end = std::max(ranges.back().end, end);
		return;
	}
	ranges.push_back({begin, end});
}

std::vector<AxisRange> BuildFlattenedPointRanges(const RegularGrid &grid, const SpatialLayout &layout,
	                                              const SpatialSelection &selection) {
	std::vector<AxisRange> result;
	if (layout.order == GridStorageOrder::LongitudeFastest) {
		if (selection.longitude_ranges.size() == 1 && selection.longitude_ranges.front().begin == 0 &&
		    selection.longitude_ranges.front().end == grid.Nx()) {
			for (const auto &latitude : selection.latitude_ranges) {
				AppendMergedRange(result, latitude.begin * grid.Nx(), latitude.end * grid.Nx());
			}
			return result;
		}
		for (const auto &latitude : selection.latitude_ranges) {
			for (auto y = latitude.begin; y < latitude.end; y++) {
				const auto row_start = y * grid.Nx();
				for (const auto &longitude : selection.longitude_ranges) {
					AppendMergedRange(result, row_start + longitude.begin, row_start + longitude.end);
				}
			}
		}
		return result;
	}
	if (layout.order == GridStorageOrder::LatitudeFastest) {
		if (selection.latitude_ranges.size() == 1 && selection.latitude_ranges.front().begin == 0 &&
		    selection.latitude_ranges.front().end == grid.Ny()) {
			for (const auto &longitude : selection.longitude_ranges) {
				AppendMergedRange(result, longitude.begin * grid.Ny(), longitude.end * grid.Ny());
			}
			return result;
		}
		for (const auto &longitude : selection.longitude_ranges) {
			for (auto x = longitude.begin; x < longitude.end; x++) {
				const auto column_start = x * grid.Ny();
				for (const auto &latitude : selection.latitude_ranges) {
					AppendMergedRange(result, column_start + latitude.begin, column_start + latitude.end);
				}
			}
		}
		return result;
	}
	throw ReaderError(ReaderErrorCode::InvalidShape, "flattened spatial layout has an invalid storage order");
}

void AddChecked(std::uint64_t &target, std::uint64_t value, const char *description) {
	constexpr auto MAX_ROW_COUNT = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
	if (value != 0 && target > MAX_ROW_COUNT / value) {
		throw ReaderError(ReaderErrorCode::ShapeOverflow, std::string(description) + " exceeds the signed 64-bit limit");
	}
	target *= value;
}

} // namespace

SpatialSelection BuildSpatialSelection(const RegularGrid &grid, const SpatialLayout &layout,
	                                   const SpatialPredicate &predicate) {
	SpatialSelection selection;
	selection.residual_filter_retained = predicate.residual_filter_retained;
	selection.fallback_reasons = predicate.fallback_reasons;
	const auto row_count = CheckedShapeProduct(layout.shape);
	std::vector<SpatialConstraint> latitude_constraints;
	std::vector<SpatialConstraint> longitude_constraints;
	for (const auto &constraint : predicate.necessary_conditions) {
		(constraint.axis == SpatialAxis::Latitude ? latitude_constraints : longitude_constraints).push_back(constraint);
	}
	selection.latitude_ranges = BuildAxisRanges(grid.Ny(), latitude_constraints, SpatialAxis::Latitude,
	                                          [&](std::uint64_t index) { return grid.Coordinate(index, 0).latitude; });
	selection.longitude_ranges = BuildAxisRanges(grid.Nx(), longitude_constraints, SpatialAxis::Longitude,
	                                            [&](std::uint64_t index) { return grid.Coordinate(0, index).longitude; });
	const auto latitude_count = CountRanges(selection.latitude_ranges);
	const auto longitude_count = CountRanges(selection.longitude_ranges);
	std::uint64_t extra_axis_count = 1;
	for (const auto axis : layout.non_spatial_axes) {
		if (axis >= layout.shape.size()) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "spatial layout contains an invalid extra axis");
		}
		AddChecked(extra_axis_count, layout.shape[axis], "spatial extra-axis row count");
	}
	selection.candidate_rows = extra_axis_count;
	AddChecked(selection.candidate_rows, latitude_count, "spatial candidate row count");
	AddChecked(selection.candidate_rows, longitude_count, "spatial candidate row count");
	if (selection.candidate_rows > row_count) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "spatial candidate row count exceeds the logical array size");
	}
	if (selection.candidate_rows == 0) {
		selection.mode = SpatialSelectionMode::Empty;
	} else if (selection.candidate_rows < row_count) {
		selection.mode = SpatialSelectionMode::Restricted;
	} else if (predicate.has_unsupported_condition) {
		selection.mode = SpatialSelectionMode::Fallback;
	} else {
		selection.mode = SpatialSelectionMode::Full;
	}
	return selection;
}

const char *SpatialSelectionModeName(SpatialSelectionMode mode) noexcept {
	switch (mode) {
	case SpatialSelectionMode::Full: return "full";
	case SpatialSelectionMode::Restricted: return "restricted";
	case SpatialSelectionMode::Empty: return "empty";
	case SpatialSelectionMode::Fallback: return "fallback";
	}
	return "full";
}

SpatialBatchCursor::SpatialBatchCursor(const RegularGrid &grid, const SpatialLayout &layout,
	                                   SpatialSelection selection)
	: grid_(grid), layout_(layout), selection_(std::move(selection)) {
	if (selection_.mode == SpatialSelectionMode::Empty) {
		exhausted_ = true;
	} else if (selection_.mode == SpatialSelectionMode::Restricted) {
		BuildAxisRanges();
	}
}

void SpatialBatchCursor::BuildAxisRanges() {
	if (layout_.shape.empty() || layout_.strides.size() != layout_.shape.size()) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "spatial cursor requires a complete row-major layout");
	}
	(void)CheckedShapeProduct(layout_.shape);
	axis_ranges_.resize(layout_.shape.size());
	if (layout_.flattened) {
		if (layout_.point_axis >= layout_.shape.size() || layout_.shape[layout_.point_axis] != grid_.PointCount()) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "flattened spatial cursor axis does not match its grid");
		}
		axis_ranges_[layout_.point_axis] = BuildFlattenedPointRanges(grid_, layout_, selection_);
		for (std::size_t axis = 0; axis < layout_.shape.size(); axis++) {
			if (axis != layout_.point_axis) {
				axis_ranges_[axis].push_back({0, layout_.shape[axis]});
			}
		}
		return;
	}
	if (layout_.latitude_axis >= layout_.shape.size() || layout_.longitude_axis >= layout_.shape.size() ||
	    layout_.latitude_axis == layout_.longitude_axis || layout_.shape[layout_.latitude_axis] != grid_.Ny() ||
	    layout_.shape[layout_.longitude_axis] != grid_.Nx()) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "separate spatial cursor axes do not match their grid");
	}
	for (std::size_t axis = 0; axis < layout_.shape.size(); axis++) {
		if (axis == layout_.latitude_axis) {
			axis_ranges_[axis] = selection_.latitude_ranges;
		} else if (axis == layout_.longitude_axis) {
			axis_ranges_[axis] = selection_.longitude_ranges;
		} else {
			axis_ranges_[axis].push_back({0, layout_.shape[axis]});
		}
	}
}

std::optional<std::uint64_t> SpatialBatchCursor::NextAllowedIndex(std::size_t axis,
	                                                               std::uint64_t minimum) const {
	if (axis >= axis_ranges_.size()) {
		return std::nullopt;
	}
	for (const auto &range : axis_ranges_[axis]) {
		if (range.end <= minimum) {
			continue;
		}
		const auto candidate = std::max(range.begin, minimum);
		if (candidate < range.end) {
			return candidate;
		}
	}
	return std::nullopt;
}

bool SpatialBatchCursor::FillMinimumSuffix(std::size_t axis, std::vector<std::uint64_t> &indices) const {
	for (; axis < axis_ranges_.size(); axis++) {
		if (axis_ranges_[axis].empty()) {
			return false;
		}
		indices[axis] = axis_ranges_[axis].front().begin;
	}
	return true;
}

bool SpatialBatchCursor::SeekCandidate(std::size_t axis, bool follow_lower_bound,
	                                     const std::vector<std::uint64_t> &lower_bound,
	                                     std::vector<std::uint64_t> &indices) const {
	if (axis == axis_ranges_.size()) {
		return true;
	}
	const auto minimum = follow_lower_bound ? lower_bound[axis] : 0;
	auto candidate = NextAllowedIndex(axis, minimum);
	if (!candidate) {
		return false;
	}
	indices[axis] = *candidate;
	if (!follow_lower_bound || *candidate > minimum) {
		return FillMinimumSuffix(axis + 1, indices);
	}
	if (SeekCandidate(axis + 1, true, lower_bound, indices)) {
		return true;
	}
	if (minimum == std::numeric_limits<std::uint64_t>::max()) {
		return false;
	}
	candidate = NextAllowedIndex(axis, minimum + 1);
	if (!candidate) {
		return false;
	}
	indices[axis] = *candidate;
	return FillMinimumSuffix(axis + 1, indices);
}

bool SpatialBatchCursor::FindNextCandidate(std::uint64_t logical_index, std::uint64_t &candidate) const {
	const auto row_count = CheckedShapeProduct(layout_.shape);
	if (logical_index >= row_count) {
		return false;
	}
	const auto lower_bound = layout_.AxisIndices(logical_index);
	std::vector<std::uint64_t> indices(layout_.shape.size());
	if (!SeekCandidate(0, true, lower_bound, indices)) {
		return false;
	}
	candidate = 0;
	for (std::size_t axis = 0; axis < indices.size(); axis++) {
		candidate += indices[axis] * layout_.strides[axis];
	}
	if (candidate < logical_index || candidate >= row_count) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "spatial cursor produced an invalid candidate position");
	}
	return true;
}

void SpatialBatchCursor::BuildReadSegments(SpatialBatch &batch) const {
	std::size_t position = 0;
	while (position < batch.logical_positions.size()) {
		const auto start = batch.logical_positions[position];
		std::size_t end = position + 1;
		while (end < batch.logical_positions.size() && batch.logical_positions[end] == batch.logical_positions[end - 1] + 1) {
			end++;
		}
		auto segments = BuildBatchSegments(layout_.shape, start, static_cast<std::uint64_t>(end - position));
		for (auto &segment : segments) {
			segment.batch_offset += static_cast<std::uint64_t>(position);
			batch.read_segments.emplace_back(std::move(segment));
		}
		position = end;
	}
}

bool SpatialBatchCursor::Next(std::uint64_t vector_size, SpatialBatch &batch,
	                          const std::function<void()> &interrupt_check) {
	batch.logical_positions.clear();
	batch.read_segments.clear();
	if (vector_size == 0 || vector_size > static_cast<std::uint64_t>(STANDARD_VECTOR_SIZE)) {
		throw ReaderError(ReaderErrorCode::InvalidSelection,
		                  "spatial cursor vector size must be between one and STANDARD_VECTOR_SIZE");
	}
	if (exhausted_) {
		return false;
	}
	const auto row_count = CheckedShapeProduct(layout_.shape);
	if (selection_.mode == SpatialSelectionMode::Full || selection_.mode == SpatialSelectionMode::Fallback) {
		const auto count = std::min(vector_size, row_count - next_source_index_);
		if (count == 0) {
			exhausted_ = true;
			return false;
		}
		batch.logical_positions.resize(static_cast<std::size_t>(count));
		std::iota(batch.logical_positions.begin(), batch.logical_positions.end(), next_source_index_);
		next_source_index_ += count;
		BuildReadSegments(batch);
		exhausted_ = next_source_index_ == row_count;
		return true;
	}

	constexpr std::uint64_t INTERRUPT_CHECK_INTERVAL = 256;
	std::uint64_t candidates_since_interrupt_check = 0;
	while (batch.logical_positions.size() < vector_size && next_source_index_ < row_count) {
		std::uint64_t candidate = 0;
		if (!FindNextCandidate(next_source_index_, candidate)) {
			exhausted_ = true;
			break;
		}
		batch.logical_positions.push_back(candidate);
		next_source_index_ = candidate + 1;
		if (++candidates_since_interrupt_check == INTERRUPT_CHECK_INTERVAL) {
			if (interrupt_check) {
				interrupt_check();
			}
			candidates_since_interrupt_check = 0;
		}
	}
	if (interrupt_check) {
		interrupt_check();
	}
	if (next_source_index_ == row_count) {
		exhausted_ = true;
	}
	if (batch.logical_positions.empty()) {
		return false;
	}
	BuildReadSegments(batch);
	return true;
}

bool SpatialBatchCursor::Exhausted() const noexcept {
	return exhausted_;
}

} // namespace duckomo
} // namespace duckdb
