#!/usr/bin/env python3
"""Contract checks for sampling validation argument and runtime identity gates."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[2] / "scripts/validate-sampling.py"
SPEC = importlib.util.spec_from_file_location("validate_sampling", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
VALIDATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATOR)


class SamplingEvidenceTests(unittest.TestCase):
    def test_build_identity_rejects_stale_source_and_replaced_artifacts(self) -> None:
        import sampling_build
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            build.mkdir()
            for relative in sampling_build.ARTIFACTS.values():
                path = build / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"built artifact")
            with mock.patch.object(sampling_build, "extension_source_digest", return_value="source-a"), \
                 mock.patch.object(sampling_build, "engine_commit", return_value="engine"):
                sampling_build.record_build(root, build, "source-a", sampling_build.ARTIFACTS)
                identity = sampling_build.checked_build(root, build, "engine")
                self.assertIn("test/native/nearest_sampling_test", identity["artifacts"])
                with self.assertRaisesRegex(ValueError, "current source/runtime"):
                    sampling_build.checked_build(root, build, "other-engine")
                with mock.patch.object(sampling_build, "extension_source_digest", return_value="source-b"):
                    with self.assertRaisesRegex(ValueError, "current source/runtime"):
                        sampling_build.checked_build(root, build, "engine")
                (build / "test/native/nearest_sampling_test").write_bytes(b"replaced artifact")
                with self.assertRaisesRegex(ValueError, "artifact changed"):
                    sampling_build.checked_build(root, build, "engine")

    def test_build_identity_is_not_recorded_if_sources_change_mid_build(self) -> None:
        import sampling_build
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            with mock.patch.object(sampling_build, "extension_source_digest", return_value="new-source"):
                with self.assertRaisesRegex(ValueError, "changed during the build"):
                    sampling_build.record_build(build, build, "old-source", {})
            self.assertFalse((build / sampling_build.MANIFEST).exists())

    def test_reference_manifest_substitution_is_rejected_by_external_pins(self) -> None:
        checker_path = Path(__file__).resolve().parents[2] / "scripts/check-sampling-fixtures.py"
        checker_spec = importlib.util.spec_from_file_location("check_sampling_fixtures", checker_path)
        assert checker_spec is not None and checker_spec.loader is not None
        checker = importlib.util.module_from_spec(checker_spec)
        checker_spec.loader.exec_module(checker)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            names = set(checker.PINNED_REFERENCE_SHA256) | set(checker.PINNED_N1_PROFILE_SHA256)
            originals = {name: b"reviewed:" + name.encode() for name in names}
            for name, content in originals.items():
                (root / name).write_bytes(content)
            baseline_pins = {
                name: hashlib.sha256(content).hexdigest() for name, content in originals.items()
            }
            with mock.patch.object(checker, "PINNED_REFERENCE_SHA256", {
                    name: baseline_pins[name] for name in checker.PINNED_REFERENCE_SHA256}), \
                 mock.patch.object(checker, "PINNED_N1_PROFILE_SHA256", {
                    name: baseline_pins[name] for name in checker.PINNED_N1_PROFILE_SHA256}):
                checker.verify_pinned_reference_identity(root)
                # Simulate replacing the boundary oracle and updating its manifest
                # hash to match the substituted content. The independent pin must fail.
                substituted = b"substituted-cases-v2"
                (root / "capability-boundaries.reference.csv").write_bytes(substituted)
                with self.assertRaisesRegex(ValueError, "frozen reference hash mismatch"):
                    checker.verify_pinned_reference_identity(root)

    def test_fixture_checker_pins_each_oracle_independently(self) -> None:
        checker_path = Path(__file__).resolve().parents[2] / "scripts/check-sampling-fixtures.py"
        checker_spec = importlib.util.spec_from_file_location("pinned_sampling_checker", checker_path)
        assert checker_spec is not None and checker_spec.loader is not None
        checker = importlib.util.module_from_spec(checker_spec)
        checker_spec.loader.exec_module(checker)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            names = set(checker.PINNED_REFERENCE_SHA256) | set(checker.PINNED_N1_PROFILE_SHA256)
            for name in names:
                (root / name).write_bytes(b"reviewed:" + name.encode())
            pins = {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in names}
            with mock.patch.object(checker, "PINNED_REFERENCE_SHA256", {
                    name: pins[name] for name in checker.PINNED_REFERENCE_SHA256}), \
                 mock.patch.object(checker, "PINNED_N1_PROFILE_SHA256", {
                    name: pins[name] for name in checker.PINNED_N1_PROFILE_SHA256}):
                checker.verify_pinned_reference_identity(root)
                for name in checker.PINNED_N1_PROFILE_SHA256:
                    with self.subTest(name=name):
                        (root / name).write_bytes(b"substituted:" + name.encode())
                        with self.assertRaisesRegex(ValueError, "frozen reference hash mismatch"):
                            checker.verify_pinned_reference_identity(root)
                        (root / name).write_bytes(b"reviewed:" + name.encode())

    def test_local_gate_checks_do_not_promote_incomplete_acceptance(self) -> None:
        n3 = VALIDATOR.local_gate_outcome(
            True,
            "projection and I/O checks passed")
        self.assertEqual(n3["status"], "pass")
        self.assertTrue(n3["local_checks_passed"])
        self.assertEqual(n3["blocking_scope"], [])

        n4 = VALIDATOR.local_gate_outcome(
            True,
            "worker and lifecycle checks passed",
            ("isolated memory-scaling replay remains required",))
        self.assertEqual(n4["status"], "blocked")
        self.assertTrue(n4["local_checks_passed"])
        self.assertIn("isolated memory-scaling replay", n4["reason"])
        self.assertEqual(n4["blocking_scope"], ["isolated memory-scaling replay remains required"])

        failed = VALIDATOR.local_gate_outcome(False, "ignored")
        self.assertEqual(failed["status"], "fail")
        self.assertFalse(failed["local_checks_passed"])

    def test_isolated_memory_measurement_requires_one_matching_record(self) -> None:
        output = "nearest_memory 64:61865,17163,1233,12,316032,20392,3185\n"
        self.assertEqual(VALIDATOR.parse_memory_measurement(output, 64), {
            "dimension": 64,
            "query_owned_upper_bound_bytes": 61865,
            "work_peak_bytes": 17163,
            "nearest_value_bytes": 1233,
            "nearest_decoded_chunks": 12,
            "locator_cpu_ns": 316032,
            "full_value_bytes": 20392,
            "full_decoded_chunks": 3185,
        })
        with self.assertRaisesRegex(ValueError, "expected isolated 256x256"):
            VALIDATOR.parse_memory_measurement(output, 256)
        with self.assertRaisesRegex(ValueError, "found 0"):
            VALIDATOR.parse_memory_measurement("region_sampling_test: ok\n", 64)
        with self.assertRaisesRegex(ValueError, "found 2"):
            VALIDATOR.parse_memory_measurement(output + output, 64)

    def test_sampling_sanitizer_registration_requires_matching_runtime_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            test_file = Path(temporary) / "CTestTestfile.cmake"
            cache_file = Path(temporary) / "CMakeCache.txt"
            spatial = Path(temporary) / "spatial.duckdb_extension"
            spatial_hash = "a" * 64
            test_file.write_text(
                f'add_test(duckomo_sanitizer_region_sampling_test "test" "{spatial}")\n',
                encoding="utf-8")
            cache_file.write_text(
                f"DUCKOMO_REGION_SANITIZER_SPATIAL_PATH:FILEPATH={spatial}\n"
                f"DUCKOMO_REGION_SANITIZER_SPATIAL_SHA256:STRING={spatial_hash}\n",
                encoding="utf-8")
            self.assertTrue(VALIDATOR.sanitizer_registration_matches(test_file, cache_file, spatial, spatial_hash))
            self.assertFalse(VALIDATOR.sanitizer_registration_matches(test_file, cache_file, spatial, "b" * 64))
            self.assertFalse(VALIDATOR.sanitizer_registration_matches(test_file, cache_file, Path(temporary) / "other-spatial", spatial_hash))
            cache_file.write_text(f"DUCKOMO_REGION_SANITIZER_SPATIAL_PATH:FILEPATH={spatial}\n", encoding="utf-8")
            self.assertFalse(VALIDATOR.sanitizer_registration_matches(test_file, cache_file, spatial, spatial_hash))

    def test_reviewed_profile_inventory_rejects_substituted_native_oracles(self) -> None:
        checker_path = Path(__file__).resolve().parents[2] / "scripts/check-sampling-fixtures.py"
        checker_spec = importlib.util.spec_from_file_location("reviewed_profile_checker", checker_path)
        assert checker_spec is not None and checker_spec.loader is not None
        checker = importlib.util.module_from_spec(checker_spec)
        checker_spec.loader.exec_module(checker)
        self.assertIn("locator.reference.csv", checker.PINNED_N1_PROFILE_SHA256)
        self.assertIn("scientific.reference.csv", checker.PINNED_N1_PROFILE_SHA256)
        self.assertIn("capability-boundaries.reference.csv", checker.PINNED_N1_PROFILE_SHA256)
        self.assertIn("ncep-gfs025-profile.reference.csv", checker.PINNED_N1_PROFILE_SHA256)
        self.assertEqual(checker.PINNED_N1_PROFILE_SHA256["capability-boundaries.reference.csv"],
                         "8b1d16d3c661d0d8d413b0305527eda44623cbe4f787b4cab5b29e4e5f73ece0")
        self.assertEqual(VALIDATOR.sha256(Path(__file__).resolve().parents[2] / "test/sql/region_sampling.test"),
                         "2c613c74a14c2137ad250ca414a391524df9a6b6423130b76c43e96e659d8064")

    def test_gate_parser_accepts_only_unique_n0_to_n7(self) -> None:
        self.assertEqual(VALIDATOR.parse_gates("N0, N2,N7"), ["N0", "N2", "N7"])
        for value in ("", "N0,,N1", "N0,N0", "P0", "N8"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                VALIDATOR.parse_gates(value)

    def test_runtime_identity_requires_matching_verified_spatial_hash(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cli = root / "duckdb"
            cli.write_text("#!/bin/sh\nprintf 'v1.5.5 (test)\\n'\n", encoding="utf-8")
            cli.chmod(0o755)
            extension = root / "duckomo.duckdb_extension"
            extension.write_bytes(b"duckomo-test")
            spatial = root / "spatial.duckdb_extension"
            spatial.write_bytes(b"spatial-test")
            spatial_hash = hashlib.sha256(spatial.read_bytes()).hexdigest()
            matrix = root / "runtime-matrix.json"
            matrix.write_text(json.dumps({"runtimes": [{
                "version": "v1.5.5", "status": "probe_pass",
                "duckdb_source_sha": "d" * 40,
                "extension_version": "abcdef1",
                "spatial_source_sha": "abcdef1" + "0" * 33,
                "spatial_package": {"sha256": spatial_hash},
            }]}), encoding="utf-8")
            args = type("Args", (), {"duckdb": cli, "spatial": spatial,
                                      "extension": extension, "runtime_matrix": matrix})()
            result = VALIDATOR.checked_runtime(args, root)
            self.assertEqual(result["duckdb_version"], "v1.5.5")
            self.assertEqual(result["spatial_sha256"], spatial_hash)
            spatial.write_bytes(b"different-spatial-package")
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                VALIDATOR.checked_runtime(args, root)

    def test_stable_points_metrics_ignore_only_run_and_artifact_fields(self) -> None:
        baseline = {
            "dependency_commits": {"duckdb": "old", "om-file-format": "same"},
            "environment": {"build": "release", "platform": "old", "build_id": "old"},
            "reads": {"bytes": 100, "requests": 4},
            "memory": {"peak": 1000},
            "outcome": {"status": "success"},
        }
        replay = {
            "dependency_commits": {"duckdb": "new", "om-file-format": "same"},
            "environment": {"build": "v1.5.6", "platform": "new", "build_id": "new"},
            "reads": {"bytes": 100, "requests": 4},
            "memory": {"peak": 2000},
            "outcome": {"status": "success"},
        }
        self.assertEqual(VALIDATOR.canonical_points_metrics(json.dumps(baseline)),
                         VALIDATOR.canonical_points_metrics(json.dumps(replay)))
        baseline["regions"] = {"candidate_pairs_observed": 12, "matched_pairs_exact": 9}
        replay["regions"] = {"candidate_pairs_observed": 12, "matched_pairs_exact": 9,
                             "applicable": False, "not_applicable_reason": "nearest_uses_target_sampling"}
        self.assertEqual(VALIDATOR.canonical_points_metrics(json.dumps(baseline)),
                         VALIDATOR.canonical_points_metrics(json.dumps(replay)))
        replay["reads"]["bytes"] = 101
        self.assertNotEqual(VALIDATOR.canonical_points_metrics(json.dumps(baseline)),
                            VALIDATOR.canonical_points_metrics(json.dumps(replay)))

    def test_legacy_worker_and_reader_runtime_fields_are_canonicalized(self) -> None:
        baseline = {
            "legacy_v3": {
                "elapsed_ms": 1.5,
                "peak_rss_bytes": 1024,
                "active_workers": 8,
                "max_active_workers": 8,
                "tasks": {"active_workers": 8, "max_active_workers": 8, "created": 9},
                "metadata": {"scan": {"physical_bytes": 1310, "physical_requests": 30}},
                "physical_read_bytes": 3151,
                "physical_read_requests": 154,
                "legacy_v2": {
                    "elapsed_ms": 1.5,
                    "peak_rss_bytes": 1024,
                    "active_workers": 8,
                    "metadata_bytes": 1310,
                    "metadata_requests": 30,
                    "scan_metadata_bytes": 1310,
                    "scan_metadata_requests": 30,
                    "bytes_fetched": 3151,
                    "read_requests": 154,
                },
            }
        }
        replay = {
            "legacy_v3": {
                "elapsed_ms": 9.0,
                "peak_rss_bytes": 8192,
                "active_workers": 7,
                "max_active_workers": 6,
                "tasks": {"active_workers": 7, "max_active_workers": 6, "created": 9},
                "metadata": {"scan": {"physical_bytes": 1310, "physical_requests": 30}},
                "physical_read_bytes": 3151,
                "physical_read_requests": 154,
                "legacy_v2": {
                    "elapsed_ms": 9.0,
                    "peak_rss_bytes": 8192,
                    "active_workers": 7,
                    "metadata_bytes": 1310,
                    "metadata_requests": 30,
                    "scan_metadata_bytes": 1310,
                    "scan_metadata_requests": 30,
                    "bytes_fetched": 3151,
                    "read_requests": 154,
                },
            }
        }
        self.assertEqual(VALIDATOR.canonical_points_metrics(json.dumps(baseline)),
                         VALIDATOR.canonical_points_metrics(json.dumps(replay)))
        replay["legacy_v3"]["legacy_v2"]["bytes_fetched"] = 3021
        self.assertNotEqual(VALIDATOR.canonical_points_metrics(json.dumps(baseline)),
                            VALIDATOR.canonical_points_metrics(json.dumps(replay)))

    def test_n0_canonicalization_ignores_nearest_only_region_applicability(self) -> None:
        points = {"regions": {"candidate_pairs_observed": 9, "matched_pairs_exact": 4}}
        nearest_aware_compat = {"regions": {"candidate_pairs_observed": 9, "matched_pairs_exact": 4,
                                             "applicable": False,
                                             "not_applicable_reason": "nearest_uses_target_sampling"}}
        self.assertEqual(VALIDATOR.canonical_points_metrics(json.dumps(points)),
                         VALIDATOR.canonical_points_metrics(json.dumps(nearest_aware_compat)))
        nearest_aware_compat["regions"]["matched_pairs_exact"] = None
        self.assertNotEqual(VALIDATOR.canonical_points_metrics(json.dumps(points)),
                            VALIDATOR.canonical_points_metrics(json.dumps(nearest_aware_compat)))

    def test_points_replay_allows_lut_reuse_but_rejects_cost_or_semantic_regressions(self) -> None:
        expected = {"reads": {"variables": {"/": {"logical_index_bytes": 4050, "index_bytes": 175,
                                                    "index_requests": 7, "data_bytes": 1535}}},
                    "legacy_v2": {"bytes_fetched": 3151, "read_requests": 154}, "rows": 162}
        actual = json.loads(json.dumps(expected))
        actual["reads"]["variables"]["/"]["index_bytes"] = 50
        actual["reads"]["variables"]["/"]["index_requests"] = 2
        actual["legacy_v2"]["bytes_fetched"] = 3026
        actual["legacy_v2"]["read_requests"] = 149
        self.assertTrue(VALIDATOR.points_metrics_match(expected, actual))
        for key, value in (("index_bytes", 176), ("index_requests", 8), ("logical_index_bytes", 4049),
                           ("data_bytes", 1534)):
            changed = json.loads(json.dumps(actual))
            changed["reads"]["variables"]["/"][key] = value
            self.assertFalse(VALIDATOR.points_metrics_match(expected, changed), key)
        actual["rows"] = 161
        self.assertFalse(VALIDATOR.points_metrics_match(expected, actual))

    def test_runtime_matrix_must_record_probe_pass(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cli = root / "duckdb"
            cli.write_text("#!/bin/sh\nprintf 'v1.5.4 (test)\\n'\n", encoding="utf-8")
            cli.chmod(0o755)
            extension = root / "duckomo.duckdb_extension"
            extension.write_bytes(b"duckomo-test")
            spatial = root / "spatial.duckdb_extension"
            spatial.write_bytes(b"spatial-test")
            matrix = root / "runtime-matrix.json"
            matrix.write_text(json.dumps({"runtimes": [{"version": "v1.5.4", "status": "not-run"}]}),
                              encoding="utf-8")
            args = type("Args", (), {"duckdb": cli, "spatial": spatial,
                                      "extension": extension, "runtime_matrix": matrix})()
            with self.assertRaisesRegex(ValueError, "no passing"):
                VALIDATOR.checked_runtime(args, root)


if __name__ == "__main__":
    unittest.main()
