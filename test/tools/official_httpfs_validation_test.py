#!/usr/bin/env python3
"""Reject failed CLI queries and stale performance artifacts without remote fixtures."""
import argparse
import csv
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / os.environ.get("DUCKOMO_BUILD", "build/release")
SPEC = importlib.util.spec_from_file_location(
    "httpfs_performance", ROOT / "scripts/compare-official-httpfs-performance.py"
)
performance = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(performance)
REGION_SPEC = importlib.util.spec_from_file_location(
    "official_httpfs", ROOT / "scripts/validate-official-httpfs.py"
)
official_httpfs = importlib.util.module_from_spec(REGION_SPEC)
REGION_SPEC.loader.exec_module(official_httpfs)
RUNTIME_SPEC = importlib.util.spec_from_file_location("region_runtime", ROOT / "scripts/region_runtime.py")
region_runtime = importlib.util.module_from_spec(RUNTIME_SPEC)
RUNTIME_SPEC.loader.exec_module(region_runtime)


class OfficialHttpfsValidationTest(unittest.TestCase):
    def test_region_runtime_requires_accepted_status_and_matching_source_identity(self):
        record = {
            "version": "v1.5.4",
            "gate_status": "ready_for_adapter_check",
            "source_identity_match": True,
            "spatial_source_sha": "a" * 40,
            "probes": {"extension_version": "aaaaaaa"},
            "spatial_artifact": {"sha256": "b" * 64},
        }
        matrix = {"runtimes": [record]}
        self.assertEqual(region_runtime.verified_spatial_sha(matrix, "v1.5.4"), "b" * 64)
        for status in ("blocked", "blocked_identity_mismatch", "unknown"):
            with self.subTest(status=status):
                record["gate_status"] = status
                self.assertIsNone(region_runtime.verified_spatial_sha(matrix, "v1.5.4"))
        record["gate_status"] = "ready_for_adapter_check"
        record["source_identity_match"] = False
        self.assertIsNone(region_runtime.verified_spatial_sha(matrix, "v1.5.4"))
        record["source_identity_match"] = True
        record["probes"]["extension_version"] = "ccccccc"
        self.assertIsNone(region_runtime.verified_spatial_sha(matrix, "v1.5.4"))
        record["probes"] = None
        self.assertIsNone(region_runtime.verified_spatial_sha(matrix, "v1.5.4"))

    def test_p5_remote_config_rejects_identity_mismatch_even_when_artifact_hash_matches(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            region_dir = root / "test/data/regions"
            region_dir.mkdir(parents=True)
            region_manifest = region_dir / "real-targets.json"
            region_manifest.write_text(json.dumps({"targets": [{} for _ in range(5)]}))
            (region_dir / "manifest.json").write_text(json.dumps({
                "real_targets": {"path": region_manifest.name, "sha256": official_httpfs.sha(region_manifest)}
            }))
            duckdb = root / "duckdb"
            duckdb.write_text("#!/bin/sh\nprintf '%s\\n' 'v1.5.4 (test)'\n")
            duckdb.chmod(0o755)
            httpfs = root / "httpfs"
            spatial = root / "spatial"
            httpfs.write_bytes(b"httpfs")
            spatial.write_bytes(b"spatial")
            (region_dir / "runtime-matrix.json").write_text(json.dumps({"runtimes": [{
                "version": "v1.5.4",
                "gate_status": "blocked_identity_mismatch",
                "source_identity_match": False,
                "spatial_source_sha": "a" * 40,
                "probes": {"extension_version": "ccccccc"},
                "official_runtime": {
                    "cli": {"sha256": official_httpfs.sha(duckdb)},
                    "httpfs": {"sha256": official_httpfs.sha(httpfs)},
                },
                "spatial_artifact": {"sha256": official_httpfs.sha(spatial)},
            }]}))
            args = argparse.Namespace(
                remote_env=root / "run.env", server_log=root / "http.log",
                region_manifest=region_manifest, spatial=spatial, root=root,
                duckdb=duckdb, httpfs=httpfs,
            )
            with self.assertRaisesRegex(official_httpfs.MissingP5Input, "identity is unavailable or unverified"):
                official_httpfs.p5_remote_config(args)

    def test_validate_sh_rejects_blocked_identity_when_package_hash_matches(self):
        cli = BUILD / "duckdb"
        spatial = Path(os.environ.get("DUCKOMO_SPATIAL_EXTENSION",
                                     ROOT / "build/regions-runtime/v1.5.4/official/spatial.duckdb_extension"))
        if not cli.is_file() or not spatial.is_file():
            self.skipTest("local runtime is unavailable")
        matrix = json.loads((ROOT / "test/data/regions/runtime-matrix.json").read_text())
        version = subprocess.check_output([str(cli), "--version"], text=True).split()[0]
        record = next(item for item in matrix["runtimes"] if item.get("version") == version)
        record["gate_status"] = "blocked_identity_mismatch"
        record["source_identity_match"] = False
        record["spatial_artifact"]["sha256"] = official_httpfs.sha(spatial)
        with tempfile.TemporaryDirectory() as directory:
            matrix_path = Path(directory) / "runtime-matrix.json"
            matrix_path.write_text(json.dumps(matrix))
            env = os.environ.copy()
            env["DUCKOMO_REGION_RUNTIME_MATRIX"] = str(matrix_path)
            result = subprocess.run(
                [str(ROOT / "scripts/validate.sh"), str(BUILD), "--local-only",
                 "--regions", "--spatial", str(spatial)],
                cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                check=False,
            )
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("No verified spatial runtime record", result.stdout)

    def test_region_validator_rejects_blocked_identity_when_package_hash_matches(self):
        validator = BUILD / "test/tools/duckomo_region_validation"
        cli = BUILD / "duckdb"
        extension = BUILD / "extension/duckomo/duckomo.duckdb_extension"
        spatial = Path(os.environ.get("DUCKOMO_SPATIAL_EXTENSION",
                                     ROOT / "build/regions-runtime/v1.5.4/official/spatial.duckdb_extension"))
        if not all(path.is_file() for path in (validator, cli, extension, spatial)):
            self.skipTest("local region validator artifacts are unavailable")
        matrix = json.loads((ROOT / "test/data/regions/runtime-matrix.json").read_text())
        version = subprocess.check_output([str(cli), "--version"], text=True).split()[0]
        record = next(item for item in matrix["runtimes"] if item.get("version") == version)
        record["gate_status"] = "blocked_identity_mismatch"
        record["source_identity_match"] = False
        record["spatial_artifact"]["sha256"] = official_httpfs.sha(spatial)
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            matrix_path = base / "runtime-matrix.json"
            matrix_path.write_text(json.dumps(matrix))
            output = base / "evidence"
            output.mkdir()
            result = subprocess.run(
                [str(validator), "--root", str(ROOT), "--manifest", str(ROOT / "test/data/regions/manifest.json"),
                 "--runtime-matrix", str(matrix_path), "--duckdb", str(cli), "--extension", str(extension),
                 "--spatial", str(spatial), "--output", str(output), "--gates", "P0"],
                cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
            )
            self.assertEqual(result.returncode, 2, result.stdout)
            recorded = json.loads((output / "manifest.json").read_text())
        self.assertEqual(recorded["status"], "not-run")
        self.assertIn("matching spatial runtime identity is not verified", recorded["gates"][0]["reason"])

    def test_remote_env_parser_is_allowlisted_and_never_evaluates_shell(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.env"
            marker = Path(directory) / "must-not-exist"
            path.write_text(
                "export DUCKOMO_HTTP_BASE='http://127.0.0.1:8080'\n"
                f"export DUCKOMO_S3_SETUP='$(touch {marker})'\n"
            )
            values = official_httpfs.read_generated_env(path)
            self.assertEqual(values["DUCKOMO_HTTP_BASE"], "http://127.0.0.1:8080")
            self.assertFalse(marker.exists())
            path.write_text("export AWS_SECRET_ACCESS_KEY=secret\n")
            with self.assertRaisesRegex(ValueError, "unknown or duplicate"):
                official_httpfs.read_generated_env(path)

    def test_p5_requires_all_three_savings_measures_to_drop(self):
        full = {"metrics": {"data_bytes": 900, "decoded_blocks": 8},
                "audit": {"sent_body_bytes": 1200}}
        region = {"metrics": {"data_bytes": 200, "decoded_blocks": 2},
                  "audit": {"sent_body_bytes": 300}}
        self.assertEqual(official_httpfs.p5_savings_failures(full, region, "http"), [])
        region["audit"]["sent_body_bytes"] = 1200
        self.assertEqual(official_httpfs.p5_savings_failures(full, region, "http"),
                         ["http service sent bytes did not strictly decrease"])

    def test_service_audit_is_compact_and_keeps_request_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "events.jsonl"
            log.write_text(
                '{"event":"started","request_id":"r1","object":"/grid.om"}\n'
                '{"event":"complete","request_id":"r1","object":"/grid.om",'
                '"status":206,"body_bytes":17}\n'
            )

            result = official_httpfs.audit(log, 0, "grid.om")

            self.assertEqual(result["requests"], 1)
            self.assertEqual(result["responses"], 1)
            self.assertEqual(result["sent_body_bytes"], 17)
            self.assertEqual(result["incomplete_requests"], 0)
            self.assertNotIn("events", result)

    def test_p5_rejects_targets_without_a_frozen_bounded_window(self):
        with self.assertRaises(KeyError):
            official_httpfs.p5_queries({"object": {"path": "build/object.om"}},
                                      "http://127.0.0.1:8123/object.om")

    def test_bounded_window_queries_compare_exact_rows_without_full_scan_or_limit(self):
        target = {
            "object": {"path": "build/object.om"}, "queries": {},
            "remote_window": {
                "baseline_local_sql": "WITH w AS MATERIALIZED (SELECT * FROM read_om('build/object.om') WHERE lon BETWEEN 0 AND 1) SELECT value,source_index FROM w ORDER BY ALL",
                "region_local_sql": "SELECT value,source_index FROM read_om_regions('build/object.om') WHERE lon BETWEEN 0 AND 1 ORDER BY ALL",
            },
        }
        baseline, region = official_httpfs.p5_queries(target, "https://127.0.0.1:8443/object.om")
        self.assertIn("MATERIALIZED", baseline)
        for query in (baseline, region):
            self.assertIn("https://127.0.0.1:8443/object.om", query)
            self.assertIn("lon BETWEEN 0 AND 1", query)
            self.assertIn("ORDER BY ALL", query)
            self.assertNotIn("LIMIT", query.upper())
            self.assertNotIn("sum(hash", query)

    def test_cold_cli_disables_duckdb_external_object_and_metadata_caches(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root / "commands.sql"
            cli = root / "cli"
            cli.write_text(f"#!{sys.executable}\n" + f'''import re
import sys
with open({str(log)!r}, "a", encoding="utf-8") as capture:
    for line in sys.stdin:
        capture.write(line)
        capture.flush()
        match = re.fullmatch(r"SELECT '(__duckomo_complete_\\d+__)';\\s*", line)
        if match:
            print(match.group(1), flush=True)
''')
            cli.chmod(0o755)
            args = argparse.Namespace(root=root, duckdb=cli, httpfs=root / "httpfs",
                                      extension=root / "duckomo", spatial=None)
            with performance.v.CliSession(args, cold_client=True) as session:
                self.assertTrue(session.client_caches_disabled)
            commands = log.read_text(encoding="utf-8")
            for setting in ("enable_external_file_cache", "enable_http_metadata_cache", "enable_object_cache"):
                self.assertIn(f"SET {setting}=false", commands)

    def test_run_scan_forwards_long_timeout_to_query_and_terminal_metrics(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            class Session:
                client_caches_disabled = True

                def __init__(self):
                    self.timeouts = []

                def rss(self):
                    return 1

                def send(self, sql, timeout=120, check_success=False):
                    self.timeouts.append(timeout)
                    if not sql.startswith("COPY ("):
                        return
                    start = sql.index(" TO '") + len(" TO '")
                    end = sql.index("' (FORMAT CSV", start)
                    output = Path(sql[start:end])
                    if "duckomo_last_scan_metrics" in sql:
                        metric = {
                            "outcome": {"status": "success", "terminal_published": True},
                            "legacy_v3": {"cache": {"enabled": False, "capacity_bytes": 0}},
                            "transport": {"response_body_bytes": 0, "attempts": 0,
                                          "responses": 0, "complete": True},
                        }
                        with output.open("w", newline="") as stream:
                            csv.writer(stream).writerow([json.dumps(metric)])
                    else:
                        output.write_text("1\n")

            session = Session()
            args = argparse.Namespace(root=root, output=root / "output")
            record = official_httpfs.run_scan(args, "large", "SELECT 1", session=session,
                                              timeout=official_httpfs.P5_SCAN_TIMEOUT_SECONDS)
            self.assertEqual(session.timeouts, [120, official_httpfs.P5_SCAN_TIMEOUT_SECONDS,
                                                official_httpfs.P5_SCAN_TIMEOUT_SECONDS])
            self.assertEqual(record["result_rows"], 1)

    def test_p5_metrics_require_complete_terminal_value_and_exact_counts(self):
        metric = {
            "operation": "read_om_regions",
            "outcome": {"status": "success", "scan_complete": True, "terminal_published": True},
            "reads": {"value_totals": {"data_bytes": 50, "decoded_chunks": 2, "decode_complete": True}},
            "regions": {"match_count_complete": True, "matched_pairs_exact": 9},
        }
        result = official_httpfs.p5_metrics({"metrics": [metric]}, "read_om_regions", True)
        self.assertEqual(result["data_bytes"], 50)
        self.assertEqual(result["matched_pairs_exact"], 9)
        metric["regions"]["match_count_complete"] = False
        with self.assertRaisesRegex(AssertionError, "exact count is incomplete"):
            official_httpfs.p5_metrics({"metrics": [metric]}, "read_om_regions", True)

    def test_p5_service_audit_counts_retries_and_rejects_incomplete_responses(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "http.jsonl"
            rows = [
                {"request_id": "http-1", "event": "started", "object": "target.om"},
                {"request_id": "http-1", "event": "complete", "object": "target.om", "status": 206, "body_bytes": 4},
                {"request_id": "http-2", "event": "started", "object": "target.om"},
            ]
            log.write_text("".join(json.dumps(row) + "\n" for row in rows))
            result = official_httpfs.audit(log, 0, "target.om")
            self.assertEqual(result["requests"], 2)
            self.assertEqual(result["sent_body_bytes"], 4)
            self.assertEqual(result["incomplete_requests"], 1)

    def test_cli_success_check_rejects_current_diagnostics_and_allows_recovery(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cli = root / "cli"
            cli.write_text(f"#!{sys.executable}\n" + '''import re
import sys
for line in sys.stdin:
    if "FAIL_COPY" in line:
        print("IO Error: credential-that-must-not-leak", file=sys.stderr, flush=True)
    match = re.fullmatch(r"SELECT '(__duckomo_complete_\\d+__)';\\s*", line)
    if match:
        print(match.group(1), flush=True)
''')
            cli.chmod(0o755)
            args = argparse.Namespace(root=root, duckdb=cli, httpfs=root / "httpfs", extension=root / "duckomo")
            with performance.v.CliSession(args) as session:
                session.send("SELECT 1;", check_success=True)
                with self.assertRaisesRegex(RuntimeError, "private diagnostics withheld") as failure:
                    session.send("FAIL_COPY;", check_success=True)
                self.assertNotIn("credential-that-must-not-leak", str(failure.exception))
                session.send("SELECT 1;", check_success=True)
                session.send("FAIL_COPY;")
                session.send("SELECT 1;", check_success=True)

    def test_failed_copy_cannot_reuse_old_csv_or_passing_summary(self):
        for diagnostics in (False, True):
            with self.subTest(diagnostics=diagnostics), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                output = root / "output"
                output.mkdir()
                result = output / "http-old-disabled-0-0.csv"
                result.write_text("stale results\n")
                summary = output / "summary.json"
                summary.write_text('{"status":"pass"}\n')
                fixture = root / "fixture.om"
                fixture.write_bytes(b"fixed fixture")
                policy = root / "specs/004-multi-grid-selection/evidence/official-httpfs-refactor/performance-policy.json"
                policy.parent.mkdir(parents=True)
                policy.write_text(json.dumps({"fixture": fixture.name, "sha256": performance.v.sha(fixture)}))
                setup = root / "setup.sql"
                setup.write_text("")
                (root / "http.jsonl").write_text("")
                args = argparse.Namespace(root=root, output=output, baseline=root, official_root=root,
                                          matrix_output=root, http_base="http://example.invalid",
                                          s3_base="s3://bucket", s3_setup=setup, server_log=root)
                session = MagicMock()
                session.__enter__.return_value = session
                if diagnostics:
                    session.send.side_effect = RuntimeError("private diagnostics withheld")
                with patch.object(performance.argparse.ArgumentParser, "parse_args", return_value=args), \
                        patch.object(performance.v, "CliSession", return_value=session):
                    with self.assertRaises(RuntimeError):
                        performance.main()
                self.assertTrue(session.send.call_args.kwargs["check_success"])
                self.assertFalse(result.exists(), "failed COPY must not preserve the previous result")
                self.assertFalse(summary.exists(), "failed reruns must not preserve a passing summary")


if __name__ == "__main__":
    unittest.main()
