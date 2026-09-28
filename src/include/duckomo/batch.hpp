#pragma once

#include <cstdint>
#include <vector>

#include "duckdb/common/vector_size.hpp"
#include "duckomo/om_reader.hpp"

namespace duckdb {
namespace duckomo {

// One contiguous OM read along the final (fastest-varying) axis. `batch_offset`
// is the position to place the returned values at in the DuckDB output batch;
// `linear_index` is the corresponding row-major position in the full array.
struct BatchSegment final {
	std::uint64_t batch_offset = 0;
	std::uint64_t linear_index = 0;
	std::uint64_t count = 0;
	std::vector<std::uint64_t> read_offset;
	std::vector<std::uint64_t> read_count;
};

// Converts a valid row-major linear index to its per-axis coordinates.
// Invalid shapes and indices throw ReaderError with InvalidShape,
// ShapeOverflow, or InvalidSelection as appropriate.
std::vector<std::uint64_t> LinearIndexToCoordinates(const std::vector<std::uint64_t> &shape,
                                                   std::uint64_t linear_index);

// Splits a logical row range into final-axis-contiguous OM reads. The requested
// range must fit within one DuckDB vector batch and within the array. A range
// ending exactly at row_count is valid; an empty range is valid when its start
// is at or before row_count.
std::vector<BatchSegment> BuildBatchSegments(const std::vector<std::uint64_t> &shape,
                                             std::uint64_t linear_start, std::uint64_t count);

} // namespace duckomo
} // namespace duckdb
