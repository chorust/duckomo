#include "duckomo/nearest_sampling.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace {
using namespace duckdb::duckomo;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

template <class F> void RequireCapabilityFailure(F &&action, const std::string &message) {
	try {
		action();
	} catch (const ReaderError &error) {
		Require(std::string(error.what()).find("[sampling_capability]") != std::string::npos,
		        message + " did not identify a sampling capability error");
		return;
	}
	throw std::runtime_error(message + " was accepted");
}

GridDefinition Definition(RegularGrid grid, double radius = 6371229.0) {
	GridEarth earth;
	earth.kind = GridEarthKind::Sphere;
	earth.radius_m = radius;
	return GridDefinition("regular_lonlat_v1", earth, GridNumericPolicy::Float64V1, std::move(grid));
}

double CsvDouble(const std::string &value) {
	char *end = nullptr;
	const auto result = std::strtod(value.c_str(), &end);
	if (end == value.c_str() || end != value.c_str() + value.size())
		throw std::runtime_error("invalid binary64 value in reference CSV: " + value);
	return result;
}

std::vector<std::string> CsvRow(const std::string &line) {
	std::vector<std::string> fields;
	std::string field;
	bool quoted = false;
	const auto normalized_line = !line.empty() && line.back() == '\r' ? line.substr(0, line.size() - 1) : line;
	for (std::size_t index = 0; index < normalized_line.size(); ++index) {
		const char character = normalized_line[index];
		if (quoted) {
			if (character == '"' && index + 1 < normalized_line.size() && normalized_line[index + 1] == '"') {
				field.push_back('"');
				++index;
			} else if (character == '"') {
				quoted = false;
			} else {
				field.push_back(character);
			}
		} else if (character == ',') {
			fields.push_back(field);
			field.clear();
		} else if (character == '"' && field.empty()) {
			quoted = true;
		} else {
			field.push_back(character);
		}
	}
	fields.push_back(field);
	return fields;
}

struct GridSpec {
	std::string id;
	std::uint64_t nx;
	std::uint64_t ny;
	double lat0;
	double lon0;
	double dlat;
	double dlon;
	bool latitude_fastest;
};

GridSpec GridFor(const std::string &id) {
	if (id == "four-point-symmetric") return {id, 2, 2, -1, 0, 2, 1, false};
	if (id == "periodic-global") return {id, 8, 3, -90, -180, 90, 45, false};
	if (id == "small-reverse-lat") return {id, 5, 5, 2, -2, -1, 1, false};
	if (id == "small-reverse-lon") return {id, 5, 5, -2, 2, 1, -1, false};
	if (id == "small-reverse-both") return {id, 5, 5, 2, 2, -1, -1, false};
	if (id == "small-flat-lat") return {id, 5, 5, -2, -2, 1, 1, true};
	if (id == "axis-nearest-counterexample") return {id, 2, 2, 23.30927414123454,
	                                                        -121.83048854140098, -1.0, 10.0, false};
	if (id == "small-separate" || id == "small-interleaved" || id == "small-flat-lon")
		return {id, 5, 5, -2, -2, 1, 1, false};
	throw std::runtime_error("unknown grid profile: " + id);
}

struct TargetSpec {
	std::string id;
	double longitude;
	double latitude;
};

std::vector<TargetSpec> Targets() {
	return {{"exact-source", 0, 0}, {"four-point-tie", 0.5, 0},
	        {"interior-gap", 0.15000000000000002, 0.15000000000000002},
	        {"near-equal", 0.50000000001, 0.0}, {"outside-domain", 10.5, 10.5},
	        {"axis-nearest-counterexample", -117.5536193515942, 22.757593720108147},
	        {"periodic-seam", 179.9, 0}};
}

std::uint64_t CompareColumn(const std::vector<std::string> &header, const std::string &name) {
	const auto found = std::find(header.begin(), header.end(), name);
	if (found == header.end()) throw std::runtime_error("missing CSV column: " + name);
	return static_cast<std::uint64_t>(std::distance(header.begin(), found));
}

void TestIndependentManifest() {
#ifndef DUCKOMO_SAMPLING_MANIFEST_PATH
	throw std::runtime_error("DUCKOMO_SAMPLING_MANIFEST_PATH is not configured");
#else
	std::ifstream input(std::string(DUCKOMO_SAMPLING_MANIFEST_PATH).substr(
	                        0, std::string(DUCKOMO_SAMPLING_MANIFEST_PATH).find_last_of('/')) +
	                    "/locator.reference.csv");
	Require(input.good(), "independent locator reference CSV must be readable");
	std::string line;
	Require(static_cast<bool>(std::getline(input, line)), "locator reference CSV must have a header");
	const auto header = CsvRow(line);
	const auto grid_col = CompareColumn(header, "grid_id");
	const auto target_col = CompareColumn(header, "target_id");
	const auto status_col = CompareColumn(header, "status");
	const auto point_col = CompareColumn(header, "source_point_index");
	const auto lon_col = CompareColumn(header, "source_lon");
	const auto lat_col = CompareColumn(header, "source_lat");
	const auto distance_col = CompareColumn(header, "distance_m");
	const auto tie_count_col = CompareColumn(header, "tie_count");
	const auto axis_nearest_col = CompareColumn(header, "axis_nearest_point_index");
	std::uint64_t compared = 0;
	std::uint64_t candidate_ties = 0;
	std::uint64_t between_cell_references = 0;
	std::uint64_t axis_nearest_counterexamples = 0;
	while (std::getline(input, line)) {
		if (line.empty()) continue;
		const auto fields = CsvRow(line);
		Require(fields.size() == header.size(), "locator CSV row width must match its header");
		if (fields[grid_col].empty()) continue; // EMPTY geometry has no grid location.
		const auto grid = GridFor(fields[grid_col]);
		const auto targets = Targets();
		const auto target = std::find_if(targets.begin(), targets.end(), [&](const auto &candidate) {
			return candidate.id == fields[target_col];
		});
		Require(target != targets.end(), "unknown independent target: " + fields[target_col]);
		auto definition = Definition(RegularGrid(grid.nx, grid.ny, grid.lat0, grid.lon0, grid.dlat, grid.dlon,
		                                          grid.latitude_fastest ? GridStorageOrder::LatitudeFastest
		                                                                : GridStorageOrder::Separate));
		const auto &regular = std::get<RegularGrid>(definition.geometry);
		const auto capability = QualifyNearestGrid(definition);
		const auto result = LocateNearestRegularGrid(regular, capability, target->longitude, target->latitude);
		if (fields[status_col] == "outside_domain") {
			Require(!result.inside_domain && fields[point_col].empty(),
			        "domain classification must match the independent reference");
			++compared;
			continue;
		}
		Require(fields[status_col] == "ok" && result.inside_domain && result.source,
		        "nearest locator must return the expected successful status");
		if (!fields[tie_count_col].empty() && std::stoull(fields[tie_count_col]) > 1) {
			++candidate_ties;
			Require(result.source->point_index == std::stoull(fields[point_col]),
			        "independent exact tie must choose the smallest original point index");
		}
		if (grid.id == "axis-nearest-counterexample" && target->id == "axis-nearest-counterexample") {
			// Independently compute the nearest coordinate on each axis. Their
			// Cartesian product is deliberately not the spherical-nearest point.
			double nearest_lon_delta = std::numeric_limits<double>::infinity();
			double nearest_lat_delta = std::numeric_limits<double>::infinity();
			std::uint64_t nearest_x = 0, nearest_y = 0;
			for (std::uint64_t x = 0; x < grid.nx; ++x) {
				const auto source_lon = grid.lon0 + static_cast<double>(x) * grid.dlon;
				const auto delta = std::abs(source_lon - target->longitude);
				if (delta < nearest_lon_delta) {
					nearest_lon_delta = delta;
					nearest_x = x;
				}
			}
			for (std::uint64_t y = 0; y < grid.ny; ++y) {
				const auto source_lat = grid.lat0 + static_cast<double>(y) * grid.dlat;
				const auto delta = std::abs(source_lat - target->latitude);
				if (delta < nearest_lat_delta) {
					nearest_lat_delta = delta;
					nearest_y = y;
				}
			}
			const auto independent_axis_index = nearest_y * grid.nx + nearest_x;
			Require(independent_axis_index == 2 && independent_axis_index == std::stoull(fields[axis_nearest_col]),
			        "independent per-axis nearest calculation must select original point index 2");
			Require(result.source->point_index == 0,
			        "spherical-distance search must select point index 0 in the axis-nearest counterexample");
			++axis_nearest_counterexamples;
		}
		bool target_on_lon_axis = false;
		bool target_on_lat_axis = false;
		const bool periodic = grid.id == "periodic-global";
		for (std::uint64_t x = 0; x < grid.nx; ++x) {
			const auto source_lon = grid.lon0 + static_cast<double>(x) * grid.dlon;
			const auto delta = periodic ? std::remainder(source_lon - target->longitude, 360.0)
			                            : source_lon - target->longitude;
			target_on_lon_axis = target_on_lon_axis || delta == 0.0;
		}
		for (std::uint64_t y = 0; y < grid.ny; ++y) {
			target_on_lat_axis = target_on_lat_axis ||
			                     grid.lat0 + static_cast<double>(y) * grid.dlat == target->latitude;
		}
		if (!target_on_lon_axis || !target_on_lat_axis) ++between_cell_references;
		Require(result.source->point_index == std::stoull(fields[point_col]),
		        "nearest source point index differs from independent exhaustive reference");
		Require(result.source->longitude == CsvDouble(fields[lon_col]) &&
			            result.source->latitude == CsvDouble(fields[lat_col]),
		        "nearest source coordinates differ from independent reference");
		const auto reference_distance = CsvDouble(fields[distance_col]);
		Require(std::abs(result.source->distance_m - reference_distance) <=
			            std::max(1e-6, reference_distance * 1e-12),
		        "nearest ordinary distance exceeds its frozen binary64 tolerance");
		++compared;
	}
	Require(compared == 61, "independent locator comparator must cover all non-empty references");
	Require(candidate_ties == 10, "independent reference must cover all ten exact-tie rows");
	Require(between_cell_references >= 20,
	        "independent reference must cover at least twenty between-cell nearest targets");
	Require(axis_nearest_counterexamples == 1,
	        "native per-axis calculation must prove spherical nearest differs in exactly one reference case");
#endif
}

void TestIndependentScientificDistances() {
#ifndef DUCKOMO_SAMPLING_SCIENTIFIC_REFERENCE_PATH
	throw std::runtime_error("DUCKOMO_SAMPLING_SCIENTIFIC_REFERENCE_PATH is not configured");
#else
	std::ifstream input(DUCKOMO_SAMPLING_SCIENTIFIC_REFERENCE_PATH);
	Require(input.good(), "independent scientific distance CSV must be readable");
	std::string line;
	Require(static_cast<bool>(std::getline(input, line)), "scientific reference CSV must have a header");
	const auto header = CsvRow(line);
	const auto target_lon_col = CompareColumn(header, "target_lon");
	const auto target_lat_col = CompareColumn(header, "target_lat");
	const auto source_lon_col = CompareColumn(header, "source_lon");
	const auto source_lat_col = CompareColumn(header, "source_lat");
	const auto radius_col = CompareColumn(header, "radius_m");
	const auto binary_col = CompareColumn(header, "distance_f64_m");
	const auto high_col = CompareColumn(header, "distance_high_precision_m");
	const auto bound_col = CompareColumn(header, "bound");
	const auto id_col = CompareColumn(header, "id");
	std::uint64_t compared = 0;
	std::uint64_t ordinary = 0;
	std::uint64_t singular = 0;
	std::unordered_set<std::string> vector_ids;
	while (std::getline(input, line)) {
		if (line.empty()) continue;
		const auto fields = CsvRow(line);
		Require(fields.size() == header.size(), "scientific CSV row width must match its header");
		Require(!fields[id_col].empty() && vector_ids.insert(fields[id_col]).second,
		        "scientific reference vector identifiers must be non-empty and unique");
		const auto target_lon = CsvDouble(fields[target_lon_col]);
		const auto target_lat = CsvDouble(fields[target_lat_col]);
		const auto source_lon = CsvDouble(fields[source_lon_col]);
		const auto source_lat = CsvDouble(fields[source_lat_col]);
		const auto reference_radius = CsvDouble(fields[radius_col]);
		const auto observed = SphereHaversineF64V1(target_lon, target_lat, source_lon, source_lat,
		                                          reference_radius);
		const auto expected_binary = CsvDouble(fields[binary_col]);
		const auto scientific = std::stold(fields[high_col]);
		const auto error = std::abs(static_cast<long double>(observed) - scientific);
		const auto allowed = fields[bound_col] == "ordinary"
		                         ? std::max(1e-6L, std::abs(scientific) * 1e-12L)
		                         : 2.0L;
		Require(fields[bound_col] == "ordinary" || fields[bound_col] == "singular",
		        "unknown scientific error class: " + fields[id_col]);
		if (fields[bound_col] == "ordinary") ++ordinary;
		else ++singular;
		Require(observed == expected_binary,
		        "binary64 distance differs from independent compatibility reference: " + fields[id_col]);
		Require(error <= allowed,
		        "scientific distance exceeds its high-precision error bound: " + fields[id_col]);
		++compared;
	}
	Require(compared == 10, "scientific comparator must cover all ten frozen vectors");
	const std::unordered_set<std::string> required_ids{
	    "exact-source", "source-pole", "near-pole", "antipodal", "near-antipodal", "ordinary-local",
	    "target-pole", "alternate-earth-radius", "small-positive-radius", "large-finite-radius"};
	Require(vector_ids == required_ids, "scientific references must include every frozen accuracy vector");
	Require(ordinary == 7 && singular == 3,
	        "scientific references must include exactly seven ordinary and three singular-class vectors");
#endif
}

void TestExactTiesAndLayouts() {
	auto exact_definition = Definition(RegularGrid(5, 5, -2, -2, 1, 1, GridStorageOrder::Separate));
	const auto &exact_grid = std::get<RegularGrid>(exact_definition.geometry);
	const auto exact_capability = QualifyNearestGrid(exact_definition);
	const auto exact = LocateNearestRegularGrid(exact_grid, exact_capability, 0, 0);
	Require(exact.inside_domain && exact.source && exact.source->point_index == 12 && exact.source->distance_m == 0,
	        "exact source coordinate must preserve its original point index and zero distance");

	auto tie_definition = Definition(RegularGrid(2, 2, -1, 0, 2, 1, GridStorageOrder::Separate));
	const auto &tie_grid = std::get<RegularGrid>(tie_definition.geometry);
	const auto tie_capability = QualifyNearestGrid(tie_definition);
	const auto tie = LocateNearestRegularGrid(tie_grid, tie_capability, 0.5, 0);
	Require(tie.source && tie.source->point_index == 0 && tie.coordinate_comparisons == 4,
	        "four equal spherical distances must choose the smallest original point index");

	auto flat = Definition(RegularGrid(5, 5, 2, 2, -1, -1, GridStorageOrder::LatitudeFastest));
	const auto &flat_grid = std::get<RegularGrid>(flat.geometry);
	const auto flat_result = LocateNearestRegularGrid(flat_grid, QualifyNearestGrid(flat), 0, 0);
	Require(flat_result.source && flat_result.source->x == 2 && flat_result.source->y == 2 &&
	            flat_result.source->point_index == 12,
	        "negative coordinate steps and latitude-fastest storage must preserve point identity");
}

void TestNcepGfs025Profile() {
	// The pinned domain registry declares this exact GFS 0.25-degree profile.
	auto definition = Definition(RegularGrid(1440, 721, -90.0, -180.0, 0.25, 0.25,
	                                        GridStorageOrder::Separate));
	const auto &grid = std::get<RegularGrid>(definition.geometry);
	const auto capability = QualifyNearestGrid(definition);
	Require(capability.periodic_longitude && capability.coverage == NearestCoverageKind::GlobalSphere &&
	            capability.latitude_first == -90.0 && capability.latitude_last == 90.0 &&
	            capability.unwrapped_longitude_first == -180.0 && capability.unwrapped_longitude_last == 179.75,
	        "ncep_gfs025 profile must qualify as an exact global spherical grid");
	const auto result = LocateNearestRegularGrid(grid, capability, 0.0, 0.0);
	Require(result.inside_domain && result.source && result.source->x == 720 && result.source->y == 360 &&
	            result.source->point_index == 519120 && result.source->distance_m == 0.0,
	        "ncep_gfs025 exhaustive location must recover its exact equatorial source identity");
}

void TestThresholdAdjacency() {
	auto definition = Definition(RegularGrid(2, 2, -1.0, 0.0, 2.0, 1.0, GridStorageOrder::Separate));
	const auto &grid = std::get<RegularGrid>(definition.geometry);
	const auto capability = QualifyNearestGrid(definition);
	const auto winner = LocateNearestRegularGrid(grid, capability, 0.5, 0.0);
	Require(winner.source && winner.source->point_index == 0,
	        "symmetric threshold fixture must resolve to the smallest source index");
	const auto exact_threshold = winner.source->distance_m;
	const auto below = std::nextafter(exact_threshold, -std::numeric_limits<double>::infinity());
	const auto above = std::nextafter(exact_threshold, std::numeric_limits<double>::infinity());
	Require(below < exact_threshold && above > exact_threshold,
	        "threshold neighbors must be adjacent binary64 values");
	const auto replay = LocateNearestRegularGrid(grid, capability, 0.5, 0.0);
	Require(replay.source && replay.source->distance_m == exact_threshold && replay.source->point_index == 0,
	        "the returned threshold distance and exact tie winner must replay exactly");
	const auto radius_below = SphereHaversineF64V1(
	    0.5, 0.0, 0.0, -1.0, std::nextafter(6371229.0, 0.0));
	const auto radius_above = SphereHaversineF64V1(
	    0.5, 0.0, 0.0, -1.0,
	    std::nextafter(6371229.0, std::numeric_limits<double>::infinity()));
	Require(radius_below <= exact_threshold && radius_above >= exact_threshold,
	        "binary64 earth-radius multiplication must preserve adjacent threshold ordering");
}

void TestCoverageAndCapabilityBoundaries() {
	auto unsupported_version = Definition(RegularGrid(2, 2, -1, -1, 1, 1, GridStorageOrder::Separate));
	unsupported_version.version = 2;
	RequireCapabilityFailure([&] { (void)QualifyNearestGrid(unsupported_version); }, "unsupported definition version");
	auto nonregular = GridDefinition("rotated_lonlat_v1", GridEarth{}, GridNumericPolicy::Float64V1,
	                                 ProjectedGrid(RotatedLatLonParameters{2, 2, 0, 0, 1, 1,
	                                                                      90, 0, 0, GridStorageOrder::Separate}));
	RequireCapabilityFailure([&] { (void)QualifyNearestGrid(nonregular); }, "projected regular-looking grid");
	for (const auto radius : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
	                           std::numeric_limits<double>::infinity()}) {
		bool rejected = false;
		try {
			(void)Definition(RegularGrid(2, 2, -1, -1, 1, 1, GridStorageOrder::Separate), radius);
		} catch (const ReaderError &) {
			rejected = true;
		}
		Require(rejected, "sphere identity must reject non-positive or non-finite radius");
	}
	auto singleton = Definition(RegularGrid(1, 1, 0, 10, 1, 1, GridStorageOrder::Separate));
	const auto singleton_capability = QualifyNearestGrid(singleton);
	const auto &singleton_grid = std::get<RegularGrid>(singleton.geometry);
	Require(singleton_capability.coverage == NearestCoverageKind::Regional &&
	            NearestTargetInsideDomain(singleton_grid, singleton_capability, 370, 0) &&
	            !NearestTargetInsideDomain(singleton_grid, singleton_capability, 10.1, 0),
	        "singleton longitude axis is a single meridian, not a periodic grid");

	auto regional = Definition(RegularGrid(4, 3, -1, 170, 1, 10, GridStorageOrder::Separate));
	const auto &regional_grid = std::get<RegularGrid>(regional.geometry);
	const auto regional_capability = QualifyNearestGrid(regional);
	Require(!regional_capability.periodic_longitude &&
	            NearestTargetInsideDomain(regional_grid, regional_capability, 530, 0) &&
	            !NearestTargetInsideDomain(regional_grid, regional_capability, 160, 0),
	        "regional coverage accepts one unique longitude lift only");
	const auto west_endpoint_below = std::nextafter(170.0, -std::numeric_limits<double>::infinity());
	const auto east_endpoint_above = std::nextafter(200.0, std::numeric_limits<double>::infinity());
	Require(NearestTargetInsideDomain(regional_grid, regional_capability, 170.0, 0) &&
	            NearestTargetInsideDomain(regional_grid, regional_capability, 200.0, 0) &&
	            !NearestTargetInsideDomain(regional_grid, regional_capability, west_endpoint_below, 0) &&
	            !NearestTargetInsideDomain(regional_grid, regional_capability, east_endpoint_above, 0),
	        "regional longitude endpoints are closed with adjacent outside binary64 values rejected");
	Require(NearestTargetInsideDomain(regional_grid, regional_capability, 180, -1) &&
	            NearestTargetInsideDomain(regional_grid, regional_capability, 180, 1) &&
	            !NearestTargetInsideDomain(regional_grid, regional_capability, 180,
	                                       std::nextafter(-1.0, -std::numeric_limits<double>::infinity())) &&
	            !NearestTargetInsideDomain(regional_grid, regional_capability, 180,
	                                       std::nextafter(1.0, std::numeric_limits<double>::infinity())),
	        "regional latitude endpoints are closed with adjacent outside binary64 values rejected");
	auto negative_regional = Definition(RegularGrid(4, 3, 1, -200, -1, 10, GridStorageOrder::Separate));
	const auto &negative_regional_grid = std::get<RegularGrid>(negative_regional.geometry);
	const auto negative_regional_capability = QualifyNearestGrid(negative_regional);
	Require(NearestTargetInsideDomain(negative_regional_grid, negative_regional_capability, 160, 0) &&
	            NearestTargetInsideDomain(negative_regional_grid, negative_regional_capability, -170, 0) &&
	            !NearestTargetInsideDomain(negative_regional_grid, negative_regional_capability,
	                                       std::nextafter(160.0, -std::numeric_limits<double>::infinity()), 0),
	        "negative and unwrapped longitude origins preserve the same closed lifted interval");

	for (const auto &[origin, longitude_step] :
	     {std::pair<double, double>{1e15, 0.125}, std::pair<double, double>{-1e15, -0.125}}) {
		auto large_origin = Definition(RegularGrid(4, 3, -1.0, origin, 1.0, longitude_step,
		                                           GridStorageOrder::Separate));
		const auto &large_origin_grid = std::get<RegularGrid>(large_origin.geometry);
		const auto large_origin_capability = QualifyNearestGrid(large_origin);
		const auto source_meridian = large_origin_grid.Coordinate(1, 2).longitude;
		const auto lifted_source = LocateNearestRegularGrid(large_origin_grid, large_origin_capability,
		                                                    source_meridian, 0.0);
		Require(lifted_source.inside_domain && lifted_source.source &&
		            lifted_source.source->point_index == 6 && lifted_source.source->distance_m == 0.0,
		        "a normalized interior source meridian on a large positive or negative unwrapped axis remains in-domain");
		const auto endpoint = large_origin_grid.Coordinate(1, 3).longitude;
		const auto lifted_endpoint = LocateNearestRegularGrid(large_origin_grid, large_origin_capability,
		                                                      endpoint, 0.0);
		Require(lifted_endpoint.inside_domain && lifted_endpoint.source &&
		            lifted_endpoint.source->point_index == 7 && lifted_endpoint.source->distance_m == 0.0,
		        "a normalized source meridian at the closed endpoint of a large unwrapped axis remains in-domain");
		const auto outside = std::nextafter(
		    endpoint, longitude_step > 0 ? std::numeric_limits<double>::infinity()
		                                 : -std::numeric_limits<double>::infinity());
		Require(!NearestTargetInsideDomain(large_origin_grid, large_origin_capability, outside, 0.0),
		        "large-origin regional coverage rejects the adjacent longitude outside its closed endpoint");
	}

	for (const auto &[origin, step] : {std::pair<double, double>{-0.9, 0.7},
	                                 std::pair<double, double>{1.9, -0.7}}) {
		auto fractional = Definition(RegularGrid(5, 5, -2, origin, 1, step, GridStorageOrder::Separate));
		const auto &grid = std::get<RegularGrid>(fractional.geometry);
		const auto capability = QualifyNearestGrid(fractional);
		for (const auto x : {0ULL, 4ULL}) {
			const auto endpoint = grid.Coordinate(2, x).longitude;
			const auto located = LocateNearestRegularGrid(grid, capability, endpoint, 0);
			Require(located.inside_domain && located.source && located.source->point_index == 10 + x &&
			            located.source->distance_m == 0,
			        "fractional origin/step endpoints must remain zero-distance sources");
			const auto direction = (x == 4) == (step > 0) ? std::numeric_limits<double>::infinity()
			                                                       : -std::numeric_limits<double>::infinity();
			Require(!NearestTargetInsideDomain(grid, capability, std::nextafter(endpoint, direction), 0),
			        "fractional endpoint's adjacent outside value must remain outside");
		}
	}
	const auto descending_global = QualifyNearestGrid(
	    Definition(RegularGrid(4, 3, 90, -180, -90, 90, GridStorageOrder::Separate)));
	Require(descending_global.periodic_longitude && descending_global.coverage == NearestCoverageKind::GlobalSphere,
	        "descending global latitude endpoints must retain global coverage identity");

	auto global = Definition(RegularGrid(8, 3, -90, -180, 90, 45, GridStorageOrder::Separate));
	const auto &global_grid = std::get<RegularGrid>(global.geometry);
	const auto global_capability = QualifyNearestGrid(global);
	const auto seam = LocateNearestRegularGrid(global_grid, global_capability, 179.9, 0);
	Require(global_capability.periodic_longitude &&
	            global_capability.coverage == NearestCoverageKind::GlobalSphere && seam.source &&
	            seam.source->x == 0 && seam.source->y == 1,
	        "exact global period and seam target must use the wrapped nearest point");
	Require(NearestTargetInsideDomain(global_grid, global_capability, 0, std::nextafter(90.0, 0.0)) &&
	            NearestTargetInsideDomain(global_grid, global_capability, 0, std::nextafter(-90.0, 0.0)) &&
	            !NearestTargetInsideDomain(global_grid, global_capability, 0, 90.0) &&
	            !NearestTargetInsideDomain(global_grid, global_capability, 0, -90.0),
	        "target latitude excludes exact poles but accepts adjacent representable interior values");

	auto band = Definition(RegularGrid(4, 2, -45, -180, 90, 90, GridStorageOrder::Separate));
	const auto band_capability = QualifyNearestGrid(band);
	Require(band_capability.periodic_longitude && band_capability.coverage == NearestCoverageKind::LatitudeBand,
	        "periodic grid without both poles is a latitude band");
	auto short_period = Definition(RegularGrid(4, 2, -45, -180, 90, std::nextafter(90.0, 0.0),
	                                          GridStorageOrder::Separate));
	Require(!QualifyNearestGrid(short_period).periodic_longitude,
	        "one-ulp-short span must not be promoted to a periodic grid");
	auto long_period = Definition(RegularGrid(4, 2, -45, -180, 90,
	                                        std::nextafter(90.0, std::numeric_limits<double>::infinity()),
	                                        GridStorageOrder::Separate));
	Require(!QualifyNearestGrid(long_period).periodic_longitude,
	        "one-ulp-long period with a regional endpoint span must remain nonperiodic");
	RequireCapabilityFailure([&] {
		auto duplicate = Definition(RegularGrid(4, 2, -1, 0, 1, 180, GridStorageOrder::Separate));
		(void)QualifyNearestGrid(duplicate);
	}, "duplicate normalized meridians");
	RequireCapabilityFailure([&] {
		auto wrapped = Definition(RegularGrid(5, 2, -1, 0, 1, 100, GridStorageOrder::Separate));
		(void)QualifyNearestGrid(wrapped);
	}, "multiple longitude wraps");
	RequireCapabilityFailure([&] {
		auto nonperiodic_full = Definition(RegularGrid(5, 2, -1, 0, 1, 90, GridStorageOrder::Separate));
		(void)QualifyNearestGrid(nonperiodic_full);
	}, "full longitude span without periodic qualification");
	RequireCapabilityFailure([&] {
		auto permissive = GridDefinition("regular_lonlat_v1", GridEarth{}, GridNumericPolicy::Float64V1,
		                                 RegularGrid(2, 2, -91, 0, 1, 1, GridStorageOrder::Separate, true));
		(void)QualifyNearestGrid(permissive);
	}, "out-of-range latitude profile");
	RequireCapabilityFailure([&] {
		auto ellipsoid = GridEarth{};
		ellipsoid.kind = GridEarthKind::Wgs84Source;
		auto definition = GridDefinition("regular_lonlat_v1", ellipsoid, GridNumericPolicy::Float64V1,
		                                  RegularGrid(2, 2, -1, -1, 1, 1, GridStorageOrder::Separate));
		(void)QualifyNearestGrid(definition);
	}, "ellipsoidal earth");
	RequireCapabilityFailure([&] {
		auto collapsed = Definition(RegularGrid(1, 2, 90, 0, std::numeric_limits<double>::denorm_min(), 1,
		                                        GridStorageOrder::Separate));
		(void)QualifyNearestGrid(collapsed);
	}, "binary64 latitude collapse");
	RequireCapabilityFailure([&] {
		auto collapsed = Definition(RegularGrid(2, 1, 0, 1e16, 1, 1, GridStorageOrder::Separate));
		(void)QualifyNearestGrid(collapsed);
	}, "binary64 longitude collapse");
}

void TestDistanceAndCancellation() {
	const double pole = SphereHaversineF64V1(10, 80, -170, 90, 6371229.0);
	const double expected = 10.0 * (3.141592653589793238462643383279502884 / 180.0) * 6371229.0;
	Require(pole == expected, "source-pole distance must use the explicit meridional branch");
	const double same_north_pole_a = SphereHaversineF64V1(10, 90, -170, 90, 6371229.0);
	const double same_north_pole_b = SphereHaversineF64V1(0, 90, 135, 90, 6371229.0);
	const double same_south_pole = SphereHaversineF64V1(10, -90, 170, -90, 6371229.0);
	Require(same_north_pole_a == 0.0 && same_north_pole_b == 0.0 && same_south_pole == 0.0,
	        "same-pole coordinates must have zero distance independent of longitude");
	const double east_half_turn = SphereHaversineF64V1(0, 0, 180, 0, 6371229.0);
	const double west_half_turn = SphereHaversineF64V1(0, 0, -180, 0, 6371229.0);
	Require(east_half_turn == west_half_turn, "signed half-turn longitude differences must have equal distance");
	const double pole_oracle = std::stod("1111989.234485458");
	Require(std::abs(pole - pole_oracle) < 1e-6, "source-pole meridional distance differs from frozen oracle");

	auto tiny_radius_definition = Definition(
	    RegularGrid(2, 1, 0.0, 0.0, 1.0, 1.0, GridStorageOrder::Separate), 1e-307);
	const auto &tiny_radius_grid = std::get<RegularGrid>(tiny_radius_definition.geometry);
	const auto tiny_radius_capability = QualifyNearestGrid(tiny_radius_definition);
	const auto just_east_of_midpoint = std::nextafter(0.5, 1.0);
	const auto west_distance = SphereHaversineF64V1(just_east_of_midpoint, 0, 0, 0, 1.0);
	const auto east_distance = SphereHaversineF64V1(just_east_of_midpoint, 0, 1, 0, 1.0);
	const auto rounded_west_distance = SphereHaversineF64V1(just_east_of_midpoint, 0, 0, 0, 1e-307);
	const auto rounded_east_distance = SphereHaversineF64V1(just_east_of_midpoint, 0, 1, 0, 1e-307);
	const auto rounded_tie = LocateNearestRegularGrid(tiny_radius_grid, tiny_radius_capability,
	                                                  just_east_of_midpoint, 0);
	Require(west_distance > east_distance && rounded_west_distance == rounded_east_distance &&
	            rounded_tie.source && rounded_tie.source->point_index == 0 &&
	            rounded_tie.source->distance_m == rounded_west_distance,
	        "winner ordering must compare radius-scaled binary64 distances, including a multiplication-induced tie");
	const double antipodal = SphereHaversineF64V1(0, 0, 180, 0, 6371229.0);
	Require(std::isfinite(antipodal) &&
	            std::abs(antipodal - 3.141592653589793238462643383279502884 * 6371229.0) < 1e-8,
	        "antipodal distance must remain finite");
	const double near_pole = SphereHaversineF64V1(12, 89.999, -167, 89.999999, 6371229.0);
	Require(std::abs(near_pole - 111.31010545302788) <= 2.0,
	        "near-pole scientific distance must remain within 2 m");
	const double near_antipodal = SphereHaversineF64V1(0, 0, 179.999999, 0.000001, 6371229.0);
	Require(std::abs(near_antipodal - 20015806.06347922) <= 2.0,
	        "near-antipodal scientific distance must remain within 2 m");
	Require(SphereHaversineF64V1(0, 80, 1, 80, 6371229.0) <
	            SphereHaversineF64V1(0, 80, 0, 82, 6371229.0),
	        "spherical distance must account for longitude convergence");

	auto definition = Definition(RegularGrid(20, 20, -10, -10, 1, 1, GridStorageOrder::Separate));
	const auto &grid = std::get<RegularGrid>(definition.geometry);
	const auto capability = QualifyNearestGrid(definition);
	std::uint64_t polls = 0;
	try {
		(void)LocateNearestRegularGrid(grid, capability, 0, 0, [&] { return ++polls == 2; });
		throw std::runtime_error("cancelled locator unexpectedly completed");
	} catch (const ReaderError &error) {
		Require(error.Code() == ReaderErrorCode::Cancelled && polls == 2,
		        "locator must poll cancellation in bounded intervals");
	}
	std::uint64_t capability_polls = 0;
	try {
		auto large = Definition(RegularGrid(600, 2, -1, -150, 2, 0.5, GridStorageOrder::Separate));
		(void)QualifyNearestGrid(large, [&] { return ++capability_polls == 2; });
		throw std::runtime_error("cancelled capability validation unexpectedly completed");
	} catch (const ReaderError &error) {
		Require(error.Code() == ReaderErrorCode::Cancelled && capability_polls == 2,
		        "capability traversal must poll cancellation in bounded intervals");
	}
}

void TestCapabilityBoundaryManifest() {
#ifndef DUCKOMO_SAMPLING_CAPABILITY_REFERENCE_PATH
	throw std::runtime_error("DUCKOMO_SAMPLING_CAPABILITY_REFERENCE_PATH is not configured");
#else
	std::ifstream input(DUCKOMO_SAMPLING_CAPABILITY_REFERENCE_PATH);
	Require(input.good(), "independent capability-boundary reference CSV must be readable");
	std::string line;
	Require(static_cast<bool>(std::getline(input, line)), "capability-boundary CSV must have a header");
	const auto header = CsvRow(line);
	const auto case_col = CompareColumn(header, "case_id");
	const auto nx_col = CompareColumn(header, "nx");
	const auto ny_col = CompareColumn(header, "ny");
	const auto lat0_col = CompareColumn(header, "lat0");
	const auto lon0_col = CompareColumn(header, "lon0");
	const auto dlat_col = CompareColumn(header, "dlat");
	const auto dlon_col = CompareColumn(header, "dlon");
	const auto accepted_col = CompareColumn(header, "accepted");
	const auto reason_col = CompareColumn(header, "reason");
	const auto earth_kind_col = CompareColumn(header, "earth_kind");
	const auto radius_col = CompareColumn(header, "radius_m");
	const auto version_col = CompareColumn(header, "version");
	const auto latitude_policy_col = CompareColumn(header, "allow_out_of_range_latitude");
	const auto geometry_col = CompareColumn(header, "geometry");
	std::uint64_t compared = 0;
	std::uint64_t accepted = 0;
	std::unordered_set<std::string> case_ids;
	std::uint64_t rejected = 0;
	const std::vector<std::string> required_rejection_classes{
	    "earth-radius", "earth-model", "definition-version", "latitude-policy", "grid-geometry",
	    "positive-dimensions", "signed-point-count", "exact-binary64-index", "finite-axis-parameters",
	    "finite-axis-endpoint", "nonzero-axis-step", "nonfinite-period", "latitude-range",
	    "latitude-binary64-collapse", "longitude-binary64-collapse", "duplicate-normalized-longitude",
	    "multiple-longitude-wraps"};
	std::vector<std::string> rejection_classes;
	while (std::getline(input, line)) {
		if (line.empty()) continue;
		const auto fields = CsvRow(line);
		Require(fields.size() == header.size(), "capability-boundary CSV row width must match its header");
		Require(!fields[case_col].empty() && case_ids.insert(fields[case_col]).second,
		        "capability reference case identifiers must be non-empty and unique");
		bool observed_acceptance = true;
		bool observed_periodic = false;
		std::string failure;
		try {
			GridEarth earth;
			earth.kind = fields[earth_kind_col] == "wgs84" ? GridEarthKind::Wgs84Source : GridEarthKind::Sphere;
			earth.radius_m = CsvDouble(fields[radius_col]);
			GridGeometry geometry = RegularGrid(std::stoull(fields[nx_col]), std::stoull(fields[ny_col]),
			                                    CsvDouble(fields[lat0_col]), CsvDouble(fields[lon0_col]),
			                                    CsvDouble(fields[dlat_col]), CsvDouble(fields[dlon_col]),
			                                    GridStorageOrder::Separate,
			                                    fields[latitude_policy_col] == "true");
			if (fields[geometry_col] == "projected") {
				geometry = ProjectedGrid(RotatedLatLonParameters{4, 2, 0, -1, 1, 1, 90, 0, 0,
				                                                GridStorageOrder::Separate});
			}
			auto definition = GridDefinition(fields[geometry_col] == "projected" ? "rotated_lonlat_v1" :
			                                                                       "regular_lonlat_v1",
			                                earth, GridNumericPolicy::Float64V1, std::move(geometry));
			definition.version = static_cast<std::uint32_t>(std::stoul(fields[version_col]));
			observed_periodic = QualifyNearestGrid(definition).periodic_longitude;
		} catch (const ReaderError &error) {
			observed_acceptance = false;
			failure = error.what();
		}
		const bool expected_acceptance = fields[accepted_col] == "true";
		Require(expected_acceptance == observed_acceptance,
		        "independent capability classification mismatch for " + fields[case_col] +
		        ": expected=" + fields[accepted_col] + ", observed=" +
		            (observed_acceptance ? "true" : "false") + ", class=" + fields[reason_col] +
		            (failure.empty() ? "" : ", native_error=" + failure));
		if (expected_acceptance) {
			const bool expected_periodic = fields[reason_col] == "accepted-periodic";
			Require(observed_periodic == expected_periodic,
			        "independent periodic classification mismatch for " + fields[case_col]);
			++accepted;
		} else {
			const std::string expected_class = fields[reason_col];
			const auto matches_class = [&]() {
				if (expected_class == "earth-radius")
					return failure.find("earth radius must be finite") != std::string::npos ||
					       failure.find("earth radius must be positive") != std::string::npos;
				if (expected_class == "earth-model")
					return failure.find("nearest requires a spherical earth definition") != std::string::npos;
				if (expected_class == "definition-version")
					return failure.find("unsupported grid definition version") != std::string::npos;
				if (expected_class == "latitude-policy")
					return failure.find("out-of-range latitude grids are not supported") != std::string::npos ||
					       failure.find("latitude values must lie") != std::string::npos;
				if (expected_class == "grid-geometry")
					return failure.find("regular longitude/latitude grids only") != std::string::npos;
				if (expected_class == "positive-dimensions")
					return failure.find("grid dimensions must be positive") != std::string::npos ||
					       failure.find("regular grid nx and ny must be positive") != std::string::npos;
				if (expected_class == "signed-point-count")
					return failure.find("point count exceeds the signed 64-bit limit") != std::string::npos;
				if (expected_class == "exact-binary64-index")
					return failure.find("axis index exceeds the exact binary64 integer range") != std::string::npos;
				if (expected_class == "finite-axis-parameters")
					return failure.find("must be finite") != std::string::npos;
				if (expected_class == "finite-axis-endpoint") return failure.find("coordinate overflows DOUBLE") != std::string::npos;
				if (expected_class == "nonzero-axis-step")
					return failure.find("dlat and dlon must be non-zero") != std::string::npos;
				if (expected_class == "nonfinite-period")
					return failure.find("longitude period calculation is non-finite") != std::string::npos;
				if (expected_class == "latitude-range")
					return failure.find("latitude values must lie") != std::string::npos ||
					       failure.find("source latitude endpoints must lie") != std::string::npos;
				if (expected_class == "latitude-binary64-collapse")
					return failure.find("latitude axis must be strictly monotonic") != std::string::npos ||
					       failure.find("latitude coordinates collapse in binary64") != std::string::npos;
				if (expected_class == "longitude-binary64-collapse")
					return failure.find("unwrapped longitude axis must be strictly monotonic") != std::string::npos ||
					       failure.find("unwrapped longitude coordinates collapse in binary64") != std::string::npos;
				if (expected_class == "duplicate-normalized-longitude")
					return failure.find("normalized source longitudes are not unique") != std::string::npos;
				if (expected_class == "multiple-longitude-wraps")
					return failure.find("longitude axis wraps more than once") != std::string::npos;
				return false;
			};
			Require(matches_class(), "native rejection class mismatch for " + fields[case_col] +
			                         ": expected=" + expected_class + ", native_error=" + failure);
			++rejected;
			if (std::find(rejection_classes.begin(), rejection_classes.end(), expected_class) == rejection_classes.end())
				rejection_classes.push_back(expected_class);
		}
		++compared;
	}
	Require(compared == 377 && accepted == 203 && rejected == 174,
	        "capability matrix must match all 377 frozen acceptance and rejection cases");
	const std::vector<std::string> required_case_ids{
	    "reject-zero-longitude-dimension", "reject-zero-latitude-dimension", "reject-signed-point-count-overflow",
	    "reject-earth-radius-zero", "reject-earth-radius-negative", "reject-earth-radius-nan",
	    "reject-earth-radius-infinity", "reject-earth-ellipsoid", "reject-definition-version",
	    "reject-out-of-range-policy", "reject-projected-geometry",
	    "reject-longitude-index-beyond-exact-binary64", "reject-latitude-index-beyond-exact-binary64",
	    "valid-regional-positive", "valid-regional-negative", "valid-global-period", "valid-latitude-band",
	    "singleton-longitude", "singleton-latitude", "reject-latitude-below-domain", "accepted-latitude-upper-endpoint",
	    "reject-latitude-collapse-subnormal", "reject-longitude-collapse-large-origin",
	    "reject-duplicate-normalized-meridian", "reject-multiple-wraps", "valid-period-one-ulp-short",
	    "valid-regional-period-one-ulp-long", "reject-full-span-preempted-by-wrap-check", "reject-zero-longitude-step",
	    "reject-negative-zero-longitude-step", "reject-zero-latitude-step", "reject-negative-zero-latitude-step",
	    "reject-nan-longitude-origin", "reject-positive-infinity-longitude-step",
	    "reject-negative-infinity-latitude-origin", "reject-nan-latitude-step", "reject-displacement-overflow",
	    "reject-nonfinite-longitude-period"};
	for (const auto &required : required_case_ids)
		Require(case_ids.find(required) != case_ids.end(), "capability matrix is missing boundary case: " + required);
	for (const auto &required : required_rejection_classes)
		Require(std::find(rejection_classes.begin(), rejection_classes.end(), required) != rejection_classes.end(),
		        "capability matrix is missing rejection class: " + required);
#endif
}

void TestNcepGfs025ProfileManifest() {
#ifndef DUCKOMO_SAMPLING_GFS025_PROFILE_REFERENCE_PATH
	throw std::runtime_error("DUCKOMO_SAMPLING_GFS025_PROFILE_REFERENCE_PATH is not configured");
#else
	std::ifstream input(DUCKOMO_SAMPLING_GFS025_PROFILE_REFERENCE_PATH);
	Require(input.good(), "independent ncep_gfs025 profile reference CSV must be readable");
	std::string line;
	Require(static_cast<bool>(std::getline(input, line)), "ncep_gfs025 profile CSV must have a header");
	const auto header = CsvRow(line);
	const auto target_col = CompareColumn(header, "target_id");
	const auto lon_col = CompareColumn(header, "target_lon");
	const auto lat_col = CompareColumn(header, "target_lat");
	const auto point_col = CompareColumn(header, "source_point_index");
	const auto x_col = CompareColumn(header, "source_x");
	const auto y_col = CompareColumn(header, "source_y");
	const auto source_lon_col = CompareColumn(header, "source_lon");
	const auto source_lat_col = CompareColumn(header, "source_lat");
	const auto distance_col = CompareColumn(header, "distance_m");
	const auto scientific_col = CompareColumn(header, "high_precision_distance_m");
	const auto tie_col = CompareColumn(header, "tie_count");
	const auto comparisons_col = CompareColumn(header, "coordinate_comparisons");
	const auto definition = Definition(RegularGrid(1440, 721, -90.0, -180.0, 0.25, 0.25,
	                                              GridStorageOrder::Separate));
	const auto &grid = std::get<RegularGrid>(definition.geometry);
	const auto capability = QualifyNearestGrid(definition);
	Require(capability.periodic_longitude && capability.coverage == NearestCoverageKind::GlobalSphere,
	        "profile matrix requires the full ncep_gfs025 global sphere capability");
	std::uint64_t compared = 0;
	std::uint64_t total_comparisons = 0;
	std::vector<std::string> target_ids;
	std::unordered_set<std::string> unique_targets;
	while (std::getline(input, line)) {
		if (line.empty()) continue;
		const auto fields = CsvRow(line);
		Require(fields.size() == header.size(), "ncep_gfs025 profile CSV row width must match its header");
		Require(!fields[target_col].empty() && unique_targets.insert(fields[target_col]).second,
		        "ncep_gfs025 profile target identifiers must be non-empty and unique");
		const auto target_lon = CsvDouble(fields[lon_col]);
		const auto target_lat = CsvDouble(fields[lat_col]);
		const auto result = LocateNearestRegularGrid(grid, capability, target_lon, target_lat);
		Require(result.inside_domain && result.source, "profile target must lie inside ncep_gfs025 global coverage");
		Require(result.source->point_index == std::stoull(fields[point_col]) &&
		            result.source->x == std::stoull(fields[x_col]) && result.source->y == std::stoull(fields[y_col]),
		        "full ncep_gfs025 profile winner identity differs for " + fields[target_col]);
		Require(result.source->longitude == CsvDouble(fields[source_lon_col]) &&
		            result.source->latitude == CsvDouble(fields[source_lat_col]),
		        "full ncep_gfs025 profile source coordinate differs for " + fields[target_col]);
		Require(result.source->distance_m == CsvDouble(fields[distance_col]),
		        "full ncep_gfs025 profile binary64 distance differs for " + fields[target_col]);
		const auto scientific = std::stold(fields[scientific_col]);
		const auto allowed = std::max(1e-6L, std::abs(scientific) * 1e-12L);
		Require(std::abs(static_cast<long double>(result.source->distance_m) - scientific) <= allowed,
		        "full ncep_gfs025 scientific distance exceeds the ordinary accuracy bound");
		Require(result.coordinate_comparisons == 1440ULL * 721ULL &&
		            std::stoull(fields[comparisons_col]) == 1440ULL * 721ULL,
		        "full ncep_gfs025 profile check must enumerate every spatial source");
		const auto expected_tie_count = fields[target_col] == "equatorial-four-way-midpoint" ? 2ULL :
		                                fields[target_col] == "north-pole-band" ? 1440ULL : 1ULL;
		const auto expected_point_index = fields[target_col] == "equatorial-four-way-midpoint" ? 519840ULL :
		                                  fields[target_col] == "north-pole-band" ? 1036800ULL :
		                                  std::stoull(fields[point_col]);
		Require(std::stoull(fields[tie_col]) == expected_tie_count &&
		            result.source->point_index == expected_point_index,
		        "GFS025 profile tie classification or minimum source point index differs for " + fields[target_col]);
		total_comparisons += result.coordinate_comparisons;
		target_ids.push_back(fields[target_col]);
		++compared;
	}
	const std::vector<std::string> required_targets{
	    "south-near-pole", "south-west-interior", "southern-midlatitude", "southern-equator-crossing",
	    "equatorial-seam-east", "equatorial-seam-west", "equatorial-four-way-midpoint",
	    "equatorial-exact-source", "northern-midlatitude", "north-near-pole", "north-pole-band",
	    "wrapped-longitude"};
	Require(compared == required_targets.size() && total_comparisons == compared * 1440ULL * 721ULL,
	        "independent profile matrix must cover every frozen target with full-grid enumeration");
	for (const auto &required : required_targets)
		Require(unique_targets.find(required) != unique_targets.end(),
		        "ncep_gfs025 profile is missing frozen target: " + required);
#endif
}

void TestAcceptedAxisMatrix() {
	const std::vector<double> longitude_origins{-179.5, -90, 0, 90, 179.5};
	const std::vector<double> longitude_steps{-2, -0.25, 0.25, 2};
	const std::vector<double> latitude_origins{-89, -45, 0, 45, 89};
	const std::vector<double> latitude_steps{-0.5, 0.5};
	std::uint64_t checked = 0;
	for (const auto lon0 : longitude_origins) for (const auto dlon : longitude_steps)
		for (const auto lat0 : latitude_origins) for (const auto dlat : latitude_steps) {
			auto definition = Definition(RegularGrid(5, 3, lat0, lon0, dlat, dlon, GridStorageOrder::Separate));
			const auto &grid = std::get<RegularGrid>(definition.geometry);
			const auto capability = QualifyNearestGrid(definition);
			const auto target = grid.Coordinate(1, 2);
			const auto result = LocateNearestRegularGrid(grid, capability, target.longitude, target.latitude);
			Require(result.inside_domain && result.source && result.source->distance_m == 0 &&
			            result.source->point_index == 7,
			        "accepted origin/step profile must preserve exact source identity");
			++checked;
		}
	Require(checked == 200, "accepted axis profile matrix must contain 200 cases");
}

} // namespace

int main() {
	try {
		TestIndependentManifest();
		TestIndependentScientificDistances();
		TestExactTiesAndLayouts();
		TestNcepGfs025Profile();
		TestThresholdAdjacency();
		TestCoverageAndCapabilityBoundaries();
		TestDistanceAndCancellation();
		TestAcceptedAxisMatrix();
		TestCapabilityBoundaryManifest();
		TestNcepGfs025ProfileManifest();
		std::cout << "nearest_sampling_test: ok\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "nearest_sampling_test: " << error.what() << '\n';
		return 1;
	}
}
