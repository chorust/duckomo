#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "duckomo/spatial_filter.hpp"
#include "duckomo/spatial_selection.hpp"

namespace duckdb {
namespace duckomo {

struct NativeWindowPreflight final {
	std::vector<AxisRange> ranges;
	std::uint64_t coordinate_evaluations = 0;
	bool whole_window = false;
	bool budget_fallback = false;
};

using NativeWindowCoordinate = std::function<GridCoordinate(const NativeWindow &, std::uint64_t)>;

bool MatchesSpatialPredicate(const SpatialPredicate &predicate, const GridCoordinate &coordinate);

// Selects exact offsets within one bounded native window. The engine WHERE
// remains installed; callers widen the whole window whenever the range budget
// cannot represent every matching interval.
NativeWindowPreflight PreflightNativeWindow(const SpatialPredicate &predicate, const NativeWindow &window,
	                                          const NativeWindowCoordinate &coordinate,
	                                          const std::function<void()> &interrupt_check,
	                                          std::uint64_t &coordinate_evaluations);

} // namespace duckomo
} // namespace duckdb
