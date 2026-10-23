#include "duckdb.hpp"
#include "duckdb_test_compat.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/common/types/selection_vector.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo/region_geometry.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace duckdb {
DUCKOMO_TEST_LOAD_ALL_EXTENSIONS(database) {
	(void)database;
}
} // namespace duckdb

namespace {
using namespace duckdb;
using namespace duckdb::duckomo;

struct Point final {
	double x;
	double y;
};

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

void U32(std::string &out, std::uint32_t value) {
	for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<char>((value >> (i * 8)) & 0xff));
}

void F64(std::string &out, double value) {
	std::uint64_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	for (unsigned i = 0; i < 8; ++i) out.push_back(static_cast<char>((bits >> (i * 8)) & 0xff));
}

std::string PolygonWkb(const std::vector<std::vector<Point>> &rings) {
	std::string out(1, '\x01');
	U32(out, 3);
	U32(out, static_cast<std::uint32_t>(rings.size()));
	for (const auto &ring : rings) {
		U32(out, static_cast<std::uint32_t>(ring.size()));
		for (const auto &point : ring) {
			F64(out, point.x);
			F64(out, point.y);
		}
	}
	return out;
}

std::string Rectangle(double min_x, double min_y, double max_x, double max_y) {
	return PolygonWkb({{{min_x, min_y}, {max_x, min_y}, {max_x, max_y}, {min_x, max_y}, {min_x, min_y}}});
}

std::string MultiPolygonWkb(const std::vector<std::string> &polygons) {
	std::string out(1, '\x01');
	U32(out, 6);
	U32(out, static_cast<std::uint32_t>(polygons.size()));
	for (const auto &polygon : polygons) out += polygon;
	return out;
}

void CheckQuery(Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result && !result->HasError(), "SQL failed: " + sql + (result ? ": " + result->GetError() : ""));
}

void RegisterStub(ClientContext &context, const std::string &name, std::vector<LogicalType> arguments,
                  LogicalType return_type) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	ScalarFunction function(Identifier(name), std::move(arguments), std::move(return_type), ScalarFunction::NopFunction);
#else
	ScalarFunction function(name, std::move(arguments), std::move(return_type), ScalarFunction::NopFunction);
#endif
	CreateScalarFunctionInfo info(std::move(function));
	Catalog::GetSystemCatalog(context).AddFunction(context, info);
}

std::vector<idx_t> Select(RegionPredicateEvaluator &predicate, ClientContext &context,
	                      const std::vector<Point> &points) {
	DataChunk coordinates;
	coordinates.Initialize(context, {LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE)},
	                       REGION_GEOMETRY_BATCH_SIZE);
	for (idx_t i = 0; i < points.size(); ++i) {
		coordinates.SetValue(0, i, Value::DOUBLE(points[i].x));
		coordinates.SetValue(1, i, Value::DOUBLE(points[i].y));
	}
	coordinates.SetCardinality(points.size());
	SelectionVector selected(REGION_GEOMETRY_BATCH_SIZE);
	const auto count = predicate.Select(coordinates, selected);
	std::vector<idx_t> result;
	result.reserve(count);
	for (idx_t i = 0; i < count; ++i) result.push_back(selected.get_index(i));
	return result;
}

void TestMissingDependency() {
	DuckDB database(nullptr);
	DUCKOMO_TEST_LOAD_EXTENSIONS(database);
	Connection connection(database);
	CheckQuery(connection, "BEGIN TRANSACTION");
	bool diagnosed = false;
	std::string diagnostic = "no error";
	try {
		RegionGeometryAdapter adapter(*connection.context);
	} catch (const std::exception &error) {
		diagnostic = error.what();
		diagnosed = diagnostic.find("[spatial_dependency]") != std::string::npos;
	}
	CheckQuery(connection, "ROLLBACK");
	Require(diagnosed, "a missing spatial function must produce the actionable dependency category: " + diagnostic);
}

void TestMissingNearestSignature() {
	DuckDB database(nullptr);
	DUCKOMO_TEST_LOAD_EXTENSIONS(database);
	Connection connection(database);
	CheckQuery(connection, "BEGIN TRANSACTION");
	const auto geometry = LogicalType::GEOMETRY();
	const auto boolean = LogicalType(LogicalTypeId::BOOLEAN);
	const auto double_type = LogicalType(LogicalTypeId::DOUBLE);
	RegisterStub(*connection.context, "st_geomfromwkb", {LogicalType(LogicalTypeId::BLOB)}, geometry);
	RegisterStub(*connection.context, "st_isvalid", {geometry}, boolean);
	RegisterStub(*connection.context, "st_point", {double_type, double_type}, geometry);
	RegisterStub(*connection.context, "st_covers", {geometry, geometry}, boolean);
	RegisterStub(*connection.context, "st_pointonsurface", {LogicalType(LogicalTypeId::VARCHAR)},
	             LogicalType(LogicalTypeId::VARCHAR));
	RegisterStub(*connection.context, "st_x", {geometry}, double_type);
	RegisterStub(*connection.context, "st_y", {geometry}, double_type);
	RegionGeometryAdapter adapter(*connection.context);
	bool rejected = false;
	std::string diagnostic = "no error";
	try {
		adapter.RequireNearestOperations();
	} catch (const std::exception &error) {
		diagnostic = error.what();
		rejected = diagnostic.find("[spatial_dependency]") != std::string::npos &&
		           diagnostic.find("exact signature") != std::string::npos;
	}
	CheckQuery(connection, "ROLLBACK");
	Require(rejected, "nearest must reject a loaded extension with the wrong representative-point signature: " +
	                       diagnostic);
}

void TestAdapterAndWorkerState(const std::string &extension_path, const std::string &expected_hash) {
	DuckDB database(nullptr);
	DUCKOMO_TEST_LOAD_EXTENSIONS(database);
	Connection connection(database);
	CheckQuery(connection, "LOAD '" + extension_path + "'");

	auto metadata = duckdb::duckomo_test::Rows(connection.Query(
	    "SELECT extension_version, coalesce(install_path, '') FROM duckdb_extensions() WHERE extension_name='spatial'"));
	Require(metadata && !metadata->HasError() && metadata->RowCount() == 1,
	        "loaded spatial extension must appear in duckdb_extensions()");
	auto version = duckdb::duckomo_test::Rows(connection.Query("PRAGMA version"));
	Require(version && !version->HasError() && version->RowCount() == 1 && version->ColumnCount() >= 2,
	        "the native runtime must report its DuckDB version");
	std::cout << "duckdb_version=" << version->GetValue(0, 0).ToString() << '\n';
	std::cout << "duckdb_source_id=" << version->GetValue(1, 0).ToString() << '\n';
	std::cout << "spatial_extension_version=" << metadata->GetValue(0, 0).ToString() << '\n';
	std::cout << "spatial_install_path=" << metadata->GetValue(1, 0).ToString() << '\n';
	std::cout << "spatial_package_sha256=" << expected_hash << '\n';

	CheckQuery(connection, "CREATE TEMP MACRO st_point(x, y) AS NULL::GEOMETRY");
	auto shadow = duckdb::duckomo_test::Rows(connection.Query("SELECT st_point(1.0, 2.0) IS NULL"));
	Require(shadow && !shadow->HasError() && shadow->GetValue(0, 0).GetValue<bool>(),
	        "the test temporary function must shadow unqualified ST_Point");

	CheckQuery(connection, "BEGIN TRANSACTION");
	RegionGeometryAdapter adapter(*connection.context);
	const auto polygon_wkb = PolygonWkb({
	    {{0, 0}, {4, 0}, {4, 4}, {0, 4}, {0, 0}},
	    {{1, 1}, {3, 1}, {3, 3}, {1, 3}, {1, 1}},
	});
	const auto polygon = adapter.CreateGeometry(polygon_wkb);
	Require(adapter.IsValid(polygon), "a polygon with a valid hole must pass ST_IsValid");
	const auto representative = adapter.PointOnSurfaceCoordinate(polygon);
	Require(std::isfinite(representative.longitude) && std::isfinite(representative.latitude) &&
	            representative.longitude >= 0.0 && representative.longitude <= 4.0 &&
	            representative.latitude >= 0.0 && representative.latitude <= 4.0,
	        "nearest representative adapter must return a finite point covered by the polygon with a hole");
	auto polygon_expression = adapter.BindCovers(polygon, 0, 1);
	RegionPredicateEvaluator polygon_predicate(*connection.context, *polygon_expression);
	const auto points = Select(polygon_predicate, *connection.context,
	                           {{0.5, 0.5}, {0, 2}, {2, 2}, {1, 2}, {5, 5}, {2, 4}});
	Require(points == std::vector<idx_t>({0, 1, 3, 5}),
	        "ST_Covers must include exterior and hole boundaries while excluding hole interiors");

	const auto empty = adapter.CreateGeometry(PolygonWkb({}));
	Require(adapter.IsValid(empty), "an EMPTY Polygon must be accepted as a valid geometry");
	auto empty_expression = adapter.BindCovers(empty, 0, 1);
	RegionPredicateEvaluator empty_predicate(*connection.context, *empty_expression);
	Require(Select(empty_predicate, *connection.context, {{0, 0}}).empty(), "an EMPTY Polygon cannot cover points");

	const auto multipolygon = adapter.CreateGeometry(MultiPolygonWkb({Rectangle(0, 0, 1, 1), Rectangle(3, 3, 4, 4)}));
	Require(adapter.IsValid(multipolygon), "a disjoint MultiPolygon must pass ST_IsValid");
	auto multi_expression = adapter.BindCovers(multipolygon, 0, 1);
	RegionPredicateEvaluator multi_predicate(*connection.context, *multi_expression);
	Require(Select(multi_predicate, *connection.context, {{0.5, 0.5}, {2, 2}, {3.5, 3.5}}) ==
	            std::vector<idx_t>({0, 2}),
	        "the predicate must evaluate every MultiPolygon component");

	const auto invalid = adapter.CreateGeometry(MultiPolygonWkb({Rectangle(0, 0, 2, 2), Rectangle(1, 1, 3, 3)}));
	Require(!adapter.IsValid(invalid), "overlapping MultiPolygon components must fail whole-geometry validity");

	bool malformed_rejected = false;
	try {
		(void)adapter.CreateGeometry(std::string("bad-wkb"));
	} catch (const std::exception &) {
		malformed_rejected = true;
	}
	Require(malformed_rejected && adapter.IsValid(adapter.CreateGeometry(Rectangle(0, 0, 1, 1))),
	        "an expression exception must release its local state and permit a later valid geometry");

	const auto second_geometry = adapter.CreateGeometry(Rectangle(10, 10, 11, 11));
	auto second_expression = adapter.BindCovers(second_geometry, 0, 1);
	RegionPredicateEvaluator second_predicate(*connection.context, *second_expression);
	for (idx_t i = 0; i < 32; ++i) {
		Require(Select(polygon_predicate, *connection.context, {{0.5, 0.5}, {2, 2}}) ==
		            std::vector<idx_t>({0}),
		        "the first worker expression must retain its own geometry state");
		Require(Select(second_predicate, *connection.context, {{0.5, 0.5}, {10.5, 10.5}}) ==
		            std::vector<idx_t>({1}),
		        "the second worker expression must retain its own geometry state");
	}
	CheckQuery(connection, "ROLLBACK");
}

} // namespace

int main(int argc, char **argv) {
	try {
		Require(argc == 3, "usage: region_geometry_test <spatial-extension-path> <spatial-package-sha256>");
		TestMissingDependency();
		TestMissingNearestSignature();
		TestAdapterAndWorkerState(argv[1], argv[2]);
		std::cout << "region_geometry_test: ok\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "region_geometry_test: " << error.what() << '\n';
		return 1;
	}
}
