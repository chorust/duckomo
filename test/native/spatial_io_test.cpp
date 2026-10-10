#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"

#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &database) {
	(void)database;
}
} // namespace duckdb

namespace {
namespace fs = std::filesystem;

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

std::unique_ptr<duckdb::MaterializedQueryResult> RequireSuccess(duckdb::Connection &connection,
	                                                             const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result != nullptr && !result->HasError(), "query failed: " + (result ? result->GetError() : "no result"));
	return result;
}

std::string ReadText(const fs::path &path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), "query did not write its metrics sidecar: " + path.string());
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string JsonObjectValue(const std::string &json, const std::string &object_key, const std::string &key) {
	const auto object_pos = json.find("\"" + object_key + "\":{");
	Require(object_pos != std::string::npos, "metrics JSON is missing object '" + object_key + "'");
	const auto entry = json.find("\"" + key + "\":{", object_pos);
	Require(entry != std::string::npos, "metrics JSON is missing explicit entry '" + key + "'");
	const auto begin = entry + key.size() + 4;
	const auto end = json.find('}', begin);
	Require(end != std::string::npos, "metrics JSON has an unterminated object for '" + key + "'");
	return json.substr(begin, end - begin);
}

std::uint64_t JsonUnsigned(const std::string &json, const std::string &key) {
	const auto marker = "\"" + key + "\":";
	const auto position = json.find(marker);
	Require(position != std::string::npos, "metrics JSON is missing required counter '" + key + "'");
	auto begin = position + marker.size();
	auto end = begin;
	while (end < json.size() && json[end] >= '0' && json[end] <= '9') {
		end++;
	}
	Require(end > begin, "metrics counter '" + key + "' is not an unsigned integer");
	return static_cast<std::uint64_t>(std::stoull(json.substr(begin, end - begin)));
}

struct RunResult final {
	std::unique_ptr<duckdb::MaterializedQueryResult> query;
	std::string metrics;
};

RunResult RunWithMetrics(duckdb::Connection &connection, const fs::path &metrics_path, const std::string &scenario,
	                     const std::string &sql, bool expect_filter_callback = true) {
	std::error_code ignored;
	fs::remove(metrics_path, ignored);
	setenv("DUCKOMO_METRICS_OUTPUT", metrics_path.c_str(), 1);
	setenv("DUCKOMO_SCENARIO", scenario.c_str(), 1);
	auto query = RequireSuccess(connection, sql);
	auto metrics = ReadText(metrics_path);
	Require(metrics.find("\"schema_version\":2") != std::string::npos, "spatial I/O sidecar must use schema v2");
	Require(metrics.find("\"status\":\"success\"") != std::string::npos, "successful query must be recorded as success");
	if (expect_filter_callback) {
		Require(metrics.find("\"filter_callback_invoked\":true") != std::string::npos,
		        "spatial coordinate predicate must reach the complex-filter callback");
	}
	return {std::move(query), std::move(metrics)};
}

fs::path FindCoreFunctions() {
	if (const auto *override_path = std::getenv("DUCKOMO_CORE_FUNCTIONS_EXTENSION")) return override_path;
	const fs::path repository("build/release/repository/v1.5.4");
	if (!fs::exists(repository)) {
		return {};
	}
	for (const auto &entry : fs::recursive_directory_iterator(repository)) {
		if (entry.is_regular_file() && entry.path().filename() == "core_functions.duckdb_extension") {
			return entry.path();
		}
	}
	return {};
}

std::string SpatialProjectionRead() {
	return "read_om('test/data/projection.om', dimensions := map(['humidity','pressure','temperature'], "
	       "[['row','column'],['row','column'],['row','column']]), "
	       "grid := {'nx':127,'ny':83,'lat0':-41.0,'lon0':-126.0,'dlat':1.0,'dlon':2.0,'order':'separate'}, "
	       "spatial_axes := ['row','column'])";
}

std::string Counters(const std::string &metrics, const std::string &path) {
	return JsonObjectValue(metrics, "variables", path);
}

void RequireZeroValueReads(const std::string &metrics, const std::string &scenario) {
	for (const auto *path : {"/humidity", "/pressure", "/temperature"}) {
		const auto counters = Counters(metrics, path);
		for (const auto *field : {"index_bytes", "index_requests", "data_bytes", "data_requests", "decoded_chunks"}) {
			Require(JsonUnsigned(counters, field) == 0,
			        scenario + " must record an explicit zero " + field + " for " + path);
		}
		Require(counters.find("\"decode_count_complete\":true") != std::string::npos,
		        scenario + " must report complete zero decode accounting for " + path);
	}
}

void RequireVariableZero(const std::string &metrics, const std::string &path, const std::string &scenario) {
	const auto counters = Counters(metrics, path);
	for (const auto *field : {"index_bytes", "index_requests", "data_bytes", "data_requests", "decoded_chunks"}) {
		Require(JsonUnsigned(counters, field) == 0,
		        scenario + " must record an explicit zero " + field + " for " + path);
	}
	Require(counters.find("\"decode_count_complete\":true") != std::string::npos,
	        scenario + " must report complete zero decode accounting for " + path);
}

void RequireVariableRead(const std::string &metrics, const std::string &path, const std::string &scenario) {
	const auto counters = Counters(metrics, path);
	Require(JsonUnsigned(counters, "data_bytes") > 0 && JsonUnsigned(counters, "decoded_chunks") > 0,
	        scenario + " must record successful data reads and decoded chunks for " + path);
	Require(counters.find("\"decode_count_complete\":true") != std::string::npos,
	        scenario + " must report complete decode accounting for " + path);
}

void TestSpatialIO(duckdb::Connection &connection) {
	const auto metrics_path = fs::temp_directory_path() /
	                          ("duckomo-spatial-io-" + std::to_string(static_cast<unsigned long long>(getpid())) + ".json");
	const auto source = SpatialProjectionRead();
	const auto full = RunWithMetrics(connection, metrics_path, "full",
	                                 "SELECT humidity, temperature, lat, lon FROM " + source, false);
	Require(full.query->RowCount() == 83 * 127, "fixed projection fixture full scan has 10,541 rows");
	Require(full.metrics.find("\"selection_mode\":\"full\"") != std::string::npos,
	        "full scan is explicitly measured as full selection");
	std::map<std::pair<double, double>, std::pair<float, float>> full_values;
	for (idx_t row = 0; row < full.query->RowCount(); row++) {
		const auto humidity = full.query->GetValue(0, row).GetValue<float>();
		const auto temperature = full.query->GetValue(1, row).GetValue<float>();
		const auto latitude = full.query->GetValue(2, row).GetValue<double>();
		const auto longitude = full.query->GetValue(3, row).GetValue<double>();
		full_values[{latitude, longitude}] = {humidity, temperature};
	}

	const auto restricted = RunWithMetrics(
	    connection, metrics_path, "restricted",
	    "SELECT temperature, lat, lon FROM " + source +
	        " WHERE lat BETWEEN -2 AND 2 AND lon BETWEEN -4 AND 4");
	Require(restricted.query->RowCount() == 25, "fixed latitude/longitude window returns the expected 25 positions");
	Require(restricted.metrics.find("\"selection_mode\":\"restricted\"") != std::string::npos,
	        "selected scan is explicitly measured as restricted");
	Require(JsonUnsigned(restricted.metrics, "candidate_rows") == 25,
	        "candidate row accounting matches the fixed spatial window");
	for (idx_t row = 0; row < restricted.query->RowCount(); row++) {
		const auto temperature = restricted.query->GetValue(0, row).GetValue<float>();
		const auto latitude = restricted.query->GetValue(1, row).GetValue<double>();
		const auto longitude = restricted.query->GetValue(2, row).GetValue<double>();
		const auto full_row = full_values.find({latitude, longitude});
		Require(full_row != full_values.end() && full_row->second.second == temperature,
		        "restricted output matches the corresponding full-scan source position exactly");
	}
	const auto full_temperature = Counters(full.metrics, "/temperature");
	const auto restricted_temperature = Counters(restricted.metrics, "/temperature");
	Require(JsonUnsigned(restricted_temperature, "data_bytes") < JsonUnsigned(full_temperature, "data_bytes"),
	        "restricted temperature data bytes are strictly below full scan bytes");
	Require(JsonUnsigned(restricted_temperature, "decoded_chunks") < JsonUnsigned(full_temperature, "decoded_chunks"),
	        "restricted temperature decoded chunks are strictly below full scan chunks");
	const auto full_bytes = JsonUnsigned(full_temperature, "data_bytes");
	const auto restricted_bytes = JsonUnsigned(restricted_temperature, "data_bytes");
	const auto full_chunks = JsonUnsigned(full_temperature, "decoded_chunks");
	const auto restricted_chunks = JsonUnsigned(restricted_temperature, "decoded_chunks");
	RequireVariableZero(restricted.metrics, "/humidity", "temperature-only restricted scan");
	RequireVariableZero(restricted.metrics, "/pressure", "temperature-only restricted scan");

	const auto empty = RunWithMetrics(connection, metrics_path, "empty",
	                                  "SELECT temperature FROM " + source + " WHERE lat > 90");
	Require(empty.query->RowCount() == 0 && empty.metrics.find("\"selection_mode\":\"empty\"") != std::string::npos,
	        "empty spatial selection completes successfully with explicit empty mode");
	RequireZeroValueReads(empty.metrics, "empty selection");

	const auto coordinates = RunWithMetrics(
	    connection, metrics_path, "coordinates",
	    "SELECT lat, lon FROM " + source + " WHERE lat BETWEEN -2 AND 2 AND lon BETWEEN -4 AND 4");
	Require(coordinates.query->RowCount() == 25, "coordinate-only query preserves the selected positions");
	RequireZeroValueReads(coordinates.metrics, "coordinate-only query");

	const auto count = RunWithMetrics(
	    connection, metrics_path, "count",
	    "SELECT count(*) FROM " + source + " WHERE lat BETWEEN -2 AND 2 AND lon BETWEEN -4 AND 4");
	Require(count.query->RowCount() == 1 && count.query->GetValue(0, 0).GetValue<std::int64_t>() == 25,
	        "count-only query returns the selected cardinality");
	RequireZeroValueReads(count.metrics, "count-only query");

	const auto mixed = RunWithMetrics(
	    connection, metrics_path, "mixed",
	    "SELECT temperature FROM " + source +
	        " WHERE humidity = 96 AND lat BETWEEN -41 AND -37 AND lon BETWEEN 50 AND 70");
	Require(mixed.query->RowCount() == 1, "mixed value/spatial window contains the pinned humidity=96 source row");
	Require(mixed.metrics.find("\"selection_mode\":\"restricted\"") != std::string::npos,
	        "safe spatial terms can narrow an AND expression that retains a value predicate");
	RequireVariableRead(mixed.metrics, "/temperature", "mixed spatial/value filter");
	RequireVariableRead(mixed.metrics, "/humidity", "mixed spatial/value filter");
	RequireVariableZero(mixed.metrics, "/pressure", "mixed spatial/value filter");

	const auto safe_or = RunWithMetrics(
	    connection, metrics_path, "safe_or",
	    "SELECT temperature, lat, lon FROM " + source +
	        " WHERE lon >= 124 OR lon <= -124");
	Require(safe_or.query->RowCount() == 332, "safe seam OR returns its exact residual-filtered rows");
	Require(safe_or.metrics.find("\"selection_mode\":\"restricted\"") != std::string::npos &&
	            safe_or.metrics.find("unsupported_or_expression") == std::string::npos,
	        "a complete two-interval seam OR narrows the spatial candidate set");
	Require(JsonUnsigned(safe_or.metrics, "candidate_rows") == 332,
	        "safe seam OR reports the conservative selected row count");
	for (idx_t row = 0; row < safe_or.query->RowCount(); row++) {
		const auto temperature = safe_or.query->GetValue(0, row).GetValue<float>();
		const auto latitude = safe_or.query->GetValue(1, row).GetValue<double>();
		const auto longitude = safe_or.query->GetValue(2, row).GetValue<double>();
		const auto full_row = full_values.find({latitude, longitude});
		Require(full_row != full_values.end() && full_row->second.second == temperature,
		        "safe OR output matches the corresponding full-scan source position exactly");
	}

	const auto unsafe_or = RunWithMetrics(
	    connection, metrics_path, "unsafe_or",
	    "SELECT temperature, lat, lon FROM " + source + " WHERE lon >= 124 OR humidity = 96");
	std::uint64_t expected_unsafe_or = 0;
	for (const auto &[coordinate, values] : full_values) {
		if (coordinate.second >= 124 || values.first == 96) expected_unsafe_or++;
	}
	Require(unsafe_or.query->RowCount() == expected_unsafe_or,
	        "an OR with a value-variable branch retains complete SQL semantics");
	Require(unsafe_or.metrics.find("\"selection_mode\":\"fallback\"") != std::string::npos &&
	            unsafe_or.metrics.find("unsafe_or_expression") != std::string::npos,
	        "an OR with an unsafe branch widens the spatial candidate set with a diagnostic");
	Require(JsonUnsigned(unsafe_or.metrics, "candidate_rows") == 10541,
	        "an unsafe OR candidate upper bound covers the complete source relation");
	for (idx_t row = 0; row < unsafe_or.query->RowCount(); row++) {
		const auto temperature = unsafe_or.query->GetValue(0, row).GetValue<float>();
		const auto latitude = unsafe_or.query->GetValue(1, row).GetValue<double>();
		const auto longitude = unsafe_or.query->GetValue(2, row).GetValue<double>();
		const auto full_row = full_values.find({latitude, longitude});
		Require(full_row != full_values.end() && full_row->second.second == temperature,
		        "unsafe OR output matches the corresponding full-scan source position exactly");
	}
	std::cout << "spatial_io full_rows=" << full.query->RowCount() << " restricted_rows=" << restricted.query->RowCount()
	          << " full_temperature_data_bytes=" << full_bytes << " restricted_temperature_data_bytes=" << restricted_bytes
	          << " full_temperature_decoded_chunks=" << full_chunks
	          << " restricted_temperature_decoded_chunks=" << restricted_chunks << " empty_rows=" << empty.query->RowCount()
	          << " coordinates_rows=" << coordinates.query->RowCount() << " mixed_rows=" << mixed.query->RowCount()
	          << " safe_or_rows=" << safe_or.query->RowCount() << " unsafe_or_rows=" << unsafe_or.query->RowCount() << " count="
	          << count.query->GetValue(0, 0).GetValue<std::int64_t>() << '\n';
	std::error_code ignored;
	fs::remove(metrics_path, ignored);
}

} // namespace

int main() {
	try {
		duckdb::DBConfig config;
		config.SetOptionByName("allow_unsigned_extensions", true);
		duckdb::DuckDB database(nullptr, &config);
		duckdb::Connection connection(database);
		const auto core_functions = FindCoreFunctions();
		Require(!core_functions.empty(), "cannot find core_functions extension in the release repository");
		auto core = RequireSuccess(connection, "LOAD '" + core_functions.string() + "'");
		const auto *extension_override = std::getenv("DUCKOMO_EXTENSION_PATH");
		const auto extension_path = extension_override ? extension_override : "./build/release/extension/duckomo/duckomo.duckdb_extension";
		auto extension = RequireSuccess(connection, "LOAD '" + std::string(extension_path) + "'");
		TestSpatialIO(connection);
		std::cout << "spatial I/O checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "spatial I/O checks failed: " << error.what() << '\n';
		return 1;
	}
}
