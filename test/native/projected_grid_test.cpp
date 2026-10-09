#include "duckomo/projected_grid.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

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

void TestRotatedBasisAndSourceArithmetic() {
	ProjectedGrid identity(RotatedLatLonParameters{3, 2, -1, -1, 1, 1, 90, 0, 0,
	                                                GridStorageOrder::Separate});
	auto p00 = identity.CoordinateAt(0, 0, GridNumericPolicy::Float64V1);
	auto p21 = identity.CoordinateAt(2, 1, GridNumericPolicy::Float64V1);
	RequireNear(p00.latitude, -1, 1e-12, "north-pole basis identity latitude at (0,0)");
	RequireNear(p00.longitude, -1, 1e-12, "north-pole basis identity longitude at (0,0)");
	RequireNear(p21.latitude, 0, 1e-12, "north-pole basis identity latitude at (2,1)");
	RequireNear(p21.longitude, 1, 1e-12, "north-pole basis identity longitude at (2,1)");

	ProjectedGrid source_compatible(RotatedLatLonParameters{4, 3, -2.5, -1.25, 1.1, 0.75,
	                                                        31.7583, 87.597, 180,
	                                                        GridStorageOrder::Separate});
	const auto source = source_compatible.CoordinateAt(2, 1, GridNumericPolicy::OpenMeteoF32V1);
	const auto analytic = source_compatible.CoordinateAt(2, 1, GridNumericPolicy::Float64V1);
	Require(std::isfinite(source.latitude) && std::isfinite(source.longitude), "source-compatible rotated result is finite");
	RequireNear(source.latitude, 57.740447998046875, 1e-6,
	            "rotated Float32 output matches the independently evaluated source arithmetic vector");
	RequireNear(source.longitude, -92.96502685546875, 1e-6,
	            "rotated Float32 longitude matches the independently evaluated source arithmetic vector");
	RequireNear(source.latitude, analytic.latitude, 1e-4, "source arithmetic agrees with independent rotated basis latitude");
	RequireNear(source.longitude, analytic.longitude, 1e-4, "source arithmetic agrees with independent rotated basis longitude");

	const auto north_pole = ProjectedGrid(RotatedLatLonParameters{1, 1, 0, 90, 1, -1, 90, 0, 0,
	                                                              GridStorageOrder::Separate})
	                            .CoordinateAt(0, 0, GridNumericPolicy::Float64V1);
	Require(north_pole.latitude == 90 && north_pole.longitude == 0,
	        "geographic pole has the deterministic longitude zero convention");
}

void TestLambertStandardParallelAndSouthernHemisphere() {
	ProjectedGrid lambert(LambertParameters{3, 2, 0, 0, 1000, 1000, 17, 46.244, 46.244, 46.244,
	                                        6371229, 0, 0, GridStorageOrder::Separate});
	const auto origin = lambert.CoordinateAt(0, 0, GridNumericPolicy::Float64V1);
	RequireNear(origin.latitude, 46.244, 1e-10, "Lambert one-standard-parallel origin latitude");
	RequireNear(origin.longitude, 17, 1e-10, "Lambert false-origin longitude");
	const auto f32 = lambert.CoordinateAt(1, 1, GridNumericPolicy::OpenMeteoF32V1);
	Require(std::isfinite(f32.latitude) && std::isfinite(f32.longitude), "Lambert source-compatible path is finite");
	RequireNear(f32.latitude, 46.25297927856445, 1e-6,
	            "Lambert Float32 latitude matches the independently evaluated source arithmetic vector");
	RequireNear(f32.longitude, 17.013004302978516, 1e-6,
	            "Lambert Float32 longitude matches the independently evaluated source arithmetic vector");

	ProjectedGrid south(LambertParameters{2, 2, 0, 0, 1000, -1000, 0, -40, -33, -45,
	                                      6371229, 0, 0, GridStorageOrder::Separate});
	const auto south_origin = south.CoordinateAt(0, 0, GridNumericPolicy::Float64V1);
	RequireNear(south_origin.latitude, -40, 1e-10, "southern Lambert false-origin latitude");
	RequireNear(south_origin.longitude, 0, 1e-10, "southern Lambert false-origin longitude");

	RequireError(ReaderErrorCode::InvalidShape,
	             [] { ProjectedGrid(LambertParameters{1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 6371229}); },
	             "degenerate Lambert cone");

	ProjectedGrid two_parallel(LambertParameters{2, 2, 0, 0, 1000, 1000, 17, 46.244, 40, 50,
	                                             6371229, 0, 0, GridStorageOrder::Separate});
	const auto false_origin = two_parallel.CoordinateAt(0, 0, GridNumericPolicy::Float64V1);
	RequireNear(false_origin.latitude, 46.244, 1e-10, "Lambert two-standard-parallel false-origin latitude");
	RequireNear(false_origin.longitude, 17, 1e-10, "Lambert two-standard-parallel false-origin longitude");

	constexpr double pi = 3.141592653589793238462643383279502884;
	const double phi1 = pi / 4;
	const double n = std::sin(phi1);
	const double f = std::cos(phi1) * std::pow(std::tan(pi / 4 + phi1 / 2), n) / n;
	const double radius = 6371229.0;
	ProjectedGrid cone_apex(LambertParameters{1, 1, 0, f * radius, 1, 1, 0, 0, 45, 45,
	                                          radius, 0, 0, GridStorageOrder::Separate});
	RequireError(ReaderErrorCode::InvalidShape,
	             [&] { (void)cone_apex.CoordinateAt(0, 0, GridNumericPolicy::Float64V1); },
	             "Lambert cone apex is a rejected projection singularity");
}

void TestStereographicCenterScaleAndFlattening() {
	ProjectedGrid stereo(StereographicParameters{3, 2, 0, 0, 12742458, 1, 0, 0, 6371229, 1,
	                                             0, 0, GridStorageOrder::LongitudeFastest});
	const auto center = stereo.CoordinateAt(0, 0, GridNumericPolicy::Float64V1);
	Require(center.latitude == 0 && center.longitude == 0, "stereographic rho=0 uses exact center limit");
	const auto quarter_turn = stereo.CoordinateAt(1, 0, GridNumericPolicy::Float64V1);
	RequireNear(quarter_turn.latitude, 0, 1e-10, "stereographic equatorial 90-degree latitude");
	RequireNear(quarter_turn.longitude, 90, 1e-10, "stereographic scale-adjusted equatorial 90-degree longitude");
	ProjectedGrid scaled_stereo(StereographicParameters{1, 1, 12742458, 0, 1, 1, 0, 0, 6371229, 2,
	                                                     0, 0, GridStorageOrder::Separate});
	const auto scaled = scaled_stereo.CoordinateAt(0, 0, GridNumericPolicy::Float64V1);
	RequireNear(scaled.latitude, 0, 1e-10, "stereographic scale-factor latitude");
	RequireNear(scaled.longitude, 2.0 * std::atan(0.5) * 180.0 / 3.14159265358979323846, 1e-10,
	            "stereographic scale factor changes inverse angular distance");
	ProjectedGrid polar_center(StereographicParameters{1, 1, 0, 0, 1, 1, 90, 37, 6371229, 1,
	                                                   0, 0, GridStorageOrder::Separate});
	const auto pole = polar_center.CoordinateAt(0, 0, GridNumericPolicy::Float64V1);
	Require(pole.latitude == 90 && pole.longitude == 37, "stereographic pole center has a finite deterministic limit");
	const auto flat = stereo.Coordinate(NativePointPosition{4}, GridNumericPolicy::Float64V1);
	const auto xy = stereo.CoordinateAt(1, 1, GridNumericPolicy::Float64V1);
	RequireNear(flat.latitude, xy.latitude, 1e-12, "x-fastest flattened y index");
	RequireNear(flat.longitude, xy.longitude, 1e-12, "x-fastest flattened x index");

	RequireError(ReaderErrorCode::InvalidShape,
	             [] { ProjectedGrid(StereographicParameters{1, 1, 0, 0, 1, 1, 90, 0, 6371229, 0}); },
	             "non-positive stereographic scale");
	RequireError(ReaderErrorCode::InvalidSelection,
	             [&] { stereo.Coordinate(NativePointPosition{6}, GridNumericPolicy::Float64V1); },
	             "flattened point outside grid");
}

} // namespace

int main() {
	try {
		TestRotatedBasisAndSourceArithmetic();
		TestLambertStandardParallelAndSouthernHemisphere();
		TestStereographicCenterScaleAndFlattening();
		std::cout << "projected grid checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "projected grid checks failed: " << error.what() << '\n';
		return 1;
	}
}
