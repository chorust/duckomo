#include "duckomo/axis_selection.hpp"

#include <algorithm>
#include <limits>

#include "duckdb/common/exception.hpp"
#include "duckomo/om_reader.hpp"

namespace duckdb {
namespace duckomo {
namespace {

constexpr std::size_t MAX_AXIS_RANGES = 65536;

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

	for (std::size_t axis_index = 0; axis_index < shape.size(); axis_index++) {
		auto &axis_ranges = ranges[axis_index];
		if (shape[axis_index] == 0) {
			empty = true;
			exhausted = true;
			return;
		}
		if (constraints[axis_index].empty()) {
			axis_ranges.push_back({0, shape[axis_index]});
			continue;
		}
		auto semantic = std::find_if(semantic_axes.begin(), semantic_axes.end(),
		                             [axis_index](const SemanticAxis &candidate) {
			                             return candidate.axis_index == axis_index;
		                             });
		if (semantic == semantic_axes.end() || semantic->axis_length != shape[axis_index]) {
			throw InternalException("typed axis predicate has no matching semantic coordinate mapping");
		}
		for (std::uint64_t coordinate_index = 0; coordinate_index < shape[axis_index]; coordinate_index++) {
			if (coordinate_index > std::numeric_limits<std::uint64_t>::max() / semantic->stride) {
				throw ReaderError(ReaderErrorCode::ShapeOverflow, "semantic coordinate position overflows");
			}
			const auto logical_position = coordinate_index * semantic->stride;
			const auto coordinate = semantic->CoordinateValue(logical_position);
			bool matches = true;
			for (const auto *constraint : constraints[axis_index]) {
				if (!Satisfies(coordinate, *constraint)) {
					matches = false;
					break;
				}
			}
			if (matches) {
				AppendRange(axis_ranges, coordinate_index);
				if (axis_ranges.size() > MAX_AXIS_RANGES) {
					axis_ranges.clear();
					axis_ranges.push_back({0, shape[axis_index]});
					break;
				}
			}
		}
		if (axis_ranges.empty()) {
			empty = true;
			exhausted = true;
			return;
		}
	}
	for (std::size_t axis = 0; axis < ranges.size(); axis++) {
		if (ranges[axis].empty()) {
			empty = true;
			exhausted = true;
			return;
		}
		indices[axis] = ranges[axis].front().begin;
	}
	candidate_count = 1;
	for (const auto &axis_ranges : ranges) {
		std::uint64_t axis_count = 0;
		for (const auto &range : axis_ranges) {
			const auto length = range.end - range.begin;
			if (axis_count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - length) {
				throw ReaderError(ReaderErrorCode::ShapeOverflow, "semantic candidate count exceeds signed 64-bit range");
			}
			axis_count += length;
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

std::uint64_t AxisSelectionCursor::CandidateCount() const noexcept {
	return candidate_count;
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

bool AxisSelectionCursor::Contains(std::uint64_t logical_position) const {
	if (empty) return false;
	for (std::size_t axis = 0; axis < ranges.size(); axis++) {
		const auto index = (logical_position / strides[axis]) % axis_lengths[axis];
		const auto &axis_ranges = ranges[axis];
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
		const auto &current_range = axis_ranges[range_indices[axis]];
		if (indices[axis] + 1 < current_range.end) {
			indices[axis]++;
			for (std::size_t suffix = axis + 1; suffix < indices.size(); suffix++) {
				range_indices[suffix] = 0;
				indices[suffix] = ranges[suffix].front().begin;
			}
			return true;
		}
		if (range_indices[axis] + 1 < axis_ranges.size()) {
			range_indices[axis]++;
			indices[axis] = axis_ranges[range_indices[axis]].begin;
			for (std::size_t suffix = axis + 1; suffix < indices.size(); suffix++) {
				range_indices[suffix] = 0;
				indices[suffix] = ranges[suffix].front().begin;
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
	std::vector<BatchSegment> result;
	std::size_t position = 0;
	while (position < logical_positions.size()) {
		std::size_t end = position + 1;
		while (end < logical_positions.size() && logical_positions[end] == logical_positions[end - 1] + 1) {
			end++;
		}
		auto contiguous = BuildBatchSegments(shape, logical_positions[position], end - position);
		for (auto &segment : contiguous) {
			segment.batch_offset += position;
			result.emplace_back(std::move(segment));
		}
		position = end;
	}
	return result;
}

} // namespace duckomo
} // namespace duckdb
