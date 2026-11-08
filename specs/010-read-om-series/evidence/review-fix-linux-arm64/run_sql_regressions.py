#!/usr/bin/env python3
"""Run the two Feature 010 SQL regressions against each fresh review-fix extension."""
import hashlib
import json
import platform
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
OUT = Path(__file__).resolve().parent
BUILD_RECORDS = json.loads((OUT / "build-artifacts.json").read_text())
TESTS = (
    "test/sql/read_om_series_validation.test",
    "test/sql/read_om_series_missing.test",
)


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    if platform.machine() != "aarch64":
        raise SystemExit(f"linux_arm64 evidence requires aarch64 host, got {platform.machine()}")
    results = {"platform": platform.machine(), "tests": list(TESTS), "pairs": []}
    for item in BUILD_RECORDS["pairs"]:
        pair = item["pair"]
        original = Path(item["previous_extension"]["path"])
        fresh = Path(item["review_fix_extension"]["path"])
        release_dir = original.parents[2]
        unittest = release_dir / "test/unittest"
        old_sha = sha(original)
        record = {"pair": pair, "unittest": str(unittest), "previous_extension_sha256": old_sha,
                  "review_fix_extension_sha256": sha(fresh), "commands": []}
        if old_sha != item["previous_extension"]["sha256"]:
            raise SystemExit(f"previous extension hash mismatch for {pair}")
        with tempfile.TemporaryDirectory(prefix=f"duckomo-{pair}-sql-tests-") as temporary:
            backup = Path(temporary) / original.name
            shutil.copy2(original, backup)
            try:
                shutil.copy2(fresh, original)
                for test in TESTS:
                    command = [str(unittest), "--test-dir", str(ROOT), str(ROOT / test)]
                    completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
                    log_path = OUT / f"sql-{pair}-{Path(test).stem}.log"
                    log_path.write_text(completed.stdout + completed.stderr)
                    record["commands"].append({"test": test, "command": command,
                                               "exit_code": completed.returncode, "log": log_path.name,
                                               "output": completed.stdout + completed.stderr})
            finally:
                shutil.copy2(backup, original)
        record["previous_extension_restored"] = sha(original) == old_sha
        results["pairs"].append(record)
        (OUT / "sql-test-results.json").write_text(json.dumps(results, indent=2) + "\n")
        failed = [command for command in record["commands"] if command["exit_code"] != 0]
        if failed or not record["previous_extension_restored"]:
            raise SystemExit(f"SQL regression or artifact preservation failed for {pair}")
        print(pair, "PASS", len(record["commands"]), "SQL files; previous extension restored")
    results["all_pass"] = True
    (OUT / "sql-test-results.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
