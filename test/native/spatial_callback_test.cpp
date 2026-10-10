#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo/read_om.hpp"

#include <filesystem>
#include <cstdlib>
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

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

std::string ProjectionRead() {
	return "read_om('test/data/projection.om', dimensions := map(['humidity','pressure','temperature'], "
	       "[['row','column'],['row','column'],['row','column']]))";
}

std::string SpatialRawRead() {
	return "read_om('test/data/raw.om', dimensions := map(['value'], [['lat','lon']]), "
	       "grid := {'nx':3, 'ny':2, 'lat0':10.0, 'lon0':100.0, 'dlat':1.0, 'dlon':2.0, 'order':'separate'}, "
	       "spatial_axes := ['lat','lon'])";
}

std::unique_ptr<duckdb::MaterializedQueryResult> RequireSuccess(duckdb::Connection &connection,
	                                                             const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result != nullptr && !result->HasError(), "query failed: " + (result ? result->GetError() : "no result"));
	return result;
}

void TestCallbackRetainsFilterAndProjection(duckdb::Connection &connection) {
	auto function = duckdb::duckomo::GetReadOmFunction();
	Require(function.projection_pushdown, "projection pushdown remains enabled");
	Require(!function.filter_pushdown && !function.filter_prune, "table filter pushdown and pruning remain disabled");
	Require(static_cast<bool>(function.pushdown_complex_filter), "complex-filter callback is registered");

	const auto source = ProjectionRead();
	auto explain = RequireSuccess(connection, "EXPLAIN SELECT temperature FROM " + source + " WHERE humidity = 96");
	std::string plan;
	for (idx_t row = 0; row < explain->RowCount(); row++) {
		plan += explain->GetValue(1, row).ToString();
		plan.push_back('\n');
	}
	Require(plan.find("FILTER") != std::string::npos && plan.find("humidity = 96.0") != std::string::npos,
	        "DuckDB's physical plan keeps the exact filter above READ_OM");
	Require(plan.find("humidity") != std::string::npos && plan.find("temperature") != std::string::npos,
	        "the scan projection retains the unselected filter dependency");

	auto result = RequireSuccess(connection,
	                             "SELECT temperature FROM " + source + " WHERE 96 <= humidity AND humidity <= 96");
	Require(result->RowCount() == 108, "integer and reversed-comparison residual filters return the exact rows");

	auto coordinate_explain = RequireSuccess(
	    connection, "EXPLAIN SELECT value FROM " + SpatialRawRead() + " WHERE lat >= 11");
	std::string coordinate_plan;
	for (idx_t row = 0; row < coordinate_explain->RowCount(); row++) {
		coordinate_plan += coordinate_explain->GetValue(1, row).ToString();
		coordinate_plan.push_back('\n');
	}
	Require(coordinate_plan.find("FILTER") != std::string::npos &&
	            coordinate_plan.find("lat >= 11.0") != std::string::npos,
	        "DOUBLE coordinate comparison with an integer literal remains as an exact filter");
	auto reverse_coordinate = RequireSuccess(connection,
	                                         "SELECT value FROM " + SpatialRawRead() + " WHERE 11 <= lat");
	Require(reverse_coordinate->RowCount() == 3, "reversed integer-to-coordinate comparison retains residual semantics");
	auto final_filter = RequireSuccess(
	    connection, "SELECT temperature FROM " + source + " WHERE 96 <= humidity AND humidity <= 96");
	Require(final_filter->RowCount() == 108, "final filter-only dependency query preserves its expected rows");
}

void TestMetricsRecordCallback() {
	const auto metrics_path = std::filesystem::temp_directory_path() /
	                          ("duckomo-spatial-callback-" + std::to_string(static_cast<unsigned long long>(getpid())) +
	                           ".json");
	std::filesystem::remove(metrics_path);
	setenv("DUCKOMO_METRICS_OUTPUT", metrics_path.c_str(), 1);
	setenv("DUCKOMO_SCENARIO", "spatial_callback", 1);
	setenv("DUCKOMO_FIXTURE_ID", "projection", 1);
	setenv("DUCKOMO_FIXTURE_SHA256", "fb36284bf8f932c82b171c7bce28b6e767b95321d1d986dc0a1dbd3b84d88a43", 1);

	duckdb::DBConfig config;
	config.SetOptionByName("allow_unsigned_extensions", true);
	duckdb::DuckDB database(nullptr, &config);
	duckdb::Connection connection(database);
	const auto *core_override = std::getenv("DUCKOMO_CORE_FUNCTIONS_EXTENSION");
	std::filesystem::path core_functions_path = core_override ? core_override : "";
	const std::filesystem::path repository("build/release/repository/v1.5.4");
	if (core_functions_path.empty() && std::filesystem::exists(repository)) {
		for (const auto &entry : std::filesystem::recursive_directory_iterator(repository)) {
			if (entry.is_regular_file() && entry.path().filename() == "core_functions.duckdb_extension") {
				core_functions_path = entry.path();
				break;
			}
		}
	}
	Require(!core_functions_path.empty(), "cannot find core_functions extension in release repository");
	RequireSuccess(connection, "LOAD '" + core_functions_path.string() + "'");
	const auto *extension_override = std::getenv("DUCKOMO_EXTENSION_PATH");
	const auto extension_path = extension_override && extension_override[0] != '\0'
	                                ? extension_override
	                                : "./build/release/extension/duckomo/duckomo.duckdb_extension";
	RequireSuccess(connection, "LOAD '" + std::string(extension_path) + "'");
	TestCallbackRetainsFilterAndProjection(connection);

	std::ifstream input(metrics_path, std::ios::binary);
	Require(input.good(), "query did not write its metrics sidecar");
	const std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
	Require(json.find("\"schema_version\":2") != std::string::npos, "callback evidence uses metrics schema v2");
	Require(json.find("\"filter_callback_invoked\":true") != std::string::npos,
	        "metrics report that DuckDB invoked the callback");
	Require(json.find("/humidity") != std::string::npos, "filter-only humidity remains a scan dependency");
	std::filesystem::remove(metrics_path);
}

} // namespace

int main() {
	try {
		TestMetricsRecordCallback();
		std::cout << "spatial complex-filter callback checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "spatial complex-filter callback checks failed: " << error.what() << '\n';
		return 1;
	}
}
