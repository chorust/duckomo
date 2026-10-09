#include "duckomo/metrics.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace duckdb::duckomo;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

void TestV4SeparatesExactObservedUpperAndCompleteness() {
	auto metrics = std::make_shared<ScanMetrics>();
	metrics->SetQueryIdentity("query-v4", "unit", "SELECT * FROM read_om('https://example.invalid/private')");
	metrics->SetOperation("grid_info");
	metrics->SetCandidateCountEvidence(std::nullopt, 20, 5, std::nullopt);
	metrics->RecordObservedCandidateRecords(3);
	metrics->RecordSelectionWindow(3, false);
	metrics->RecordSelectionWindow(1, true);
	metrics->RecordCoordinatePreparation(100, false);
	metrics->RecordCoordinatePreparation(4, true);
	metrics->DeclareVariable("/temperature");
	metrics->RecordSuccessfulRead(ScanReadPhase::Index, 11, "/temperature");
	metrics->RecordSuccessfulRead(ScanReadPhase::Data, 23, "/temperature");
	metrics->RecordSuccessfulDecode("/temperature", 2);
	metrics->SetTransportNotApplicable();
	metrics->EnableQueryMemoryAccounting();
	{
		ScanMemoryAccount account(metrics);
		account.Set(128);
	}
	metrics->FinalizeQueryMemoryAccounting(true);
	metrics->SetStatus(ScanStatus::Succeeded);
	metrics->SetScanComplete(true);
	metrics->PublishQueryTerminal();

	const auto snapshot = metrics->Snapshot();
	Require(snapshot.operation == "grid_info", "operation distinguishes metadata-description scans");
	Require(!snapshot.exact_candidate_records && snapshot.candidate_upper_bound_records == 20 &&
	            snapshot.observed_candidate_records == 8 && snapshot.candidate_count_complete == false,
	        "exact, observed, and upper-bound candidate counts have separate nullable fields");
	Require(snapshot.coordinate_preparation_evaluations == 104 &&
	            snapshot.coordinate_preparation_complete == false,
	        "coordinate preparation reports accumulated work and incomplete terminal status");
	Require(snapshot.selection_windows == 2 && snapshot.selection_ranges == 4 &&
	            snapshot.selection_fallback_windows == 1,
	        "v4 selection metrics count windows, ranges, and whole-window budget fallbacks");
	Require(snapshot.value_decode_complete == true && !snapshot.coordinate_decode_complete,
	        "value and coordinate decoder completeness remain independent and unknown stays NULL");
	Require(snapshot.memory_accounting_complete == true && snapshot.query_owned_released_at_terminal == true &&
	            snapshot.query_terminal_published,
	        "owned memory completeness, terminal account release, and publication are visible");

	const auto v2 = metrics->ToEvidenceJson();
	const auto v3 = metrics->ToMetricsV3Json();
	const auto v4 = metrics->ToMetricsV4Json();
	Require(v2.find("\"schema_version\":2") != std::string::npos,
	        "legacy evidence JSON remains schema version 2");
	Require(v3.find("\"schema_version\":3") != std::string::npos &&
	            v3.find("\"legacy_v2\":") != std::string::npos,
	        "legacy metrics profile remains complete and embeds the v2 snapshot");
	Require(v4.find("\"schema_version\":4") != std::string::npos &&
	            v4.find("\"legacy_v3\":{\"schema_version\":3") != std::string::npos &&
	            v4.find("\"legacy_v2\":{\"schema_version\":2") != std::string::npos,
	        "v4 profile embeds independent v3 and v2 compatibility snapshots");
	Require(v4.find("\"exact_candidate_records\":null") != std::string::npos &&
	            v4.find("\"candidate_upper_bound_records\":20") != std::string::npos &&
	            v4.find("\"windows\":2,\"ranges\":4,\"fallback_windows\":1,\"coordinate_evaluations\":104") !=
	                std::string::npos &&
	            v4.find("\"coordinate_preparation\":{\"evaluations\":104,\"complete\":false}") != std::string::npos,
	        "v4 JSON preserves nullable exact values, selector work counts, and coordinate preparation evidence");
	Require(v4.find("https://example.invalid/private") == std::string::npos,
	        "compatibility snapshots continue to redact remote source URIs");
}

void TestInvalidCandidateEvidenceIsRejected() {
	ScanMetrics metrics;
	bool rejected = false;
	try {
		metrics.SetCandidateCountEvidence(11, 10, std::nullopt, true);
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	Require(rejected, "exact candidate count cannot exceed its upper bound");
	rejected = false;
	try {
		metrics.SetCandidateCountEvidence(std::nullopt, 10, 11, false);
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	Require(rejected, "observed candidates cannot exceed their upper bound");
	rejected = false;
	try {
		metrics.SetCandidateCountEvidence(std::nullopt, 10, std::nullopt, true);
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	Require(rejected, "a complete count requires an exact value");
}

void TestConstructedRemoteUrisRedactEveryCompatibilitySnapshot() {
	for (const auto &sql : {
	         "SELECT * FROM read_om(concat('http', '://host/file.om?X-Amz-Signature=secret-token'))",
	         "SELECT * FROM read_om(concat('s3', '://bucket/file.om?token=secret-token'))",
	         "SELECT '\"secret-token\\\\'"}) {
		ScanMetrics metrics;
		metrics.SetQueryIdentity("redaction-v4", "unit", sql);
		const auto v3 = metrics.ToMetricsV3Json();
		const auto v4 = metrics.ToMetricsV4Json();
		Require(v3.find("secret-token") == std::string::npos && v4.find("secret-token") == std::string::npos,
		        "constructed URIs and escaped SQL literals are redacted in every metrics snapshot");
		const std::string redacted_sql = "\"sql\":\"<redacted>\"";
		const auto first = v4.find(redacted_sql);
		Require(first != std::string::npos && v4.find(redacted_sql, first + redacted_sql.size()) != std::string::npos,
		        "both the independent v2 snapshot and the v2 snapshot nested in v3 redact SQL");
	}
}

void TestNonSuccessfulOrEarlyStoppedScansDoNotPublishExactCounts() {
	ScanMetrics failed;
	failed.SetCandidateCountEvidence(5, 10, 5, true);
	failed.SetScanComplete(true);
	failed.SetStatus(ScanStatus::Failed, "downstream_error");
	const auto failed_snapshot = failed.Snapshot();
	Require(!failed_snapshot.exact_candidate_records && failed_snapshot.candidate_count_complete == false,
	        "a downstream query error clears exact candidate evidence even after source exhaustion");
	Require(failed_snapshot.error_category == "downstream_error" &&
	            failed.ToMetricsV4Json().find("\"status\":\"failure\"") != std::string::npos,
	        "a downstream query error remains visible in the terminal v4 outcome");

	ScanMetrics early_stopped;
	early_stopped.SetCandidateCountEvidence(5, 10, 5, true);
	early_stopped.SetScanComplete(false);
	early_stopped.SetStatus(ScanStatus::Succeeded);
	const auto early_snapshot = early_stopped.Snapshot();
	Require(!early_snapshot.exact_candidate_records && early_snapshot.candidate_count_complete == false,
	        "a successful but early-stopped scan does not retain an exact count");
	Require(early_stopped.ToMetricsV4Json().find("\"status\":\"success\",\"scan_complete\":false") !=
	            std::string::npos,
	        "LIMIT-style early stop is distinguishable from a fully consumed successful scan");

	ScanMetrics cancelled;
	cancelled.SetCandidateCountEvidence(5, 10, 2, false);
	cancelled.SetScanComplete(false);
	cancelled.SetStatus(ScanStatus::Cancelled, "query_cancelled");
	cancelled.PublishQueryTerminal();
	const auto cancelled_json = cancelled.ToMetricsV4Json();
	Require(cancelled_json.find("\"status\":\"cancelled\"") != std::string::npos &&
	            cancelled_json.find("\"exact_candidate_records\":null") != std::string::npos &&
	            cancelled_json.find("\"terminal_published\":true") != std::string::npos,
	        "cancellation publishes a terminal outcome without exact candidate evidence");
}

void TestTerminalMemoryReleaseReportsOutstandingAccounts() {
	auto metrics = std::make_shared<ScanMetrics>();
	metrics->EnableQueryMemoryAccounting();
	ScanMemoryAccount account(metrics, ScanMemoryComponent::TransportControl);
	account.Set(64);
	metrics->FinalizeQueryMemoryAccounting(true);
	const auto snapshot = metrics->Snapshot();
	Require(snapshot.query_owned_released_at_terminal == false,
	        "an account still live at QueryEnd must be reported as retained");
	const auto json = metrics->ToMetricsV4Json();
	Require(json.find("\"query_owned_released_at_terminal\":false") != std::string::npos,
	        "v4 memory evidence must serialize the terminal live-set state");
}

void TestUnobservedEvidenceStaysNullAndLegacyV3IsEmbeddedVerbatim() {
	ScanMetrics metrics;
	metrics.SetQueryIdentity("unknown-v4", "unit",
	                         "SELECT * FROM read_om('https://example.invalid/private.om?X-Amz-Signature=secret-token')");
	metrics.SetScanComplete(true);
	metrics.SetStatus(ScanStatus::Succeeded);
	metrics.PublishQueryTerminal();
	const auto legacy_v3 = metrics.ToMetricsV3Json();
	const auto v4 = metrics.ToMetricsV4Json();
	Require(v4.find("\"legacy_v3\":" + legacy_v3) != std::string::npos,
	        "the complete legacy_v3 snapshot remains embedded byte-for-byte in v4");
	Require(v4.find("\"exact_candidate_records\":null") != std::string::npos &&
	            v4.find("\"observed_candidate_records\":null") != std::string::npos &&
	            v4.find("\"candidate_upper_bound_records\":null") != std::string::npos &&
	            v4.find("\"count_complete\":null") != std::string::npos,
	        "unobserved candidate counts remain NULL instead of becoming zero or complete");
	Require(v4.find("\"coordinate_preparation\":{\"evaluations\":null,\"complete\":null}") !=
	            std::string::npos &&
	            v4.find("\"query_owned_complete\":null") != std::string::npos &&
	            v4.find("\"query_owned_released_at_terminal\":null") != std::string::npos &&
	            v4.find("\"complete\":null}") != std::string::npos,
	        "unknown preparation, owned-memory, and transport completeness remain NULL");
	Require(v4.find("secret-token") == std::string::npos,
	        "signed source credentials stay redacted in the embedded legacy and v4 snapshots");
}
} // namespace

int main() {
	try {
		TestV4SeparatesExactObservedUpperAndCompleteness();
		TestInvalidCandidateEvidenceIsRejected();
		TestConstructedRemoteUrisRedactEveryCompatibilitySnapshot();
		TestNonSuccessfulOrEarlyStoppedScansDoNotPublishExactCounts();
		TestTerminalMemoryReleaseReportsOutstandingAccounts();
		TestUnobservedEvidenceStaysNullAndLegacyV3IsEmbeddedVerbatim();
		std::cout << "scan metrics v4 checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "scan metrics v4 checks failed: " << error.what() << '\n';
		return 1;
	}
}
