#include "duckomo/batch.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace duckdb {
namespace duckomo {

std::vector<std::uint64_t> LinearIndexToCoordinates(const std::vector<std::uint64_t> &shape,
                                                   std::uint64_t linear_index) {
	const auto row_count = CheckedShapeProduct(shape);
	if (linear_index >= row_count) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "OM linear index is outside the array row range");
	}

	std::vector<std::uint64_t> coordinates(shape.size());
	for (std::size_t axis = shape.size(); axis > 0; axis--) {
		const auto current_axis = axis - 1;
		coordinates[current_axis] = linear_index % shape[current_axis];
		linear_index /= shape[current_axis];
	}
	return coordinates;
}

std::vector<BatchSegment> BuildBatchSegments(const std::vector<std::uint64_t> &shape,
                                             std::uint64_t linear_start, std::uint64_t count) {
	const auto row_count = CheckedShapeProduct(shape);
	if (linear_start > row_count) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "OM batch start is outside the array row range");
	}
	if (count > static_cast<std::uint64_t>(STANDARD_VECTOR_SIZE)) {
		throw ReaderError(ReaderErrorCode::InvalidSelection,
		                  "OM batch row count exceeds DuckDB STANDARD_VECTOR_SIZE");
	}
	// Subtract after checking start so range validation cannot overflow.
	if (count > row_count - linear_start) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "OM batch range extends past the array row count");
	}

	std::vector<BatchSegment> segments;
	std::uint64_t remaining = count;
	std::uint64_t current_index = linear_start;
	std::uint64_t batch_offset = 0;
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
		segments.emplace_back(std::move(segment));

		current_index += segment_count;
		batch_offset += segment_count;
		remaining -= segment_count;
	}
	return segments;
}

std::uint64_t WorstCaseBatchMappingBytes(std::size_t rank, std::uint64_t position_count) noexcept {
	constexpr auto MAX_VALUE = std::numeric_limits<std::uint64_t>::max();
	const auto vector_headers = static_cast<std::uint64_t>(sizeof(std::vector<std::uint64_t>) +
	                                                       sizeof(std::vector<BatchSegment>));
	if (rank > (MAX_VALUE - sizeof(std::uint64_t) - sizeof(BatchSegment)) /
	                (2 * sizeof(std::uint64_t))) {
		return MAX_VALUE;
	}
	const auto per_position = static_cast<std::uint64_t>(sizeof(std::uint64_t) + sizeof(BatchSegment)) +
	                          static_cast<std::uint64_t>(2 * rank * sizeof(std::uint64_t));
	if (position_count != 0 && per_position > (MAX_VALUE - vector_headers) / position_count) {
		return MAX_VALUE;
	}
	return vector_headers + position_count * per_position;
}

std::uint64_t MaxBatchPositionCount(std::size_t rank) noexcept {
	const auto per_position = WorstCaseBatchMappingBytes(rank, 1) -
	                          static_cast<std::uint64_t>(sizeof(std::vector<std::uint64_t>) +
	                                                     sizeof(std::vector<BatchSegment>));
	if (per_position == 0 || per_position >= MAX_BATCH_MAPPING_BYTES) return 1;
	const auto available = MAX_BATCH_MAPPING_BYTES -
	                       static_cast<std::uint64_t>(sizeof(std::vector<std::uint64_t>) +
	                                                  sizeof(std::vector<BatchSegment>));
	return std::max<std::uint64_t>(1, std::min<std::uint64_t>(STANDARD_VECTOR_SIZE, available / per_position));
}

} // namespace duckomo
} // namespace duckdb
