#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
	if (!condition) throw std::runtime_error(message);
}

std::unique_ptr<duckdb::MaterializedQueryResult> Query(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result != nullptr, "query returned no result");
	return result;
}

std::int64_t RequireScalar(duckdb::Connection &connection, const std::string &sql, const std::string &description) {
	auto result = Query(connection, sql);
	Require(!result->HasError(), description + " failed: " + result->GetError());
	Require(result->RowCount() == 1 && result->ColumnCount() == 1, description + " returned a malformed scalar result");
	return result->GetValue(0, 0).GetValue<std::int64_t>();
}

std::string Source(const std::string &path = "test/data/raw.om") {
	return "read_om('" + path + "', dimensions := map(['value'], [['lat','lon']]), "
	       "grid := {'nx':3,'ny':2,'lat0':10.0,'lon0':100.0,'dlat':1.0,'dlon':2.0,'order':'separate'}, "
	       "spatial_axes := ['lat','lon'])";
}

std::string ReadText(const fs::path &path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), "prepared query did not write its metrics sidecar");
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::size_t OpenFileDescriptors() {
	if (!fs::exists("/proc/self/fd")) return 0;
	std::size_t count = 0;
	for (const auto &entry : fs::directory_iterator("/proc/self/fd")) {
		(void)entry;
		count++;
	}
	return count;
}

fs::path FindCoreFunctions() {
	if (const auto *override_path = std::getenv("DUCKOMO_CORE_FUNCTIONS_EXTENSION")) {
		const fs::path path(override_path);
		return fs::is_regular_file(path) ? path : fs::path{};
	}
	const fs::path repository("build/release/repository/v1.5.4");
	if (!fs::exists(repository)) return {};
	for (const auto &entry : fs::recursive_directory_iterator(repository)) {
		if (entry.is_regular_file() && entry.path().filename() == "core_functions.duckdb_extension") return entry.path();
	}
	return {};
}

void RequireBindFailure(duckdb::Connection &connection) {
	const std::string invalid =
	    "SELECT count(*) FROM read_om('test/data/raw.om', dimensions := map(['value'], [['lat','lon']]), "
	    "grid := {'nx':4,'ny':2,'lat0':10.0,'lon0':100.0,'dlat':1.0,'dlon':2.0,'order':'separate'}, "
	    "spatial_axes := ['lat','lon'])";
	auto result = Query(connection, invalid);
	Require(result->HasError(), "invalid spatial shape must fail binding before returning rows");
	Require(result->GetError().find("spatial axis lengths") != std::string::npos,
	        "failed bind should report the spatial shape mismatch");
}

void TestPreparedRebindingAndAlternatingFailures(duckdb::Connection &connection, const fs::path &metrics_file) {
	setenv("DUCKOMO_METRICS_OUTPUT", metrics_file.c_str(), 1);
	setenv("DUCKOMO_SCENARIO", "spatial_prepared_region", 1);
	const auto prepare = "PREPARE spatial_region AS SELECT count(*) FROM " + Source() +
	                     " WHERE lat = ? AND lon BETWEEN ? AND ?";
	auto prepared = Query(connection, prepare);
	Require(!prepared->HasError(), "spatial prepared statement failed to bind: " + prepared->GetError());
	prepared.reset();
	Require(RequireScalar(connection, "EXECUTE spatial_region(10,100,102)", "first prepared region") == 2,
	        "first prepared region returned a stale or incorrect count");
	auto metrics = ReadText(metrics_file);
	Require(metrics.find("\"selection_mode\":\"restricted\"") != std::string::npos &&
	            metrics.find("\"candidate_rows\":2") != std::string::npos,
	        "first execution must capture its own selection and candidate rows");
	Require(RequireScalar(connection, "EXECUTE spatial_region(11,104,104)", "second prepared region") == 1,
	        "rebound prepared statement must not inherit the prior region");
	metrics = ReadText(metrics_file);
	Require(metrics.find("\"candidate_rows\":1") != std::string::npos,
	        "second execution must replace, not accumulate, the callback selection");

	const auto baseline_descriptors = OpenFileDescriptors();
	std::size_t maximum_descriptors = baseline_descriptors;
	for (std::size_t iteration = 0; iteration < 100; iteration++) {
		const auto row = iteration % 2 == 0 ? "10,100,102" : "11,104,104";
		const auto expected = iteration % 2 == 0 ? 2 : 1;
		Require(RequireScalar(connection, "EXECUTE spatial_region(" + std::string(row) + ")",
		                      "prepared region iteration " + std::to_string(iteration)) == expected,
		        "prepared region iteration returned stale predicate state");
		RequireBindFailure(connection);
		Require(RequireScalar(connection, "EXECUTE spatial_region(" + std::string(row) + ")",
		                      "prepared recovery iteration " + std::to_string(iteration)) == expected,
		        "valid prepared execution did not recover after a failed bind");
		maximum_descriptors = std::max(maximum_descriptors, OpenFileDescriptors());
	}
	Require(maximum_descriptors <= baseline_descriptors,
	        "100 spatial success/failure alternations grew process file descriptors");
	auto deallocate = Query(connection, "DEALLOCATE spatial_region");
	Require(!deallocate->HasError(), "cannot deallocate spatial prepared statement");
	std::error_code ignored;
	fs::remove(metrics_file, ignored);
}

void TestIndependentAliases(duckdb::Connection &connection) {
	const auto sql = "SELECT count(*) FROM " + Source() + " AS a JOIN " + Source() +
	                 " AS b ON a.lon = b.lon WHERE a.lat = 10 AND b.lat = 11";
	Require(RequireScalar(connection, sql, "two independently filtered spatial scans") == 3,
	        "aliases should retain separate coordinate selections and join matching longitudes");
}

} // namespace

int main() {
	try {
		duckdb::DBConfig config;
		config.SetOptionByName("allow_unsigned_extensions", true);
		duckdb::DuckDB database(nullptr, &config);
		duckdb::Connection connection(database);
		const auto core = FindCoreFunctions();
		Require(!core.empty(), "cannot find core_functions extension in the release repository");
		auto core_load = Query(connection, "LOAD '" + core.string() + "'");
		Require(!core_load->HasError(), "cannot load core_functions extension: " + core_load->GetError());
		const auto *extension_override = std::getenv("DUCKOMO_EXTENSION_PATH");
		const auto extension_path = extension_override ? extension_override : "./build/release/extension/duckomo/duckomo.duckdb_extension";
		auto extension_load = Query(connection, "LOAD '" + std::string(extension_path) + "'");
		Require(!extension_load->HasError(), "cannot load duckomo extension: " + extension_load->GetError());
		const auto metrics_file = fs::temp_directory_path() /
		                          ("duckomo-spatial-lifecycle-" +
		                           std::to_string(static_cast<unsigned long long>(getpid())) + ".json");
		TestPreparedRebindingAndAlternatingFailures(connection, metrics_file);
		TestIndependentAliases(connection);
		std::cout << "spatial lifecycle checks passed (prepared rebinding, 100 failures, alias isolation)\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "spatial lifecycle checks failed: " << error.what() << '\n';
		return 1;
	}
}
