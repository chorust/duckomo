#!/usr/bin/env python3
"""Auditable local validation of frozen real O1280 OM objects (not a full H gate).

Requires matching locally built CLI/extension/official fixture tool and the two
already downloaded objects. No network, GRIB conversion or historical rewrites.
Large outputs stay in the requested NEW directory; freeze.json precedes execution.
Producer arithmetic is an implementation-independent Python reference, NOT an
independent original-GRIB scanning/order reference. Remote/H9 gates stay not-run.
"""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import importlib.util
import itertools
import json
import math
import platform
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
POINTS = 6599680
TIMES = 504
STATIC = ROOT / "build/s3-samples/openmeteo-v3/ecmwf-ifs-hsurf-o1280.om"
SERIES = ROOT / "build/s3-samples/openmeteo-v3/ecmwf-ifs-temperature-2m-chunk-817.om"
EXPECTED = {
    STATIC: "2e8279f8bd12052ba5be5a0bcba2293dc4316097e434d6691f9e6711249e1fd3",
    SERIES: "483dd0be2096d3e3fff6731509400f97591ebcfc237a941a37460f42a7a10e7f",
}
FIELDS = ("om_source.logical_index AS logical_index, om_source.point_index AS point_index, "
          "om_source.parent_point_index AS parent_point_index, "
          "om_source.axis_indices[1] AS axis0, om_source.axis_indices[2] AS axis1, lat, lon, value")
START = dt.datetime(1970, 1, 1) + dt.timedelta(hours=817 * TIMES)


def require(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def sha(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def dump(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n")


def quote(value: object) -> str:
    return "'" + str(value).replace("'", "''") + "'"


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def producer_coordinate(point: int) -> tuple[float, float]:
    """Pinned Swift getPos inverse + getCoordinates; do not read registry rows."""
    index = point if point < POINTS // 2 else POINTS - point - 1
    row = int(f32(f32(f32(math.sqrt(f32(f32(2 * f32(index)) + 81))) - 9) / 2))
    y = row if point < POINTS // 2 else 2559 - row
    begin = 2 * y * y + 18 * y if y < 1280 else POINTS - (2 * (2560 - y) ** 2 + 18 * (2560 - y))
    nx = 20 + 4 * min(y, 2559 - y)
    dy = f32(180 / f32(2560.5))
    lat = f32(f32(f32(1279 - y) * dy) + f32(dy / 2))
    lon = f32(f32(point - begin) * f32(360 / nx))
    return lat, lon - 360 if lon >= 180 else lon


def value_bits(text: str) -> bytes | None:
    if text in ("", "NULL"):
        return None
    value = float(text)
    return None if math.isnan(value) else struct.pack("<f", value)


def compare_static(actual: Path, oracle: Path) -> dict:
    count = nulls = 0
    max_lat = max_lon = 0.0
    with actual.open(newline="") as a, oracle.open(newline="") as b:
        for index, pair in enumerate(itertools.zip_longest(csv.DictReader(a), csv.DictReader(b))):
            row, ref = pair
            require(row is not None and ref is not None, f"static length mismatch at {index}")
            require(int(ref["index"]) == index, f"official index mismatch at {index}")
            for key in ("logical_index", "point_index", "parent_point_index", "axis1"):
                require(int(row[key]) == index, f"static {key} mismatch at {index}")
            require(int(row["axis0"]) == 0, f"static axis0 mismatch at {index}")
            lat, lon = producer_coordinate(index)
            actual_lat, actual_lon = float(row["lat"]), float(row["lon"])
            require(math.isfinite(actual_lat) and math.isfinite(actual_lon), f"non-finite coordinate at {index}")
            max_lat = max(max_lat, abs(actual_lat - lat))
            max_lon = max(max_lon, abs(actual_lon - lon))
            require(max_lat <= 1e-4 and max_lon <= 1e-4, f"coordinate mismatch at {index}")
            require(value_bits(row["value"]) == value_bits(ref["value"]), f"static value mismatch at {index}")
            nulls += value_bits(row["value"]) is None
            count += 1
    require(count == POINTS, f"static count {count} != {POINTS}")
    return {"status": "pass", "positions": count, "nulls": nulls, "value_bit_mismatches": 0,
            "max_lat_error_degrees": max_lat, "max_lon_error_degrees": max_lon,
            "coordinate_oracle_scope": "Python port of pinned producer, not original GRIB scanning reference"}


def compare_time(actual: Path, oracle: Path, first_point: int, points: int) -> dict:
    with oracle.open(newline="") as source:
        references = list(csv.DictReader(source))
    require(len(references) == points * TIMES, "official time oracle has unexpected length")
    count = nulls = 0
    seen = set()
    with actual.open(newline="") as source:
        for row in csv.DictReader(source):
            point, t = int(row["point_index"]), int(row["axis2"])
            local_index = (point - first_point) * TIMES + t
            require(0 <= local_index < len(references) and 0 <= t < TIMES, "time position outside oracle")
            require(local_index not in seen, "duplicate time position")
            seen.add(local_index)
            require(int(references[local_index]["index"]) == local_index, "official slice-local index differs")
            require(int(row["logical_index"]) == point * TIMES + t and
                    int(row["parent_point_index"]) == point and int(row["axis0"]) == 0 and
                    int(row["axis1"]) == point, "time source position differs")
            require(dt.datetime.fromisoformat(row["valid_time"]) == START + dt.timedelta(hours=t),
                    "source-derived time labeling differs (not independent meteorological provenance)")
            require((float(row["lat"]), float(row["lon"])) == producer_coordinate(point),
                    "time coordinate differs from pinned producer")
            require(value_bits(row["value"]) == value_bits(references[local_index]["value"]),
                    f"time value mismatch at point={point}, t={t}")
            nulls += value_bits(row["value"]) is None
            count += 1
    require(count > 0, "time query returned no records")
    return {"status": "pass", "positions": count, "nulls": nulls, "finite_values": count - nulls,
            "value_bit_mismatches": 0, "time_scope": "explicit labeling from pinned 504-hour chunk rule"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/release")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    require(not out.exists(), "output must be a NEW directory; historical runs are immutable")
    build = args.build.resolve()
    cli = build / "duckdb"
    extension = build / "extension/duckomo/duckomo.duckdb_extension"
    tool = build / "test/tools/duckomo_fixture_tool"
    for path in (cli, extension, tool, *EXPECTED):
        require(path.is_file(), f"missing input: {path}")
    for path, expected in EXPECTED.items():
        require(sha(path) == expected, f"frozen object hash differs: {path}")
    spec = importlib.util.spec_from_file_location("registry_sql", ROOT / "scripts/generate-grid-registry.py")
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    definitions = json.loads((ROOT / "test/data/grids/definitions.json").read_text())["definitions"]
    by_id = {item["id"]: item for item in definitions}
    grid = generator.sql_literal(generator.sql_grid_definition(by_id["ecmwf_ifs"], by_id))
    static_domain = f"read_om({quote(STATIC)}, domain := 'ecmwf_ifs', include_source := true)"
    static_explicit = f"read_om({quote(STATIC)}, grid := {grid}, spatial_axes := ['lon'], include_source := true)"
    axes = f"{{'time': {{'axis': 'time', 'start': TIMESTAMP {quote(START)}, 'step': INTERVAL '1 hour'}}}}"
    time_domain = (f"read_om({quote(SERIES)}, dimensions := ['y','point','time'], "
                   f"domain := 'ecmwf_ifs', axes := {axes}, include_source := true)")
    time_explicit = (f"read_om({quote(SERIES)}, dimensions := ['y','point','time'], grid := {grid}, "
                     f"spatial_axes := ['point'], axes := {axes}, include_source := true)")
    time_fields = FIELDS.replace(", lat, lon, value", ", om_source.axis_indices[3] AS axis2, valid_time, lat, lon, value")
    queries = {"static-domain": f"SELECT {FIELDS} FROM {static_domain}",
               "static-explicit": f"SELECT {FIELDS} FROM {static_explicit}",
               "static-zero-value": f"SELECT count(*), sum(om_source.point_index), min(lat), max(lat) FROM {static_domain}",
               "grid-info": f"SELECT * FROM om_grid_info({quote(STATIC)}, domain := 'ecmwf_ifs')"}
    predicates = {
        "polar": "lat > 89.9", "equator": "lat BETWEEN -0.1 AND 0.1 AND lon BETWEEN -1 AND 1",
        "seam": "lat BETWEEN -80 AND 80 AND (lon >= 170 OR lon < -170)", "empty": "lat > 90",
    }
    for name, predicate in predicates.items():
        queries["static-" + name] = f"SELECT {FIELDS} FROM {static_domain} WHERE {predicate}"
    time_cases = {"polar": ("lat > 89.9", 0, 20),
                  "equator": ("lat BETWEEN -0.04 AND 0 AND lon BETWEEN -0.001 AND 0.001", POINTS // 2, 1),
                  "last": ("lat < -89.9 AND lon BETWEEN -18.001 AND -17.999", POINTS - 1, 1)}
    for name, (predicate, _, _) in time_cases.items():
        queries["time-" + name] = f"SELECT {time_fields} FROM {time_domain} WHERE {predicate}"
    queries["time-explicit"] = f"SELECT {time_fields} FROM {time_explicit} WHERE lat > 89.9"
    queries["time-filter"] = (queries["time-polar"] +
        f" AND valid_time >= TIMESTAMP {quote(START + dt.timedelta(hours=1))} "
        f"AND valid_time < TIMESTAMP {quote(START + dt.timedelta(hours=4))}")
    out.mkdir(parents=True)
    prefix = f"LOAD {quote(extension)}; SET threads=1; SET preserve_insertion_order=true;\n"
    cli_command = [str(cli), "-no-init", "-unsigned", "-bail", "-batch", ":memory:"]
    planned = []
    for name, sql in queries.items():
        result = out / (name + ".csv")
        text = prefix + f"COPY ({sql}) TO {quote(result)} (HEADER, NULL 'NULL');\n"
        text += f"COPY (SELECT * FROM duckomo_last_scan_metrics()) TO {quote(out / (name + '.metrics.csv'))} (HEADER);\n"
        (out / (name + ".sql")).write_text(text)
        planned.append({"name": name, "sql": name + ".sql", "sql_sha256": sha(out / (name + ".sql")), "command": cli_command})
    for name, predicate in predicates.items():
        actual = f"SELECT * FROM read_csv({quote(out / ('static-' + name + '.csv'))}, all_varchar=true)"
        cast_predicate = predicate.replace("lat", "lat::DOUBLE").replace("lon", "lon::DOUBLE")
        expected = f"SELECT * FROM read_csv({quote(out / 'static-domain.csv')}, all_varchar=true) WHERE {cast_predicate}"
        sql = (f"COPY (SELECT count(*) AS mismatches FROM (({actual}) EXCEPT ALL ({expected})) "
               f"UNION ALL SELECT count(*) FROM (({expected}) EXCEPT ALL ({actual}))) "
               f"TO {quote(out / (name + '-difference.csv'))} (HEADER);\n")
        path = out / (name + "-difference.sql")
        path.write_text(sql)
        planned.append({"name": name + "-difference", "sql": path.name, "sql_sha256": sha(path), "command": cli_command})
    oracle_commands = {"static-oracle": [str(tool), "--oracle", str(STATIC), "--csv", str(out / "static-oracle.csv")]}
    for name, (_, point, points) in time_cases.items():
        oracle_commands["time-" + name + "-oracle"] = [str(tool), "--oracle-slice", str(SERIES),
            "--offset", f"0,{point},0", "--shape", f"1,{points},{TIMES}", "--csv", str(out / ("time-" + name + "-oracle.csv"))]
    patch = subprocess.check_output(["git", "diff", "HEAD"], cwd=ROOT)
    (out / "tracked.patch").write_bytes(patch)
    frozen_files = (cli, extension, tool, STATIC, SERIES, ROOT / "scripts/validate-hres-o1280.py",
                    ROOT / "scripts/generate-hres-o1280.py", ROOT / "test/tools/duckomo_fixture_tool.cpp",
                    ROOT / "test/data/grids/definitions.json", ROOT / "test/data/grids/sample-manifest.json",
                    ROOT / "test/data/grids/value-reference-manifest.json", ROOT / "src/include/duckomo/generated_grid_registry.hpp")
    freeze = {"schema_version": 1, "frozen_at": dt.datetime.now(dt.timezone.utc).isoformat(),
              "scope": "local real OM queries; NOT full H0-H9 acceptance", "platform": platform.platform(),
              "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "tracked_diff_sha256": hashlib.sha256(patch).hexdigest(),
              "dependency_commits": subprocess.check_output(["git", "submodule", "status"], cwd=ROOT, text=True).splitlines(),
              "files": [{"path": str(p), "bytes": p.stat().st_size, "sha256": sha(p)} for p in frozen_files],
              "build_note": "CLI statically links DuckOMO; CLI hash is authoritative. Also retain matching loadable artifact hash.",
              "actual_engine": json.loads(subprocess.check_output(
                  [str(cli), "-no-init", "-json", ":memory:", "SELECT * FROM pragma_version();"], cwd=ROOT, text=True)),
              "cmake_cache_sha256": sha(build / "CMakeCache.txt"),
              "tolerances": {"producer_coordinates_degrees": 1e-4, "explicit_domain": "exact", "values": "Float32 bits, NaN to NULL"},
              "queries": planned, "oracle_commands": oracle_commands, "time_cases": time_cases, "time_label_start": str(START),
              "open_gates": ["independent original GRIB scanning/pl/point-value correspondence", "full time-series value coverage",
                             "controlled remote full/local and attempts audit", "signed S3/TLS", "full memory gate", "H9 independent reproduction"]}
    dump(out / "freeze.json", freeze)
    commands = []
    results = {"schema_version": 1, "freeze_sha256": sha(out / "freeze.json"), "checks": {}, "metrics": {}}

    def run(name: str, command: list[str], sql: str | None = None) -> None:
        started = time.monotonic()
        with (out / (name + ".stdout.txt")).open("w") as stdout, (out / (name + ".stderr.txt")).open("w") as stderr:
            process = subprocess.run(command, input=sql, text=True, cwd=ROOT, stdout=stdout, stderr=stderr, timeout=1200)
        commands.append({"name": name, "command": command, "sql_file": name + ".sql" if sql is not None else None,
                         "exit_code": process.returncode, "seconds": time.monotonic() - started})
        dump(out / "commands.json", commands)
        require(process.returncode == 0, f"{name} failed; see {out / (name + '.stderr.txt')}")
        print(f"{name}: exit=0 ({commands[-1]['seconds']:.1f}s)", flush=True)

    def scan(name: str) -> None:
        run(name, cli_command, (out / (name + ".sql")).read_text())
        with (out / (name + ".metrics.csv")).open(newline="") as source:
            scans = [json.loads(row["metrics"]) for row in csv.DictReader(source)]
        require(len(scans) == 1, f"{name} needs exactly one terminal scan metric")
        metrics = scans[0]
        require(metrics["outcome"]["status"] == "success" and metrics["outcome"]["terminal_published"],
                f"{name} did not publish successful terminal metrics")
        if name == "grid-info":
            require(metrics["operation"] == "grid_info", "descriptor query reported a different operation")
            require(all(metrics["reads"]["value_totals"][key] == 0
                        for key in ("index_bytes", "data_bytes", "decoded_chunks")), "grid_info read values")
        else:
            require(metrics["outcome"]["scan_complete"], f"{name} did not completely consume its scan")
        dump(out / (name + ".metrics-v4.json"), metrics)
        results["metrics"][name] = {"outcome": metrics["outcome"], "value_totals": metrics["reads"]["value_totals"],
                                    "query_owned_peak_bytes": metrics["memory"]["query_owned_peak_bytes"]}

    try:
        run("static-oracle", oracle_commands["static-oracle"])
        for name in queries:
            scan(name)
            if name == "static-domain":
                results["checks"]["static-official-values-and-producer-coordinates"] = compare_static(
                    out / "static-domain.csv", out / "static-oracle.csv")
        for left, right in (("static-domain", "static-explicit"), ("time-polar", "time-explicit")):
            require(sha(out / (left + ".csv")) == sha(out / (right + ".csv")), f"{left}/{right} differs")
            results["checks"][left + "-explicit-equivalence"] = {"status": "pass", "comparison": "identical full CSV hash"}
        for name in predicates:
            run(name + "-difference", cli_command, (out / (name + "-difference.sql")).read_text())
            with (out / (name + "-difference.csv")).open(newline="") as source:
                counts = [int(row["mismatches"]) for row in csv.DictReader(source)]
            require(counts == [0, 0], f"{name} differs from complete materialized baseline: {counts}")
            results["checks"]["spatial-" + name] = {"status": "pass", "bidirectional_except_all": counts}
        zero = results["metrics"]["static-zero-value"]["value_totals"]
        require(all(zero[key] == 0 for key in ("index_bytes", "data_bytes", "decoded_chunks")), "zero-value scan read values")
        results["checks"]["zero-value"] = {"status": "pass", "value_totals": zero}
        for name, (_, point, points) in time_cases.items():
            oracle = out / ("time-" + name + "-oracle.csv")
            run("time-" + name + "-oracle", oracle_commands["time-" + name + "-oracle"])
            result = compare_time(out / ("time-" + name + ".csv"), oracle, point, points)
            require(result["positions"] == points * TIMES, "incomplete sampled time series")
            results["checks"]["time-" + name] = result
        filtered = compare_time(out / "time-filter.csv", out / "time-polar-oracle.csv", 0, 20)
        require(filtered["positions"] == 60, "half-open time predicate must return 20 points * 3 hours")
        with (out / "time-filter.csv").open(newline="") as source:
            require({int(row["axis2"]) for row in csv.DictReader(source)} == {1, 2, 3},
                    "half-open time predicate returned different slots")
        results["checks"]["time-filter"] = filtered
        results["status"] = "pass"
    except Exception as error:
        results["status"] = "fail"
        results["error"] = str(error)
        raise
    finally:
        results["artifacts"] = [{"path": str(p.relative_to(out)), "bytes": p.stat().st_size, "sha256": sha(p)}
                                for p in sorted(out.iterdir()) if p.is_file() and p.name != "manifest.json"]
        dump(out / "manifest.json", results)
    print(f"Local O1280 validation passed; evidence: {out / 'manifest.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
