#include "duckomo/metrics.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace duckdb::duckomo;

namespace {
void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}
} // namespace

int main() {
	try {
		ScanMetrics metrics;
		metrics.SetQueryIdentity("connection-7-query-2", "v3-test", "SELECT * FROM read_om('https://secret.invalid/object?sig=do-not-leak')");
		metrics.SetScanId(3);
		metrics.DeclareVariable("/temperature");
		metrics.SetSpatialSelection("axis_restricted", true, {}, 12);
		metrics.RecordSuccessfulRead(ScanReadPhase::Index, 7, "/temperature");
		metrics.RecordSuccessfulRead(ScanReadPhase::Data, 21, "/temperature");
		metrics.RecordCoordinateRead(16);
		metrics.RecordCacheLookup(true, 21);
		metrics.RecordCacheLookup(false);
		metrics.RecordScanTaskClaimed();
		metrics.RecordWorkerActive(11);
		metrics.RecordScannedRows(12);
		metrics.RecordTransportResponse(28);
		metrics.SetStatus(ScanStatus::Succeeded);
		metrics.SetElapsedMilliseconds(1.25);
		const auto json = metrics.ToMetricsV3Json();
		Require(json.find("\"schema_version\":3") != std::string::npos, "SQL profile must use schema version 3");
		Require(json.find("\"scan_id\":3") != std::string::npos, "SQL profile must retain its scan id");
		Require(json.find("\"coordinate_bytes\":16") != std::string::npos,
		        "coordinate costs must be independently visible");
		Require(json.find("\"response_body_bytes\":28") != std::string::npos,
		        "transport response bytes must be actual observer input");
		Require(json.find("\"legacy_v2\":{\"schema_version\":2") != std::string::npos,
		        "v2 evidence field meanings must remain available");
		Require(json.find("do-not-leak") == std::string::npos &&
		            json.find("secret.invalid") == std::string::npos,
		        "SQL profile must not expose remote URI credentials or signature parameters");
		Require(json.find("\"cache\":{\"hits\":1,\"misses\":1,\"hit_bytes\":21}") != std::string::npos,
		        "cache counters must preserve hit/miss and byte totals");

		ScanMetrics local_metrics;
		local_metrics.SetQueryIdentity("local", "", "SELECT 1");
		const auto local_json = local_metrics.ToMetricsV3Json();
		Require(local_json.find("\"response_body_bytes\":null") != std::string::npos,
		        "unknown transport body costs must remain null for local scans");

		std::cout << "scan_metrics_v3_test: v3 fields, v2 compatibility, unknown values, and URI redaction passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "scan_metrics_v3_test: " << error.what() << '\n';
		return 1;
	}
}
