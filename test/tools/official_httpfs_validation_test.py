#!/usr/bin/env python3
"""Reject failed CLI queries and stale performance artifacts without remote fixtures."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "httpfs_performance", ROOT / "scripts/compare-official-httpfs-performance.py"
)
performance = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(performance)


class OfficialHttpfsValidationTest(unittest.TestCase):
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
