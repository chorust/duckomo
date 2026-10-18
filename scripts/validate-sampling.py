#!/usr/bin/env python3
"""Run the sampling checks that are available and record scoped N0–N7 evidence.

Each gate is judged against its declared acceptance scope. Frozen finite matrices
may pass without claiming exhaustive coverage of larger input spaces; missing
inputs or required evidence remain blocked or not-run.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sampling_build import checked_build, SANITIZERS

GATES = tuple(f"N{index}" for index in range(8))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(argv: list[str], cwd: Path, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(argv, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, check=False)


def git(root: Path, *args: str) -> str | None:
    result = run(["git", *args], root)
    return result.stdout.strip() if result.returncode == 0 else None


def source_identity(root: Path, exclude: Path | None = None) -> dict:
    head = git(root, "rev-parse", "HEAD")
    patch = subprocess.run(["git", "diff", "--binary", "HEAD"], cwd=root,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    digest = hashlib.sha256(patch.stdout)
    untracked = git(root, "ls-files", "--others", "--exclude-standard") or ""
    excluded = exclude.resolve() if exclude is not None else None
    untracked_files = []
    for name in sorted(untracked.splitlines()):
        path = root / name
        if excluded is not None and path.resolve().is_relative_to(excluded):
            continue
        if path.is_file():
            file_hash = sha256(path)
            digest.update(name.encode())
            digest.update(bytes.fromhex(file_hash))
            untracked_files.append({"path": name, "sha256": file_hash})
    return {"head": head, "dirty_patch_sha256": digest.hexdigest(),
            "untracked_files": untracked_files}


def parse_gates(value: str) -> list[str]:
    gates = [item.strip() for item in value.split(",")]
    if not gates or any(not item for item in gates):
        raise ValueError("--gates must be a comma-separated list of N0 through N7")
    if len(set(gates)) != len(gates):
        raise ValueError("--gates contains duplicates")
    invalid = [item for item in gates if item not in GATES]
    if invalid:
        raise ValueError(f"unknown gate(s): {', '.join(invalid)}; expected N0 through N7")
    return gates


def checked_runtime(args: argparse.Namespace, root: Path,
                    version_result: subprocess.CompletedProcess[str] | None = None) -> dict:
    matrix = json.loads(args.runtime_matrix.read_text(encoding="utf-8"))
    if version_result is None:
        version_result = run([str(args.duckdb), "--version"], root)
    if version_result.returncode:
        raise ValueError(f"DuckDB CLI --version failed: {version_result.stdout.strip()}")
    version_match = re.search(r"v?1\.5\.[456]", version_result.stdout)
    if not version_match:
        raise ValueError("sampling runtime must be pinned to DuckDB v1.5.4, v1.5.5, or v1.5.6")
    version = version_match.group(0)
    if not version.startswith("v"):
        version = "v" + version
    record = next((item for item in matrix.get("runtimes", []) if item.get("version") == version), None)
    if not record or record.get("status") != "probe_pass":
        raise ValueError(f"no passing nearest sampling runtime probe for {version}")
    actual_spatial_sha = sha256(args.spatial)
    expected_spatial_sha = record.get("spatial_package", {}).get("sha256")
    if actual_spatial_sha != expected_spatial_sha:
        raise ValueError(f"spatial package SHA-256 does not match the verified {version} runtime matrix")
    if not str(record.get("extension_version", "")).startswith(str(record.get("spatial_source_sha", ""))[:7]):
        raise ValueError("runtime matrix extension version does not match its source identity")
    return {"duckdb_version": version, "duckdb_cli_sha256": sha256(args.duckdb),
            "duckdb_source_sha": record.get("duckdb_source_sha"),
            "extension_sha256": sha256(args.extension),
            "spatial_sha256": actual_spatial_sha,
            "spatial_source_sha": record.get("spatial_source_sha"),
            "spatial_extension_version": record.get("extension_version"),
            "runtime_matrix_sha256": sha256(args.runtime_matrix)}


def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def require_empty_output(output: Path, root: Path) -> None:
    if output == root:
        raise ValueError("--output cannot be the repository root")
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError("--output must be a new or empty directory; historical evidence is never overwritten")


def parse_memory_measurement(output: str, dimension: int) -> dict[str, int]:
    """Read one memory-only region_sampling_test result line."""
    pattern = re.compile(r"^nearest_memory\s+(\d+):(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\s*$", re.MULTILINE)
    matches = pattern.findall(output)
    if len(matches) != 1:
        raise ValueError(f"expected one isolated nearest_memory record, found {len(matches)}")
    row = tuple(int(value) for value in matches[0])
    if row[0] != dimension:
        raise ValueError(f"expected isolated {dimension}x{dimension} memory record, got {row[0]}")
    return {"dimension": row[0], "query_owned_upper_bound_bytes": row[1],
            "work_peak_bytes": row[2], "nearest_value_bytes": row[3],
            "nearest_decoded_chunks": row[4], "locator_cpu_ns": row[5],
            "full_value_bytes": row[6], "full_decoded_chunks": row[7]}


def sanitizer_registration_matches(test_file: Path, cache_file: Path,
                                   spatial: Path, spatial_sha256: str) -> bool:
    """Require CTest registration and configure cache to bind the selected spatial package."""
    if not test_file.is_file() or not cache_file.is_file():
        return False
    test_text = test_file.read_text(encoding="utf-8")
    registrations = re.findall(
        r"add_test\(duckomo_sanitizer_region_sampling_test[^\n]*\)", test_text)
    cache = cache_file.read_text(encoding="utf-8")
    expected_cache = {
        "DUCKOMO_REGION_SANITIZER_SPATIAL_PATH": str(spatial),
        "DUCKOMO_REGION_SANITIZER_SPATIAL_SHA256": spatial_sha256,
    }
    return (any(str(spatial) in registration for registration in registrations) and
            all(re.search(rf"^{key}:[^=]+=({re.escape(value)})$", cache, re.MULTILINE)
                for key, value in expected_cache.items()))


def local_gate_outcome(local_checks_passed: bool, pass_reason: str,
                       blockers: tuple[str, ...] = ()) -> dict:
    """Separate registered local check results from full acceptance status."""
    if not local_checks_passed:
        return {"status": "fail", "reason": "one or more registered local checks failed",
                "local_checks_passed": False, "blocking_scope": list(blockers)}
    if blockers:
        return {"status": "blocked", "reason": pass_reason + "; acceptance remains blocked: " +
                "; ".join(blockers), "local_checks_passed": True,
                "blocking_scope": list(blockers)}
    return {"status": "pass", "reason": pass_reason,
            "local_checks_passed": True, "blocking_scope": []}


def canonical_points_metrics(raw: str) -> dict:
    metrics = json.loads(raw)
    volatile = {"scan_id", "query_id", "elapsed_ms", "peak_rss_bytes", "process_peak_rss_bytes",
                "geometry_prepare_ns", "geometry_evaluate_ns", "owned_work_peak_bytes",
                "owned_work_bound_bytes", "owned_input_peak_bytes", "query_owned_peak_bytes",
                "query_owned_upper_bound_peak_bytes", "peak_query_owned_bytes",
                "active_workers", "max_active_workers"}
    artifact_identity = {"build", "build_id", "build_pair", "platform", "cxx_abi"}

    def stable(value: object, parent: str = "") -> object:
        if isinstance(value, dict):
            result = {}
            for key, item in value.items():
                if key in volatile or (parent == "regions" and key in {"applicable", "not_applicable_reason"}) or parent == "memory" or (parent == "environment" and key in artifact_identity):
                    continue
                if parent == "dependency_commits" and key == "duckdb":
                    continue
                result[key] = stable(item, key)
            return result
        if isinstance(value, list):
            return [stable(item, parent) for item in value]
        return value

    return stable(metrics)


def points_metrics_match(expected: dict, actual: dict, path: tuple[str, ...] = ()) -> bool:
    # A worker's retained LUT range can avoid repeated physical reads. Task
    # assignment changes that reuse without changing logical work or values.
    # Preserve the frozen cost ceiling rather than requiring identical reuse.
    physical_index = path[-1:] in {("index_bytes",), ("index_requests",)}
    physical_total = path[-1:] in {("bytes_fetched",), ("physical_read_bytes",),
                                  ("read_requests",), ("physical_read_requests",)}
    if physical_index or (physical_total and any(part in {"legacy_v2", "legacy_v3"} for part in path)):
        return type(actual) is int and type(expected) is int and 0 <= actual <= expected
    if isinstance(expected, dict) and isinstance(actual, dict):
        return expected.keys() == actual.keys() and all(
            points_metrics_match(expected[key], actual[key], (*path, key)) for key in expected)
    return expected == actual


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--duckdb", type=Path, required=True)
    parser.add_argument("--extension", type=Path, required=True)
    parser.add_argument("--spatial", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--runtime-matrix", type=Path)
    parser.add_argument("--matrix-output-root", type=Path, default=Path("build/official-matrix"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--gates", required=True)
    parser.add_argument("--httpfs", type=Path)
    parser.add_argument("--remote-env", type=Path)
    parser.add_argument("--server-log", type=Path)
    parser.add_argument("--real-file", type=Path)
    parser.add_argument("--real-regions", type=Path)
    parser.add_argument("--evidence-root", type=Path)
    parser.add_argument("--private-archive", type=Path)
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    for name in ("build", "duckdb", "extension", "spatial", "manifest", "runtime_matrix",
                 "httpfs", "remote_env", "server_log", "real_file", "real_regions", "evidence_root", "matrix_output_root", "private_archive"):
        value = getattr(args, name)
        if value is not None:
            resolved = value if value.is_absolute() else root / value
            setattr(args, name, resolved.resolve())
    if args.runtime_matrix is None:
        args.runtime_matrix = root / "test/data/sampling/runtime-matrix.json"
    output = args.output if args.output.is_absolute() else root / args.output
    output = output.resolve()
    gates = parse_gates(args.gates)

    if not root.is_dir():
        raise SystemExit(f"repository root does not exist: {root}")
    for label, path in (("build", args.build), ("DuckDB CLI", args.duckdb),
                        ("extension", args.extension), ("spatial extension", args.spatial),
                        ("sampling manifest", args.manifest), ("runtime matrix", args.runtime_matrix)):
        if not path.exists():
            raise SystemExit(f"{label} does not exist: {path}")
    if args.manifest != (root / "test/data/sampling/manifest.json").resolve():
        print("validate-sampling: blocked: validation requires the canonical reviewed sampling reference manifest",
              file=sys.stderr)
        return 2
    try:
        require_empty_output(output, root)
        runtime_identity = checked_runtime(args, root)
        fixture_check = run([sys.executable, str(root / "scripts/check-sampling-fixtures.py"),
                             "--root", str(args.manifest.parent)], root)
        if fixture_check.returncode != 0:
            raise ValueError(f"sampling fixture identity check failed: {fixture_check.stdout.strip()}")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"validate-sampling: blocked: {error}", file=sys.stderr)
        return 2
    output.mkdir(parents=True, exist_ok=True)

    started = datetime.now(timezone.utc).isoformat()
    results = {gate: {"status": "not-run", "reason": "not selected or not reached", "commands": []}
               for gate in GATES}
    commands_file = output / "commands.jsonl"
    command_stream = commands_file.open("w", encoding="utf-8")
    runtime = runtime_identity
    inputs = {"started_at": started, "root": str(root), "source": source_identity(root, exclude=output),
              "runtime": runtime, "build": str(args.build),
              "manifest": {"path": str(args.manifest), "sha256": sha256(args.manifest)},
              "oracle_inputs": {
                  "fixture_checker_sha256": sha256(root / "scripts/check-sampling-fixtures.py"),
                  "locator_reference_sha256": sha256(args.manifest.parent / "locator.reference.csv"),
                  "scientific_reference_sha256": sha256(args.manifest.parent / "scientific.reference.csv"),
                  "capability_boundary_reference_sha256": sha256(args.manifest.parent / "capability-boundaries.reference.csv"),
                  "numeric_contract_sha256": sha256(root / "specs/006-region-nearest-sampling/contracts/numeric-and-capability.md"),
                  "sql_contract_sha256": sha256(root / "specs/006-region-nearest-sampling/contracts/sql-interface.md"),
                  "gfs025_profile_reference_sha256": sha256(args.manifest.parent / "ncep-gfs025-profile.reference.csv"),
                  "registered_grid_definitions_sha256": sha256(root / "test/data/grids/definitions.json"),
                  "registered_grid_source_manifest_sha256": sha256(root / "test/data/grids/source-manifest.json"),
                  "domain_profile_reference_sha256": sha256(root / "test/data/domain-manifest.json"),
                  "domain_registry_source_sha256": sha256(root / "src/grid/domain_registry.cpp"),
                  "nearest_sql_profile_sha256": sha256(root / "test/sql/region_sampling.test"),
                  "inherited_region_sql_profile_sha256": sha256(root / "test/sql/regions.test"),
                  "inherited_read_om_sql_sha256": sha256(root / "test/sql/read_om.test"),
                  "inherited_semantic_axes_sql_sha256": sha256(root / "test/sql/semantic_axes.test"),
                  "inherited_region_input_native_sha256": sha256(root / "test/native/region_input_test.cpp"),
                  "spatial_dependency_profile_sha256": sha256(root / "test/sql/region_sampling_spatial_dependency.test"),
                  "runtime_matrix_sha256": sha256(args.runtime_matrix),
              },
              "fixture_check": fixture_check.stdout.strip(),
              "gate_limits": {"N0": "three pinned runtimes execute ordinary read_om/no-spatial regressions and the frozen ordered points rows/stable-metrics replay, with exact nearest spatial signatures checked against the runtime matrix",
                              "N1": "per-profile scope: synthetic regular spherical grids and NCEP GFS025; other registered domains excluded. Frozen capability cases, ten independent scientific vectors and 12 exhaustive GFS025 target scans execute. The 377-case capability inventory has 203 accepted and 174 rejected cases across 17 reachable classes, including finite computed longitude-period overflow. The declared contract requires a frozen representative finite matrix and does not claim arbitrary binary64 or complete target-space exhaustion",
                              "N2": "per-profile scope: synthetic regular spherical grids and NCEP GFS025; other registered domains excluded. Nearest SQL, legacy points, inherited read_om/semantic-axis SQL and shared 005 region-input checks execute on three runtimes. Shared RegionCollection::Build tests reachable count, per-region and aggregate WKB-byte rejection with isolated aggregate-byte precedence, and opaque-geometry allocation; per-region/aggregate vertex-count guards and the owned-input cap are unreachable before stricter WKB/count limits. Four precedence edges are covered. The declared contract records parser permutation and precedence cross-products as finite-sample limitations, not required exhaustive acceptance",
                              "N3": "pass only when the cross-version projection, residual-filter, zero-value-I/O, and same-object full-read matrix succeeds",
                              "N4": "three-version worker/lifecycle checks plus isolated 64x64 and 256x256 process measurements; owned-memory and work-peak growth are checked against the 2x bound, with v1.5.4 sampling sanitizer coverage",
                              "N5": "owner-authorized private real-input archive/retrieval plus current three-version real/synthetic local/HTTP/HTTPS/signed-S3 and denial/recovery checks are required",
                              "N6": "blocked until complete three-version N0-N5 evidence exists",
                              "N7": "blocked pending independent execution evidence"},
              "artifacts": {"duckdb": str(args.duckdb), "extension": str(args.extension),
                            "spatial": str(args.spatial)}}
    write_json(output / "inputs.json", inputs)
    if args.self_check:
        for gate in gates:
            results[gate] = {"status": "not-run", "reason": "--self-check validates arguments and fixture hashes only",
                             "commands": []}
        write_json(output / "manifest.json", {"schema_version": 1, "status": "self-check-only",
                                               "requested_gates": gates, "gates": results})
        (output / "final.md").write_text("# Sampling validation self-check\n\nNo acceptance gates were executed.\n",
                                         encoding="utf-8")
        command_stream.close()
        print(json.dumps({"status": "self-check-only", "output": str(output)}, sort_keys=True))
        return 0

    # Resolve every version from its published build manifest, then verify
    # source and artifact identities before running any acceptance command.
    matrix_data = json.loads(args.runtime_matrix.read_text(encoding="utf-8"))
    runtime_records = {item["version"]: item for item in matrix_data["runtimes"]}
    builds = {runtime["duckdb_version"]: args.build}
    try:
        for version in ("v1.5.4", "v1.5.5", "v1.5.6"):
            if version not in builds:
                build_manifest = args.matrix_output_root / version / "build-manifest.json"
                manifest = json.loads(build_manifest.read_text())
                builds[version] = Path(manifest["release_dir"]).resolve()
        identities = {version: checked_build(root, build, runtime_records[version]["duckdb_source_sha"])
                      for version, build in builds.items()}
        for version, record in runtime_records.items():
            if version not in builds:
                continue
            if record.get("status") != "probe_pass":
                raise ValueError(f"no passing runtime probe for {version}")
            for kind in ("duckdb_cli", "spatial_package"):
                artifact = record[kind]
                if sha256(Path(artifact["path"])) != artifact["sha256"]:
                    raise ValueError(f"{version} {kind} differs from the verified runtime")
        primary_extension = args.build / "extension/duckomo/duckomo.duckdb_extension"
        if args.extension != primary_extension.resolve():
            raise ValueError("--extension must be the extension recorded in --build's sampling manifest")
        if "N4" in gates and any(relative not in identities[runtime["duckdb_version"]]["artifacts"]
                                 for relative in SANITIZERS.values()):
            raise ValueError("N4 requires a current sampling build recorded with --sanitizers")
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        for gate in gates:
            results[gate] = {"status": "blocked", "reason": f"build identity check failed: {error}", "commands": []}
        write_json(output / "manifest.json", {"schema_version": 1, "status": "blocked",
                                               "requested_gates": gates, "gates": results})
        (output / "final.md").write_text(f"# Sampling validation\n\nBlocked: {error}\n")
        command_stream.close()
        print(json.dumps({"status": "blocked", "reason": str(error), "output": str(output)}))
        return 2
    inputs["build_identities"] = identities
    write_json(output / "inputs.json", inputs)
    matrix_builds = [(builds[version], Path(runtime_records[version]["spatial_package"]["path"]))
                     for version in ("v1.5.4", "v1.5.5", "v1.5.6")]
    runner = args.build / "test/unittest"
    native = args.build / "test/native"
    core_extension = args.build / "extension/core_functions/core_functions.duckdb_extension"
    env = os.environ.copy()
    env.update({"DUCKOMO_TEST_EXTENSION": str(args.extension),
                "DUCKOMO_CORE_FUNCTIONS_EXTENSION": str(core_extension),
                "DUCKOMO_SPATIAL_EXTENSION": str(args.spatial),
                "DUCKOMO_SPATIAL_SHA256": runtime["spatial_sha256"]})
    command_ordinal = 0

    def execute(gate: str, argv: list[str], *, extra_env: dict[str, str] | None = None,
                sql_path: Path | None = None) -> bool:
        nonlocal command_ordinal
        command_ordinal += 1
        began = time.monotonic()
        command = run(argv, root, {**env, **(extra_env or {})})
        elapsed = (time.monotonic() - began) * 1000
        filename = f"{gate}-{command_ordinal:02d}.stdout.txt"
        output_path = output / filename
        output_path.write_text(command.stdout, encoding="utf-8")
        record = {"gate": gate, "argv": argv, "cwd": str(root), "exit_code": command.returncode,
                  "executable_sha256": sha256(Path(shutil.which(argv[0]) or argv[0])),
                  "build_manifest_sha256": {version: identity["manifest_sha256"]
                                            for version, identity in identities.items()},
                  "elapsed_ms": round(elapsed, 3), "output_path": filename,
                  "output_sha256": sha256(output_path), "started_at": datetime.now(timezone.utc).isoformat()}
        if sql_path is not None:
            record.update(sql_path=sql_path.name, sql_sha256=sha256(sql_path))
        command_stream.write(json.dumps(record, sort_keys=True) + "\n")
        command_stream.flush()
        results[gate]["commands"].append(record)
        return command.returncode == 0

    def pinned_spatial_sha(version: str, spatial_path: Path) -> str:
        matrix_data = json.loads(args.runtime_matrix.read_text(encoding="utf-8"))
        runtime_record = next((item for item in matrix_data.get("runtimes", [])
                               if item.get("version") == version), None)
        expected_hash = (runtime_record or {}).get("spatial_package", {}).get("sha256")
        actual_hash = sha256(spatial_path)
        if not expected_hash or actual_hash != expected_hash:
            raise ValueError(f"matching spatial runtime hash mismatch for {version}")
        return actual_hash

    requested = set(gates)
    if "N0" in requested:
        checks = ["test/sql/regions.test", "test/sql/regions_spatial_dependency.test",
                  "test/sql/region_sampling_spatial_dependency.test"]
        sql_path = root / "specs/006-region-nearest-sampling/evidence/N0-points.sql"
        expected_rows = root / "specs/006-region-nearest-sampling/evidence/N0-points-rows.csv"
        expected_metrics = root / "specs/006-region-nearest-sampling/evidence/N0-points-metrics.csv"
        n0_dependency_check = root / "test/sql/region_sampling_spatial_dependency.test"
        if not n0_dependency_check.is_file():
            checks = checks[:2]
        expected_signatures = {
            "ST_Covers": {"parameters": ["GEOMETRY", "GEOMETRY"], "return_type": "BOOLEAN"},
            "ST_PointOnSurface": {"parameters": ["GEOMETRY"], "return_type": "GEOMETRY"},
            "ST_X": {"parameters": ["GEOMETRY"], "return_type": "DOUBLE"},
            "ST_Y": {"parameters": ["GEOMETRY"], "return_type": "DOUBLE"},
        }
        matrix_data = json.loads(args.runtime_matrix.read_text(encoding="utf-8"))
        runtime_records = {item.get("version"): item for item in matrix_data.get("runtimes", [])}
        pinned_builds = [
            (version, builds[version], Path(runtime_records[version]["duckdb_cli"]["path"]),
             builds[version] / "extension/duckomo/duckomo.duckdb_extension",
             Path(runtime_records[version]["spatial_package"]["path"]))
            for version in ("v1.5.4", "v1.5.5", "v1.5.6")
        ]
        missing = [str(path) for path in [sql_path, expected_rows, expected_metrics]
                   if not path.is_file()]
        invalid_runtime = []
        for version, build, cli, extension, spatial in pinned_builds:
            required_paths = [build / "test/unittest", cli, extension, spatial,
                              build / "extension/core_functions/core_functions.duckdb_extension"]
            missing.extend(str(path) for path in required_paths if not path.is_file())
            record = runtime_records.get(version, {})
            version_result = run([str(cli), "--version"], root) if cli.is_file() else None
            version_match = re.search(r"v?1\.5\.[456]", version_result.stdout) if version_result else None
            actual_version = version_match.group(0) if version_match else None
            if actual_version and not actual_version.startswith("v"):
                actual_version = "v" + actual_version
            identity_matches = (record.get("status") == "probe_pass" and
                                record.get("exact_function_signatures") == expected_signatures and
                                record.get("geometry_probe", {}).get("covered") is True and
                                record.get("geometry_probe", {}).get("point") == matrix_data.get("expected_point_wkt") and
                                actual_version == version and cli.is_file() and
                                record.get("duckdb_cli", {}).get("sha256") == sha256(cli) and
                                spatial.is_file() and
                                record.get("spatial_package", {}).get("sha256") == sha256(spatial))
            if not identity_matches:
                invalid_runtime.append(version)
        if missing or invalid_runtime:
            reasons = []
            if missing:
                reasons.append(f"required artifacts missing: {missing}")
            if invalid_runtime:
                reasons.append(f"runtime identities or exact nearest signatures did not verify: {invalid_runtime}")
            results["N0"] = {"status": "blocked", "reason": "; ".join(reasons), "commands": []}
        else:
            all_passed = True
            runtime_comparisons = {}
            for version, build, cli, extension, spatial in pinned_builds:
                version_runner = build / "test/unittest"
                version_env = {"DUCKOMO_TEST_EXTENSION": str(extension),
                               "DUCKOMO_CORE_FUNCTIONS_EXTENSION": str(build / "extension/core_functions/core_functions.duckdb_extension"),
                               "DUCKOMO_SPATIAL_EXTENSION": str(spatial)}
                for test in checks:
                    check_passed = execute("N0", [str(version_runner), "--test-dir", str(root), str(root / test)],
                                           extra_env=version_env)
                    all_passed = check_passed and all_passed
                baseline_sql = sql_path.read_text(encoding="utf-8")
                baseline_sql = baseline_sql.replace("build/release/extension/duckomo/duckomo.duckdb_extension",
                                                    str(extension).replace("'", "''"))
                baseline_sql = baseline_sql.replace(
                    "build/regions-runtime/v1.5.4/official/spatial.duckdb_extension",
                    str(spatial).replace("'", "''"))
                spatial_load = f"LOAD '{str(spatial).replace(chr(39), chr(39) * 2)}';"
                if "SET threads=" not in baseline_sql:
                    baseline_sql = baseline_sql.replace(
                        spatial_load, spatial_load + "\n-- N0 capture runtime settings\nSET threads=9;\nSET duckomo_max_threads=9;", 1)
                rows_path = output / f"N0-points-rows-{version}.csv"
                metrics_path = output / f"N0-points-metrics-{version}.csv"
                baseline_sql = baseline_sql.replace("specs/006-region-nearest-sampling/evidence/N0-points-rows.csv", str(rows_path))
                baseline_sql = baseline_sql.replace("specs/006-region-nearest-sampling/evidence/N0-points-metrics.csv", str(metrics_path))
                executable_sql = output / f"N0-points-{version}.sql"
                executable_sql.write_text(baseline_sql, encoding="utf-8")
                replay_passed = execute("N0", [str(cli), "-unsigned", "-f", str(executable_sql)], sql_path=executable_sql)
                if not replay_passed or not rows_path.is_file() or not metrics_path.is_file():
                    all_passed = False
                    runtime_comparisons[version] = {"rows_match": False, "stable_metrics_match": False,
                                                    "reason": "points replay did not produce both output artifacts"}
                    continue
                rows_match = sha256(rows_path) == sha256(expected_rows)
                current_metrics = next(csv.DictReader(metrics_path.open(encoding="utf-8")))["metrics"]
                expected_metrics_value = next(csv.DictReader(expected_metrics.open(encoding="utf-8")))["metrics"]
                metrics_match = points_metrics_match(canonical_points_metrics(expected_metrics_value),
                                                    canonical_points_metrics(current_metrics))
                all_passed = rows_match and metrics_match and all_passed
                runtime_comparisons[version] = {
                    "rows_match": rows_match, "stable_metrics_match": metrics_match,
                    "rows_path": rows_path.name, "metrics_path": metrics_path.name,
                    "expected_rows_sha256": sha256(expected_rows), "actual_rows_sha256": sha256(rows_path),
                    "expected_metrics_sha256": sha256(expected_metrics), "actual_metrics_sha256": sha256(metrics_path),
                    "duckdb_cli_sha256": sha256(cli), "extension_sha256": sha256(extension),
                    "spatial_sha256": sha256(spatial),
                }
            results["N0"] = {
                "status": "pass" if all_passed else "fail",
                "reason": ("three pinned runtime signatures, no-spatial/points regressions, frozen ordered rows and logical metrics match; physical index reuse costs do not exceed the frozen baseline"
                           if all_passed else "one or more pinned N0 runtime regressions or baseline replays failed"),
                "runtime_comparisons": runtime_comparisons,
                "commands": results["N0"]["commands"],
            }
    if "N1" in requested:
        reviewed_profiles = {
            "locator_reference_sha256": "ff1e5bc3aae2acbf6d76354521b0feb07e462c1852cdd8b23b8c02f01afb2ca1",
            "scientific_reference_sha256": "bd1070959ba1a316f8e274c4d160bd693a49b1e6dd6b5f556105f2f27b7d9f33",
            "capability_boundary_reference_sha256": "8b1d16d3c661d0d8d413b0305527eda44623cbe4f787b4cab5b29e4e5f73ece0",
            "gfs025_profile_reference_sha256": "99453febba1000b3bf141982b7c09c7a703956d147063e61ebbaa466c6bca231",
        }
        profile_paths = {
            "locator_reference_sha256": args.manifest.parent / "locator.reference.csv",
            "scientific_reference_sha256": args.manifest.parent / "scientific.reference.csv",
            "capability_boundary_reference_sha256": args.manifest.parent / "capability-boundaries.reference.csv",
            "gfs025_profile_reference_sha256": args.manifest.parent / "ncep-gfs025-profile.reference.csv",
        }
        profile_drift = {name: {"expected": expected, "actual": sha256(profile_paths[name])}
                         for name, expected in reviewed_profiles.items()
                         if not profile_paths[name].is_file() or sha256(profile_paths[name]) != expected}
        if profile_drift:
            results["N1"] = {"status": "blocked", "reason": "reviewed N1 oracle input identities differ from the locked package",
                             "oracle_drift": profile_drift, "commands": []}
        versioned_builds = [(builds[version], builds[version] / "test/native/nearest_sampling_test",
                             Path(runtime_records[version]["spatial_package"]["path"]), version)
                            for version in ("v1.5.4", "v1.5.5", "v1.5.6")]
        layout_test = native / "spatial_layout_test"
        if results["N1"]["status"] == "blocked":
            pass
        elif not layout_test.is_file() or any(not binary.is_file() or not spatial.is_file() or
                                               not (build / "test/native/region_geometry_test").is_file()
                                             for build, binary, spatial, _ in versioned_builds):
            results["N1"] = {"status": "blocked", "reason": "locator/layout/geometry executable or one pinned runtime artifact is missing",
                             "commands": []}
        else:
            passed = True
            for build, binary, spatial, version in versioned_builds:
                passed = execute("N1", [str(binary)]) and passed
                passed = execute("N1", [str(build / "test/native/region_geometry_test"), str(spatial),
                                          pinned_spatial_sha(version, spatial)]) and passed
            passed = execute("N1", [str(layout_test)]) and passed
            results["N1"] = {"status": "pass" if passed and not profile_drift else "fail" if not passed else "blocked",
                             "reason": ("declared acceptance scope passed: synthetic regular spherical grids and NCEP GFS025; other registered domains are excluded. Locked finite capability cases, ten scientific vectors using independent 90-digit Decimal unit-vector cross/dot distance, 12 exhaustive full-grid GFS025 target profiles, spatial adapter signatures, representative point, source-point mapping and layout checks passed on all three pinned builds. The reviewed oracle derivation and all reachable rejection classes are mapped. Periodic closure is independent of the regional first-to-last endpoint span. This finite acceptance matrix does not claim arbitrary binary64 or complete target-space coverage"
                                        if passed and not profile_drift else "one or more locator/layout/geometry commands failed" if not passed else "reviewed N1 oracle input identities differ from the locked package"),
                             "commands": results["N1"]["commands"]}
    if "N2" in requested:
        checks = ["test/sql/region_sampling.test", "test/sql/regions.test",
                  "test/sql/read_om.test", "test/sql/semantic_axes.test",
                  "test/sql/regions_spatial_dependency.test"]
        native_input_test = "test/native/region_input_test.cpp"
        native_input_binary = "test/native/region_input_test"
        dependency_test = root / "test/sql/region_sampling_spatial_dependency.test"
        if dependency_test.is_file():
            checks.append("test/sql/region_sampling_spatial_dependency.test")
        required = [(build / "test/unittest", spatial) for build, spatial in matrix_builds]
        required.extend((build / "test/native/region_sampling_test", spatial) for build, spatial in matrix_builds)
        required.extend((build / native_input_binary, spatial) for build, spatial in matrix_builds)
        missing = [(str(path), str(spatial)) for path, spatial in required if not path.is_file() or not spatial.is_file()]
        missing_tests = [test for test in checks if not (root / test).is_file()]
        if missing_tests or not (root / native_input_test).is_file():
            missing.append((f"missing SQL/native input-contract sources: {missing_tests}, {native_input_test}", ""))
        if missing:
            results["N2"] = {"status": "blocked", "reason": f"nearest SQL/native matrix artifacts are missing: {missing}",
                             "commands": []}
        else:
            passed = True
            for build, spatial in matrix_builds:
                runner = build / "test/unittest"
                for test in checks:
                    passed = execute("N2", [str(runner), "--test-dir", str(root), str(root / test)],
                                     extra_env={"DUCKOMO_TEST_EXTENSION": str(build / "extension/duckomo/duckomo.duckdb_extension"),
                                                "DUCKOMO_CORE_FUNCTIONS_EXTENSION": str(build / "extension/core_functions/core_functions.duckdb_extension"),
                                                "DUCKOMO_SPATIAL_EXTENSION": str(spatial)}) and passed
                native_scan = build / "test/native/region_sampling_test"
                passed = execute("N2", [str(native_scan), str(spatial)]) and passed
                inherited_input = build / native_input_binary
                passed = execute("N2", [str(inherited_input)]) and passed
            results["N2"] = {"status": "pass" if passed else "fail",
                             "reason": ("declared acceptance scope passed: synthetic regular spherical grids and NCEP GFS025; other registered domains are excluded. Nearest SQL/value/source/state/options/layout/rebind, legacy points compatibility, inherited read_om/semantic-axis SQL, native official-reader parity, and inherited 005 region-input/limit checks passed on all three runtimes. Nearest SQL covers bind behavior, schema/name conflicts, states, residual filters and strict failures. Combined-invalid probes cover four binder-precedence edges: method options before required region arguments, region presence before CRS validation, CRS validation before collection parsing, and collection parsing before later grid/domain binding. Shared RegionCollection::Build native checks cover reachable count, per-region WKB rejection, aggregate WKB rejection isolated from vertex/owned-memory limits with a specific diagnostic, opaque-geometry allocation and WKB-form limits; per-region/aggregate vertex-count guards and the owned-input cap are unreachable under stricter WKB/count limits. Parser permutations and combined-invalid precedence are finite samples; exhaustive cross-products are outside the declared acceptance contract"
                                        if passed else "one or more nearest SQL/native/inherited region-input commands failed"),
                             "commands": results["N2"]["commands"]}
    if "N3" in requested or "N4" in requested:
        missing = [(str(build / "test/native/region_sampling_test"), str(spatial))
                   for build, spatial in matrix_builds
                   if not (build / "test/native/region_sampling_test").is_file() or not spatial.is_file()]
        if missing:
            if "N3" in requested:
                results["N3"] = {"status": "blocked", "reason": f"projection/I/O native runtime artifacts are missing: {missing}", "commands": []}
            if "N4" in requested:
                results["N4"] = {"status": "blocked", "reason": f"sampling resource native runtime artifacts are missing: {missing}", "commands": []}
        else:
            n3_passed = True
            n4_passed = True
            n3_commands = []
            n4_commands = []
            memory_measurements = {}
            for build, spatial in matrix_builds:
                binary = build / "test/native/region_sampling_test"
                if "N3" in requested:
                    n3_passed = execute("N3", [str(binary), str(spatial)]) and n3_passed
                    n3_commands.append(results["N3"]["commands"][-1])
                    sql_runner = build / "test/unittest"
                    sql_env = {"DUCKOMO_TEST_EXTENSION": str(build / "extension/duckomo/duckomo.duckdb_extension"),
                               "DUCKOMO_CORE_FUNCTIONS_EXTENSION": str(build / "extension/core_functions/core_functions.duckdb_extension"),
                               "DUCKOMO_SPATIAL_EXTENSION": str(spatial)}
                    n3_passed = execute("N3", [str(sql_runner), "--test-dir", str(root),
                                                   str(root / "test/sql/region_sampling.test")],
                                          extra_env=sql_env) and n3_passed
                    n3_commands.append(results["N3"]["commands"][-1])
                if "N4" in requested:
                    native_passed = execute("N4", [str(binary), str(spatial)])
                    n4_passed = native_passed and n4_passed
                    n4_commands.append(results["N4"]["commands"][-1])
                    version = next(version for version, path in builds.items() if path == build)
                    memory_measurements[version] = {}
                    for dimension in (64, 256):
                        isolated_passed = execute(
                            "N4", [str(binary), str(spatial), f"--memory-dimension={dimension}"])
                        record = results["N4"]["commands"][-1]
                        n4_commands.append(record)
                        if not isolated_passed:
                            n4_passed = False
                            continue
                        measurement_output = (output / record["output_path"]).read_text(encoding="utf-8")
                        try:
                            memory_measurements[version][str(dimension)] = parse_memory_measurement(
                                measurement_output, dimension)
                        except ValueError as error:
                            n4_passed = False
                            memory_measurements[version][str(dimension)] = {"error": str(error)}
                    if set(memory_measurements[version]) == {"64", "256"}:
                        small = memory_measurements[version]["64"]
                        large = memory_measurements[version]["256"]
                        if ("error" not in small and "error" not in large and
                                large["query_owned_upper_bound_bytes"] > 2 * small["query_owned_upper_bound_bytes"]):
                            n4_passed = False
                            memory_measurements[version]["owned_growth_ratio_exceeded"] = True
                        if ("error" not in small and "error" not in large and
                                large["work_peak_bytes"] > 2 * small["work_peak_bytes"]):
                            n4_passed = False
                            memory_measurements[version]["work_growth_ratio_exceeded"] = True
            if "N3" in requested:
                outcome = local_gate_outcome(
                    n3_passed,
                    "cross-version SQL/native projection, residual-filter, geometry/count zero-value-I/O, and same-object full-read acceptance passed")
                outcome["commands"] = n3_commands
                results["N3"] = outcome
            if "N4" in requested:
                sanitizer_dir = args.build / "extension/duckomo/test"
                sanitizer_test = sanitizer_dir / "CTestTestfile.cmake"
                sanitizer_cache = args.build / "CMakeCache.txt"
                sanitizer_registered = sanitizer_registration_matches(
                    sanitizer_test, sanitizer_cache, args.spatial, runtime["spatial_sha256"])
                if not sanitizer_registered:
                    n4_passed = False
                else:
                    sanitizer_passed = execute("N4", ["ctest", "--test-dir", str(sanitizer_dir),
                                                       "--output-on-failure", "-R", "^duckomo_sanitizer_region_sampling_test$"])
                    n4_passed = sanitizer_passed and n4_passed
                    n4_commands.append(results["N4"]["commands"][-1])
                outcome = local_gate_outcome(
                    n4_passed,
                    "three-version worker/task/lifecycle matrix, separate-process 64x64 versus 256x256 memory scaling, and matching-spatial v1.5.4 sampling ASan/UBSan passed")
                outcome["isolated_memory_measurements"] = memory_measurements
                if not sanitizer_registered:
                    outcome = {"status": "blocked", "reason": "sampling ASan/UBSan registration is missing or bound to a different spatial package",
                               "local_checks_passed": False,
                               "blocking_scope": ["configure and register duckomo_sanitizer_region_sampling_test with the selected spatial path and SHA-256"]}
                outcome["commands"] = n4_commands
                results["N4"] = outcome
    from sampling_remote import run_n5, audit_n6
    if "N5" in requested:
        try:
            results["N5"] = run_n5(args, root, builds, runtime_records, output)
        except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
            results["N5"] = {"status": "fail", "reason": str(error), "commands": []}
    if "N6" in requested:
        try:
            if args.evidence_root is None:
                raise ValueError("N6 requires --evidence-root pointing to a completed N0–N5 package")
            results["N6"] = audit_n6(args.evidence_root, next(iter(identities.values()))["extension_source_sha256"], identities, runtime_records)
        except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
            results["N6"] = {"status": "blocked", "reason": str(error), "commands": []}
    blocker_reasons = {
        "N3": "blocked pending SQL projection, metrics matrix, and complete cross-version audit",
        "N4": "blocked pending lifecycle completeness, overflow boundary, and independent gate audit",
        "N5": "real-data and controlled-transport subcheck evidence is external to this validator; the registered complete N5 gate package, raw-source authorization and durable archive are missing",
        "N6": "N0–N5 complete evidence packages are not available for cross-version audit",
        "N7": "independent execution evidence has not been supplied",
    }
    for gate in gates:
        if gate in {"N0", "N1", "N2"} and results[gate]["status"] == "not-run":
            results[gate] = {"status": "not-run", "reason": "requested gate was not executed because its required tests were unavailable",
                             "commands": []}
        elif gate in blocker_reasons and results[gate]["status"] == "not-run":
            results[gate] = {"status": "blocked", "reason": blocker_reasons[gate], "commands": []}
    command_stream.close()

    statuses = [results[gate]["status"] for gate in gates]
    overall = "pass" if statuses and all(status == "pass" for status in statuses) else \
              "fail" if "fail" in statuses else "blocked"
    write_json(output / "manifest.json", {"schema_version": 1, "status": overall,
                                           "requested_gates": gates, "runtime": runtime,
                                           "inputs_path": "inputs.json", "inputs_sha256": sha256(output / "inputs.json"),
                                           "commands_path": "commands.jsonl", "commands_sha256": sha256(commands_file),
                                           "gates": results})
    lines = ["# Sampling validation run", "", f"Status: **{overall}**", "",
             f"DuckDB: {runtime['duckdb_version']}", f"Output: `{output}`", "", "## Gates", ""]
    for gate in gates:
        item = results[gate]
        lines.append(f"- **{gate}: {item['status']}** — {item['reason']}")
        for command in item["commands"]:
            lines.append(f"  - exit {command['exit_code']}: `{shlex.join(command.get('argv', [command.get('sql_path', command.get('label', 'audit'))]))}`; output SHA-256 `{command['output_sha256']}`")
    (output / "final.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps({"status": overall, "output": str(output),
                      "gates": {gate: results[gate]["status"] for gate in gates}}, sort_keys=True))
    return 0 if overall == "pass" else 1 if overall == "fail" else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"validate-sampling: {error}", file=sys.stderr)
        raise SystemExit(2)
