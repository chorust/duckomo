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

void TestRegionWorkMemoryExcludesInputAtEachPeak() {
	auto metrics = std::make_shared<ScanMetrics>();
	metrics->EnableQueryMemoryAccounting();
	metrics->SetOperation("read_om_regions");
	{
		ScanMemoryAccount input(metrics, ScanMemoryComponent::RegionInput);
		ScanMemoryAccount work(metrics, ScanMemoryComponent::Decoder);
		input.Set(1000, 2000);
		work.Set(100, 200);
		input.Set(0);
		work.Set(500, 700);
		const auto snapshot = metrics->Snapshot();
		Require(snapshot.peak_query_owned_bytes == 1100 &&
		            snapshot.peak_query_owned_upper_bound_bytes == 2200 &&
		            snapshot.peak_work_owned_bytes == 500 &&
		            snapshot.peak_work_owned_upper_bound_bytes == 700 &&
		            snapshot.owned_input_peak_bytes == 1000,
		        "work peaks track simultaneous scan allocations independently of retained input peaks");
	}
	metrics->FinalizeQueryMemoryAccounting(true);
	const auto json = metrics->ToMetricsV4Json();
	Require(json.find("\"owned_work_peak_bytes\":500,\"owned_work_bound_bytes\":700") != std::string::npos,
	        "region JSON serializes independent work peaks");
	metrics->MarkQueryMemoryAccountingIncomplete();
	Require(!metrics->Snapshot().peak_work_owned_bytes &&
	            metrics->ToMetricsV4Json().find("\"owned_work_peak_bytes\":null,\"owned_work_bound_bytes\":null") !=
	                std::string::npos,
	        "incomplete memory accounting also invalidates work peaks");
}

void TestRegionMetricsAreAdditiveAndExactOnlyAtCompletion() {
	ScanMetrics complete;
	complete.SetOperation("read_om_regions");
	complete.SetRegionInputMetrics(2, 1, 64, 8, 1024);
	complete.RecordRegionCandidatePairs(10);
	complete.RecordRegionEnvelopeRejects(3);
	complete.RecordRegionGeometryBatch(7, 4, 25);
	complete.SetCandidateCountEvidence(10, 10, 10, true);
	complete.SetRegionMatchCount(4, true);
	complete.SetScanComplete(true);
	complete.SetStatus(ScanStatus::Succeeded);
	const auto complete_snapshot = complete.Snapshot();
	const auto complete_json = complete.ToMetricsV4Json();
	Require(complete_snapshot.operation == "read_om_regions" &&
	            complete_snapshot.candidate_pairs_observed == 10 && complete_snapshot.geometry_tested_pairs == 7 &&
	            complete_snapshot.matched_pairs_observed == 4 && complete_snapshot.matched_pairs_exact == 4 &&
	            complete_snapshot.match_count_complete == true,
	        "region metrics preserve pair units and publish exact matches only when complete");
	Require(complete_json.find("\"count_unit\":\"region_record_pair\"") != std::string::npos &&
	            complete_json.find("\"envelope_rejected_pairs\":3") != std::string::npos &&
	            complete_json.find("\"matched_pairs_exact\":4") != std::string::npos &&
	            complete_json.find("\"spatial_internal_peak_bytes\":null,\"spatial_internal_memory_complete\":false") !=
	                std::string::npos,
	        "v4 serializes additive region fields while leaving unobservable spatial memory unknown");
	Require(complete.ToMetricsV3Json().find("read_om_regions") == std::string::npos,
	        "legacy v3 retains its existing schema and does not expose additive region fields");

	ScanMetrics limited;
	limited.SetOperation("read_om_regions");
	limited.RecordRegionCandidatePairs(10);
	limited.RecordRegionGeometryBatch(7, 4, 25);
	limited.SetRegionMatchCount(std::nullopt, false);
	limited.SetScanComplete(false);
	limited.SetStatus(ScanStatus::Succeeded);
	const auto limited_snapshot = limited.Snapshot();
	Require(!limited_snapshot.matched_pairs_exact && limited_snapshot.match_count_complete == false &&
	            limited.ToMetricsV4Json().find("\"matched_pairs_exact\":null") != std::string::npos,
	        "LIMIT-style early stop keeps observed matches but never publishes an exact count");
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
		TestRegionMetricsAreAdditiveAndExactOnlyAtCompletion();
		TestRegionWorkMemoryExcludesInputAtEachPeak();
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
