#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "duckomo/projected_grid.hpp"

namespace duckdb {
namespace duckomo {

struct GaussianRow final {
	double latitude = 0;
	std::uint64_t point_count = 0;
	double longitude_origin = 0;
	double longitude_step = 0;
};

struct GaussianRegionSegment final {
	std::uint64_t parent_row = 0;
	std::uint64_t parent_begin = 0;
	std::uint64_t count = 0;
};

class GaussianGrid final {
public:
	GaussianGrid(std::uint64_t n, std::string latitude_rule, std::vector<GaussianRow> parent_rows,
	             std::vector<GaussianRegionSegment> subset_segments = {});

	std::uint64_t N() const noexcept;
	std::uint64_t PointCount() const noexcept;
	bool IsSubset() const noexcept;
	const std::string &LatitudeRule() const noexcept;
	const std::vector<GaussianRow> &Rows() const noexcept;
	const std::vector<GaussianRegionSegment> &SubsetSegments() const noexcept;
	std::uint64_t OwnedCapacityBytes() const noexcept;
	std::uint64_t ParentPointIndex(std::uint64_t local_point) const;
	GridCoordinate Coordinate(std::uint64_t local_point, GridNumericPolicy numeric_policy) const;

private:
	std::uint64_t n_ = 0;
	std::uint64_t point_count_ = 0;
	std::string latitude_rule_;
	std::vector<GaussianRow> rows_;
	std::vector<GaussianRegionSegment> segments_;
	std::vector<std::uint64_t> row_prefix_;
	std::vector<std::uint64_t> segment_prefix_;
};

} // namespace duckomo
} // namespace duckdb
