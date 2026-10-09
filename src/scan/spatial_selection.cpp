#include "duckomo/spatial_selection.hpp"

#include <algorithm>
#include <limits>
#include <numeric>

#include "duckomo/axis_selection.hpp"
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

bool MatchesInterval(double coordinate, const SpatialInterval &interval) {
	const bool above_lower = interval.lower_inclusive ? coordinate >= interval.lower : coordinate > interval.lower;
	const bool below_upper = interval.upper_inclusive ? coordinate <= interval.upper : coordinate < interval.upper;
	return above_lower && below_upper;
}

bool MatchesLongitudeUnion(double coordinate, const std::vector<SpatialInterval> *longitude_union) {
	if (longitude_union == nullptr || longitude_union->empty()) return true;
	return std::any_of(longitude_union->begin(), longitude_union->end(),
	                   [&](const auto &interval) { return MatchesInterval(coordinate, interval); });
}

template <class COORDINATE>
std::vector<AxisRange> BuildAxisRanges(std::uint64_t extent, const std::vector<SpatialConstraint> &constraints,
	                                  SpatialAxis axis, COORDINATE coordinate,
	                                  const std::vector<SpatialInterval> *longitude_union,
	                                  std::uint64_t reserve_for_remaining_axes,
	                                  std::uint64_t &interval_count, bool &budget_fallback) {
	std::vector<AxisRange> ranges;
	const auto maximum = std::min(MAX_SELECTION_INTERVAL_BYTES / sizeof(AxisRange), MAX_SPATIAL_SELECTOR_RANGES);
	const auto axis_limit = maximum - std::min(maximum, reserve_for_remaining_axes);
	const auto widen_to_full = [&]() {
		std::vector<AxisRange>().swap(ranges);
		ranges.reserve(1);
		ranges.push_back({0, extent});
		interval_count++;
		budget_fallback = true;
		return ranges;
	};
	const auto count_ranges = [&]() {
		std::uint64_t required = 0;
		bool in_range = false;
		for (std::uint64_t index = 0; index < extent; index++) {
			const auto value = coordinate(index);
			const bool matches = (constraints.empty() || MatchesConstraints(value, axis, constraints)) &&
			                     MatchesLongitudeUnion(value, longitude_union);
			if (matches && !in_range) required++;
			in_range = matches;
		}
		return required;
	};
	const bool has_filter = !constraints.empty() || (longitude_union != nullptr && !longitude_union->empty());
	const auto required_ranges = has_filter ? count_ranges() : std::uint64_t(1);
	if (required_ranges == 0) return ranges;
	if (required_ranges > axis_limit - std::min(interval_count, axis_limit)) return widen_to_full();
	// The exact count is known, so reserve only the checked payload before the
	// second pass. No vector growth can transiently exceed the query budget.
	ranges.reserve(static_cast<std::size_t>(required_ranges));
	if (!has_filter) {
		ranges.push_back({0, extent});
		interval_count++;
		return ranges;
	}
	bool in_range = false;
	std::uint64_t range_begin = 0;
	for (std::uint64_t index = 0; index < extent; index++) {
		const auto value = coordinate(index);
		const bool matches = MatchesConstraints(value, axis, constraints) && MatchesLongitudeUnion(value, longitude_union);
		if (matches && !in_range) {
			range_begin = index;
			in_range = true;
		} else if (!matches && in_range) {
			ranges.push_back({range_begin, index});
			in_range = false;
		}
	}
	if (in_range) ranges.push_back({range_begin, extent});
	interval_count += ranges.size();
	return ranges;
}

bool AppendMergedRange(std::vector<AxisRange> &ranges, std::uint64_t begin, std::uint64_t end,
                       std::size_t maximum_ranges) {
	if (begin >= end) {
		return true;
	}
	if (!ranges.empty() && begin <= ranges.back().end) {
		ranges.back().end = std::max(ranges.back().end, end);
		return true;
	}
	if (ranges.size() >= maximum_ranges) return false;
	ranges.push_back({begin, end});
	return true;
}

std::vector<AxisRange> BuildFlattenedPointRanges(const RegularGrid &grid, const SpatialLayout &layout,
	                                              const SpatialSelection &selection, bool &budget_fallback) {
	std::vector<AxisRange> result;
	const auto maximum_ranges = static_cast<std::size_t>(MAX_SPATIAL_SELECTOR_RANGES);
	const auto append = [&](std::uint64_t begin, std::uint64_t end) {
		if (AppendMergedRange(result, begin, end, maximum_ranges)) return true;
		std::vector<AxisRange>().swap(result);
		budget_fallback = true;
		return false;
	};
	if (layout.order == GridStorageOrder::LongitudeFastest) {
		if (selection.longitude_ranges.size() == 1 && selection.longitude_ranges.front().begin == 0 &&
		    selection.longitude_ranges.front().end == grid.Nx()) {
			for (const auto &latitude : selection.latitude_ranges) {
				if (!append(latitude.begin * grid.Nx(), latitude.end * grid.Nx())) return result;
			}
			return result;
		}
		for (const auto &latitude : selection.latitude_ranges) {
			for (auto y = latitude.begin; y < latitude.end; y++) {
				const auto row_start = y * grid.Nx();
				for (const auto &longitude : selection.longitude_ranges) {
					if (!append(row_start + longitude.begin, row_start + longitude.end)) return result;
				}
			}
		}
		return result;
	}
	if (layout.order == GridStorageOrder::LatitudeFastest) {
		if (selection.latitude_ranges.size() == 1 && selection.latitude_ranges.front().begin == 0 &&
		    selection.latitude_ranges.front().end == grid.Ny()) {
			for (const auto &longitude : selection.longitude_ranges) {
				if (!append(longitude.begin * grid.Ny(), longitude.end * grid.Ny())) return result;
			}
			return result;
		}
		for (const auto &longitude : selection.longitude_ranges) {
			for (auto x = longitude.begin; x < longitude.end; x++) {
				const auto column_start = x * grid.Ny();
				for (const auto &latitude : selection.latitude_ranges) {
					if (!append(column_start + latitude.begin, column_start + latitude.end)) return result;
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

NativeWindow NativeWindow::Create(std::uint64_t spatial_axis, std::uint64_t begin, std::uint64_t count,
                                 const std::vector<std::uint64_t> &shape,
                                 const std::vector<std::uint64_t> &fixed_axis_indices) {
	if (shape.empty() || shape.size() != fixed_axis_indices.size() || spatial_axis >= shape.size()) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "native window axis indices must match a non-empty source shape");
	}
	if (count == 0 || count > MAX_NATIVE_WINDOW_POINTS || begin > shape[spatial_axis] ||
	    count > shape[spatial_axis] - begin) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "native window extent must fit its axis and point budget");
	}
	if (shape.size() > (MAX_SELECTOR_PAYLOAD_BYTES - sizeof(NativeWindow)) / sizeof(std::uint64_t)) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "native window selector payload exceeds 256 KiB");
	}
	for (std::size_t axis = 0; axis < shape.size(); axis++) {
		if (axis != spatial_axis && fixed_axis_indices[axis] >= shape[axis]) {
			throw ReaderError(ReaderErrorCode::InvalidSelection, "native window fixed axis index is outside the source shape");
		}
	}
	NativeWindow result;
	result.spatial_axis_ = spatial_axis;
	result.begin_ = begin;
	result.end_ = begin + count;
	result.fixed_axis_indices_ = fixed_axis_indices;
	return result;
}

std::uint64_t NativeWindow::LogicalPosition(const SpatialLayout &layout, std::uint64_t offset) const {
	if (layout.shape.empty() || layout.shape.size() != layout.strides.size() ||
	    layout.shape.size() != fixed_axis_indices_.size() || spatial_axis_ >= layout.shape.size() || offset >= Count()) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "native window cannot map this source position");
	}
	const auto axis_position = begin_ + offset;
	if (axis_position >= layout.shape[spatial_axis_]) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "native window position exceeds its spatial axis");
	}
	constexpr auto MAX_ROW_COUNT = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
	std::uint64_t result = 0;
	for (std::size_t axis = 0; axis < layout.shape.size(); axis++) {
		const auto index = axis == spatial_axis_ ? axis_position : fixed_axis_indices_[axis];
		if (index >= layout.shape[axis]) {
			throw ReaderError(ReaderErrorCode::InvalidSelection, "native window fixed axis index exceeds its source shape");
		}
		if (index != 0 && layout.strides[axis] > MAX_ROW_COUNT / index) {
			throw ReaderError(ReaderErrorCode::ShapeOverflow, "native window logical position exceeds signed 64-bit range");
		}
		const auto term = index * layout.strides[axis];
		if (term > MAX_ROW_COUNT - result) {
			throw ReaderError(ReaderErrorCode::ShapeOverflow, "native window logical position exceeds signed 64-bit range");
		}
		result += term;
	}
	return result;
}

std::uint64_t NativeWindow::EstimatedBytes() const noexcept {
	if (fixed_axis_indices_.capacity() > std::numeric_limits<std::uint64_t>::max() / sizeof(std::uint64_t)) {
		return std::numeric_limits<std::uint64_t>::max();
	}
	return sizeof(*this) + static_cast<std::uint64_t>(fixed_axis_indices_.capacity() * sizeof(std::uint64_t));
}

NativeWindowCursor::NativeWindowCursor(const SpatialLayout &layout, bool contiguous_reads) : layout_(layout) {
	if (layout_.shape.empty() || layout_.shape.size() != layout_.strides.size()) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "native window cursor requires a complete spatial layout");
	}
	(void)CheckedShapeProduct(layout_.shape);
	std::uint64_t expected_stride = 1;
	for (std::size_t reverse = layout_.shape.size(); reverse > 0; reverse--) {
		const auto axis = reverse - 1;
		if (layout_.strides[axis] != expected_stride) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "native window cursor requires row-major source strides");
		}
		if (layout_.shape[axis] != 0 && expected_stride >
		                                      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) /
		                                          layout_.shape[axis]) {
			throw ReaderError(ReaderErrorCode::ShapeOverflow, "native window source stride exceeds signed 64-bit range");
		}
		expected_stride *= layout_.shape[axis];
	}
	if (layout_.flattened || layout_.geometry == SpatialLayoutGeometry::Gaussian) {
		spatial_axis_ = layout_.point_axis;
		if (spatial_axis_ >= layout_.shape.size()) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "native point axis is outside the spatial layout");
		}
	} else {
		if (layout_.latitude_axis >= layout_.shape.size() || layout_.longitude_axis >= layout_.shape.size() ||
		    layout_.latitude_axis == layout_.longitude_axis) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "separate spatial axes are invalid");
		}
		// Unit-length axes can tie in row-major strides (notably a 1x1
		// grid). Either traversal is equivalent; the tie breaks to longitude.
		spatial_axis_ = layout_.strides[layout_.latitude_axis] < layout_.strides[layout_.longitude_axis]
		                    ? layout_.latitude_axis
		                    : layout_.longitude_axis;
	}
	if (contiguous_reads) spatial_axis_ = layout_.shape.size() - 1;
	if (layout_.shape[spatial_axis_] == 0) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "native spatial axis must not be empty");
	}
	fixed_axis_indices_.resize(layout_.shape.size(), 0);
	std::uint64_t fixed_combinations = 1;
	constexpr auto MAX_ROW_COUNT = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
	for (std::size_t axis = 0; axis < layout_.shape.size(); axis++) {
		if (axis == spatial_axis_) continue;
		if (fixed_combinations > MAX_ROW_COUNT / layout_.shape[axis]) {
			throw ReaderError(ReaderErrorCode::ShapeOverflow, "native window count exceeds signed 64-bit range");
		}
		fixed_combinations *= layout_.shape[axis];
	}
	const auto axis_length = layout_.shape[spatial_axis_];
	const auto windows_per_combination = axis_length / MAX_NATIVE_WINDOW_POINTS +
	                                     (axis_length % MAX_NATIVE_WINDOW_POINTS != 0);
	if (windows_per_combination != 0 && fixed_combinations > MAX_ROW_COUNT / windows_per_combination) {
		throw ReaderError(ReaderErrorCode::ShapeOverflow, "native window count exceeds signed 64-bit range");
	}
	window_upper_bound_ = fixed_combinations * windows_per_combination;
}

std::uint64_t NativeWindowCursor::WindowUpperBound(const AxisSelectionCursor &axis_selection) const {
	if (!axis_selection.MatchesShape(layout_.shape)) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "semantic axis selection does not match the native window shape");
	}
	std::uint64_t result = axis_selection.CandidateWindowCount(spatial_axis_, MAX_NATIVE_WINDOW_POINTS);
	for (std::size_t axis = 0; axis < layout_.shape.size(); axis++) {
		if (axis == spatial_axis_) continue;
		AddChecked(result, axis_selection.CandidateAxisCount(axis), "selected native window count");
	}
	return result;
}

bool NativeWindowCursor::InitializeSelectedIndices(const AxisSelectionCursor *axis_selection) {
	for (std::size_t axis = 0; axis < fixed_axis_indices_.size(); axis++) {
		if (axis == spatial_axis_) continue;
		if (axis_selection) {
			const auto first = axis_selection->NextSelectedAxisIndex(axis, 0);
			if (!first) return false;
			fixed_axis_indices_[axis] = *first;
		} else {
			fixed_axis_indices_[axis] = 0;
		}
	}
	if (axis_selection) {
		const auto first = axis_selection->NextSelectedAxisIndex(spatial_axis_, 0);
		if (!first) return false;
		next_begin_ = (*first / MAX_NATIVE_WINDOW_POINTS) * MAX_NATIVE_WINDOW_POINTS;
	} else {
		next_begin_ = 0;
	}
	return true;
}

bool NativeWindowCursor::AdvanceFixedIndices(const AxisSelectionCursor *axis_selection,
	                                           const std::function<void()> &interrupt_check) {
	std::uint64_t axis_steps = 0;
	for (std::size_t reverse = fixed_axis_indices_.size(); reverse > 0; reverse--) {
		const auto axis = reverse - 1;
		if (axis == spatial_axis_) continue;
		if ((axis_steps++ & 255U) == 0 && interrupt_check) interrupt_check();
		const auto next = axis_selection
		                      ? axis_selection->NextSelectedAxisIndex(axis, fixed_axis_indices_[axis] + 1)
		                      : (fixed_axis_indices_[axis] + 1 < layout_.shape[axis]
	                             ? std::optional<std::uint64_t>(fixed_axis_indices_[axis] + 1)
	                             : std::nullopt);
		if (next) {
			fixed_axis_indices_[axis] = *next;
			for (std::size_t suffix = axis + 1; suffix < fixed_axis_indices_.size(); suffix++) {
				if (suffix == spatial_axis_) continue;
				if (axis_selection) {
					const auto first = axis_selection->NextSelectedAxisIndex(suffix, 0);
					if (!first) return false;
					fixed_axis_indices_[suffix] = *first;
				} else {
					fixed_axis_indices_[suffix] = 0;
				}
			}
			return true;
		}
		if (axis_selection) {
			const auto first = axis_selection->NextSelectedAxisIndex(axis, 0);
			if (!first) return false;
			fixed_axis_indices_[axis] = *first;
		} else {
			fixed_axis_indices_[axis] = 0;
		}
	}
	return false;
}

bool NativeWindowCursor::Next(NativeWindow &window, const std::function<void()> &interrupt_check) {
	return NextInternal(window, nullptr, interrupt_check);
}

bool NativeWindowCursor::Next(NativeWindow &window, const AxisSelectionCursor &axis_selection,
	                          const std::function<void()> &interrupt_check) {
	return NextInternal(window, &axis_selection, interrupt_check);
}

bool NativeWindowCursor::NextInternal(NativeWindow &window, const AxisSelectionCursor *axis_selection,
	                                  const std::function<void()> &interrupt_check) {
	if (exhausted_) return false;
	if (interrupt_check) interrupt_check();
	if (!initialized_) {
		if (axis_selection && !axis_selection->MatchesShape(layout_.shape)) {
			throw ReaderError(ReaderErrorCode::InvalidShape,
			                  "semantic axis selection does not match the native window shape");
		}
		initialized_ = true;
		if (!InitializeSelectedIndices(axis_selection)) {
			exhausted_ = true;
			return false;
		}
	}
	const auto remaining = layout_.shape[spatial_axis_] - next_begin_;
	const auto count = std::min<std::uint64_t>(remaining, MAX_NATIVE_WINDOW_POINTS);
	window = NativeWindow::Create(spatial_axis_, next_begin_, count, layout_.shape, fixed_axis_indices_);
	const auto current_begin = next_begin_;
	const auto end = current_begin + count;
	const auto next_fast = axis_selection ? axis_selection->NextSelectedAxisIndex(spatial_axis_, end)
	                                      : (end < layout_.shape[spatial_axis_]
	                                             ? std::optional<std::uint64_t>(end)
	                                             : std::nullopt);
	if (next_fast) {
		const auto next_window_begin = (*next_fast / MAX_NATIVE_WINDOW_POINTS) * MAX_NATIVE_WINDOW_POINTS;
		if (next_window_begin > current_begin) {
			next_begin_ = next_window_begin;
		} else {
			throw ReaderError(ReaderErrorCode::InvalidSelection,
			                  "native window cursor did not advance to a later spatial chunk");
		}
	} else {
		next_begin_ = 0;
		if (AdvanceFixedIndices(axis_selection, interrupt_check)) {
			if (axis_selection) {
				const auto first = axis_selection->NextSelectedAxisIndex(spatial_axis_, 0);
				if (!first) {
					exhausted_ = true;
				} else {
					next_begin_ = (*first / MAX_NATIVE_WINDOW_POINTS) * MAX_NATIVE_WINDOW_POINTS;
				}
			}
		} else {
			exhausted_ = true;
		}
	}
	return true;
}

std::uint64_t NativeWindowCursor::EstimatedBytes() const noexcept {
	std::uint64_t bytes = sizeof(*this);
	const auto add = [&bytes](std::uint64_t value) {
		bytes = bytes > std::numeric_limits<std::uint64_t>::max() - value
		            ? std::numeric_limits<std::uint64_t>::max()
		            : bytes + value;
	};
	const auto add_capacity = [&add](std::size_t capacity, std::size_t element_size) {
		if (element_size != 0 && capacity > std::numeric_limits<std::uint64_t>::max() / element_size) {
			add(std::numeric_limits<std::uint64_t>::max());
		} else {
			add(static_cast<std::uint64_t>(capacity * element_size));
		}
	};
	add_capacity(layout_.shape.capacity(), sizeof(std::uint64_t));
	add_capacity(layout_.axes.capacity(), sizeof(std::string));
	add_capacity(layout_.strides.capacity(), sizeof(std::uint64_t));
	add_capacity(layout_.non_spatial_axes.capacity(), sizeof(std::uint64_t));
	add_capacity(fixed_axis_indices_.capacity(), sizeof(std::uint64_t));
	for (const auto &axis : layout_.axes) add(static_cast<std::uint64_t>(axis.capacity()));
	return bytes;
}

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
	std::uint64_t interval_count = 0;
	selection.latitude_ranges = BuildAxisRanges(grid.Ny(), latitude_constraints, SpatialAxis::Latitude,
	                                          [&](std::uint64_t index) { return grid.Coordinate(index, 0).latitude; },
	                                          nullptr,
	                                          1, interval_count, selection.budget_fallback);
	selection.longitude_ranges = BuildAxisRanges(grid.Nx(), longitude_constraints, SpatialAxis::Longitude,
	                                            [&](std::uint64_t index) { return grid.Coordinate(0, index).longitude; },
	                                            &predicate.longitude_union,
	                                            0, interval_count, selection.budget_fallback);
	if (selection.budget_fallback) {
		selection.fallback_reasons.push_back("axis_interval_payload_limit");
		selection.residual_filter_retained = true;
	}
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
		bool range_budget_fallback = false;
		axis_ranges_[layout_.point_axis] = BuildFlattenedPointRanges(grid_, layout_, selection_, range_budget_fallback);
		if (range_budget_fallback) {
			selection_.mode = SpatialSelectionMode::Fallback;
			selection_.budget_fallback = true;
			selection_.residual_filter_retained = true;
			selection_.candidate_rows = CheckedShapeProduct(layout_.shape);
			selection_.fallback_reasons.push_back("flattened_spatial_range_limit");
			std::vector<std::vector<AxisRange>>().swap(axis_ranges_);
			return;
		}
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

const SpatialSelection &SpatialBatchCursor::Selection() const noexcept {
	return selection_;
}

std::uint64_t SpatialBatchCursor::EstimatedBytes() const noexcept {
	std::uint64_t bytes = sizeof(*this);
	const auto add = [&bytes](std::uint64_t value) {
		bytes = bytes > std::numeric_limits<std::uint64_t>::max() - value
		            ? std::numeric_limits<std::uint64_t>::max()
		            : bytes + value;
	};
	const auto count = [&add](std::size_t capacity, std::size_t element_size) {
		if (element_size != 0 && capacity > std::numeric_limits<std::uint64_t>::max() / element_size) {
			add(std::numeric_limits<std::uint64_t>::max());
		} else {
			add(static_cast<std::uint64_t>(capacity * element_size));
		}
	};
	count(layout_.shape.capacity(), sizeof(std::uint64_t));
	count(layout_.axes.capacity(), sizeof(std::string));
	for (const auto &axis : layout_.axes) add(axis.size());
	count(layout_.strides.capacity(), sizeof(std::uint64_t));
	count(layout_.non_spatial_axes.capacity(), sizeof(std::uint64_t));
	count(selection_.latitude_ranges.capacity(), sizeof(AxisRange));
	count(selection_.longitude_ranges.capacity(), sizeof(AxisRange));
	count(selection_.fallback_reasons.capacity(), sizeof(std::string));
	for (const auto &reason : selection_.fallback_reasons) add(reason.size());
	count(axis_ranges_.capacity(), sizeof(std::vector<AxisRange>));
	for (const auto &ranges : axis_ranges_) count(ranges.capacity(), sizeof(AxisRange));
	return bytes;
}

} // namespace duckomo
} // namespace duckdb
