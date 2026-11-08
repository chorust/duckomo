#!/usr/bin/env python3
"""Re-run Feature 010 spatial and slotless-presence probes on linux_arm64 pairs."""
import hashlib
import json
import platform
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
OUT = Path(__file__).resolve().parent
ARTIFACTS = json.loads((OUT / "build-artifacts.json").read_text())
BUILD_ROOT = ROOT / "build/series-fixtures"
PRESENT = BUILD_ROOT / "data/synthetic/temperature"
ALL_MISSING = BUILD_ROOT / "data/gaps/all-missing/temperature"
FIRST_MISSING = BUILD_ROOT / "data/gaps/first-missing/temperature"
MISMATCH_TIME_EXTENT = BUILD_ROOT / "data/mismatch/time-extent/temperature"

GRID = "grid := {'nx':3,'ny':2,'lat0':10.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'lon_fastest'}, spatial_axes := ['point']"
INVALID_GRID = "grid := 42, spatial_axes := ['point']"
INVALID_AXES = "grid := {'nx':3,'ny':2,'lat0':10.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'lon_fastest'}, spatial_axes := ['not_an_axis']"
UNKNOWN_DOMAIN = "domain := 'review_nonexistent_domain'"
DIMS = "dimensions := ['lead','point','time']"
WITH_SLOTS = "start := TIMESTAMP '1970-01-01', \"end\" := TIMESTAMP '1970-01-01 01:00:00'"
SLOTLESS = "start := TIMESTAMP '1970-01-01 00:00:00.000001', \"end\" := TIMESTAMP '1970-01-01 00:59:59.999999'"


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sql_literal(value):
    return "'" + str(value).replace("'", "''") + "'"


def make_query(prefix, spatial, temporal, extra=""):
    return (
        "SELECT count(*) FROM read_om_series("
        f"{sql_literal(prefix)}, chunk_time_length := 4, time_step := INTERVAL '1 hour', "
        f"{DIMS}, {spatial}, {temporal}{extra});"
    )


def build_cases():
    cases = []
    declarations = (
        ("invalid_grid", INVALID_GRID, "grid must be a named STRUCT"),
        ("unknown_domain", UNKNOWN_DOMAIN, "unknown or not verified"),
        ("invalid_axes", INVALID_AXES, "not present in declared dimensions"),
    )
    scopes = (
        ("present_slots", PRESENT, WITH_SLOTS),
        ("all_missing_slots", ALL_MISSING, WITH_SLOTS),
        ("present_slotless", PRESENT, SLOTLESS),
        ("all_missing_slotless", ALL_MISSING, SLOTLESS),
    )
    for decl_name, declaration, diagnostic in declarations:
        for scope_name, prefix, temporal in scopes:
            cases.append({
                "name": f"{decl_name}_{scope_name}",
                "sql": make_query(prefix, declaration, temporal),
                "expected_exit_code": 1,
                "expected_diagnostic": diagnostic,
                "expected_fixture_opens": 0,
            })
    cases.extend((
        {
            "name": "strict_missing_slotless",
            "sql": make_query(ALL_MISSING, GRID, SLOTLESS, ", strict := true"),
            "expected_exit_code": 1,
            "expected_diagnostic": "could not open local OM file",
            "expected_fixture_opens_min": 1,
        },
        {
            "name": "strict_missing_with_slots_control",
            "sql": make_query(ALL_MISSING, GRID, WITH_SLOTS, ", strict := true"),
            "expected_exit_code": 1,
            "expected_diagnostic": "could not open local OM file",
            "expected_fixture_opens_min": 1,
        },
        {
            "name": "strict_present_slotless",
            "sql": make_query(PRESENT, GRID, SLOTLESS, ", strict := true")
                  + "\nSELECT json_extract_string(metrics, '$.reads.value_totals.data_requests') AS data_requests, "
                    "json_extract_string(metrics, '$.reads.value_totals.data_bytes') AS data_bytes, "
                    "json_extract_string(metrics, '$.reads.value_totals.decoded_chunks') AS decoded_chunks "
                    "FROM duckomo_last_scan_metrics();",
            "expected_exit_code": 0,
            "expected_stdout_fragments": [
                '"count_star()":0', '"data_requests":"0"', '"data_bytes":"0"', '"decoded_chunks":"0"'
            ],
            "expected_fixture_opens_min": 1,
        },
        {
            "name": "strict_present_slotless_metadata_mismatch",
            "sql": make_query(
                MISMATCH_TIME_EXTENT,
                GRID,
                """start := TIMESTAMP '1970-01-01 04:00:00.000001', "end" := TIMESTAMP '1970-01-01 04:59:59.999999'""",
                ", strict := true",
            ),
            "expected_exit_code": 1,
            "expected_diagnostic": "child array declaration or time extent differs",
            "expected_fixture_opens_min": 1,
            "expected_fixture_path": "chunk_0001.om",
        },
        {
            "name": "strict_cross_chunk_slotless_first",
            "sql": make_query(
                FIRST_MISSING,
                GRID,
                "start := TIMESTAMP '1970-01-01 03:00:00.000001', \"end\" := TIMESTAMP '1970-01-01 05:00:00'",
                ", strict := true",
            ),
            "expected_exit_code": 1,
            "expected_diagnostic": "chunk_0000.om",
            "expected_fixture_opens_min": 1,
            "expected_absent_from_trace": "chunk_0001.om",
        },
    ))
    return cases


def main():
    if platform.machine() != "aarch64":
        raise SystemExit(f"linux_arm64 evidence requires aarch64 host, got {platform.machine()}")
    strace = shutil.which("strace")
    if strace is None:
        raise SystemExit("strace is required to retain zero-object-access traces")
    if not (PRESENT / "chunk_0000.om").is_file() or not ALL_MISSING.is_dir():
        raise SystemExit("series fixture preconditions are not satisfied")
    if (ALL_MISSING / "chunk_0000.om").exists():
        raise SystemExit("all-missing fixture unexpectedly contains chunk_0000.om")

    results = {
        "platform": platform.machine(),
        "purpose": "Review-fix probes only; these results do not upgrade acceptance gates",
        "cases_per_pair": len(build_cases()),
        "pairs": [],
    }
    for artifact_pair in ARTIFACTS["pairs"]:
        pair = artifact_pair["pair"]
        cli = artifact_pair["cli"]
        extension = artifact_pair["review_fix_extension"]
        if sha(cli["path"]) != cli["sha256"] or sha(extension["path"]) != extension["sha256"]:
            raise SystemExit(f"artifact SHA mismatch for {pair}")
        pair_record = {
            "pair": pair,
            "engine_version": artifact_pair["engine_version"],
            "engine_pin": artifact_pair["engine_pin"],
            "verified_artifacts": [
                {"kind": "official_cli", **cli},
                {"kind": "review_fix_extension", **extension},
            ],
            "cases": [],
        }
        for case in build_cases():
            sql_text = f"LOAD {sql_literal(extension['path'])};\n{case['sql']}\n"
            sql_path = OUT / "queries" / f"{pair}-{case['name']}.sql"
            sql_path.parent.mkdir(parents=True, exist_ok=True)
            sql_path.write_text(sql_text)
            trace_path = OUT / "traces" / f"{pair}-{case['name']}.strace"
            trace_path.parent.mkdir(parents=True, exist_ok=True)
            command = [
                strace, "-f", "-qq", "-e", "trace=openat,openat2", "-o", str(trace_path), "--",
                cli["path"], "-no-init", "-unsigned", "-bail", "-json", ":memory:",
            ]
            completed = subprocess.run(command, input=sql_text, text=True, capture_output=True,
                                       cwd=ROOT, timeout=30)
            trace_lines = trace_path.read_text().splitlines() if trace_path.exists() else []
            fixture_events = [line for line in trace_lines if "series-fixtures/" in line and "chunk_" in line]
            diagnostic = case.get("expected_diagnostic")
            passes = completed.returncode == case["expected_exit_code"]
            if diagnostic is not None:
                passes = passes and diagnostic.lower() in completed.stderr.lower()
            expected_fragments = case.get("expected_stdout_fragments", [])
            if expected_fragments:
                passes = passes and all(fragment in completed.stdout for fragment in expected_fragments)
            expected_min = case.get("expected_fixture_opens_min")
            if expected_min is not None:
                passes = passes and len(fixture_events) >= expected_min
            expected_path = case.get("expected_fixture_path")
            if expected_path is not None:
                passes = passes and any(expected_path in line for line in fixture_events)
            absent = case.get("expected_absent_from_trace")
            if absent is not None:
                passes = passes and not any(absent in line for line in fixture_events)
            record = {
                **case,
                "sql_file": str(sql_path.relative_to(OUT)),
                "command": command,
                "exit_code": completed.returncode,
                "stdout": completed.stdout,
                "stderr": completed.stderr,
                "fixture_open_events": fixture_events,
                "trace_file": str(trace_path.relative_to(OUT)),
                "probe_pass": passes,
            }
            pair_record["cases"].append(record)
            print(pair, case["name"], "PASS" if passes else "FAIL",
                  "exit=", completed.returncode, "fixture_events=", len(fixture_events))
        results["pairs"].append(pair_record)
        (OUT / "probe-results.json").write_text(json.dumps(results, indent=2) + "\n")
    all_pass = all(case["probe_pass"] for pair in results["pairs"] for case in pair["cases"])
    results["all_probes_pass"] = all_pass
    (OUT / "probe-results.json").write_text(json.dumps(results, indent=2) + "\n")
    if not all_pass:
        raise SystemExit("one or more review-fix probes failed; see probe-results.json")


if __name__ == "__main__":
    main()
