#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo_extension.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &database) {
	database.LoadStaticExtension<DuckomoExtension>();
}
} // namespace duckdb

namespace {
using namespace duckdb;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

std::string Scalar(Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result && !result->HasError(), sql + ": " + (result ? result->GetError() : "no result"));
	Require(result->RowCount() == 1 && result->ColumnCount() == 1, "expected one scalar result: " + sql);
	return result->GetValue(0, 0).ToString();
}
} // namespace

int main() {
	try {
		DuckDB database(nullptr);
		Connection first(database);
		Connection second(database);
		Require(Scalar(first, "SELECT value FROM duckdb_settings() WHERE name='duckomo_cache_enabled'") == "true",
		        "first connection should use its default session cache setting");
		Require(Scalar(second, "SELECT count(*) FROM duckomo_last_scan_metrics()") == "0",
		        "a new connection must start without scan metrics");
		Require(!second.Query("PREPARE latest_metrics AS SELECT count(*) FROM duckomo_last_scan_metrics()")->HasError(),
		        "last scan metrics query should prepare before a scan");
		Require(!second.Query("PREPARE latest_metrics_id AS SELECT query_id FROM duckomo_last_scan_metrics()")->HasError(),
		        "query ID should prepare before a scan");
		Require(Scalar(second, "EXECUTE latest_metrics") == "0", "prepared metrics should initially be empty");
		Require(!first.Query("SET duckomo_cache_enabled=false")->HasError(), "session cache setting should be accepted");
		Require(Scalar(second, "SELECT value FROM duckdb_settings() WHERE name='duckomo_cache_enabled'") == "true",
		        "changing one connection's cache setting must not affect another connection");

		Require(Scalar(first, "SELECT count(*) FROM read_om('test/data/raw.om')") == "6",
		        "first connection scan should succeed");
		Require(Scalar(first, "SELECT count(*) FROM duckomo_last_scan_metrics()") == "1",
		        "first connection should publish its scan metrics");
		Require(Scalar(second, "SELECT count(*) FROM duckomo_last_scan_metrics()") == "0",
		        "first connection metrics must not leak to the second connection");

		Require(Scalar(second, "SELECT count(*) FROM read_om('test/data/raw.om')") == "6",
		        "second connection scan should succeed");
		Require(Scalar(second, "EXECUTE latest_metrics") == "1",
		        "prepared metrics should read session history at execution time");
		const auto first_query_id = Scalar(second, "EXECUTE latest_metrics_id");
		Require(Scalar(second, "SELECT count(*) FROM read_om('test/data/raw.om')") == "6",
		        "subsequent scan should succeed");
		Require(Scalar(second, "EXECUTE latest_metrics_id") != first_query_id,
		        "prepared metrics should see the latest scan query ID");
		Require(Scalar(first, "SELECT count(*) FROM duckomo_last_scan_metrics()") == "1",
		        "second connection scan must not replace first connection metrics");
		Require(Scalar(second, "SELECT count(*) FROM duckomo_last_scan_metrics()") == "1",
		        "second connection should publish its own scan metrics");
		Require(Scalar(first, "SELECT count(*) FROM duckomo_clear_cache()") == "1",
		        "cache clear should return one row without overwriting scan history");
		Require(Scalar(first, "SELECT count(*) FROM duckomo_last_scan_metrics()") == "1",
		        "cache clear must not clear the last scan profile");
		auto remote = first.Query(
		    "SELECT * FROM read_om('https://example.invalid/file.om?X-Amz-Signature=secret-signature')");
		Require(remote && remote->HasError(), "remote reads must stay unavailable without the paired range provider");
		Require(remote->GetError().find("paired httpfs range-session extension") != std::string::npos,
		        "unsupported remote input should explain the missing capability");
		Require(remote->GetError().find("secret-signature") == std::string::npos,
		        "remote path errors must not expose signed URL query parameters");

		std::cout << "session_metrics_test: settings and last-scan state are connection-local\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "session_metrics_test: " << error.what() << '\n';
		return 1;
	}
}
