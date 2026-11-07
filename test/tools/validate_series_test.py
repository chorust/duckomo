#!/usr/bin/env python3
"""Unit checks for the read_om_series evidence runner."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/validate-series.py"
SPEC = importlib.util.spec_from_file_location("validate_series", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
validate_series = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(validate_series)


class ValidateSeriesTests(unittest.TestCase):
    def test_schema_and_full_multiset_comparison_preserves_duplicates_and_nulls(self) -> None:
        row = {"value": None, "lat": 10.0, "lon": 100.0, "valid_time": "1970-01-01 04:00:00"}
        payload = {
            "actual_schema": validate_series.EXPECTED_SCHEMA,
            "expected_schema": validate_series.EXPECTED_SCHEMA,
            "actual_rows": [row, row],
            "expected_rows": [row, row],
            "actual_only_rows": [],
            "expected_only_rows": [],
            "actual_count": 2,
            "expected_count": 2,
            "actual_null_values": 2,
            "expected_null_values": 2,
            "engine_version": "v1.5.5 test",
        }
        result = validate_series.compare_payload(payload)
        self.assertEqual(result["status"], "passed")
        self.assertTrue(result["schema_equal"])
        self.assertTrue(result["python_multiset_equal"])
        self.assertEqual(result["actual_count"], 2)

    def test_cli_parser_accepts_duckdb_json_object_and_string_encodings(self) -> None:
        payload = {"scenario": "x", "actual_count": 0}
        self.assertEqual(
            validate_series.parse_cli_result(json.dumps([{"validation_result": payload}])), payload
        )
        self.assertEqual(
            validate_series.parse_cli_result(json.dumps([{"validation_result": json.dumps(payload)}])), payload
        )

    def test_remote_cli_parser_skips_cli_init_status_output(self) -> None:
        payload = {"actual_count": 3}
        stdout = "Success\nboolean\ntrue\n" + json.dumps([{"validation_result": payload}])
        self.assertEqual(validate_series.parse_cli_result_with_preface(stdout), payload)

    def test_extension_load_requires_the_exact_requested_file_to_be_opened(self) -> None:
        with tempfile.TemporaryDirectory(prefix="series-extension-load-test-") as temporary:
            extension = Path(temporary) / "duckomo.duckdb_extension"
            loaded = validate_series.summarize_extension_load(
                f'openat(AT_FDCWD, "{extension.resolve()}", O_RDONLY)', extension, 0)
            self.assertEqual(loaded["status"], "passed")
            self.assertTrue(loaded["opened_exact_extension"])
            static_copy = validate_series.summarize_extension_load(
                'openat(AT_FDCWD, "duckdb", O_RDONLY)', extension, 0)
            self.assertEqual(static_copy["status"], "blocked")
            self.assertFalse(static_copy["opened_exact_extension"])
            failed = validate_series.summarize_extension_load("", extension, 1)
            self.assertEqual(failed["status"], "failed")

    def test_equal_counts_do_not_hide_a_multiset_difference(self) -> None:
        first = {"value": 1.0, "lat": 10.0, "lon": 100.0, "valid_time": "1970-01-01 00:00:00"}
        second = {"value": 2.0, "lat": 10.0, "lon": 100.0, "valid_time": "1970-01-01 00:00:00"}
        payload = {
            "actual_schema": validate_series.EXPECTED_SCHEMA,
            "expected_schema": validate_series.EXPECTED_SCHEMA,
            "actual_rows": [first],
            "expected_rows": [second],
            "actual_only_rows": [first],
            "expected_only_rows": [second],
            "actual_count": 1,
            "expected_count": 1,
            "actual_null_values": 0,
            "expected_null_values": 0,
        }
        result = validate_series.compare_payload(payload)
        self.assertEqual(result["status"], "failed")
        self.assertFalse(result["sql_two_way_except_all_empty"])
        self.assertFalse(result["python_multiset_equal"])

    def test_s2_sql_corpus_covers_preflight_and_present_metadata_mismatches(self) -> None:
        source = (ROOT / "test/sql/read_om_series_validation.test").read_text(encoding="utf-8")
        pure_marker = "# Existing candidates are bound and reconciled before the logical result can execute."
        metadata_marker = "# S2 metadata-dependent mismatches open the stated present candidate"
        pure_cases = validate_series.parse_sql_logic_error_cases(source.split(pure_marker, 1)[0], "preflight")
        metadata_cases = validate_series.parse_sql_logic_error_cases(
            source.split(metadata_marker, 1)[1], "metadata")
        self.assertGreaterEqual(len(pure_cases), 35)
        self.assertEqual(len(metadata_cases), 8)
        pure_sql = "\n".join(case["sql"] for case in pure_cases)
        self.assertIn("58982400000000", pure_sql)
        self.assertIn("make_timestamp(-9000000000000000000", pure_sql)
        self.assertIn("make_timestamp(9223372036854775806", pure_sql)
        self.assertIn("unknown_option", pure_sql)
        normalized = [{**case, "sql": case["sql"].replace("__WORKING_DIRECTORY__", str(ROOT))}
                      for case in metadata_cases]
        labels = {validate_series.s2_case_label(case)[0] for case in normalized}
        self.assertEqual(labels, {
            "embedded_time_conflict_selected", "embedded_time_conflict_slotless", "declared_type",
            "dimension_order", "spatial_profile", "grid_identity", "time_extent_across_chunks",
            "point_extent_across_chunks",
        })
        trace = '\n'.join([
            'openat(AT_FDCWD, "/tmp/chunk_0000.om", O_RDONLY) = 3',
            'connect(4, {sa_family=AF_INET, sin_port=htons(443)}, 16) = 0',
        ])
        summary = validate_series.summarize_s2_access_trace(trace)
        self.assertEqual(summary["candidate_paths"], ["/tmp/chunk_0000.om"])
        self.assertEqual(len(summary["network_events"]), 1)

    def test_registry_equivalence_sql_runs_registered_and_explicit_calls_with_two_way_except(self) -> None:
        prefix = "https://127.0.0.1:9443/data/registry-equivalence/ecmwf_ifs025"
        sql = validate_series.registry_equivalence_sql(prefix, "ecmwf_ifs025", 104, 10800)
        self.assertIn("model := 'ecmwf_ifs025'", sql)
        self.assertEqual(sql.count("model := 'ecmwf_ifs025'"), 2)
        self.assertIn("chunk_time_length := 104", sql)
        self.assertIn("time_step := INTERVAL '10800 seconds'", sql)
        self.assertEqual(sql.count("EXCEPT ALL"), 2)
        self.assertIn("registered_scan_metrics", sql)
        self.assertIn("explicit_scan_metrics", sql)
        self.assertIn("1970-01-13 21:00:00", sql)

    def test_registry_equivalence_requires_schema_counts_times_and_empty_differences(self) -> None:
        payload = {
            "registered_schema": validate_series.EXPECTED_SCHEMA,
            "explicit_schema": validate_series.EXPECTED_SCHEMA,
            "registered_count": 624,
            "explicit_count": 624,
            "registered_only_count": 0,
            "explicit_only_count": 0,
            "registered_first_time": "1970-01-01 00:00:00",
            "registered_last_time": "1970-01-13 21:00:00",
            "explicit_first_time": "1970-01-01 00:00:00",
            "explicit_last_time": "1970-01-13 21:00:00",
            "registered_scan_metrics": [{"scan_id": 1}],
            "explicit_scan_metrics": [{"scan_id": 2}],
            "engine_version": "v1.5.5 test",
        }
        model = {"key": "ecmwf_ifs025", "chunk_time_length": 104, "step_seconds": 10800}
        result = validate_series.compare_registry_equivalence(payload, model)
        self.assertEqual(result["status"], "passed")
        self.assertTrue(result["two_way_except_all_empty"])
        self.assertEqual(result["expected_count"], 624)
        changed = {**payload, "registered_only_count": 1}
        self.assertEqual(validate_series.compare_registry_equivalence(changed, model)["status"], "failed")

    def test_post_bind_replacement_requires_changed_etag_and_identity_diagnostic(self) -> None:
        target = "data/synthetic/temperature/chunk_0000.om"
        bound = [{"event": "response", "object": target, "status": 200,
                  "etag": '"bound"', "state": "normal"}]
        replacement = [{"event": "response", "object": target, "status": 200,
                        "etag": '"replacement"', "state": "replacement"}]
        result = validate_series.summarize_post_bind_mutation(
            "replace", target, bound, replacement, 1,
            "Invalid Error: remote OM object identity changed")
        self.assertTrue(result["passed"])
        self.assertTrue(result["identity_changed"])
        self.assertEqual(result["expected_post_mutation_status"],
                         "HTTP 200 replacement HEAD with changed ETag")
        unchanged = validate_series.summarize_post_bind_mutation(
            "replace", target, bound, [{**replacement[0], "etag": '"bound"'}], 1,
            "remote OM object identity changed")
        self.assertFalse(unchanged["passed"])
        precondition = validate_series.summarize_post_bind_mutation(
            "replace", target, bound, [{**replacement[0], "status": 412}], 1,
            "HTTP 412")
        self.assertFalse(precondition["passed"])
        missing = validate_series.summarize_post_bind_mutation(
            "remove", target, bound,
            [{"event": "response", "object": target, "status": 404}], 1,
            "remote OM object missing")
        self.assertTrue(missing["passed"])

    def test_remote_sql_snapshots_series_metrics_before_the_manual_oracle(self) -> None:
        actual_sql = validate_series.remote_actual_sql(
            "http://127.0.0.1:9000/data/gaps/first-missing/temperature",
            "1970-01-01 00:00:00", "1970-01-01 08:00:00")
        self.assertLess(actual_sql.index("CREATE TEMP TABLE series_actual"),
                        actual_sql.index("CREATE TEMP TABLE series_metrics"))
        self.assertIn("duckomo_last_scan_metrics()", actual_sql)
        expected_sql = validate_series.remote_expected_sql(
            "s3://duckomo-series/data/gaps/all-missing/temperature", [],
            "1970-01-01 00:00:00", "1970-01-01 08:00:00")
        self.assertIn("CAST(NULL AS TIMESTAMP) AS valid_time WHERE false", expected_sql)
        self.assertNotIn("read_om(", expected_sql)

    def test_n160_matrix_sql_accepts_remote_uri_and_preserves_independent_oracle(self) -> None:
        prefix = "https://127.0.0.1:9443/data/synthetic/reduced-gaussian-n160/temperature"
        sql = validate_series.n160_reduced_gaussian_matrix_sql(prefix, 2)
        self.assertIn("read_om_series('" + prefix + "'", sql)
        self.assertIn("read_om('" + prefix + "/chunk_0000.om'", sql)
        self.assertIn("read_om('" + prefix + "/chunk_0001.om'", sql)
        self.assertIn("domain := 'n160'", sql)
        self.assertIn("grid := n160_explicit_grid(), spatial_axes := ['point']", sql)
        self.assertIn("SET duckomo_max_threads=2", sql)
        self.assertLess(sql.index("CREATE TEMP TABLE series_value_metrics"),
                        sql.index("CREATE TEMP TABLE manual_expected"))
        self.assertIn("CREATE TEMP TABLE series_value_free_metrics", sql)

    def test_scan_metric_summary_checks_worker_caps_and_query_memory_bounds(self) -> None:
        metric = {
            "outcome": {"scan_complete": True},
            "reads": {"metadata": {"scan_requests": 1},
                      "value_totals": {"index_bytes": 0, "index_requests": 0,
                                       "data_bytes": 0, "data_requests": 0, "decoded_chunks": 0}},
            "legacy_v3": {"tasks": {"max_active_workers": 2}},
            "memory": {"query_owned_peak_bytes": 80, "query_owned_upper_bound_peak_bytes": 100},
        }
        records = [{"query_id": "q1", "scan_id": 0, "metrics": json.dumps(metric)}]
        passed = validate_series.summarize_local_scan_metrics(records, 1, False, worker_cap=2)
        self.assertEqual(passed["status"], "passed")
        self.assertTrue(passed["all_children_respect_worker_cap"])
        self.assertTrue(passed["all_query_memory_peaks_within_recorded_upper_bounds"])
        self.assertEqual(validate_series.summarize_local_scan_metrics(
            records, 1, False, worker_cap=1)["status"], "failed")
        metric["memory"]["query_owned_peak_bytes"] = 101
        over_bound = validate_series.summarize_local_scan_metrics(
            [{"query_id": "q1", "scan_id": 0, "metrics": json.dumps(metric)}], 1, False, worker_cap=2)
        self.assertEqual(over_bound["status"], "failed")

    def test_registered_remote_sql_inherits_the_unoverridden_step(self) -> None:
        sql = validate_series.remote_actual_sql(
            "http://127.0.0.1:9000/data/synthetic/temperature",
            "1970-01-01 00:00:00", "1970-01-01 08:00:00",
            model="ecmwf_ifs", chunk_time_length=4, time_step_sql=None)
        self.assertIn("model := 'ecmwf_ifs'", sql)
        self.assertIn("chunk_time_length := 4", sql)
        self.assertNotIn("time_step :=", sql)
        self.assertIn("grid :=", sql)
        self.assertIn("spatial_axes :=", sql)
        self.assertIn("WHERE false", validate_series.remote_actual_sql(
            "http://127.0.0.1:9000/data/gaps/first-missing/temperature",
            "1970-01-01 00:00:00", "1970-01-01 08:00:00", strict=True,
            outer_clause="WHERE false"))
        self.assertIn("LIMIT 0", validate_series.remote_actual_sql(
            "http://127.0.0.1:9000/data/gaps/first-missing/temperature",
            "1970-01-01 00:00:00", "1970-01-01 08:00:00", strict=True,
            outer_clause="LIMIT 0"))

    def test_remote_audit_rejects_listing_metadata_and_unplanned_witness_reads(self) -> None:
        planned = ["data/gaps/first-missing/temperature/chunk_0000.om",
                   "data/gaps/first-missing/temperature/chunk_0001.om"]
        clean_events = [
            {"event": "response", "object": planned[0], "status": 404, "list_request": False},
            {"event": "response", "object": planned[1], "status": 206, "list_request": False},
        ]
        result = validate_series.summarize_remote_audit(clean_events, planned)
        self.assertTrue(result["all_requests_within_planned_candidates"])
        self.assertEqual(result["list_request_count"], 0)
        self.assertEqual(result["metadata_or_unclassified_request_count"], 0)
        dirty_events = clean_events + [
            {"event": "response", "object": "<bucket-list>", "status": 200, "list_request": True},
            {"event": "response", "object": "<unknown>", "status": 200, "list_request": False},
            {"event": "response", "object": "data/ecmwf_ifs/static/meta.json", "status": 200,
             "list_request": False},
            {"event": "response", "object": "data/synthetic/temperature/chunk_0002.om",
             "status": 206, "list_request": False},
        ]
        rejected = validate_series.summarize_remote_audit(dirty_events, planned)
        self.assertFalse(rejected["no_list_requests"])
        self.assertFalse(rejected["no_metadata_or_unclassified_requests"])
        self.assertEqual(rejected["metadata_or_unclassified_request_count"], 2)
        self.assertFalse(rejected["no_witness_requests"])

    def test_remote_child_accounting_separates_bind_execution_retries_and_restarts(self) -> None:
        attempted = ["data/gaps/middle-missing/temperature/chunk_0000.om",
                     "data/gaps/middle-missing/temperature/chunk_0001.om",
                     "data/gaps/middle-missing/temperature/chunk_0002.om"]
        events = [
            {"event": "response", "transport": "http", "object": attempted[0], "method": "GET",
             "range": "bytes=0-31", "status": 206},
            {"event": "response", "transport": "http", "object": attempted[1], "method": "GET",
             "range": "bytes=0-31", "status": 404, "fault": None},
            {"event": "response", "transport": "http", "object": attempted[1], "method": "GET",
             "range": "bytes=0-31", "status": 404, "fault": None},
        ]
        result = validate_series.summarize_child_activity(
            attempted, attempted, events,
            [{"scan_id": 7, "metrics": json.dumps({"outcome": {"scan_complete": True}})},
             {"scan_id": 8, "metrics": json.dumps({"outcome": {"scan_complete": True}})},
             {"scan_id": 9, "metrics": json.dumps({"outcome": {"scan_complete": False}})}])
        self.assertEqual(result["bound_children_inferred_from_successful_origin_responses"], [attempted[0]])
        self.assertEqual(result["scan_metric_count"], 3)
        self.assertEqual(result["nonexecuted_scan_metric_ids"], [9])
        self.assertEqual(result["executed_child_scan_count"], 2)
        self.assertEqual(result["transport_activity"]["series_restarts"], 0)
        self.assertEqual(result["transport_activity"]["repeated_failed_request_count_candidate_for_transport_retry"], 1)
        self.assertEqual(result["transport_activity"]["series_query_invocations"], 1)
        corrupt = validate_series.summarize_child_activity(
            [attempted[0]], [attempted[0]],
            [{"event": "response", "transport": "http", "object": attempted[0],
              "method": "GET", "range": "bytes=0-31", "status": 200, "fault": "corrupt"}],
            [], bind_failure_keys=[attempted[0]])
        self.assertEqual(corrupt["bound_children_inferred_from_successful_origin_responses"], [])
        self.assertEqual(corrupt["executed_child_scan_count"], 0)

    def test_remote_compare_does_not_claim_sql_except_was_executed(self) -> None:
        row = {"value": None, "lat": 10.0, "lon": 100.0, "valid_time": "1970-01-01 04:00:00"}
        actual = {"actual_schema": validate_series.EXPECTED_SCHEMA, "actual_rows": [row],
                  "actual_count": 1, "actual_null_values": 1}
        expected = {"expected_schema": validate_series.EXPECTED_SCHEMA, "expected_rows": [row],
                    "expected_count": 1, "expected_null_values": 1}
        result = validate_series.compare_remote_payloads(actual, expected)
        self.assertEqual(result["status"], "passed")
        self.assertIsNone(result["sql_two_way_except_all_empty"])

    def test_generated_alternate_window_and_schema_query_are_portable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="series-sql-test-") as temporary:
            sql = validate_series.scenario_sql(
                "alternate", Path(temporary), "1970-01-01 00:00:00", "1970-01-01 01:00:00", [0], "start_end"
            )
        self.assertIn('"end" := TIMESTAMP', sql)
        self.assertIn("row_number() OVER () AS ordinal_position", sql)

    def test_fixture_paths_cannot_escape_the_fixture_root(self) -> None:
        with self.assertRaises(validate_series.ValidationError):
            validate_series.safe_relative("../outside.om", "fixture path")

    def test_missing_controlled_remote_prerequisites_are_blocked(self) -> None:
        with tempfile.TemporaryDirectory(prefix="validate-series-remote-test-") as temporary:
            root = Path(temporary)
            extension = root / "duckomo.duckdb_extension"
            extension.write_bytes(b"test extension placeholder")
            output = root / "evidence"
            stdout = io.StringIO()
            stderr = io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                exit_code = validate_series.main([
                    "--duckdb", sys.executable,
                    "--extension", str(extension),
                    "--fixtures", str(root / "missing-fixtures"),
                    "--mode", "controlled-remote",
                    "--output", str(output),
                ])
            self.assertEqual(exit_code, 2)
            manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["status"], "blocked")
            self.assertEqual(manifest["gates"]["S3"]["status"], "blocked")
            self.assertEqual(manifest["gates"]["S4"]["status"], "blocked")
            self.assertEqual(manifest["gates"]["SC-004"]["status"], "blocked")
            self.assertEqual(manifest["identities"]["httpfs"]["status"], "missing")

    def test_frozen_real_manifest_validates_producer_identity_and_oracle(self) -> None:
        manifest_path = ROOT / "specs/010-read-om-series/evidence/run-20260803-linux-arm64/t040/real-object-manifest.json"
        loaded = validate_series.load_real_manifest(manifest_path)
        data = loaded["manifest"]
        self.assertEqual(data["producer_provenance"]["model_key"], "ecmwf_ifs025")
        self.assertEqual(data["temporal"]["chunk_time_length"], 104)
        self.assertEqual(data["temporal"]["temporal_resolution_seconds"], 10800)
        self.assertEqual([item["chunk_index"] for item in loaded["objects"]], [1594, 1595])
        self.assertEqual(data["frozen_oracle"]["rows"][0]["valid_time"], "2026-10-08 21:00:00")
        self.assertEqual(data["frozen_oracle"]["rows"][1]["valid_time"], "2026-10-09 00:00:00")
        self.assertFalse(data["input_retention"]["om_object_bytes_retained"])

    def test_real_manifest_rejects_changed_identity_snapshot(self) -> None:
        source_manifest = ROOT / "specs/010-read-om-series/evidence/run-20260803-linux-arm64/t040/real-object-manifest.json"
        with tempfile.TemporaryDirectory(prefix="series-real-manifest-test-") as temporary:
            evidence = Path(temporary)
            shutil.copytree(source_manifest.parent / "sources", evidence / "sources")
            shutil.copytree(source_manifest.parent / "oracles", evidence / "oracles")
            manifest_data = json.loads(source_manifest.read_text(encoding="utf-8"))
            (evidence / "real-object-manifest.json").write_text(json.dumps(manifest_data), encoding="utf-8")
            listing = evidence / "sources/live-listing.xml"
            listing.write_text(listing.read_text(encoding="utf-8") + "\\n", encoding="utf-8")
            with self.assertRaisesRegex(validate_series.ValidationError, "SHA-256 mismatch"):
                validate_series.load_real_manifest(evidence / "real-object-manifest.json")

    def test_real_validation_sql_uses_adjacent_chunks_and_independent_union(self) -> None:
        manifest_path = ROOT / "specs/010-read-om-series/evidence/run-20260803-linux-arm64/t040/real-object-manifest.json"
        real_manifest = validate_series.load_real_manifest(manifest_path)["manifest"]
        sql = validate_series.real_validation_sql(real_manifest)
        self.assertIn("read_om_series(", sql)
        self.assertIn("model := 'ecmwf_ifs025'", sql)
        self.assertIn("domain := 'ecmwf_ifs025'", sql)
        self.assertIn("chunk_1594.om", sql)
        self.assertIn("chunk_1595.om", sql)
        self.assertIn("UNION ALL", sql)
        self.assertIn("EXCEPT ALL", sql)
        self.assertIn("INTERVAL '10800 seconds'", sql)
        self.assertIn("2026-10-08 21:00:00", sql)
        self.assertIn("2026-10-09 00:00:00", sql)

    def test_real_result_requires_schema_rows_and_both_sql_differences_to_match(self) -> None:
        manifest_path = ROOT / "specs/010-read-om-series/evidence/run-20260803-linux-arm64/t040/real-object-manifest.json"
        frozen_rows = validate_series.load_real_manifest(manifest_path)["manifest"]["frozen_oracle"]["rows"]
        payload = {
            "actual_schema": validate_series.EXPECTED_SCHEMA,
            "manual_schema": validate_series.EXPECTED_SCHEMA,
            "actual_rows": frozen_rows,
            "manual_rows": frozen_rows,
            "actual_only_rows": [], "manual_only_rows": [],
            "actual_count": 2, "manual_count": 2, "engine_version": "v1.5.5 test",
        }
        self.assertEqual(validate_series.compare_real_payload(payload, frozen_rows)["status"], "passed")
        payload["actual_rows"] = [dict(frozen_rows[0], value=25.0), frozen_rows[1]]
        self.assertEqual(validate_series.compare_real_payload(payload, frozen_rows)["status"], "failed")
        mismatched_schema = [{"name": "value", "type": "DOUBLE"}, *validate_series.EXPECTED_SCHEMA[1:]]
        self.assertEqual(validate_series.compare_real_payload(
            {**payload, "actual_rows": frozen_rows}, frozen_rows, mismatched_schema)["status"], "failed")

    def test_real_mode_is_independent_of_synthetic_fixture_prerequisite(self) -> None:
        manifest_path = ROOT / "specs/010-read-om-series/evidence/run-20260803-linux-arm64/t040/real-object-manifest.json"
        with tempfile.TemporaryDirectory(prefix="validate-series-real-test-") as temporary:
            root = Path(temporary)
            extension = root / "duckomo.duckdb_extension"
            extension.write_bytes(b"test extension placeholder")
            output = root / "evidence"
            stdout = io.StringIO()
            stderr = io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                exit_code = validate_series.main([
                    "--duckdb", sys.executable,
                    "--extension", str(extension),
                    "--manifest", str(manifest_path),
                    "--mode", "real",
                    "--output", str(output),
                ])
            self.assertEqual(exit_code, 2)
            result = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(result["status"], "blocked")
            self.assertEqual(result["gates"]["S7"]["status"], "blocked")
            self.assertNotIn("fixtures", [item["name"] for item in result["prerequisites"]])
            self.assertEqual(result["identities"]["httpfs"]["status"], "missing")
            self.assertEqual(result["commands"], [])

    def test_missing_fixture_prerequisite_is_recorded_as_blocked(self) -> None:
        with tempfile.TemporaryDirectory(prefix="validate-series-test-") as temporary:
            root = Path(temporary)
            extension = root / "duckomo.duckdb_extension"
            extension.write_bytes(b"test extension placeholder")
            output = root / "evidence"
            missing_fixtures = root / "not-generated"
            stdout = io.StringIO()
            stderr = io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                exit_code = validate_series.main([
                    "--duckdb", sys.executable,
                    "--extension", str(extension),
                    "--fixtures", str(missing_fixtures),
                    "--mode", "local",
                    "--output", str(output),
                ])
            self.assertEqual(exit_code, 2)
            manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["status"], "blocked")
            self.assertEqual(manifest["gates"]["S1"]["status"], "blocked")
            self.assertEqual(manifest["gates"]["S2"]["status"], "blocked")
            self.assertFalse(any(gate["status"] == "passed" for gate in manifest["gates"].values()))
            self.assertIn("missing", [item["status"] for item in manifest["prerequisites"]])


if __name__ == "__main__":
    unittest.main()
