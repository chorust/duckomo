#include "duckomo/axis_selection.hpp"

#include <algorithm>
#include <limits>
#include <optional>

#include "duckdb/common/exception.hpp"
#include "duckomo/om_reader.hpp"

namespace duckdb {
namespace duckomo {
namespace {

bool Satisfies(const Value &coordinate, const AxisConstraint &constraint) {
	switch (constraint.comparison) {
	case AxisComparison::Equal:
		return Value::NotDistinctFrom(coordinate, constraint.constant);
	case AxisComparison::Less:
		return coordinate < constraint.constant;
	case AxisComparison::LessEqual:
		return coordinate <= constraint.constant;
	case AxisComparison::Greater:
		return coordinate > constraint.constant;
	case AxisComparison::GreaterEqual:
		return coordinate >= constraint.constant;
	}
	return false;
}

void AppendRange(std::vector<LogicalAxisRange> &ranges, std::uint64_t index) {
	if (!ranges.empty() && ranges.back().end == index) {
		ranges.back().end++;
	} else {
		ranges.push_back({index, index + 1});
	}
}

bool MatchesAxisConstraints(const SemanticAxis &semantic, std::uint64_t coordinate_index,
                            const std::vector<const AxisConstraint *> &constraints) {
	if (coordinate_index > std::numeric_limits<std::uint64_t>::max() / semantic.stride) {
		throw ReaderError(ReaderErrorCode::ShapeOverflow, "semantic coordinate position overflows");
	}
	const auto coordinate = semantic.CoordinateValue(coordinate_index * semantic.stride);
	for (const auto *constraint : constraints) {
		if (!Satisfies(coordinate, *constraint)) return false;
	}
	return true;
}

} // namespace

AxisSelectionCursor::AxisSelectionCursor(const std::vector<std::uint64_t> &shape,
                                         const SemanticAxes &semantic_axes,
                                         const AxisPredicate &predicate) {
	if (shape.empty()) {
		empty = true;
		exhausted = true;
		return;
	}
	axis_lengths = shape;
	ranges.resize(shape.size());
	strides.resize(shape.size(), 1);
	indices.resize(shape.size(), 0);
	range_indices.resize(shape.size(), 0);
	for (std::size_t reverse = shape.size(); reverse > 1; reverse--) {
		const auto axis = reverse - 2;
		if (shape[axis + 1] != 0 && strides[axis + 1] >
		                                   static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) /
		                                       shape[axis + 1]) {
			throw ReaderError(ReaderErrorCode::ShapeOverflow, "semantic selection stride exceeds signed 64-bit range");
		}
		strides[axis] = strides[axis + 1] * shape[axis + 1];
	}

	std::vector<std::vector<const AxisConstraint *>> constraints(shape.size());
	for (const auto &constraint : predicate.necessary_conditions) {
		if (constraint.axis_index >= shape.size()) {
			throw InternalException("typed axis predicate references a logical axis outside the array rank");
		}
		constraints[constraint.axis_index].push_back(&constraint);
		has_constraints = true;
	}
	std::uint64_t total_interval_count = 0;

	for (std::size_t axis_index = 0; axis_index < shape.size(); axis_index++) {
		auto &axis_ranges = ranges[axis_index];
		if (shape[axis_index] == 0) {
			empty = true;
			exhausted = true;
			return;
		}
		// An empty interval vector denotes the complete axis. This avoids
		// allocating one copied interval for every unconstrained dimension.
		if (constraints[axis_index].empty()) continue;
		auto semantic = std::find_if(semantic_axes.begin(), semantic_axes.end(),
		                             [axis_index](const SemanticAxis &candidate) {
			                             return candidate.axis_index == axis_index;
		                             });
		if (semantic == semantic_axes.end() || semantic->axis_length != shape[axis_index]) {
			std::string detail = "typed axis predicate has no matching semantic coordinate mapping for axis " +
			                     std::to_string(axis_index) + " (shape length " +
			                     std::to_string(shape[axis_index]) + "); mappings:";
			for (const auto &candidate : semantic_axes) {
				detail += " [axis=" + std::to_string(candidate.axis_index) + ", length=" +
				          std::to_string(candidate.axis_length) + ", kind=" +
				          std::to_string(static_cast<unsigned int>(candidate.kind)) + "]";
			}
			throw InternalException(detail);
		}
		// Count matches and ranges first. If this axis would exceed the global
		// interval budget, widen this axis before allocating any interval data;
		// DuckDB still evaluates the original WHERE expression.
		std::uint64_t matched_count = 0;
		std::uint64_t required_ranges = 0;
		bool previous_matches = false;
		for (std::uint64_t coordinate_index = 0; coordinate_index < shape[axis_index]; coordinate_index++) {
			const bool matches = MatchesAxisConstraints(*semantic, coordinate_index, constraints[axis_index]);
			if (matches) {
				matched_count++;
				if (!previous_matches) required_ranges++;
			}
			previous_matches = matches;
		}
		if (matched_count == 0) {
			empty = true;
			exhausted = true;
			return;
		}
		const auto max_interval_count = MAX_SELECTION_INTERVAL_BYTES / sizeof(LogicalAxisRange);
		if (required_ranges > max_interval_count - std::min(total_interval_count, max_interval_count)) {
			budget_fallback = true;
			continue;
		}
		axis_ranges.reserve(static_cast<std::size_t>(required_ranges));
		for (std::uint64_t coordinate_index = 0; coordinate_index < shape[axis_index]; coordinate_index++) {
			if (MatchesAxisConstraints(*semantic, coordinate_index, constraints[axis_index])) {
				AppendRange(axis_ranges, coordinate_index);
			}
		}
		total_interval_count += axis_ranges.size();
	}
	for (std::size_t axis = 0; axis < ranges.size(); axis++) {
		indices[axis] = ranges[axis].empty() ? 0 : ranges[axis].front().begin;
	}
	candidate_count = 1;
	for (std::size_t axis = 0; axis < ranges.size(); axis++) {
		std::uint64_t axis_count = 0;
		if (ranges[axis].empty()) {
			axis_count = axis_lengths[axis];
		} else {
			for (const auto &range : ranges[axis]) {
				const auto length = range.end - range.begin;
				if (axis_count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - length) {
					throw ReaderError(ReaderErrorCode::ShapeOverflow, "semantic candidate count exceeds signed 64-bit range");
				}
				axis_count += length;
			}
		}
		if (axis_count != 0 && candidate_count >
		                               static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / axis_count) {
			throw ReaderError(ReaderErrorCode::ShapeOverflow, "semantic candidate count exceeds signed 64-bit range");
		}
		candidate_count *= axis_count;
	}
}

bool AxisSelectionCursor::HasConstraints() const noexcept {
	return has_constraints;
}

bool AxisSelectionCursor::IsEmpty() const noexcept {
	return empty;
}

bool AxisSelectionCursor::IsExhausted() const noexcept {
	return exhausted;
}

bool AxisSelectionCursor::BudgetFallback() const noexcept {
	return budget_fallback;
}

std::uint64_t AxisSelectionCursor::CandidateCount() const noexcept {
	return candidate_count;
}

bool AxisSelectionCursor::MatchesShape(const std::vector<std::uint64_t> &shape) const noexcept {
	return shape == axis_lengths;
}

std::uint64_t AxisSelectionCursor::CandidateAxisCount(std::size_t axis) const {
	if (axis >= axis_lengths.size()) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "semantic axis count requested outside the source rank");
	}
	if (empty) return 0;
	const auto &axis_ranges = ranges[axis];
	if (axis_ranges.empty()) return axis_lengths[axis];
	std::uint64_t count = 0;
	for (const auto &range : axis_ranges) {
		const auto length = range.end - range.begin;
		if (count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - length) {
			throw ReaderError(ReaderErrorCode::ShapeOverflow, "semantic axis candidate count exceeds signed 64-bit range");
		}
		count += length;
	}
	return count;
}

std::uint64_t AxisSelectionCursor::CandidateWindowCount(std::size_t axis, std::uint64_t window_size) const {
	if (window_size == 0) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "semantic axis window size must be positive");
	}
	if (axis >= axis_lengths.size()) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "semantic axis windows requested outside the source rank");
	}
	if (empty) return 0;
	const auto &axis_ranges = ranges[axis];
	if (axis_ranges.empty()) {
		return axis_lengths[axis] / window_size + (axis_lengths[axis] % window_size != 0);
	}
	std::uint64_t count = 0;
	std::optional<std::uint64_t> last_chunk;
	for (const auto &range : axis_ranges) {
		if (range.begin >= range.end) continue;
		const auto first_chunk = range.begin / window_size;
		const auto final_chunk = (range.end - 1) / window_size;
		const auto uncovered_begin = last_chunk && first_chunk <= *last_chunk ? *last_chunk + 1 : first_chunk;
		if (uncovered_begin <= final_chunk) {
			const auto added = final_chunk - uncovered_begin + 1;
			if (count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - added) {
				throw ReaderError(ReaderErrorCode::ShapeOverflow, "semantic axis window count exceeds signed 64-bit range");
			}
			count += added;
		}
		last_chunk = final_chunk;
	}
	return count;
}

std::optional<std::uint64_t> AxisSelectionCursor::NextSelectedAxisIndex(std::size_t axis,
	                                                                    std::uint64_t minimum) const {
	if (axis >= axis_lengths.size()) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "semantic axis index requested outside the source rank");
	}
	if (empty || minimum >= axis_lengths[axis]) return std::nullopt;
	const auto &axis_ranges = ranges[axis];
	if (axis_ranges.empty()) return minimum;
	const auto next = std::lower_bound(axis_ranges.begin(), axis_ranges.end(), minimum,
	                                   [](const LogicalAxisRange &range, std::uint64_t value) {
		                                   return range.end <= value;
	                                   });
	if (next == axis_ranges.end()) return std::nullopt;
	const auto candidate = std::max(next->begin, minimum);
	return candidate < next->end ? std::optional<std::uint64_t>(candidate) : std::nullopt;
}

std::uint64_t AxisSelectionCursor::EstimatedBytes() const noexcept {
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
	count(ranges.capacity(), sizeof(std::vector<LogicalAxisRange>));
	for (const auto &axis_ranges : ranges) count(axis_ranges.capacity(), sizeof(LogicalAxisRange));
	count(strides.capacity(), sizeof(std::uint64_t));
	count(indices.capacity(), sizeof(std::uint64_t));
	count(range_indices.capacity(), sizeof(std::size_t));
	count(axis_lengths.capacity(), sizeof(std::uint64_t));
	return bytes;
}

std::uint64_t AxisSelectionCursor::IntervalPayloadBytes() const noexcept {
	std::uint64_t bytes = 0;
	for (const auto &axis_ranges : ranges) {
		if (axis_ranges.capacity() > (std::numeric_limits<std::uint64_t>::max() - bytes) / sizeof(LogicalAxisRange)) {
			return std::numeric_limits<std::uint64_t>::max();
		}
		bytes += static_cast<std::uint64_t>(axis_ranges.capacity() * sizeof(LogicalAxisRange));
	}
	return bytes;
}

bool AxisSelectionCursor::Contains(std::uint64_t logical_position) const {
	if (empty) return false;
	std::uint64_t row_count = 1;
	for (const auto length : axis_lengths) {
		if (length != 0 && row_count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / length) {
			return false;
		}
		row_count *= length;
	}
	if (logical_position >= row_count) return false;
	for (std::size_t axis = 0; axis < ranges.size(); axis++) {
		const auto index = (logical_position / strides[axis]) % axis_lengths[axis];
		const auto &axis_ranges = ranges[axis];
		if (axis_ranges.empty()) continue;
		const auto next = std::upper_bound(axis_ranges.begin(), axis_ranges.end(), index,
		                                   [](std::uint64_t value, const LogicalAxisRange &range) {
			                                   return value < range.begin;
		                                   });
		if (next == axis_ranges.begin() || index >= (next - 1)->end) return false;
	}
	return true;
}

std::uint64_t AxisSelectionCursor::CurrentLogicalPosition() const {
	std::uint64_t result = 0;
	for (std::size_t axis = 0; axis < indices.size(); axis++) {
		if (indices[axis] != 0 && strides[axis] >
	                                  (static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - result) /
	                                      indices[axis]) {
			throw ReaderError(ReaderErrorCode::ShapeOverflow, "semantic selection logical position overflows");
		}
		result += indices[axis] * strides[axis];
	}
	return result;
}

bool AxisSelectionCursor::Advance() {
	for (std::size_t reverse = indices.size(); reverse > 0; reverse--) {
		const auto axis = reverse - 1;
		const auto &axis_ranges = ranges[axis];
		if (axis_ranges.empty()) {
			if (indices[axis] + 1 < axis_lengths[axis]) {
				indices[axis]++;
				for (std::size_t suffix = axis + 1; suffix < indices.size(); suffix++) {
					range_indices[suffix] = 0;
					indices[suffix] = ranges[suffix].empty() ? 0 : ranges[suffix].front().begin;
				}
				return true;
			}
			indices[axis] = 0;
			continue;
		}
		const auto &current_range = axis_ranges[range_indices[axis]];
		if (indices[axis] + 1 < current_range.end) {
			indices[axis]++;
			for (std::size_t suffix = axis + 1; suffix < indices.size(); suffix++) {
				range_indices[suffix] = 0;
				indices[suffix] = ranges[suffix].empty() ? 0 : ranges[suffix].front().begin;
			}
			return true;
		}
		if (range_indices[axis] + 1 < axis_ranges.size()) {
			range_indices[axis]++;
			indices[axis] = axis_ranges[range_indices[axis]].begin;
			for (std::size_t suffix = axis + 1; suffix < indices.size(); suffix++) {
				range_indices[suffix] = 0;
				indices[suffix] = ranges[suffix].empty() ? 0 : ranges[suffix].front().begin;
			}
			return true;
		}
	}
	exhausted = true;
	return false;
}

idx_t AxisSelectionCursor::Next(idx_t limit, std::vector<std::uint64_t> &logical_positions,
                                const std::function<bool(std::uint64_t)> &include) {
	logical_positions.clear();
	if (limit == 0 || exhausted) return 0;
	if (limit > MAX_NATIVE_WINDOW_POINTS) {
		throw ReaderError(ReaderErrorCode::InvalidSelection,
		                  "semantic axis cursor batch exceeds the native window point limit");
	}
	logical_positions.reserve(limit);
	while (!exhausted && logical_positions.size() < limit) {
		const auto position = CurrentLogicalPosition();
		Advance();
		if (!include || include(position)) {
			logical_positions.push_back(position);
		}
	}
	return static_cast<idx_t>(logical_positions.size());
}

std::vector<BatchSegment> BuildSelectedBatchSegments(const std::vector<std::uint64_t> &shape,
                                                     const std::vector<std::uint64_t> &logical_positions) {
	if (logical_positions.size() > STANDARD_VECTOR_SIZE) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "selected positions exceed DuckDB vector size");
	}
	const auto row_count = CheckedShapeProduct(shape);
	if (WorstCaseBatchMappingBytes(shape.size(), logical_positions.size()) > MAX_BATCH_MAPPING_BYTES) {
		throw ReaderError(ReaderErrorCode::InvalidSelection,
		                  "selected batch mapping exceeds its 64 KiB budget; shrink the batch before building segments");
	}
	for (std::size_t index = 0; index < logical_positions.size(); index++) {
		if (logical_positions[index] >= row_count ||
		    (index != 0 && logical_positions[index] <= logical_positions[index - 1])) {
			throw ReaderError(ReaderErrorCode::InvalidSelection,
			                  "selected logical positions must be strictly increasing and within the source shape");
		}
	}
	std::vector<BatchSegment> result;
	result.reserve(logical_positions.size());
	std::size_t position = 0;
	while (position < logical_positions.size()) {
		std::size_t end = position + 1;
		while (end < logical_positions.size() && logical_positions[end] == logical_positions[end - 1] + 1) {
			end++;
		}
		auto current_index = logical_positions[position];
		std::uint64_t remaining = end - position;
		std::uint64_t batch_offset = position;
		while (remaining > 0) {
			auto coordinates = LinearIndexToCoordinates(shape, current_index);
			const auto axis_remaining = shape.back() - coordinates.back();
			const auto segment_count = std::min(remaining, axis_remaining);

			BatchSegment segment;
			segment.batch_offset = batch_offset;
			segment.linear_index = current_index;
			segment.count = segment_count;
			segment.read_offset = std::move(coordinates);
			segment.read_count.assign(shape.size(), 1);
			segment.read_count.back() = segment_count;
			result.emplace_back(std::move(segment));

			current_index += segment_count;
			batch_offset += segment_count;
			remaining -= segment_count;
		}
		position = end;
	}
	return result;
}

} // namespace duckomo
} // namespace duckdb
