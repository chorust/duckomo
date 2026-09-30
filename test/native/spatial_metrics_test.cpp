#include "duckomo/metrics.hpp"
#include "../tools/validation_support.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {
using namespace duckdb::duckomo;

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <class ACTION>
void RequireOverflow(ACTION action, const std::string &message) {
	try {
		action();
	} catch (const std::overflow_error &) {
		return;
	}
	throw std::runtime_error(message + ": expected overflow_error");
}

void TestVersionTwoSerializationAndAccounting() {
	ScanMetrics metrics;
	metrics.SetQueryIdentity("q1", "restricted", "SELECT * FROM spatial");
	metrics.SetFixture("spatial_flat", "fixture-hash");
	metrics.RecordMetadataRead(ScanMetadataStage::Bind, 17);
	metrics.RecordMetadataRead(ScanMetadataStage::Scan, 23);
	metrics.SetSpatialContext("nx=3,ny=2", "lon_fastest", "explicit");
	metrics.MarkFilterCallbackInvoked();
	metrics.SetSpatialSelection("restricted", true, {"unsupported OR branch"}, 2);
	metrics.SetSpatialReference("reference-hash", 1e-9, true, true);
	metrics.RecordSuccessfulRead(ScanReadPhase::Index, 5, "/value");
	metrics.RecordSuccessfulRead(ScanReadPhase::Data, 11, "/value");
	metrics.RecordSuccessfulDecode("/value", 3);
	metrics.MarkDecodeCountIncomplete("/other");
	metrics.RecordScanTaskClaimed();
	metrics.RecordScanTaskClaimed();
	metrics.RecordWorkerActive(7);
	metrics.RecordWorkerActive(7);
	metrics.RecordWorkerActive(8);
	metrics.SetStatus(ScanStatus::Succeeded);

	const auto snapshot = metrics.Snapshot();
	Require(snapshot.schema_version == 2, "schema version is 2");
	Require(snapshot.bind_metadata_bytes == 17 && snapshot.scan_metadata_bytes == 23 && snapshot.metadata_bytes == 40,
	        "bind and scan metadata are separately accounted and summed");
	Require(snapshot.metadata_requests == 2 && snapshot.bytes_fetched == 56 && snapshot.read_requests == 4,
	        "actual positional reads reconcile with total bytes and requests");
	Require(snapshot.variables.at("/value").decoded_chunks == 3 && snapshot.variables.at("/value").decode_count_complete,
	        "successful decoder work remains attributed by variable");
	Require(!snapshot.variables.at("/other").decode_count_complete && !snapshot.decode_count_complete,
	        "failed decoder accounting cannot appear complete");
	Require(snapshot.scan_tasks_claimed == 2 && snapshot.active_workers == 2,
	        "task and distinct active worker evidence is counted without duplicate worker attribution");
	const auto json = metrics.ToEvidenceJson();
	for (const auto *field : {"schema_version", "bind_metadata_bytes", "scan_metadata_bytes", "grid_definition",
	                          "spatial_layout", "grid_source", "selection_mode", "filter_callback_invoked",
	                          "residual_filter_retained", "fallback_reasons", "candidate_rows", "reference_identity",
	                          "logical_positions_match", "null_positions_match", "scan_tasks_claimed", "active_workers"}) {
		Require(json.find(std::string("\"") + field + "\":") != std::string::npos,
		        std::string("serialized evidence is missing ") + field);
	}
}

void TestCounterOverflowIsRejected() {
	ScanMetrics metrics;
	metrics.RecordMetadataRead(ScanMetadataStage::Bind, std::numeric_limits<std::uint64_t>::max());
	RequireOverflow([&] { metrics.RecordMetadataRead(ScanMetadataStage::Bind, 1); },
	                "metadata counter addition is checked");

	ScanMetrics totals;
	totals.RecordMetadataRead(ScanMetadataStage::Bind, std::numeric_limits<std::uint64_t>::max());
	totals.RecordMetadataRead(ScanMetadataStage::Scan, 1);
	RequireOverflow([&] { (void)totals.Snapshot(); }, "metadata total overflow is checked");

	ScanMetrics variable;
	variable.RecordSuccessfulRead(ScanReadPhase::Data, std::numeric_limits<std::uint64_t>::max(), "/value");
	RequireOverflow([&] { variable.RecordSuccessfulRead(ScanReadPhase::Data, 1, "/value"); },
	                "variable read counter addition is checked");
}

void WriteEvidence(const std::filesystem::path &path, const std::string &content) {
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	Require(output.good(), "cannot create test evidence sidecar");
	output << content;
	Require(output.good(), "cannot write test evidence sidecar");
}

void TestEvidenceCompletenessAndFailureRejection() {
	using duckomo_validation_support::SpatialEvidenceExpectation;
	using duckomo_validation_support::ValidateSpatialMetricsEvidence;
	const auto temporary = std::filesystem::temp_directory_path() /
	                       ("duckomo-spatial-metrics-" + std::to_string(static_cast<unsigned long long>(getpid())));
	std::filesystem::create_directories(temporary);
	const auto sidecar = temporary / "evidence.json";
	const auto fixture_id = std::string("fixture");
	const auto fixture_hash = std::string(64, 'a');
	const auto evidence = [&](const std::string &selection, bool optimizer_empty, const std::string &fallback,
	                          const std::string &variables, std::uint64_t candidate_rows = 1,
	                          std::uint64_t bind_metadata_bytes = 1) {
		return std::string("{\"schema_version\":2,\"fixture_id\":\"fixture\",\"fixture_sha256\":\"") +
		       fixture_hash + "\",\"sql\":\"SELECT 1\",\"comparison\":\"reference match\","
		       "\"dependency_commits\":{\"duckdb\":\"08e34c447bae34eaee3723cac61f2878b6bdf787\","
	       "\"om-file-format\":\"d8855e418e2231ae8439f0c7e840fa3f93b371e3\","
	       "\"extension-ci-tools\":\"b777c70d30942cca5bef62d6d4fa23a13362f398\"},"
	       "\"status\":\"success\",\"comparison_passed\":true,\"result_rows\":1,"
		       "\"elapsed_ms\":1,\"peak_rss_bytes\":4096,\"reference_identity\":\"reference#hash\","
		       "\"coordinate_tolerance\":1e-9,\"command\":[\"duckdb\"],\"error_category\":\"\","
		       "\"child_exit_code\":0,\"bind_metadata_bytes\":" + std::to_string(bind_metadata_bytes) +
		       ",\"bind_metadata_requests\":1,\"scan_metadata_bytes\":1,\"scan_metadata_requests\":0,"
		       "\"metadata_bytes\":2,\"metadata_requests\":1,\"grid_definition\":\"nx=2,ny=2\","
		       "\"spatial_layout\":\"separate:lat,lon\",\"grid_source\":\"explicit\","
	       "\"residual_filter_retained\":true,\"fallback_reasons\":" + fallback +
	       ",\"candidate_rows\":" + std::to_string(candidate_rows) + ",\"optimizer_empty\":" +
	       (optimizer_empty ? "true" : "false") + ",\"selection_mode\":\"" + selection +
	       "\",\"variables\":" + variables +
	       ",\"decode_count_complete\":true,\"bytes_fetched\":" +
	       (selection == "empty" ? "2" : "6") + ",\"read_requests\":" +
	       (selection == "empty" ? "1" : "2") + ","
	       "\"environment\":{\"build\":\"release\",\"threads\":\"1\",\"system\":\"Linux\","
	       "\"machine\":\"aarch64\"},\"cache_policy\":{\"application_cache\":\"disabled\","
	       "\"os_page_cache\":\"not cleared\"}}";
	};
	const auto variables_with_read = std::string(
	    "{\"/temperature\":{\"index_bytes\":0,\"index_requests\":0,\"data_bytes\":4,\"data_requests\":1,"
	    "\"decoded_chunks\":1,\"decode_count_complete\":true},\"/humidity\":{\"index_bytes\":0,"
	    "\"index_requests\":0,\"data_bytes\":0,\"data_requests\":0,\"decoded_chunks\":0,"
	    "\"decode_count_complete\":true}}");
	const auto variables_zero = std::string(
	    "{\"/temperature\":{\"index_bytes\":0,\"index_requests\":0,\"data_bytes\":0,\"data_requests\":0,"
	    "\"decoded_chunks\":0,\"decode_count_complete\":true},\"/humidity\":{\"index_bytes\":0,"
	    "\"index_requests\":0,\"data_bytes\":0,\"data_requests\":0,\"decoded_chunks\":0,"
	    "\"decode_count_complete\":true}}");
	auto expect = [&](const std::string &mode, bool zero = false) {
		SpatialEvidenceExpectation expectation;
		expectation.fixture_id = fixture_id;
		expectation.fixture_sha256 = fixture_hash;
		expectation.selection_mode = mode;
		expectation.require_zero_value_reads = zero;
		return expectation;
	};
	const auto run = [&](const std::string &content, const SpatialEvidenceExpectation &expectation) {
		WriteEvidence(sidecar, content);
		return ValidateSpatialMetricsEvidence(sidecar, expectation, temporary);
	};
	const auto replace = [](std::string content, const std::string &from, const std::string &to) {
		const auto position = content.find(from);
		Require(position != std::string::npos, "test evidence replacement target is missing: " + from);
		content.replace(position, from.size(), to);
		return content;
	};

	Require(run(evidence("restricted", false, "[]", variables_with_read), expect("restricted")),
	        "complete restricted evidence with reconciled counters is accepted");
	Require(run(evidence("empty", false, "[]", variables_zero, 0), expect("empty", true)),
	        "complete empty evidence with explicit zero value counters is accepted");
	Require(!run(replace(evidence("restricted", false, "[]", variables_with_read),
	                    "\"bind_metadata_bytes\":1,", ""),
	                expect("restricted")),
	        "missing v2 metadata fields are rejected");
	Require(!run(evidence("restricted", false, "[]", variables_with_read),
	             SpatialEvidenceExpectation{fixture_id, std::string(64, 'b'), "restricted", false, false, false}),
	        "wrong fixture identity hash is rejected");
	Require(!run(replace(evidence("restricted", false, "[]", variables_with_read),
	                    "\"status\":\"success\"", "\"status\":\"failed\""),
	                expect("restricted")),
	        "failed query evidence is rejected");
	Require(!run(replace(evidence("restricted", false, "[]", variables_with_read),
	                    "\"decode_count_complete\":true,\"bytes_fetched\"",
	                    "\"decode_count_complete\":false,\"bytes_fetched\""),
	                expect("restricted")),
	        "incomplete decode accounting is rejected");
	std::filesystem::remove(sidecar);
	Require(!ValidateSpatialMetricsEvidence(sidecar, expect("restricted"), temporary), "missing sidecar is rejected");
	Require(!run(evidence("fallback", false, "[\"unsupported_or_expression\"]", variables_with_read),
	             expect("restricted")),
	        "fallback evidence cannot satisfy a restricted expectation");
	Require(!run(evidence("restricted", true, "[]", variables_zero, 0), expect("restricted")),
	        "optimizer-eliminated scan cannot masquerade as a restricted scan");
	Require(!run(evidence("fallback", false, "[]", variables_with_read), expect("fallback")),
	        "fallback without a diagnostic reason is rejected");

	SpatialEvidenceExpectation optimizer = expect("optimizer_empty", true);
	optimizer.optimizer_plan_is_empty_result = true;
	optimizer.query_result_succeeded = true;
	Require(run(evidence("empty", true, "[]", variables_zero, 0), optimizer),
	        "optimizer-empty requires and accepts explicit plan, bind, and successful-result evidence");
	optimizer.optimizer_plan_is_empty_result = false;
	Require(!run(evidence("empty", true, "[]", variables_zero, 0), optimizer),
	        "optimizer-empty evidence without an EMPTY_RESULT plan is rejected");
	optimizer.optimizer_plan_is_empty_result = true;
	optimizer.query_result_succeeded = false;
	Require(!run(evidence("empty", true, "[]", variables_zero, 0), optimizer),
	        "optimizer-empty evidence without a successful result is rejected");
	optimizer.query_result_succeeded = true;
	Require(!run(evidence("empty", true, "[]", variables_zero, 0, 0), optimizer),
	        "optimizer-empty evidence without bind metadata is rejected");
	std::filesystem::remove_all(temporary);
}

} // namespace

int main() {
	try {
		TestVersionTwoSerializationAndAccounting();
		TestCounterOverflowIsRejected();
		TestEvidenceCompletenessAndFailureRejection();
		std::cout << "spatial metrics checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "spatial metrics checks failed: " << error.what() << '\n';
		return 1;
	}
}
