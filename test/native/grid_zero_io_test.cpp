#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include "core_functions_extension.hpp"
#include "query_result_compat.hpp"
#include "duckomo_extension.hpp"

#include <cstdint>
#include <filesystem>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace duckdb {
#if DUCKOMO_DUCKDB_API_GENERATION < 2
void ExtensionHelper::LoadAllExtensions(DuckDB &database) {
	database.LoadStaticExtension<DuckomoExtension>();
}
#endif
} // namespace duckdb

namespace {

using namespace duckdb;
using duckomo_test::Query;

std::vector<std::pair<std::string, std::string>> captured_metrics;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

std::string Scalar(Connection &connection, const std::string &sql) {
	auto result = Query(connection, sql);
	Require(result->RowCount() == 1 && result->ColumnCount() == 1, "expected one scalar result: " + sql);
	return result->GetValue(0, 0).ToString();
}

std::string InterleavedRead() {
	return "read_om('test/data/grids/interleaved.om', dimensions := map("
	       "['interleaved/temperature','interleaved/humidity'], "
	       "[['time','latitude_axis','level','longitude_axis','lead_time','member','run'], "
	       "['time','latitude_axis','level','longitude_axis','lead_time','member','run']]), "
	       "grid := {'version':1,'type':'rotated_latlon','numeric_policy':'float64_v1', "
	       "'earth':{'model':'sphere','radius_m':6371229.0}, "
	       "'layout':{'nx':4,'ny':3,'order':'separate'}, "
	       "'parameters':{'x0':0.0,'y0':0.0,'dx':1.0,'dy':1.0,'north_pole_latitude':90.0,"
	       "'north_pole_longitude':0.0,'rotation':0.0}}, "
	       "spatial_axes := ['latitude_axis','longitude_axis'])";
}

std::string LargeValueChunkRead() {
	return "read_om('test/data/grids/large-value-chunk.om', "
	       "dimensions := map(['large_chunk/value'], [['latitude_axis','longitude_axis']]), "
	       "grid := {'nx':256,'ny':256,'lat0':-64.0,'lon0':-128.0,'dlat':0.5,'dlon':1.0,'order':'separate'}, "
	       "spatial_axes := ['latitude_axis','longitude_axis'])";
}

std::string GridFamilyRead(const std::string &family) {
	const auto file = family == "stereographic" ? "y-fastest.om" : "x-fastest.om";
	const auto variable = family == "stereographic" ? "flattened_y_fastest/value" : "flattened_x_fastest/value";
	std::string grid;
	if (family == "rotated_latlon") {
		grid = "{'version':1,'type':'rotated_latlon','numeric_policy':'float64_v1', "
		       "'earth':{'model':'sphere','radius_m':6371229.0}, "
		       "'layout':{'nx':4,'ny':3,'order':'x_fastest'}, "
		       "'parameters':{'x0':0.0,'y0':0.0,'dx':1.0,'dy':1.0,"
		       "'north_pole_latitude':90.0,'north_pole_longitude':0.0,'rotation':0.0}}";
	} else if (family == "lambert_conformal_conic") {
		grid = "{'version':1,'type':'lambert_conformal_conic','numeric_policy':'float64_v1', "
		       "'earth':{'model':'sphere','radius_m':6371229.0}, "
		       "'layout':{'nx':4,'ny':3,'order':'x_fastest'}, "
		       "'parameters':{'x0':0.0,'y0':0.0,'dx':1000.0,'dy':1000.0,"
		       "'central_meridian':10.0,'latitude_of_origin':45.0,"
		       "'standard_parallel_1':45.0,'standard_parallel_2':45.0}}";
	} else if (family == "stereographic") {
		grid = "{'version':1,'type':'stereographic','numeric_policy':'float64_v1', "
		       "'earth':{'model':'sphere','radius_m':6371229.0}, "
		       "'layout':{'nx':4,'ny':3,'order':'y_fastest'}, "
		       "'parameters':{'x0':0.0,'y0':0.0,'dx':1000.0,'dy':1000.0,"
		       "'central_meridian':0.0,'latitude_of_origin':90.0,'scale_factor':1.0}}";
	} else if (family == "reduced_gaussian") {
		grid = "{'version':1,'type':'reduced_gaussian','numeric_policy':'float64_v1', "
		       "'earth':{'model':'wgs84','semi_major_m':6378137.0,'inverse_flattening':298.257223563}, "
		       "'layout':{'order':'row_major'}, "
		       "'parameters':{'n':1,'latitude_rule':'explicit_v1',"
		       "'rows':[{'latitude':60.0,'point_count':6,'longitude_origin':0.0,'longitude_step':60.0},"
		       "{'latitude':-60.0,'point_count':6,'longitude_origin':0.0,'longitude_step':60.0}],"
		       "'subset_segments':NULL}}";
	} else {
		throw std::runtime_error("unknown grid family " + family);
	}
	return "read_om('test/data/grids/" + std::string(file) + "', dimensions := map(['" + variable + "'], [['point']]), "
	       "grid := " + grid + ", spatial_axes := ['point'])";
}

std::string LastMetrics(Connection &connection) {
	const auto metrics = Scalar(connection,
	                            "SELECT metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1");
	Require(metrics.find("\"schema_version\":4") != std::string::npos &&
	            metrics.find("\"terminal_published\":true") != std::string::npos &&
	            metrics.find("\"coordinate_preparation\":{\"evaluations\":") != std::string::npos,
	        "each completed zero-I/O scenario must publish terminal v4 costs and coordinate-preparation evidence");
	return metrics;
}

void CaptureMetrics(const std::string &scenario, const std::string &metrics) {
	captured_metrics.emplace_back(scenario, metrics);
}

void WriteMetricsEvidence() {
	const auto *path = std::getenv("DUCKOMO_GRID_ZERO_IO_EVIDENCE");
	if (path == nullptr || *path == '\0') return;
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	Require(output.good(), "cannot create grid zero-I/O metrics evidence");
	output << "{\"schema_version\":1,\"scenarios\":[";
	for (std::size_t index = 0; index < captured_metrics.size(); index++) {
		if (index) output << ',';
		output << "{\"name\":\"" << captured_metrics[index].first << "\",\"metrics\":"
		       << captured_metrics[index].second << '}';
	}
	output << "]}\n";
	Require(output.good(), "cannot write grid zero-I/O metrics evidence");
}

std::string VariableMetrics(const std::string &metrics, const std::string &path) {
	const auto marker = "\"" + path + "\":{";
	const auto begin = metrics.find(marker);
	Require(begin != std::string::npos, "metrics are missing variable " + path);
	const auto end = metrics.find('}', begin + marker.size());
	Require(end != std::string::npos, "metrics have an unterminated variable " + path);
	return metrics.substr(begin + marker.size(), end - begin - marker.size());
}

void RequireZeroValueReads(const std::string &metrics, const std::string &path, const std::string &scenario) {
	const auto value = VariableMetrics(metrics, path);
	for (const auto *field : {"\"index_bytes\":0", "\"index_requests\":0", "\"data_bytes\":0",
	                          "\"data_requests\":0", "\"decoded_chunks\":0",
	                          "\"decode_complete\":true"}) {
		Require(value.find(field) != std::string::npos,
		        scenario + " must prove zero value decoder/read cost for " + path + " (missing " + field + ")");
	}
}

void TestZeroValueReadsAndNonSpatialMultiplicity(Connection &connection) {
	const auto source = InterleavedRead();
	const std::string geographic_window =
	    " lat BETWEEN 0.99 AND 1.01 AND lon BETWEEN 1.99 AND 2.01";
	auto coordinates = Query(connection, "SELECT lat, lon FROM " + source + " WHERE" + geographic_window);
	Require(coordinates->RowCount() == 32,
	        "coordinate projection preserves time/level/lead/member/run records at one geographic point");
	auto metrics = LastMetrics(connection);
	CaptureMetrics("interleaved_coordinates", metrics);
	RequireZeroValueReads(metrics, "/interleaved/temperature", "coordinate-only spatial query");
	RequireZeroValueReads(metrics, "/interleaved/humidity", "coordinate-only spatial query");

	Require(Scalar(connection, "SELECT count(*) FROM " + source + " WHERE" + geographic_window) == "32",
	        "coordinate COUNT keeps non-spatial source-record multiplicity");
	metrics = LastMetrics(connection);
	CaptureMetrics("interleaved_count", metrics);
	RequireZeroValueReads(metrics, "/interleaved/temperature", "coordinate COUNT");
	RequireZeroValueReads(metrics, "/interleaved/humidity", "coordinate COUNT");

	Require(Scalar(connection, "SELECT count(*) FROM " + source +
	                           " WHERE lat > 90 AND lat < -90") == "0",
	        "contradictory coordinate bounds prove an empty result before value decoding");
	metrics = LastMetrics(connection);
	CaptureMetrics("interleaved_empty", metrics);
	RequireZeroValueReads(metrics, "/interleaved/temperature", "contradictory coordinate query");
	RequireZeroValueReads(metrics, "/interleaved/humidity", "contradictory coordinate query");

	Require(Scalar(connection, "SELECT count(*) FROM " + source +
	                           " WHERE lat >= -90 AND lat <= 90 AND lon >= -180 AND lon < 180") ==
	            "384",
	        "a proven full-domain predicate retains every source record");
	metrics = LastMetrics(connection);
	CaptureMetrics("interleaved_full_domain_count", metrics);
	RequireZeroValueReads(metrics, "/interleaved/temperature", "full-domain coordinate COUNT");
	RequireZeroValueReads(metrics, "/interleaved/humidity", "full-domain coordinate COUNT");
}

void TestValueFilterReadsOnlyItsDependency(Connection &connection) {
	const auto source = InterleavedRead();
	const std::string sql = "SELECT count(*) FROM " + source +
	                        " WHERE lat BETWEEN 0.99 AND 1.01 AND lon BETWEEN 1.99 AND 2.01"
	                        " AND \"interleaved/temperature\" > 0";
	Require(Scalar(connection, sql) == "32", "value filter preserves the complete spatial and residual result");
	const auto metrics = LastMetrics(connection);
	CaptureMetrics("interleaved_value_filter", metrics);
	const auto temperature = VariableMetrics(metrics, "/interleaved/temperature");
	Require(temperature.find("\"data_bytes\":0") == std::string::npos &&
	            temperature.find("\"decoded_chunks\":0") == std::string::npos,
	        "a value predicate reads and decodes its required variable");
	RequireZeroValueReads(metrics, "/interleaved/humidity", "temperature-only value filter");
}

void TestLargeValueChunkIsolation(Connection &connection) {
	const auto result = Query(connection, "SELECT lat, lon FROM " + LargeValueChunkRead() +
	                                        " WHERE lat BETWEEN -0.01 AND 0.01"
	                                        " AND lon BETWEEN -0.01 AND 0.01");
	Require(result->RowCount() == 1, "coordinate selection from a 256 KiB value chunk returns the origin");
	Require(result->GetValue(0, 0).GetValue<double>() == 0.0 &&
	            result->GetValue(1, 0).GetValue<double>() == 0.0,
	        "large-chunk coordinate output is correct");
	const auto metrics = LastMetrics(connection);
	CaptureMetrics("large_value_chunk_coordinates", metrics);
	RequireZeroValueReads(metrics, "/large_chunk/value", "coordinate query with a 256 KiB value chunk");
}

std::string DoubleLiteral(double value) {
	std::ostringstream output;
	output << std::setprecision(17) << value;
	return output.str();
}

void TestGridFamilyZeroValueReads(Connection &connection, const std::string &family) {
	const auto source = GridFamilyRead(family);
	const auto variable = family == "stereographic" ? "/flattened_y_fastest/value" : "/flattened_x_fastest/value";
	auto coordinates = Query(connection, "SELECT lat, lon FROM " + source);
	Require(coordinates->RowCount() == 12, family + " coordinate projection returns all 12 source positions");
	const auto first_latitude = coordinates->GetValue(0, 0).GetValue<double>();
	const auto first_longitude = coordinates->GetValue(1, 0).GetValue<double>();
	Require(std::isfinite(first_latitude) && std::isfinite(first_longitude),
	        family + " coordinate projection returns finite coordinates");
	auto metrics = LastMetrics(connection);
	CaptureMetrics(family + "_full_coordinates", metrics);
	RequireZeroValueReads(metrics, variable, family + " full coordinate projection");

	const auto local = "SELECT count(*) FROM " + source + " WHERE lat BETWEEN " +
	                    DoubleLiteral(first_latitude - 1e-7) + " AND " + DoubleLiteral(first_latitude + 1e-7) +
	                    " AND lon BETWEEN " + DoubleLiteral(first_longitude - 1e-7) + " AND " +
	                    DoubleLiteral(first_longitude + 1e-7);
	const auto local_count = std::stoull(Scalar(connection, local));
	Require(local_count > 0 && local_count <= 12, family + " local coordinate COUNT keeps source positions");
	metrics = LastMetrics(connection);
	CaptureMetrics(family + "_local_count", metrics);
	RequireZeroValueReads(metrics, variable, family + " local coordinate COUNT");

	Require(Scalar(connection, "SELECT count(*) FROM " + source + " WHERE lat > 0 AND lat < 0") == "0",
	        family + " contradictory coordinate COUNT is empty");
	metrics = LastMetrics(connection);
	CaptureMetrics(family + "_empty", metrics);
	RequireZeroValueReads(metrics, variable, family + " contradictory coordinate COUNT");
}

} // namespace

int main() {
	try {
		DBConfig config;
		config.SetOptionByName("allow_unsigned_extensions", true);
		DuckDB database(nullptr, &config);
		#if DUCKOMO_DUCKDB_API_GENERATION >= 2
		database.LoadStaticExtension<DuckomoExtension>();
		#endif
		database.LoadStaticExtension<CoreFunctionsExtension>();
		Connection connection(database);
		TestZeroValueReadsAndNonSpatialMultiplicity(connection);
		TestValueFilterReadsOnlyItsDependency(connection);
		TestLargeValueChunkIsolation(connection);
		for (const auto *family : {"rotated_latlon", "lambert_conformal_conic", "stereographic", "reduced_gaussian"}) {
			TestGridFamilyZeroValueReads(connection, family);
		}
		WriteMetricsEvidence();
		std::cout << "grid zero-I/O checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "grid zero-I/O checks failed: " << error.what() << '\n';
		return 1;
	}
}
