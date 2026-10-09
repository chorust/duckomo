#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "duckomo/batch.hpp"
#include "duckomo/selection_budget.hpp"
#include "duckomo/spatial_filter.hpp"
#include "duckomo/spatial_layout.hpp"

namespace duckdb {
namespace duckomo {

class AxisSelectionCursor;

enum class SpatialSelectionMode : std::uint8_t { Full, Restricted, Empty, Fallback };

// A bounded descriptor for one native scanline/window. Fixed axis indices are
// source indices for every axis except `spatial_axis`, whose half-open range is
// [begin, end). Construction validates shape, extent and payload before
// allocating the copied indices.
class NativeWindow final {
public:
	static NativeWindow Create(std::uint64_t spatial_axis, std::uint64_t begin, std::uint64_t count,
	                           const std::vector<std::uint64_t> &shape,
	                           const std::vector<std::uint64_t> &fixed_axis_indices);

	std::uint64_t SpatialAxis() const noexcept { return spatial_axis_; }
	std::uint64_t Begin() const noexcept { return begin_; }
	std::uint64_t End() const noexcept { return end_; }
	std::uint64_t Count() const noexcept { return end_ - begin_; }
	const std::vector<std::uint64_t> &FixedAxisIndices() const noexcept { return fixed_axis_indices_; }
	std::uint64_t LogicalPosition(const SpatialLayout &layout, std::uint64_t offset) const;
	std::uint64_t EstimatedBytes() const noexcept;

private:
	std::uint64_t spatial_axis_ = 0;
	std::uint64_t begin_ = 0;
	std::uint64_t end_ = 0;
	std::vector<std::uint64_t> fixed_axis_indices_;
};

// Lazily enumerates non-overlapping windows over the fastest spatial axis,
// or the final storage axis when contiguous value reads are requested.
// Advancing the cursor creates one O(rank) descriptor and never scans points.
class NativeWindowCursor final {
public:
	explicit NativeWindowCursor(const SpatialLayout &layout, bool contiguous_reads = false);

	bool Next(NativeWindow &window, const std::function<void()> &interrupt_check = {});
	bool Next(NativeWindow &window, const AxisSelectionCursor &axis_selection,
	          const std::function<void()> &interrupt_check = {});
	std::uint64_t SpatialAxis() const noexcept { return spatial_axis_; }
	std::uint64_t WindowUpperBound() const noexcept { return window_upper_bound_; }
	std::uint64_t WindowUpperBound(const AxisSelectionCursor &axis_selection) const;
	std::uint64_t EstimatedBytes() const noexcept;

private:
	bool NextInternal(NativeWindow &window, const AxisSelectionCursor *axis_selection,
	                  const std::function<void()> &interrupt_check);
	bool InitializeSelectedIndices(const AxisSelectionCursor *axis_selection);
	bool AdvanceFixedIndices(const AxisSelectionCursor *axis_selection,
	                         const std::function<void()> &interrupt_check);

	SpatialLayout layout_;
	std::vector<std::uint64_t> fixed_axis_indices_;
	std::uint64_t spatial_axis_ = 0;
	std::uint64_t next_begin_ = 0;
	std::uint64_t window_upper_bound_ = 0;
	bool initialized_ = false;
	bool exhausted_ = false;
};

struct SpatialSelection final {
	SpatialSelectionMode mode = SpatialSelectionMode::Full;
	std::vector<AxisRange> latitude_ranges;
	std::vector<AxisRange> longitude_ranges;
	std::uint64_t candidate_rows = 0;
	bool residual_filter_retained = true;
	std::vector<std::string> fallback_reasons;
	bool budget_fallback = false;
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
	const SpatialSelection &Selection() const noexcept;
	std::uint64_t EstimatedBytes() const noexcept;

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
