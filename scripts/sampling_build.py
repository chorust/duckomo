#!/usr/bin/env python3
"""Build sampling validation artifacts and bind them to their source inputs."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

from version_matrix import extension_source_digest, digest_file

ARTIFACTS = {
    "duckomo_loadable_extension": "extension/duckomo/duckomo.duckdb_extension",
    "core_functions_loadable_extension": "extension/core_functions/core_functions.duckdb_extension",
    "json_loadable_extension": "extension/json/json.duckdb_extension",
    "unittest": "test/unittest",
    "duckomo_fixture_tool": "test/tools/duckomo_fixture_tool",
    **{name: f"test/native/{name}" for name in (
        "nearest_sampling_test", "region_sampling_test", "region_geometry_test",
        "region_input_test", "spatial_layout_test", "reader_capacity_test",
    )},
}
SANITIZERS = {
    "region_sampling_test_sanitizer": "test/sanitizer/native/region_sampling_test_sanitizer",
    "duckomo_sanitizer_loadable_extension": "extension/duckomo/test/sanitizer_runtime/build/release/extension/duckomo/duckomo.duckdb_extension",
}
MANIFEST = "sampling-build-manifest.json"


def engine_commit(root: Path, build: Path) -> str:
    cache = (build / "CMakeCache.txt").read_text()
    source = next(line.split("=", 1)[1] for line in cache.splitlines()
                  if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL="))
    return subprocess.check_output(["git", "-C", source, "rev-parse", "HEAD"], text=True).strip()


def record_build(root: Path, build: Path, source_digest: str, artifacts: dict[str, str]) -> dict:
    if extension_source_digest(root) != source_digest:
        raise ValueError("sampling source inputs changed during the build; rebuild before recording evidence")
    hashes = {relative: digest_file(build / relative) for relative in artifacts.values()}
    record = {"schema_version": 1, "extension_source_sha256": source_digest,
              "duckdb_source_sha": engine_commit(root, build),
              "build": str(build.resolve()), "artifacts": hashes}
    (build / MANIFEST).write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    return record


def checked_build(root: Path, build: Path, expected_engine: str) -> dict:
    path = build / MANIFEST
    if not path.is_file():
        raise ValueError(f"sampling build identity missing: {path}; run scripts/sampling_build.py --build {build}")
    record = json.loads(path.read_text())
    if (record.get("schema_version") != 1 or record.get("build") != str(build.resolve()) or
            record.get("extension_source_sha256") != extension_source_digest(root) or
            record.get("duckdb_source_sha") != expected_engine):
        raise ValueError(f"sampling build does not match current source/runtime: {path}; rebuild")
    hashes = record.get("artifacts", {})
    for relative in ARTIFACTS.values():
        if relative not in hashes:
            raise ValueError(f"sampling build has no identity for {relative}: {path}")
    for relative, expected in hashes.items():
        artifact = build / relative
        if not artifact.is_file() or digest_file(artifact) != expected:
            raise ValueError(f"sampling artifact changed or is missing: {artifact}; rebuild")
    return {"manifest": str(path), "manifest_sha256": digest_file(path), **record}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--sanitizers", action="store_true")
    args = parser.parse_args()
    root, build = args.root.resolve(), args.build.resolve()
    artifacts = {**ARTIFACTS, **(SANITIZERS if args.sanitizers else {})}
    source_digest = extension_source_digest(root)
    subprocess.run(["cmake", "--build", str(build), "--target", *artifacts,
                    "--parallel", str(args.jobs)], cwd=root, check=True)
    record_build(root, build, source_digest, artifacts)
    print(f"Sampling build identity: {build / MANIFEST}")


if __name__ == "__main__":
    main()
