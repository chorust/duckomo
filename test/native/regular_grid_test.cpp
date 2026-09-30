#include "duckomo/regular_grid.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "duckomo/domain_bbox.hpp"
#include "duckomo/om_reader.hpp"

namespace {
using namespace duckdb::duckomo;

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
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

void TestNonSquareAndNegativeSteps() {
	RegularGrid grid(3, 2, 10.0, 100.0, -1.0, 2.0, GridStorageOrder::Separate);
	Require(grid.Nx() == 3 && grid.Ny() == 2 && grid.PointCount() == 6, "non-square grid dimensions");
	auto point = grid.Coordinate(1, 2);
	Require(point.latitude == 9.0 && point.longitude == 104.0, "negative latitude step and row-major source point");
	RegularGrid reverse_lon(2, 1, 0, 181, 1, -1, GridStorageOrder::Separate);
	Require(reverse_lon.Coordinate(0, 0).longitude == -179, "normalize positive seam crossing");
	Require(reverse_lon.Coordinate(0, 1).longitude == -180, "normalize negative longitude step");
}

void TestSingleCellAndSeamDuplicates() {
	RegularGrid one(1, 1, 90, 180, 0.25, 0.25, GridStorageOrder::Separate);
	Require(one.Coordinate(0, 0).latitude == 90 && one.Coordinate(0, 0).longitude == -180,
	        "single polar point and half-open longitude interval");
	RegularGrid repeated(3, 1, 0, 0, 1, 360, GridStorageOrder::Separate);
	Require(repeated.Coordinate(0, 0).longitude == repeated.Coordinate(0, 1).longitude &&
	            repeated.Coordinate(0, 1).longitude == repeated.Coordinate(0, 2).longitude,
	        "repeated geographic points preserve source positions");
	Require(repeated.Coordinate(0, 0).longitude == 0 && repeated.Coordinate(0, 2).longitude == 0,
	        "0–360 wrapping has an independent expected longitude");
	RegularGrid negative_endpoint(1, 1, -90, -180, 1, 1, GridStorageOrder::Separate);
	RegularGrid positive_endpoint(1, 1, -90, 180, 1, 1, GridStorageOrder::Separate);
	Require(negative_endpoint.Coordinate(0, 0).longitude == -180 &&
	            positive_endpoint.Coordinate(0, 0).longitude == -180,
	        "both longitude endpoint conventions map to the half-open -180 endpoint");
	RegularGrid one_row(3, 1, 90, -3, -1, 2, GridStorageOrder::Separate);
	Require(one_row.Coordinate(0, 2).latitude == 90 && one_row.Coordinate(0, 2).longitude == 1,
	        "single-row grids preserve their only latitude and varying longitudes");
	RegularGrid one_column(1, 3, -90, 7, 1, -1, GridStorageOrder::Separate);
	Require(one_column.Coordinate(2, 0).latitude == -88 && one_column.Coordinate(2, 0).longitude == 7,
	        "single-column grids preserve their only longitude and varying latitudes");
}

void TestInvalidDefinitions() {
	RequireError(ReaderErrorCode::InvalidShape,
	             [] { RegularGrid(0, 1, 0, 0, 1, 1, GridStorageOrder::Separate); }, "zero nx");
	RequireError(ReaderErrorCode::InvalidShape,
	             [] { RegularGrid(1, 1, 0, 0, 0, 1, GridStorageOrder::Separate); }, "zero latitude step");
	RequireError(ReaderErrorCode::InvalidShape,
	             [] { RegularGrid(1, 1, 0, 0, 1, std::numeric_limits<double>::infinity(),
	                              GridStorageOrder::Separate); }, "infinite longitude step");
	RequireError(ReaderErrorCode::InvalidShape,
	             [] { RegularGrid(1, 2, 90, 0, 1, 1, GridStorageOrder::Separate); }, "latitude overflow");
	RequireError(ReaderErrorCode::ShapeOverflow,
	             [] { RegularGrid(static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), 2,
	                              0, 0, 1, 1, GridStorageOrder::Separate); }, "point-count overflow");
	RegularGrid small(2, 2, 0, 0, 1, 1, GridStorageOrder::Separate);
	RequireError(ReaderErrorCode::InvalidSelection, [&] { small.Coordinate(2, 0); }, "row outside grid");
}

void TestWktBboxWhitespace() {
	const RegularGrid grid(1440, 721, -90.0, -180.0, 0.25, 0.25, GridStorageOrder::Separate);
	auto bbox = ParseWktBbox("GEOGCRS[\"WGS 84\", BBOX [ -90.0 , -180.0 , 90.0 , 179.75 ]] ");
	Require(bbox.has_value(), "BBOX keyword and coordinates allow WKT whitespace");
	Require((*bbox)[0] == -90.0 && (*bbox)[1] == -180.0 && (*bbox)[2] == 90.0 && (*bbox)[3] == 179.75,
	        "spaced BBOX values parse without skipping separators");
	Require(ValidateWktBbox("GEOGCRS[\"WGS 84\", BBOX [ -90.0 , -180.0 , 90.0 , 179.75 ]]", grid) ==
	            WktBboxStatus::MatchesGrid,
	        "matching bounds with permitted whitespace are accepted");
	Require(ValidateWktBbox("GEOGCRS[\"WGS 84\", bbox [ -90.0 , -180.0 , 91.0 , 179.75 ]]", grid) ==
	            WktBboxStatus::ConflictsWithGrid,
	        "case-insensitive conflicting bounds are detected");
	Require(ValidateWktBbox("GEOGCRS[\"WGS 84\"]", grid) == WktBboxStatus::Absent,
	        "WKT without a BBOX remains valid");
	Require(!ParseWktBbox("REMARK[\"literal BBOX [1,2,3,4]\"]").has_value(),
	        "BBOX-like text inside a quoted WKT remark is ignored");
	RequireError(ReaderErrorCode::InvalidMetadata,
	             [] { (void)ParseWktBbox("GEOGCRS[\"WGS 84\", BBOX [1,2,3]]"); },
	             "malformed BBOX coordinates reject");
}

} // namespace

int main() {
	try {
		TestNonSquareAndNegativeSteps();
		TestSingleCellAndSeamDuplicates();
		TestInvalidDefinitions();
		TestWktBboxWhitespace();
		std::cout << "regular grid checks passed\n";
		return 0;
	} catch (const std::exception &exception) {
		std::cerr << "regular grid checks failed: " << exception.what() << '\n';
		return 1;
	}
}
