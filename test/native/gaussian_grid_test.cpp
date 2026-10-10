#include "duckomo/gaussian_grid.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "duckomo/om_reader.hpp"

namespace {
using namespace duckdb::duckomo;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

void RequireNear(double actual, double expected, double tolerance, const std::string &message) {
	if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
		throw std::runtime_error(message + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

void RequireError(ReaderErrorCode code, const std::function<void()> &action, const std::string &message) {
	try {
		action();
	} catch (const ReaderError &error) {
		Require(error.Code() == code, message + ": wrong error category");
		return;
	}
	throw std::runtime_error(message + ": expected ReaderError");
}

std::string Trim(std::string value) {
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) return {};
	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

std::vector<std::map<std::string, std::string>> ReadDefinitionObjects(const std::string &definition_id,
	                                                                  const std::string &array_name) {
	std::ifstream input("test/data/grids/definitions.json");
	Require(static_cast<bool>(input), "frozen grid definitions.json is available from the repository root");
	std::string line;
	bool in_definition = false;
	bool in_array = false;
	std::map<std::string, std::string> current;
	std::vector<std::map<std::string, std::string>> result;
	while (std::getline(input, line)) {
		if (line.find("\"id\":") != std::string::npos) {
			if (in_definition) break;
			in_definition = line.find("\"id\": \"" + definition_id + "\"") != std::string::npos;
			continue;
		}
		if (!in_definition) continue;
		if (!in_array) {
			if (line.find("\"" + array_name + "\": [") != std::string::npos) in_array = true;
			continue;
		}
		const auto trimmed = Trim(line);
		if (trimmed == "]," || trimmed == "]") break;
		if (trimmed == "{") {
			current.clear();
			continue;
		}
		if (trimmed == "}," || trimmed == "}") {
			if (!current.empty()) result.push_back(current);
			current.clear();
			continue;
		}
		const auto colon = trimmed.find(':');
		if (colon == std::string::npos) continue;
		auto key = Trim(trimmed.substr(0, colon));
		if (key.size() >= 2 && key.front() == '"' && key.back() == '"') key = key.substr(1, key.size() - 2);
		auto value = Trim(trimmed.substr(colon + 1));
		if (!value.empty() && value.back() == ',') value.pop_back();
		current.emplace(std::move(key), Trim(std::move(value)));
	}
	Require(in_array && !result.empty(), "frozen grid definition contains the requested non-empty array");
	return result;
}

std::vector<GaussianRow> ReadFrozenGaussianRows(const std::string &definition_id) {
	const auto objects = ReadDefinitionObjects(definition_id, "rows");
	std::vector<GaussianRow> rows;
	rows.reserve(objects.size());
	for (const auto &object : objects) {
		rows.push_back({std::stod(object.at("latitude")), std::stoull(object.at("point_count")),
		                std::stod(object.at("longitude_origin")), std::stod(object.at("longitude_step"))});
	}
	return rows;
}

std::vector<GaussianRegionSegment> ReadFrozenGaussianSegments(const std::string &definition_id) {
	const auto objects = ReadDefinitionObjects(definition_id, "segments");
	std::vector<GaussianRegionSegment> segments;
	segments.reserve(objects.size());
	for (const auto &object : objects) {
		segments.push_back({std::stoull(object.at("parent_row")), std::stoull(object.at("parent_begin")),
		                    std::stoull(object.at("count"))});
	}
	return segments;
}

std::vector<GaussianRow> SmallExplicitRows() {
	return {{60, 4, 0, 90}, {20, 6, 0, 60}, {-20, 6, 0, 60}, {-60, 4, 0, 90}};
}

void TestExplicitRowsAndSourcePositions() {
	GaussianGrid grid(2, "explicit_v1", SmallExplicitRows());
	Require(grid.PointCount() == 20 && !grid.IsSubset(), "explicit 2N row table is preserved without rectangular padding");
	const auto first = grid.Coordinate(0, GridNumericPolicy::Float64V1);
	const auto row_end = grid.Coordinate(3, GridNumericPolicy::Float64V1);
	const auto next_row = grid.Coordinate(4, GridNumericPolicy::Float64V1);
	const auto last = grid.Coordinate(19, GridNumericPolicy::Float64V1);
	Require(first.latitude == 60 && first.longitude == 0, "first source point uses first explicit row");
	Require(row_end.latitude == 60 && row_end.longitude == -90, "row endpoint normalizes to the half-open longitude interval");
	Require(next_row.latitude == 20 && next_row.longitude == 0, "prefix starts the next reduced row");
	Require(last.latitude == -60 && last.longitude == -90, "last point uses the final explicit row");
	Require(grid.ParentPointIndex(19) == 19, "full-domain local and parent positions are identical");

	const auto source_f32 = grid.Coordinate(5, GridNumericPolicy::OpenMeteoF32V1);
	Require(source_f32.latitude == 20 && source_f32.longitude == 60, "float32 policy widens an explicit row coordinate");
}

void TestOwnedCapacityIncludesPrefixesAndReservedStorage() {
	for (bool subset : {false, true}) {
		auto rows = SmallExplicitRows();
		rows.reserve(32);
		std::vector<GaussianRegionSegment> segments;
		if (subset) segments = {{0, 0, 2}, {1, 0, 3}};
		segments.reserve(16);
		std::string rule = "explicit_v1";
		rule.reserve(64);
		const GaussianGrid grid(2, std::move(rule), std::move(rows), std::move(segments));
		const auto copy = grid;
		for (const auto *definition_ptr : {&grid, &copy}) {
			const auto &definition = *definition_ptr;
			const auto minimum = definition.Rows().capacity() * sizeof(GaussianRow) +
			                     definition.SubsetSegments().capacity() * sizeof(GaussianRegionSegment) +
			                     definition.LatitudeRule().capacity() + 1 +
			                     (definition.Rows().size() + 1) * sizeof(std::uint64_t) +
			                     (subset ? definition.SubsetSegments().size() + 1 : 0) * sizeof(std::uint64_t);
			Require(definition.OwnedCapacityBytes() >= minimum,
			        "full and subset definition copies account for latitude rule and both prefix buffers");
		}
		Require(grid.OwnedCapacityBytes() >= 32 * sizeof(GaussianRow) + 16 * sizeof(GaussianRegionSegment) + 65,
		        "memory accounting includes retained spare capacity rather than only live elements");
	}
}

void TestOrderedRegionSegmentsAndParentMap() {
	GaussianGrid grid(2, "explicit_v1", SmallExplicitRows(),
	                  {{0, 3, 1}, {0, 0, 1}, {1, 1, 2}, {2, 4, 2}});
	Require(grid.IsSubset() && grid.PointCount() == 6, "region count is the sum of explicit segment counts");
	Require(grid.ParentPointIndex(0) == 3 && grid.ParentPointIndex(1) == 0,
	        "local ordering follows declared segment order even across a seam");
	Require(grid.ParentPointIndex(2) == 5 && grid.ParentPointIndex(3) == 6,
	        "row prefix and wrapped segment offsets map to parent positions");
	Require(grid.ParentPointIndex(5) == 15, "last local point maps into its declared parent row");
	const auto first = grid.Coordinate(0, GridNumericPolicy::Float64V1);
	const auto second = grid.Coordinate(1, GridNumericPolicy::Float64V1);
	Require(first.latitude == 60 && first.longitude == -90, "first region segment retains its parent longitude");
	Require(second.latitude == 60 && second.longitude == 0, "wrapped region segment retains local source order");
}

void TestMalformedRowsAndSegmentsReject() {
	RequireError(ReaderErrorCode::InvalidShape,
	             [] { GaussianGrid(2, "explicit_v1", {{50, 4, 0, 90}}); },
	             "row table must contain exactly 2N rows");
	RequireError(ReaderErrorCode::InvalidShape,
	             [] { GaussianGrid(2, "unknown_rule", SmallExplicitRows()); },
	             "unknown Gaussian rule");
	RequireError(ReaderErrorCode::InvalidShape,
	             [] { GaussianGrid(2, "explicit_v1", {{60, 4, 0, 90}, {20, 6, 0, 60}, {-20, 6, 0, 60}, {-10, 4, 0, 90}}); },
	             "Gaussian rows must be in descending source order");
	RequireError(ReaderErrorCode::InvalidShape,
	             [] { GaussianGrid(2, "explicit_v1", SmallExplicitRows(), {{0, 0, 3}, {0, 2, 2}}); },
	             "overlapping parent segment");
	RequireError(ReaderErrorCode::InvalidShape,
	             [] { GaussianGrid(2, "explicit_v1", SmallExplicitRows(), {{1, 5, 2}}); },
	             "segment must stay inside the parent row");
	RequireError(ReaderErrorCode::ShapeOverflow,
	             [] {
		             GaussianGrid(1, "explicit_v1",
		                          {{45, static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), 0, 1},
		                           {-45, 1, 0, 1}});
	             },
	             "row prefix sum must reject signed 64-bit overflow");
	GaussianGrid grid(2, "explicit_v1", SmallExplicitRows());
	RequireError(ReaderErrorCode::InvalidSelection, [&] { grid.Coordinate(20, GridNumericPolicy::Float64V1); },
	             "Gaussian position outside the grid");
}

void TestFrozenN160N320TablesAndN320RegionMapping() {
	auto n160_rows = ReadFrozenGaussianRows("n160");
	GaussianGrid n160(160, "openmeteo_approx_v1", n160_rows);
	Require(n160_rows.size() == 320 && n160.PointCount() == 138346, "N160 uses its full explicit 2N source row table");
	std::uint64_t row_prefix = 0;
	for (std::size_t row = 0; row < n160_rows.size(); row++) {
		const auto expected_latitude = static_cast<double>(static_cast<float>(n160_rows[row].latitude));
		const auto first = n160.Coordinate(row_prefix, GridNumericPolicy::OpenMeteoF32V1);
		const auto last = n160.Coordinate(row_prefix + n160_rows[row].point_count - 1,
		                                  GridNumericPolicy::OpenMeteoF32V1);
		Require(first.latitude == expected_latitude && last.latitude == expected_latitude,
		        "N160 applies every explicit Float32 source row latitude without mirroring");
		row_prefix += n160_rows[row].point_count;
	}
	RequireNear(n160.Coordinate(0, GridNumericPolicy::OpenMeteoF32V1).latitude,
	            89.57877349853516, 0, "N160 source Float32 north edge");
	RequireNear(n160.Coordinate(n160.PointCount() - 1, GridNumericPolicy::OpenMeteoF32V1).latitude,
	            -89.57878112792969, 0, "N160 source Float32 south edge is not mirrored");

	auto n320_rows = ReadFrozenGaussianRows("n320");
	GaussianGrid n320(320, "openmeteo_approx_v1", n320_rows);
	Require(n320_rows.size() == 640 && n320.PointCount() == 542080, "N320 uses its full explicit 2N source row table");
	row_prefix = 0;
	for (std::size_t row = 0; row < n320_rows.size(); row++) {
		const auto expected_latitude = static_cast<double>(static_cast<float>(n320_rows[row].latitude));
		const auto first = n320.Coordinate(row_prefix, GridNumericPolicy::OpenMeteoF32V1);
		const auto last = n320.Coordinate(row_prefix + n320_rows[row].point_count - 1,
		                                  GridNumericPolicy::OpenMeteoF32V1);
		Require(first.latitude == expected_latitude && last.latitude == expected_latitude,
		        "N320 applies every explicit Float32 source row latitude without mirroring");
		row_prefix += n320_rows[row].point_count;
	}
	RequireNear(n320.Coordinate(0, GridNumericPolicy::OpenMeteoF32V1).latitude,
	            89.78923034667969, 0, "N320 source Float32 north edge");
	RequireNear(n320.Coordinate(n320.PointCount() - 1, GridNumericPolicy::OpenMeteoF32V1).latitude,
	            -89.78922271728516, 0, "N320 source Float32 south edge is not mirrored");

	const auto segments = ReadFrozenGaussianSegments("n320_ecmwf_aifs_europe_ensemble");
	GaussianGrid region(320, "openmeteo_approx_v1", n320_rows, segments);
	Require(segments.size() == 272 && region.PointCount() == 14747, "N320 source area expands to ordered row/seam segments");
	Require(segments.front().parent_row == 67 && segments.front().parent_begin == 437 && segments.front().count == 13,
	        "N320 region first row begins at the frozen source position");
	Require(segments[1].parent_row == 67 && segments[1].parent_begin == 0 && segments[1].count == 47,
	        "N320 region first row preserves its dateline wrap order");
	std::uint64_t row_67_prefix = 0;
	for (std::size_t row = 0; row < 67; row++) row_67_prefix += n320_rows[row].point_count;
	Require(region.ParentPointIndex(0) == row_67_prefix + 437,
	        "N320 region first local point maps to its original parent row and column");
	Require(region.ParentPointIndex(13) == row_67_prefix,
	        "wrapped second segment follows the first segment in local object order");
	std::uint64_t row_202_prefix = 0;
	for (std::size_t row = 0; row < 202; row++) row_202_prefix += n320_rows[row].point_count;
	Require(region.ParentPointIndex(region.PointCount() - 1) == row_202_prefix + 115,
	        "N320 region final local point maps to the final declared parent segment");
	std::vector<std::uint64_t> parent_row_prefix(n320_rows.size() + 1, 0);
	for (std::size_t row = 0; row < n320_rows.size(); row++) {
		parent_row_prefix[row + 1] = parent_row_prefix[row] + n320_rows[row].point_count;
	}
	std::vector<std::uint64_t> expected_local_to_parent;
	expected_local_to_parent.reserve(static_cast<std::size_t>(region.PointCount()));
	for (const auto &segment : segments) {
		const auto begin = parent_row_prefix.at(static_cast<std::size_t>(segment.parent_row)) + segment.parent_begin;
		for (std::uint64_t offset = 0; offset < segment.count; offset++) {
			expected_local_to_parent.push_back(begin + offset);
		}
	}
	Require(expected_local_to_parent.size() == region.PointCount(),
	        "independently expanded source segments cover the complete local point sequence");
	for (std::uint64_t local = 0; local < region.PointCount(); local++) {
		const auto expected_parent = expected_local_to_parent[static_cast<std::size_t>(local)];
		Require(region.ParentPointIndex(local) == expected_parent,
		        "each local N320 point follows the independently expanded frozen source segment order");
		const auto local_coordinate = region.Coordinate(local, GridNumericPolicy::OpenMeteoF32V1);
		const auto parent_coordinate = n320.Coordinate(expected_parent, GridNumericPolicy::OpenMeteoF32V1);
		Require(local_coordinate.latitude == parent_coordinate.latitude &&
		            local_coordinate.longitude == parent_coordinate.longitude,
		        "every frozen N320 region position retains its declared parent coordinate");
	}
}

void TestFrozenHresO1280Rows() {
	const auto rows = ReadFrozenGaussianRows("ecmwf_ifs");
	GaussianGrid grid(1280, "openmeteo_approx_v1", rows);
	Require(rows.size() == 2560 && grid.PointCount() == 6599680,
	        "HRES O1280 has its own octahedral row table, not N160/N320 or a rectangular grid");
	const float dy = 180.0F / (2.0F * 1280.0F + 0.5F);
	std::uint64_t prefix = 0;
	for (std::uint64_t y = 0; y < 2560; y++) {
		const auto nx = 20 + 4 * std::min(y, std::uint64_t(2559) - y);
		// Split multiply/add so the compiler cannot contract them into an FMA;
		// the pinned producer semantics are two separate Float32 roundings.
		const float scaled_latitude = static_cast<float>(1279 - static_cast<std::int64_t>(y)) * dy;
		const float latitude = scaled_latitude + dy / 2.0F;
		const float dx = 360.0F / static_cast<float>(nx);
		if (rows[y].point_count != nx || rows[y].latitude != latitude || rows[y].longitude_step != dx) {
			std::cerr << "row " << y << " nx " << nx << " lat(def/cpp) " << std::setprecision(17)
			          << rows[y].latitude << '/' << static_cast<double>(latitude)
			          << " dx " << rows[y].longitude_step << '/' << static_cast<double>(dx) << '\n';
			throw std::runtime_error("O1280 row rule mismatch");
		}
		const auto source_prefix = y < 1280 ? 2 * y * y + 18 * y :
		    6599680 - (2 * (2560 - y) * (2560 - y) + 18 * (2560 - y));
		Require(prefix == source_prefix, "O1280 row offsets follow the producer integral point order");
		for (std::uint64_t x = 0; x < nx; x++) {
			const float unwrapped = static_cast<float>(x) * dx;
			const float longitude = unwrapped >= 180.0F ? unwrapped - 360.0F : unwrapped;
			const auto actual = grid.Coordinate(prefix + x, GridNumericPolicy::OpenMeteoF32V1);
			Require(actual.latitude == latitude && actual.longitude == longitude,
			        "all HRES source positions retain producer row/longitude coordinates");
		}
		prefix += nx;
	}
	Require(prefix == grid.PointCount() && rows.front().point_count == 20 && rows.back().point_count == 20,
	        "both polar rows and the complete point count are preserved");
}

void TestIndependentLegendreRootReference() {
	const double root_outer = std::sqrt((15.0 + 2.0 * std::sqrt(30.0)) / 35.0);
	const double root_inner = std::sqrt((15.0 - 2.0 * std::sqrt(30.0)) / 35.0);
	constexpr double radians_to_degrees = 57.2957795130823208768;
	const std::vector<GaussianRow> rows{{std::asin(root_outer) * radians_to_degrees, 8, 0, 45},
	                                    {std::asin(root_inner) * radians_to_degrees, 8, 0, 45},
	                                    {-std::asin(root_inner) * radians_to_degrees, 8, 0, 45},
	                                    {-std::asin(root_outer) * radians_to_degrees, 8, 0, 45}};
	GaussianGrid standard(2, "legendre_roots_v1", rows);
	RequireNear(standard.Coordinate(0, GridNumericPolicy::Float64V1).latitude,
	            59.44440828916677, 1e-12, "independent N=2 Legendre root latitude");
	RequireNear(standard.Coordinate(8, GridNumericPolicy::Float64V1).latitude,
	            19.8757191474409, 1e-12, "independent N=2 inner Legendre root latitude");
}

} // namespace

int main() {
	try {
		TestExplicitRowsAndSourcePositions();
		TestOwnedCapacityIncludesPrefixesAndReservedStorage();
		TestOrderedRegionSegmentsAndParentMap();
		TestMalformedRowsAndSegmentsReject();
		TestFrozenN160N320TablesAndN320RegionMapping();
		TestFrozenHresO1280Rows();
		TestIndependentLegendreRootReference();
		std::cout << "Gaussian grid checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "Gaussian grid checks failed: " << error.what() << '\n';
		return 1;
	}
}
