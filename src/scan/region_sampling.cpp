#include "duckomo/region_sampling.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "duckdb/common/exception.hpp"
#include "duckdb/common/exception/binder_exception.hpp"

namespace duckdb {
namespace duckomo {

std::uint64_t SpatialLayoutLogicalIndex(const SpatialLayout &layout,
                                        const std::vector<std::uint64_t> &non_spatial_indices,
                                        std::uint64_t x, std::uint64_t y, std::uint64_t point_index) {
	return layout.SourcePosition(non_spatial_indices, x, y, point_index).logical_index;
}

NearestAxisCursor::NearestAxisCursor(const SpatialLayout &layout, AxisFilter include_p)
    : axes(layout.non_spatial_axes), include(std::move(include_p)) {
	if (layout.shape.empty() || layout.axes.size() != layout.shape.size() ||
	    layout.strides.size() != layout.shape.size()) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "nearest axis cursor requires a complete spatial layout");
	}
	lengths.reserve(axes.size());
	current.assign(axes.size(), 0);
	result_count = 1;
	for (const auto axis : axes) {
		if (axis >= layout.shape.size() || layout.shape[axis] == 0) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "nearest axis cursor has an invalid non-spatial axis");
		}
		const auto length = layout.shape[axis];
		if (result_count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / length) {
			throw ReaderError(ReaderErrorCode::ShapeOverflow, "nearest result count exceeds signed 64-bit limit");
		}
		result_count *= length;
		lengths.push_back(length);
	}
	if (axes.empty()) result_count = 1;
}

NearestAxisCursor::NearestAxisCursor(const SpatialLayout &layout, const AxisSelectionCursor &selection_p)
    : NearestAxisCursor(layout) {
	if (!selection_p.MatchesShape(layout.shape)) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "nearest axis selection does not match the source layout");
	}
	selection = &selection_p;
	result_count = 1;
	for (const auto axis : axes) {
		const auto count = selection->CandidateAxisCount(axis);
		if (count == 0) {
			result_count = 0;
			exhausted = true;
			return;
		}
		if (result_count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / count) {
			throw ReaderError(ReaderErrorCode::ShapeOverflow, "nearest selected result count exceeds signed 64-bit limit");
		}
		result_count *= count;
	}
	if (axes.empty()) result_count = selection->IsEmpty() ? 0 : 1;
	if (result_count == 0) exhausted = true;
}

bool NearestAxisCursor::IsExhausted() const noexcept { return exhausted; }
std::uint64_t NearestAxisCursor::ResultCount() const noexcept { return result_count; }

bool NearestAxisCursor::AdvanceSelected() {
	if (axes.empty()) {
		exhausted = true;
		return false;
	}
	for (std::size_t reverse = current.size(); reverse > 0; --reverse) {
		const auto position = reverse - 1;
		const auto axis = static_cast<std::size_t>(axes[position]);
		const auto next = selection->NextSelectedAxisIndex(axis, current[position] + 1);
		if (next) {
			current[position] = *next;
			for (std::size_t suffix = position + 1; suffix < current.size(); ++suffix) {
				const auto first = selection->NextSelectedAxisIndex(static_cast<std::size_t>(axes[suffix]), 0);
				if (!first) {
					exhausted = true;
					return false;
				}
				current[suffix] = *first;
			}
			return true;
		}
		const auto first = selection->NextSelectedAxisIndex(axis, 0);
		if (!first) {
			exhausted = true;
			return false;
		}
		current[position] = *first;
	}
	exhausted = true;
	return false;
}

NearestAxisBatch NearestAxisCursor::Next(std::uint64_t limit) {
	if (limit == 0) throw ReaderError(ReaderErrorCode::InvalidSelection, "nearest axis batch limit must be positive");
	NearestAxisBatch batch;
	if (exhausted) {
		batch.exhausted = true;
		return batch;
	}
	batch.indices.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(limit, result_count)));
	auto advance = [&]() {
		if (axes.empty()) {
			exhausted = true;
			return;
		}
		for (std::size_t reverse = current.size(); reverse > 0; --reverse) {
			const auto index = reverse - 1;
			if (++current[index] < lengths[index]) return;
			current[index] = 0;
		}
		exhausted = true;
	};
	while (!exhausted && batch.indices.size() < limit) {
		if (!started) {
			started = true;
			if (selection) {
				for (std::size_t position = 0; position < axes.size(); ++position) {
					const auto first = selection->NextSelectedAxisIndex(static_cast<std::size_t>(axes[position]), 0);
					if (!first) {
						exhausted = true;
						break;
					}
					current[position] = *first;
				}
			}
		} else if (selection) {
			(void)AdvanceSelected();
		} else {
			advance();
		}
		if (exhausted) break;
		bool accepted = true;
		if (include) {
			for (std::size_t position = 0; position < axes.size(); ++position) {
				if (!include(static_cast<std::size_t>(axes[position]), current[position])) {
					accepted = false;
					break;
				}
			}
		}
		if (accepted) batch.indices.push_back(current);
		if (axes.empty()) exhausted = true;
	}
	batch.exhausted = exhausted;
	return batch;
}

RegionSamplingOptions ParseRegionSamplingOptions(const Value *method_value, const Value *max_distance_value,
                                                 const Value *out_of_bounds_value) {
	RegionSamplingOptions options;
	options.max_distance_explicit = max_distance_value != nullptr;
	options.out_of_bounds_explicit = out_of_bounds_value != nullptr;
	if (method_value != nullptr) {
		if (method_value->IsNull() || method_value->type().id() != LogicalTypeId::VARCHAR) {
			throw BinderException("[region_method] method must be a non-NULL VARCHAR");
		}
		const auto method = method_value->GetValue<std::string>();
		if (method == "points") options.method = RegionMethod::Points;
		else if (method == "nearest") options.method = RegionMethod::Nearest;
		else throw BinderException("[region_method] method must be exactly 'points' or 'nearest'");
	}
	if (options.method == RegionMethod::Points && (options.max_distance_explicit || options.out_of_bounds_explicit)) {
		throw BinderException("[region_method] max_distance_m and out_of_bounds are only valid for method='nearest'");
	}
	if (options.method == RegionMethod::Nearest && max_distance_value != nullptr) {
		if (max_distance_value->IsNull()) throw BinderException("[region_sampling] max_distance_m must not be NULL");
		const auto type = max_distance_value->type().id();
		const bool numeric = type == LogicalTypeId::TINYINT || type == LogicalTypeId::SMALLINT ||
		                     type == LogicalTypeId::INTEGER || type == LogicalTypeId::BIGINT ||
		                     type == LogicalTypeId::UTINYINT || type == LogicalTypeId::USMALLINT ||
		                     type == LogicalTypeId::UINTEGER || type == LogicalTypeId::UBIGINT ||
		                     type == LogicalTypeId::FLOAT || type == LogicalTypeId::DOUBLE || type == LogicalTypeId::DECIMAL ||
		                     type == LogicalTypeId::HUGEINT || type == LogicalTypeId::UHUGEINT;
		if (!numeric) throw BinderException("[region_sampling] max_distance_m must be numeric");
		if (type == LogicalTypeId::BOOLEAN)
			throw BinderException("[region_sampling] max_distance_m must not be BOOLEAN");
		try {
			const auto converted = max_distance_value->DefaultCastAs(LogicalType(LogicalTypeId::DOUBLE), true);
			const auto distance = converted.GetValue<double>();
			if (!std::isfinite(distance) || distance < 0.0)
				throw BinderException("[region_sampling] max_distance_m must be finite and non-negative");
			options.max_distance_m = distance;
		} catch (const BinderException &) {
			throw;
		} catch (const std::exception &) {
			throw BinderException("[region_sampling] max_distance_m cannot be converted to DOUBLE");
		}
	}
	if (options.method == RegionMethod::Nearest && out_of_bounds_value != nullptr) {
		if (out_of_bounds_value->IsNull() || out_of_bounds_value->type().id() != LogicalTypeId::VARCHAR)
			throw BinderException("[region_sampling] out_of_bounds must be a non-NULL VARCHAR");
		const auto policy = out_of_bounds_value->GetValue<std::string>();
		if (policy == "null") options.out_of_bounds = OutOfBoundsPolicy::Null;
		else if (policy == "error") options.out_of_bounds = OutOfBoundsPolicy::Error;
		else throw BinderException("[region_sampling] out_of_bounds must be exactly 'null' or 'error'");
	}
	return options;
}

std::uint64_t NearestSamplingPlan::EstimatedBytes() const noexcept {
	const auto add = [](std::uint64_t left, std::uint64_t right) {
		return left > std::numeric_limits<std::uint64_t>::max() - right
		           ? std::numeric_limits<std::uint64_t>::max()
		           : left + right;
	};
	std::uint64_t bytes = sizeof(*this);
	bytes = add(bytes, static_cast<std::uint64_t>(spatial_version.capacity() + 1));
	bytes = add(bytes, static_cast<std::uint64_t>(grid_id.capacity() + 1));
	bytes = add(bytes, static_cast<std::uint64_t>(layout_id.capacity() + 1));
	bytes = add(bytes, static_cast<std::uint64_t>(regions.capacity() * sizeof(NearestRegionPlanEntry)));
	return bytes;
}

bool NearestSamplingPlan::Equals(const NearestSamplingPlan &other) const noexcept {
	if (spatial_version != other.spatial_version || grid_id != other.grid_id || layout_id != other.layout_id ||
	    earth_radius_m != other.earth_radius_m || regions.size() != other.regions.size()) return false;
	for (std::size_t index = 0; index < regions.size(); ++index) {
		const auto &left = regions[index];
		const auto &right = other.regions[index];
		if (left.status != right.status || left.has_representative != right.has_representative ||
		    (left.has_representative && (left.representative.latitude != right.representative.latitude ||
		                                 left.representative.longitude != right.representative.longitude)) ||
		    left.location.inside_domain != right.location.inside_domain ||
		    left.location.source.has_value() != right.location.source.has_value()) return false;
		if (left.location.source && (left.location.source->point_index != right.location.source->point_index ||
		                             left.location.source->distance_m != right.location.source->distance_m)) return false;
	}
	return true;
}

std::shared_ptr<const NearestSamplingPlan> BuildNearestSamplingPlan(
    const RegularGrid &grid, const NearestGridCapability &capability, const std::vector<NearestRegionTarget> &targets,
    std::string spatial_version, std::string grid_id, std::string layout_id,
    const std::optional<double> &max_distance_m, OutOfBoundsPolicy out_of_bounds,
    const NearestCancellationCheck &cancelled) {
	if (spatial_version.empty() || grid_id.empty() || layout_id.empty())
		throw BinderException("[spatial_dependency] nearest sampling plan requires actual spatial, grid, and layout identities");
	if (max_distance_m && (!std::isfinite(*max_distance_m) || *max_distance_m < 0))
		throw BinderException("[region_sampling] max_distance_m must be finite and non-negative");
	auto plan = std::make_shared<NearestSamplingPlan>();
	plan->spatial_version = std::move(spatial_version);
	plan->grid_id = std::move(grid_id);
	plan->layout_id = std::move(layout_id);
	plan->earth_radius_m = capability.earth_radius_m;
	plan->regions.reserve(targets.size());
	for (const auto &target : targets) {
		if (cancelled && cancelled()) throw InterruptException();
		NearestRegionPlanEntry entry;
		if (target.empty) {
			entry.status = GeometryStatus::EmptyGeometry;
			plan->regions.emplace_back(std::move(entry));
			continue;
		}
		entry.has_representative = true;
		entry.representative = target.representative;
		entry.location = LocateNearestRegularGrid(grid, capability, target.representative.longitude,
		                                          target.representative.latitude, cancelled);
		if (!entry.location.inside_domain) {
			if (out_of_bounds == OutOfBoundsPolicy::Error)
				throw BinderException("[region_sampling] representative point is outside the nearest grid domain");
			entry.status = GeometryStatus::OutsideDomain;
		} else if (!entry.location.source) {
			throw InternalException("[region_sampling] in-domain target produced no nearest source candidate");
		} else if (max_distance_m && entry.location.source->distance_m > *max_distance_m) {
			entry.status = GeometryStatus::BeyondMaxDistance;
		} else {
			entry.status = GeometryStatus::Ok;
		}
		plan->regions.emplace_back(std::move(entry));
		if (plan->EstimatedBytes() > 8ULL * 1024 * 1024)
			throw BinderException("[region_input_limit] immutable nearest plan exceeds 8 MiB");
	}
	return plan;
}

} // namespace duckdb
} // namespace duckomo
