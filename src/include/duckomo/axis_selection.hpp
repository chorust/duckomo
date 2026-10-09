#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "duckomo/axis_filter.hpp"
#include "duckomo/batch.hpp"
#include "duckomo/selection_budget.hpp"

namespace duckdb {
namespace duckomo {

struct LogicalAxisRange final {
	std::uint64_t begin = 0;
	std::uint64_t end = 0;
};

class AxisSelectionCursor final {
public:
	AxisSelectionCursor(const std::vector<std::uint64_t> &shape, const SemanticAxes &semantic_axes,
	                    const AxisPredicate &predicate);

	bool HasConstraints() const noexcept;
	bool IsEmpty() const noexcept;
	bool IsExhausted() const noexcept;
	std::uint64_t CandidateCount() const noexcept;
	bool MatchesShape(const std::vector<std::uint64_t> &shape) const noexcept;
	std::uint64_t CandidateAxisCount(std::size_t axis) const;
	std::uint64_t CandidateWindowCount(std::size_t axis, std::uint64_t window_size) const;
	std::optional<std::uint64_t> NextSelectedAxisIndex(std::size_t axis, std::uint64_t minimum) const;
	std::uint64_t EstimatedBytes() const noexcept;
	std::uint64_t IntervalPayloadBytes() const noexcept;
	bool BudgetFallback() const noexcept;
	bool Contains(std::uint64_t logical_position) const;
	idx_t Next(idx_t limit, std::vector<std::uint64_t> &logical_positions,
	           const std::function<bool(std::uint64_t)> &include = {});

private:
	bool Advance();
	std::uint64_t CurrentLogicalPosition() const;

	std::vector<std::vector<LogicalAxisRange>> ranges;
	std::vector<std::uint64_t> strides;
	std::vector<std::uint64_t> indices;
	std::vector<std::size_t> range_indices;
	std::vector<std::uint64_t> axis_lengths;
	bool has_constraints = false;
	bool empty = false;
	bool exhausted = false;
	std::uint64_t candidate_count = 0;
	bool budget_fallback = false;
};

std::vector<BatchSegment> BuildSelectedBatchSegments(const std::vector<std::uint64_t> &shape,
	                                                 const std::vector<std::uint64_t> &logical_positions);

} // namespace duckomo
} // namespace duckdb
