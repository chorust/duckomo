#!/usr/bin/env python3
"""Run and record the local regressions required by the region P6 gate."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "test/tools"))
from region_evidence_test import (EvidenceError, SANITIZER_TESTS, ensure_safe_output,
                                  read_sanitizer_results, regression_artifacts, regression_environment,
                                  required_regression_tests, sanitizer_extension_path, sanitizer_test_directory, sha256_file)
import version_matrix


def record(root: Path, build_manifest: Path, output: Path, spatial: Path,
           domain_file: Path, domain_reference: Path) -> dict:
    ensure_safe_output(output, build_manifest.parent)
    build = json.loads(build_manifest.read_text())
    release = Path(build["release_dir"]).resolve()
    version = build["pair_id"]
    source_hash = version_matrix.extension_source_digest(root)
    if build["inputs"]["extension_source_sha256"] != source_hash:
        raise EvidenceError("build manifest does not identify the current source")
    if version_matrix.compute_build_id(build["inputs"]) != build["build_id"]:
        raise EvidenceError("build ID does not match its inputs")
    extension = release / "extension/duckomo/duckomo.duckdb_extension"
    if sha256_file(extension) != build["artifact_hashes"]["duckomo"]:
        raise EvidenceError("DuckOMO artifact differs from the build manifest")
    if not domain_file.is_file() or not domain_reference.is_dir():
        raise EvidenceError("complete native regression requires domain object and reference inputs")
    spatial_sha = subprocess.check_output(
        [sys.executable, str(root / "scripts/region_runtime.py"),
         str(root / "test/data/regions/runtime-matrix.json"), version], text=True).strip()
    if sha256_file(spatial) != spatial_sha:
        raise EvidenceError("spatial artifact differs from the pinned runtime")
    env = dict(os.environ, DUCKOMO_SPATIAL_EXTENSION=str(spatial), DUCKOMO_SPATIAL_SHA256=spatial_sha,
               PYTHONDONTWRITEBYTECODE="1", **regression_environment(release))
    report = {"schema_version": 1, "status": "running", "runtime_version": version,
              "build_manifest_path": str(build_manifest), "build_manifest_sha256": sha256_file(build_manifest),
              "duckomo_sha256": sha256_file(extension), "spatial_sha256": spatial_sha,
              "feature_manifest_sha256": sha256_file(root / "test/data/regions/manifest.json"),
              "commands": [], "sanitizer_tests": []}

    def save():
        (output / "manifest.json").write_text(json.dumps(report, indent=2) + "\n")

    def run(test_id, argv):
        selected = regression_environment(release, sanitizer=test_id == "sanitizers")
        artifacts = [{"path": str(path), "sha256": sha256_file(path)}
                     for path in regression_artifacts(root, release, test_id)]
        log = output / (str(len(report["commands"]) + 1) + ".log")
        with log.open("w") as stream:
            result = subprocess.run(argv, cwd=root, env=dict(env, **selected), stdout=stream, stderr=subprocess.STDOUT)
        report["commands"].append({"id": test_id, "argv": argv, "exit_code": result.returncode,
                                   "environment": selected,
                                   "output_path": log.name, "output_sha256": sha256_file(log),
                                   "artifacts": artifacts})
        save()
        if result.returncode:
            raise EvidenceError(f"{test_id} failed: see {log}")

    save()
    try:
        # CTest supplies individual terminal results; a successful aggregate exit alone is insufficient.
        junit = output / "sanitizers.xml"
        run("sanitizers", ["ctest", "--test-dir", str(sanitizer_test_directory(release)), "--output-on-failure",
                           "-R", "^duckomo_sanitizer_", "--output-junit", str(junit)])
        report["sanitizer_results_path"] = junit.name
        report["sanitizer_results_sha256"] = sha256_file(junit)
        report["sanitizer_tests"] = read_sanitizer_results(junit, sanitizer_extension_path(release))
        for test_id in sorted(required_regression_tests(root)):
            kind, name = test_id.split(":", 1)
            if kind == "sql":
                runner = release / "test/unittest"
                run(test_id, [str(runner), "--test-dir", str(root), name])
            elif kind == "native":
                binary = release / "test/native" / name
                argv = [str(binary)]
                if name == "region_geometry_test":
                    argv += [str(spatial), spatial_sha]
                elif name == "region_scan_test":
                    argv += [str(spatial), env["DUCKOMO_CORE_FUNCTIONS_EXTENSION"]]
                elif name == "domain_reference_test":
                    argv += [str(domain_file), str(root / "test/data/domain-manifest.json"), str(domain_reference)]
                run(test_id, argv)
            else:
                source = root / name
                run(test_id, [sys.executable, str(source)])
        if version_matrix.extension_source_digest(root) != source_hash:
            raise EvidenceError("source changed during regression execution")
        report["status"] = "pass"
    except Exception as error:
        report["status"] = "fail"
        report["error"] = str(error)
        raise
    finally:
        save()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--spatial", type=Path, required=True)
    parser.add_argument("--domain-file", type=Path, required=True)
    parser.add_argument("--domain-reference", type=Path, required=True)
    args = parser.parse_args()
    try:
        record(*(getattr(args, key).resolve() for key in
                 ("root", "build_manifest", "output", "spatial", "domain_file", "domain_reference")))
    except (EvidenceError, OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"region regressions: {error}", file=sys.stderr)
        return 1
    print(f"Region regression evidence: {args.output / 'manifest.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
