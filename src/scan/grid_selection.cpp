#include "duckomo/grid_selection.hpp"

#include <algorithm>

#include "duckomo/selection_budget.hpp"

namespace duckdb {
namespace duckomo {
namespace {

} // namespace

bool MatchesSpatialPredicate(const SpatialPredicate &predicate, const GridCoordinate &coordinate) {
	for (const auto &constraint : predicate.necessary_conditions) {
		const auto value = constraint.axis == SpatialAxis::Latitude ? coordinate.latitude : coordinate.longitude;
		switch (constraint.comparison) {
		case SpatialComparison::Equal: if (value != constraint.constant) return false; break;
		case SpatialComparison::Less: if (!(value < constraint.constant)) return false; break;
		case SpatialComparison::LessEqual: if (!(value <= constraint.constant)) return false; break;
		case SpatialComparison::Greater: if (!(value > constraint.constant)) return false; break;
		case SpatialComparison::GreaterEqual: if (!(value >= constraint.constant)) return false; break;
		}
	}
	if (!predicate.longitude_union.empty()) {
		const bool matches_any = std::any_of(predicate.longitude_union.begin(), predicate.longitude_union.end(),
		                                     [&](const SpatialInterval &interval) {
			                                     const bool above_lower = interval.lower_inclusive
			                                                                 ? coordinate.longitude >= interval.lower
			                                                                 : coordinate.longitude > interval.lower;
			                                     const bool below_upper = interval.upper_inclusive
			                                                                 ? coordinate.longitude <= interval.upper
			                                                                 : coordinate.longitude < interval.upper;
			                                     return above_lower && below_upper;
		                                     });
		if (!matches_any) return false;
	}
	return true;
}

NativeWindowPreflight PreflightNativeWindow(const SpatialPredicate &predicate, const NativeWindow &window,
	                                          const NativeWindowCoordinate &coordinate,
	                                          const std::function<void()> &interrupt_check,
	                                          std::uint64_t &coordinate_evaluations) {
	NativeWindowPreflight result;
	coordinate_evaluations = 0;
	const bool has_spatial_filter = !predicate.necessary_conditions.empty() || !predicate.longitude_union.empty();
	if (!has_spatial_filter) {
		result.whole_window = true;
		return result;
	}
	constexpr std::uint64_t COORDINATE_SCRATCH_BYTES = sizeof(GridCoordinate);
	const auto descriptor_bytes = window.EstimatedBytes();
	if (descriptor_bytes >= MAX_SELECTOR_PAYLOAD_BYTES - COORDINATE_SCRATCH_BYTES) {
		result.whole_window = true;
		result.budget_fallback = true;
		return result;
	}
	const auto range_budget = MAX_SELECTOR_PAYLOAD_BYTES - descriptor_bytes - COORDINATE_SCRATCH_BYTES;
	const auto range_limit = std::min<std::uint64_t>(MAX_SPATIAL_SELECTOR_RANGES, range_budget / sizeof(AxisRange));
	if (range_limit == 0) {
		result.whole_window = true;
		result.budget_fallback = true;
		return result;
	}
	const auto matches = [&](std::uint64_t offset) {
		if ((result.coordinate_evaluations & 255U) == 0 && interrupt_check) interrupt_check();
		const auto point = coordinate(window, offset);
		result.coordinate_evaluations++;
		coordinate_evaluations++;
		return MatchesSpatialPredicate(predicate, point);
	};
	std::uint64_t required_ranges = 0;
	bool in_range = false;
	for (std::uint64_t offset = 0; offset < window.Count(); offset++) {
		const bool selected = matches(offset);
		if (selected && !in_range) required_ranges++;
		in_range = selected;
	}
	if (required_ranges == 0) return result;
	if (required_ranges > range_limit) {
		result.whole_window = true;
		result.budget_fallback = true;
		return result;
	}
	result.ranges.reserve(static_cast<std::size_t>(required_ranges));
	if (result.ranges.capacity() > range_budget / sizeof(AxisRange)) {
		std::vector<AxisRange>().swap(result.ranges);
		result.whole_window = true;
		result.budget_fallback = true;
		return result;
	}
	in_range = false;
	std::uint64_t range_begin = 0;
	for (std::uint64_t offset = 0; offset < window.Count(); offset++) {
		const bool selected = matches(offset);
		if (selected && !in_range) {
			range_begin = offset;
			in_range = true;
		} else if (!selected && in_range) {
			result.ranges.push_back({range_begin, offset});
			in_range = false;
		}
	}
	if (in_range) result.ranges.push_back({range_begin, window.Count()});
	return result;
}

} // namespace duckomo
} // namespace duckdb
