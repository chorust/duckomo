#!/usr/bin/env python3
"""Keep regression recording bound to each selected version's build artifacts."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location("record_region_regressions", ROOT / "scripts/record-region-regressions.py")
recorder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(recorder)


class RecordRegionRegressionsTest(unittest.TestCase):
    def test_each_version_overrides_inherited_build_and_records_used_artifacts(self):
        for version in ("v1.5.4", "v1.5.5", "v1.5.6"):
            with self.subTest(version=version), TemporaryDirectory() as directory:
                base = Path(directory)
                release = base / version / "release"
                sanitized = release / "extension/duckomo/test/sanitizer_runtime/build/release/extension/duckomo/duckomo.duckdb_extension"
                extension = release / "extension/duckomo/duckomo.duckdb_extension"
                paths = [extension, sanitized, release / "duckdb", release / "test/tools/duckomo_fixture_tool",
                         release / "test/tools/duckomo_region_validation",
                         release / "extension/core_functions/core_functions.duckdb_extension"]
                for path in paths:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(str(path))
                spatial = base / "spatial.duckdb_extension"
                spatial.write_bytes(b"spatial")
                domain = base / "domain.om"
                domain.write_bytes(b"domain")
                reference = base / "reference"
                reference.mkdir()
                inputs = {"pair_id": version, "extension_source_sha256": recorder.version_matrix.extension_source_digest(ROOT)}
                build_path = base / version / "build-manifest.json"
                build_path.write_text(json.dumps({"pair_id": version, "inputs": inputs,
                    "build_id": recorder.version_matrix.compute_build_id(inputs), "release_dir": str(release),
                    "artifact_hashes": {"duckomo": recorder.sha256_file(extension)}}))
                test_id = "python:test/tools/hres_domain_test.py"
                seen = []

                def run(argv, *, cwd, env, stdout, stderr):
                    seen.append((argv, env))
                    if argv[0] == "ctest":
                        self.assertEqual(argv[2], str(release / "extension/duckomo/test"))
                        self.assertEqual(env["DUCKOMO_EXTENSION_PATH"], str(sanitized))
                        self.assertEqual(env["DUCKOMO_TEST_EXTENSION"], str(sanitized))
                        suite = ET.Element("testsuite")
                        for name in recorder.SANITIZER_TESTS:
                            case = ET.SubElement(suite, "testcase", name=name, status="run")
                            if name == "duckomo_sanitizer_lifecycle":
                                ET.SubElement(case, "system-out").text = f"lifecycle loaded DuckOMO: {sanitized}\n"
                        ET.ElementTree(suite).write(argv[-1])
                    else:
                        self.assertEqual(env["DUCKOMO_BUILD"], str(release))
                        self.assertEqual(env["DUCKOMO_EXTENSION_PATH"], str(extension))
                        self.assertEqual(env["DUCKOMO_OFFICIAL_DUCKDB"], str(release / "duckdb"))
                        with patch.dict(os.environ, env, clear=True):
                            module = runpy.run_path(str(ROOT / "test/tools/hres_domain_test.py"))
                        self.assertEqual(module["DUCKDB"], release / "duckdb")
                        self.assertEqual(module["EXTENSION"], extension)
                        self.assertEqual(module["FIXTURE_TOOL"], release / "test/tools/duckomo_fixture_tool")
                    return subprocess.CompletedProcess(argv, 0)

                with patch.dict(os.environ, {"DUCKOMO_BUILD": "/stale/release", "DUCKOMO_OFFICIAL_DUCKDB": "/stale/duckdb"}), \
                     patch.object(recorder, "required_regression_tests", return_value={test_id}), \
                     patch.object(recorder.subprocess, "check_output", return_value=recorder.sha256_file(spatial)), \
                     patch.object(recorder.subprocess, "run", side_effect=run):
                    report = recorder.record(ROOT, build_path, base / "evidence", spatial, domain, reference)
                self.assertEqual(report["status"], "pass")
                self.assertEqual(len(seen), 2)
                sanitizer, python = report["commands"]
                self.assertEqual(sanitizer["environment"]["DUCKOMO_EXTENSION_PATH"], str(sanitized))
                artifacts = {Path(item["path"]): item["sha256"] for item in python["artifacts"]}
                for path in (ROOT / "test/tools/hres_domain_test.py", extension, release / "duckdb",
                             release / "test/tools/duckomo_fixture_tool"):
                    self.assertEqual(artifacts[path], recorder.sha256_file(path))


if __name__ == "__main__":
    unittest.main()
