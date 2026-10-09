#include "duckomo/metrics.hpp"

#include <iostream>
#include <memory>
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
		metrics.SetScanComplete(true);
		metrics.SetTransportUnobserved();
			metrics.DeclareVariable("/temperature");
			metrics.RecordLogicalRead(ScanReadPhase::Metadata, 20, "", ScanMetadataStage::Bind);
			metrics.RecordMetadataRead(ScanMetadataStage::Bind, 20);
		metrics.SetSpatialSelection("axis_restricted", true, {}, 12);
		metrics.RecordLogicalRead(ScanReadPhase::Index, 7, "/temperature");
		metrics.RecordSuccessfulRead(ScanReadPhase::Index, 7, "/temperature");
		metrics.RecordLogicalRead(ScanReadPhase::Data, 21, "/temperature");
			metrics.RecordSuccessfulRead(ScanReadPhase::Data, 21, "/temperature");
			metrics.RecordLogicalRead(ScanReadPhase::Data, 5, "/temperature"); // unfulfilled logical request
		metrics.RecordLogicalRead(ScanReadPhase::Coordinate, 16);
		metrics.RecordCoordinateRead(16);
		metrics.SetCacheRemoved();
		metrics.RecordScanTaskCreated();
		metrics.RecordScanTaskClaimed();
		metrics.RecordScanTaskCompleted();
		metrics.RecordWorkerActive(11);
			metrics.BeginWorkerExecution(11);
			metrics.BeginWorkerExecution(12);
			metrics.EndWorkerExecution(12);
		metrics.EndWorkerExecution(11);
		metrics.RecordScannedRows(12);
		metrics.SetStatus(ScanStatus::Succeeded);
		metrics.SetElapsedMilliseconds(1.25);
		const auto json = metrics.ToMetricsV3Json();
		Require(json.find("\"schema_version\":3") != std::string::npos, "SQL profile must use schema version 3");
		Require(json.find("\"scan_id\":3") != std::string::npos, "SQL profile must retain its scan id");
		Require(json.find("\"coordinate\":{\"logical_bytes\":16,\"logical_requests\":1,\"physical_bytes\":16") != std::string::npos,
		        "coordinate costs must be independently visible");
		Require(json.find("\"response_body_bytes\":null") != std::string::npos,
		        "official remote transport must be unknown in v3");
		Require(json.find("\"legacy_v2\":{\"schema_version\":2") != std::string::npos,
		        "v2 evidence field meanings must remain available");
		Require(json.find("do-not-leak") == std::string::npos &&
		            json.find("secret.invalid") == std::string::npos,
		        "SQL profile must not expose remote URI credentials or signature parameters");
			Require(json.find("\"logical_requested_bytes\":69") != std::string::npos &&
			            json.find("\"physical_read_bytes\":64") != std::string::npos,
			        "logical requests remain distinct from successful application reads");
			Require(json.find("\"bind\":{\"logical_bytes\":20,\"logical_requests\":1,\"physical_bytes\":20") !=
			            std::string::npos,
			        "metadata logical and physical totals must be separated by bind stage");
			Require(json.find("\"logical_data_bytes\":26") != std::string::npos &&
			            json.find("\"data_bytes\":21") != std::string::npos &&
			            json.find("\"bytes_fetched\":48") != std::string::npos,
			        "per-variable totals and legacy v2 totals must preserve their prior meanings");
		Require(json.find("\"cache\":{\"enabled\":false,\"capacity_bytes\":0") != std::string::npos &&
		            json.find("\"bypass_reason\":\"removed\"") != std::string::npos,
		        "removed cache retains honest legacy shape");
		Require(json.find("\"transport_attempts\":null") != std::string::npos &&
		            json.find("\"transport_count_complete\":false") != std::string::npos,
		        "transport unknown cannot become known zero");
			Require(json.find("\"active_workers\":2") != std::string::npos &&
			            json.find("\"max_active_workers\":2") != std::string::npos,
			        "worker identities and overlapping activity must be retained");
		Require(json.find("\"scan_complete\":true") != std::string::npos &&
		            json.find("\"result_rows_state\":\"unobserved\"") != std::string::npos &&
		            json.find("\"peak_query_owned_bytes\":null") != std::string::npos,
			        "completion and unobserved product memory/result fields must be explicit");

		ScanMetrics coordinate_metrics;
		coordinate_metrics.EnableQueryMemoryAccounting();
		coordinate_metrics.RecordLogicalRead(ScanReadPhase::CoordinateIndex, 8);
		coordinate_metrics.RecordSuccessfulRead(ScanReadPhase::CoordinateIndex, 8);
		coordinate_metrics.RecordLogicalRead(ScanReadPhase::CoordinateData, 32);
		coordinate_metrics.RecordSuccessfulRead(ScanReadPhase::CoordinateData, 32);
		coordinate_metrics.RecordSuccessfulCoordinateDecode(2);
		const auto coordinate_json = coordinate_metrics.ToMetricsV3Json();
		Require(coordinate_json.find("\"coordinate\":{\"logical_bytes\":40") != std::string::npos &&
		            coordinate_json.find("\"logical_index_bytes\":8") != std::string::npos &&
		            coordinate_json.find("\"data_bytes\":32") != std::string::npos &&
		            coordinate_json.find("\"decoded_chunks\":2") != std::string::npos,
		        "coordinate decoder index/data/decode costs must stay separate from value variables");
		ScanMetrics failed_coordinate_metrics;
		failed_coordinate_metrics.RecordSuccessfulCoordinateDecode(1);
		failed_coordinate_metrics.MarkCoordinateDecodeCountIncomplete();
		const auto failed_coordinate_snapshot = failed_coordinate_metrics.Snapshot();
		Require(!failed_coordinate_snapshot.decode_count_complete &&
		            !failed_coordinate_snapshot.coordinate.decode_count_complete,
		        "a failed coordinate decode must mark both coordinate and aggregate decode counts incomplete");

		auto memory_metrics = std::make_shared<ScanMetrics>();
		memory_metrics->EnableQueryMemoryAccounting();
		auto bind_memory = std::make_shared<ScanMemoryAccount>(memory_metrics);
		auto scan_memory = std::make_shared<ScanMemoryAccount>(memory_metrics);
		bind_memory->Set(128);
		scan_memory->Set(64);
		scan_memory->Set(192);
		bind_memory->Set(32);
		memory_metrics->FinalizeQueryMemoryAccounting(true);
		const auto accounted_json = memory_metrics->ToMetricsV3Json();
		Require(accounted_json.find("\"peak_query_owned_bytes\":320") != std::string::npos &&
		            accounted_json.find("\"query_memory_count_complete\":true") != std::string::npos,
		        "concurrent bind/scan memory accounts must publish their aggregate high-water mark");

		ScanMetrics limited_metrics;
		limited_metrics.SetStatus(ScanStatus::Succeeded);
		limited_metrics.SetScanComplete(false);
		const auto limited_json = limited_metrics.ToMetricsV3Json();
		Require(limited_json.find("\"status\":\"success\"") != std::string::npos &&
		            limited_json.find("\"scan_complete\":false") != std::string::npos,
		        "successful LIMIT completion must preserve an incomplete physical scan state");

		ScanMetrics local_metrics;
		local_metrics.SetQueryIdentity("local", "", "SELECT 1");
		local_metrics.SetTransportNotApplicable();
		const auto local_json = local_metrics.ToMetricsV3Json();
		Require(local_json.find("\"response_body_bytes\":0") != std::string::npos &&
		            local_json.find("\"transport_count_complete\":true") != std::string::npos,
		        "local transport body costs must be known zero rather than unknown");

		ScanMetrics failed_metrics;
		failed_metrics.EnableQueryMemoryAccounting();
		auto failed_memory = std::make_shared<ScanMemoryAccount>(
		    std::shared_ptr<ScanMetrics>(&failed_metrics, [](ScanMetrics *) {}));
		failed_memory->Set(128);
		failed_metrics.SetTransportUnobserved();
		failed_metrics.RecordLogicalRead(ScanReadPhase::Data, 12, "/temperature");
		failed_metrics.MarkDecodeCountIncomplete("/temperature");
		failed_metrics.SetStatus(ScanStatus::Failed, "transport_error");
		failed_metrics.FinalizeQueryMemoryAccounting(false);
		const auto failed_json = failed_metrics.ToMetricsV3Json();
			Require(failed_json.find("\"transport_count_complete\":false") != std::string::npos &&
			            failed_json.find("\"response_body_bytes\":null") != std::string::npos &&
			            failed_json.find("\"logical_requested_bytes\":12") != std::string::npos &&
			            failed_json.find("\"physical_read_bytes\":0") != std::string::npos &&
			            failed_json.find("\"decode_count_complete\":false") != std::string::npos,
				        "failed remote profiles retain logical application costs and unknown transport");
		Require(failed_json.find("\"peak_query_owned_bytes\":null") != std::string::npos &&
		            failed_json.find("\"query_memory_count_complete\":false") != std::string::npos,
		        "failed queries must keep memory unknown when the final peak cannot be proven");

		std::cout << "scan_metrics_v3_test: v3 fields, v2 compatibility, unknown values, and URI redaction passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "scan_metrics_v3_test: " << error.what() << '\n';
		return 1;
	}
}
