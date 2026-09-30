#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "duckomo/batch.hpp"
#include "duckomo/spatial_filter.hpp"
#include "duckomo/spatial_layout.hpp"

namespace duckdb {
namespace duckomo {

enum class SpatialSelectionMode : std::uint8_t { Full, Restricted, Empty, Fallback };

struct SpatialSelection final {
	SpatialSelectionMode mode = SpatialSelectionMode::Full;
	std::vector<AxisRange> latitude_ranges;
	std::vector<AxisRange> longitude_ranges;
	std::uint64_t candidate_rows = 0;
	bool residual_filter_retained = true;
	std::vector<std::string> fallback_reasons;
};

struct SpatialBatch final {
	std::vector<std::uint64_t> logical_positions;
	std::vector<BatchSegment> read_segments;
};

SpatialSelection BuildSpatialSelection(const RegularGrid &grid, const SpatialLayout &layout,
	                                   const SpatialPredicate &predicate);
const char *SpatialSelectionModeName(SpatialSelectionMode mode) noexcept;

// Emits at most vector_size source positions per batch, preserving source
// order and keeping each OM read segment contiguous along its final axis.
class SpatialBatchCursor final {
public:
	SpatialBatchCursor(const RegularGrid &grid, const SpatialLayout &layout, SpatialSelection selection);
	bool Next(std::uint64_t vector_size, SpatialBatch &batch,
	          const std::function<void()> &interrupt_check = {});
	bool Exhausted() const noexcept;

private:
	std::optional<std::uint64_t> NextAllowedIndex(std::size_t axis, std::uint64_t minimum) const;
	bool FillMinimumSuffix(std::size_t axis, std::vector<std::uint64_t> &indices) const;
	bool SeekCandidate(std::size_t axis, bool follow_lower_bound,
	                    const std::vector<std::uint64_t> &lower_bound,
	                    std::vector<std::uint64_t> &indices) const;
	bool FindNextCandidate(std::uint64_t logical_index, std::uint64_t &candidate) const;
	void BuildAxisRanges();
	void BuildReadSegments(SpatialBatch &batch) const;

	RegularGrid grid_;
	SpatialLayout layout_;
	SpatialSelection selection_;
	std::vector<std::vector<AxisRange>> axis_ranges_;
	std::uint64_t next_source_index_ = 0;
	bool exhausted_ = false;
};

} // namespace duckomo
} // namespace duckdb
