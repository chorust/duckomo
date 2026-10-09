#include "duckomo/gaussian_grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "duckomo/om_reader.hpp"

namespace duckdb {
namespace duckomo {
namespace {

constexpr std::uint64_t MAX_GRID_POINTS = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

std::uint64_t AddChecked(std::uint64_t left, std::uint64_t right, const char *what) {
	if (right > MAX_GRID_POINTS || left > MAX_GRID_POINTS - right) {
		throw ReaderError(ReaderErrorCode::ShapeOverflow, std::string("Gaussian ") + what + " exceeds signed 64-bit limit");
	}
	return left + right;
}

double NormalizeLongitude(double longitude) {
	double normalized = std::fmod(longitude, 360.0);
	if (normalized < -180.0) normalized += 360.0;
	if (normalized >= 180.0) normalized -= 360.0;
	return normalized;
}

float NormalizeLongitude(float longitude) {
	float normalized = std::fmod(longitude, 360.0F);
	if (normalized < -180.0F) normalized += 360.0F;
	if (normalized >= 180.0F) normalized -= 360.0F;
	return normalized;
}

std::uint64_t FindPrefix(const std::vector<std::uint64_t> &prefix, std::uint64_t position) {
	const auto it = std::upper_bound(prefix.begin(), prefix.end(), position);
	if (it == prefix.begin() || it == prefix.end()) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "Gaussian local point is outside the grid");
	}
	return static_cast<std::uint64_t>(std::distance(prefix.begin(), it) - 1);
}

} // namespace

GaussianGrid::GaussianGrid(std::uint64_t n, std::string latitude_rule, std::vector<GaussianRow> parent_rows,
	                       std::vector<GaussianRegionSegment> subset_segments)
	: n_(n), latitude_rule_(std::move(latitude_rule)), rows_(std::move(parent_rows)), segments_(std::move(subset_segments)) {
	if (n_ == 0 || n_ > MAX_GRID_POINTS / 2 || rows_.size() != n_ * 2) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "Gaussian grid requires exactly 2N rows for positive N");
	}
	if (latitude_rule_ != "openmeteo_approx_v1" && latitude_rule_ != "legendre_roots_v1" &&
	    latitude_rule_ != "explicit_v1") {
		throw ReaderError(ReaderErrorCode::InvalidShape, "Gaussian latitude_rule is not supported");
	}
	row_prefix_.reserve(rows_.size() + 1);
	row_prefix_.push_back(0);
	for (std::size_t row_index = 0; row_index < rows_.size(); row_index++) {
		const auto &row = rows_[row_index];
		if (!std::isfinite(row.latitude) || row.latitude < -90 || row.latitude > 90 || row.point_count == 0 ||
		    !std::isfinite(row.longitude_origin) || !std::isfinite(row.longitude_step) || row.longitude_step == 0) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "Gaussian row contains invalid latitude or longitude parameters");
		}
		if (row_index > 0 && rows_[row_index - 1].latitude <= row.latitude) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "Gaussian latitude rows must be in strictly descending source order");
		}
		const auto last_lon = row.longitude_origin + static_cast<double>(row.point_count - 1) * row.longitude_step;
		if (!std::isfinite(last_lon)) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "Gaussian row longitude extent overflows DOUBLE");
		}
		row_prefix_.push_back(AddChecked(row_prefix_.back(), row.point_count, "parent point count"));
	}

	if (segments_.empty()) {
		point_count_ = row_prefix_.back();
		return;
	}

	segment_prefix_.reserve(segments_.size() + 1);
	segment_prefix_.push_back(0);
	std::vector<std::vector<std::pair<std::uint64_t, std::uint64_t>>> intervals(rows_.size());
	for (const auto &segment : segments_) {
		if (segment.parent_row >= rows_.size() || segment.count == 0 || segment.parent_begin > rows_[segment.parent_row].point_count ||
		    segment.count > rows_[segment.parent_row].point_count - segment.parent_begin) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "Gaussian subset segment is outside its parent row");
		}
		intervals[segment.parent_row].emplace_back(segment.parent_begin, segment.parent_begin + segment.count);
		segment_prefix_.push_back(AddChecked(segment_prefix_.back(), segment.count, "subset point count"));
	}
	for (auto &row_intervals : intervals) {
		std::sort(row_intervals.begin(), row_intervals.end());
		for (std::size_t i = 1; i < row_intervals.size(); i++) {
			if (row_intervals[i].first < row_intervals[i - 1].second) {
				throw ReaderError(ReaderErrorCode::InvalidShape, "Gaussian subset contains overlapping or duplicate parent points");
			}
		}
	}
	point_count_ = segment_prefix_.back();
}

std::uint64_t GaussianGrid::N() const noexcept { return n_; }
std::uint64_t GaussianGrid::PointCount() const noexcept { return point_count_; }
bool GaussianGrid::IsSubset() const noexcept { return !segments_.empty(); }
const std::string &GaussianGrid::LatitudeRule() const noexcept { return latitude_rule_; }
const std::vector<GaussianRow> &GaussianGrid::Rows() const noexcept { return rows_; }
const std::vector<GaussianRegionSegment> &GaussianGrid::SubsetSegments() const noexcept { return segments_; }

std::uint64_t GaussianGrid::OwnedCapacityBytes() const noexcept {
	constexpr auto limit = std::numeric_limits<std::uint64_t>::max();
	std::uint64_t bytes = latitude_rule_.capacity() < limit ? latitude_rule_.capacity() + 1 : limit;
	const auto add_capacity = [&](std::size_t capacity, std::size_t element_size) {
		if (capacity > (limit - bytes) / element_size) {
			bytes = limit;
		} else {
			bytes += capacity * element_size;
		}
	};
	add_capacity(rows_.capacity(), sizeof(GaussianRow));
	add_capacity(segments_.capacity(), sizeof(GaussianRegionSegment));
	add_capacity(row_prefix_.capacity(), sizeof(std::uint64_t));
	add_capacity(segment_prefix_.capacity(), sizeof(std::uint64_t));
	return bytes;
}

std::uint64_t GaussianGrid::ParentPointIndex(std::uint64_t local_point) const {
	if (local_point >= point_count_) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "Gaussian local point is outside the grid");
	}
	if (segments_.empty()) return local_point;
	const auto segment_index = FindPrefix(segment_prefix_, local_point);
	const auto &segment = segments_[static_cast<std::size_t>(segment_index)];
	const auto within = local_point - segment_prefix_[static_cast<std::size_t>(segment_index)];
	return row_prefix_[static_cast<std::size_t>(segment.parent_row)] + segment.parent_begin + within;
}

GridCoordinate GaussianGrid::Coordinate(std::uint64_t local_point, GridNumericPolicy numeric_policy) const {
	if (local_point >= point_count_) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "Gaussian local point is outside the grid");
	}
	std::uint64_t row_index = 0;
	std::uint64_t x_index = 0;
	if (segments_.empty()) {
		row_index = FindPrefix(row_prefix_, local_point);
		x_index = local_point - row_prefix_[static_cast<std::size_t>(row_index)];
	} else {
		const auto segment_index = FindPrefix(segment_prefix_, local_point);
		const auto &segment = segments_[static_cast<std::size_t>(segment_index)];
		row_index = segment.parent_row;
		x_index = segment.parent_begin + local_point - segment_prefix_[static_cast<std::size_t>(segment_index)];
	}
	const auto &row = rows_[static_cast<std::size_t>(row_index)];
	if (numeric_policy == GridNumericPolicy::OpenMeteoF32V1) {
		const float origin = static_cast<float>(row.longitude_origin);
		const float step = static_cast<float>(row.longitude_step);
		const float longitude_unwrapped = origin + static_cast<float>(x_index) * step;
		const float longitude = NormalizeLongitude(longitude_unwrapped);
		const float latitude = static_cast<float>(row.latitude);
		if (!std::isfinite(latitude) || !std::isfinite(longitude)) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "Gaussian grid produced a non-finite geographic coordinate");
		}
		return {latitude, longitude};
	}
	const double longitude = NormalizeLongitude(row.longitude_origin + static_cast<double>(x_index) * row.longitude_step);
	if (!std::isfinite(row.latitude) || !std::isfinite(longitude)) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "Gaussian grid produced a non-finite geographic coordinate");
	}
	return {row.latitude, longitude};
}

} // namespace duckomo
} // namespace duckdb
