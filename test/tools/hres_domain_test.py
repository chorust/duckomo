#!/usr/bin/env python3
"""End-to-end check for the registered ECMWF IFS HRES O1280 domain.

Generates synthetic full-shape fixtures on demand (they are not part of the
frozen 004 manifest), then verifies domain binding, producer-order
coordinates, longitude-wrap filtering, and the [1, point, time] layout
against an independent Python port of the pinned Swift arithmetic.
"""

from __future__ import annotations

import csv
import math
import struct
import subprocess
import tempfile
from pathlib import Path


import os
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / os.environ.get("DUCKOMO_BUILD", "build/release")
DUCKDB = BUILD / "duckdb"
EXTENSION = BUILD / "extension/duckomo/duckomo.duckdb_extension"
FIXTURE_TOOL = BUILD / "test/tools/duckomo_fixture_tool"


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def o1280_coordinates() -> list[tuple[float, float]]:
    n = 1280
    dy = f32(180.0 / f32(2.0 * n + 0.5))
    result: list[tuple[float, float]] = []
    for y in range(2 * n):
        nx = 20 + 4 * min(y, 2 * n - y - 1)
        latitude = f32(f32(f32(n - y - 1) * dy) + f32(dy / 2.0))
        dx = f32(360.0 / nx)
        for x in range(nx):
            unwrapped = f32(f32(x * dx) + 0.0)
            lon = math.fmod(unwrapped, 360.0)
            if lon < -180.0:
                lon += 360.0
            if lon >= 180.0:
                lon -= 360.0
            result.append((latitude, lon))
    return result


def run_sql(sql: str) -> subprocess.CompletedProcess:
    prefix = f"LOAD '{EXTENSION}'; SET threads=1; "
    return subprocess.run([str(DUCKDB), "-unsigned", "-csv", "-noheader", ":memory:", prefix + sql],
                          cwd=ROOT, capture_output=True, text=True, timeout=600)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def expect_ok(process: subprocess.CompletedProcess, label: str) -> str:
    require(process.returncode == 0, f"{label} failed: {process.stderr.strip()}")
    return process.stdout.strip()


def main() -> int:
    for path in (DUCKDB, EXTENSION, FIXTURE_TOOL):
        require(path.exists(), f"required build artifact is missing: {path}")

    coords = o1280_coordinates()
    require(len(coords) == 6599680, "independent O1280 reference must cover all producer points")

    with tempfile.TemporaryDirectory(prefix="duckomo-hres-test-") as temporary:
        fixture_dir = Path(temporary)
        generated = subprocess.run([str(FIXTURE_TOOL), "--hres-output", str(fixture_dir)],
                                   cwd=ROOT, capture_output=True, text=True, timeout=1200)
        require(generated.returncode == 0, f"fixture generation failed: {generated.stderr.strip()}")
        static_file = fixture_dir / "static.om"
        timeseries_file = fixture_dir / "timeseries.om"

        # The official slice adapter must preserve non-zero offsets and return
        # slice-local indices, rather than accidentally decoding the prefix.
        oracle_csv = fixture_dir / "slice.csv"
        oracle = subprocess.run([str(FIXTURE_TOOL), "--oracle-slice", str(timeseries_file),
                                 "--offset", "0,34567,1", "--shape", "1,1,2", "--csv", str(oracle_csv)],
                                cwd=ROOT, capture_output=True, text=True, timeout=60)
        require(oracle.returncode == 0, f"official slice failed: {oracle.stderr}")
        with oracle_csv.open(newline="") as source:
            rows = list(csv.DictReader(source))
        require([(int(row["index"]), float(row["value"])) for row in rows] ==
                [(0, 34567 % 4096 + 0.25), (1, 34567 % 4096 + 0.5)],
                "official slice must use the requested offset/count and local indices")
        for offset, shape in (("0,6599680,0", "1,1,1"), ("0,0", "1,1"),
                              ("0,0,0", "1,1,0"), ("0,-1,0", "1,1,1")):
            bad_oracle = subprocess.run([str(FIXTURE_TOOL), "--oracle-slice", str(timeseries_file),
                                        "--offset", offset, "--shape", shape, "--csv", str(fixture_dir / "bad.csv")],
                                       cwd=ROOT, capture_output=True, text=True, timeout=60)
            require(bad_oracle.returncode != 0, "invalid official slice must fail closed")

        static_read = f"read_om('{static_file}', dimensions := map(['value'], [['y','point']]), domain := 'ecmwf_ifs', include_source := true)"
        out = expect_ok(run_sql(f"SELECT count(*), min(lat), max(lat) FROM {static_read};"), "HRES full scan")
        count, min_lat, max_lat = out.split(",")
        expected_lats = [c[0] for c in coords]
        require(int(count) == 6599680, "HRES static scan must return every grid point")
        require(abs(float(min_lat) - min(expected_lats)) == 0 and abs(float(max_lat) - max(expected_lats)) == 0,
                "HRES latitude bounds must match the producer rows")

        out = expect_ok(run_sql(
            f"SELECT om_source.point_index, value, lat, lon FROM {static_read} "
            "WHERE om_source.point_index < 3 ORDER BY om_source.point_index;"), "HRES first points")
        rows = [line.split(",") for line in out.splitlines()]
        for index, row in enumerate(rows):
            lat, lon = coords[index]
            require(int(row[0]) == index and abs(float(row[1]) - (index % 4096)) < 1e-6 and
                    abs(float(row[2]) - lat) == 0 and abs(float(row[3]) - lon) == 0,
                    f"producer-order point {index} differs: {row} vs {lat},{lon}")

        seam_filter = "lat BETWEEN -80 AND 80 AND (lon >= 170 OR lon < -170)"
        expected_seam = [point for point, (lat, lon) in enumerate(coords)
                         if -80 <= lat <= 80 and (lon >= 170 or lon < -170)]
        out = expect_ok(run_sql(f"SELECT count(*) FROM {static_read} WHERE {seam_filter};"),
                        "HRES seam filter")
        require(int(out) == len(expected_seam),
                f"HRES longitude-wrap count differs: SQL {out} vs reference {len(expected_seam)}")

        out = expect_ok(run_sql(
            f"SELECT om_source.point_index, value FROM {static_read} WHERE {seam_filter} "
            "ORDER BY om_source.point_index LIMIT 5;"), "HRES seam values")
        sql_rows = [(int(line.split(",")[0]), float(line.split(",")[1])) for line in out.splitlines()]
        expected_rows = [(point, float(point % 4096)) for point in expected_seam[:5]]
        require(sql_rows == expected_rows, f"HRES seam values differ: {sql_rows} vs {expected_rows}")

        out = expect_ok(run_sql(
            f"SELECT count(*) FROM {static_read} WHERE lon BETWEEN 170 AND -170;"), "reversed BETWEEN")
        require(int(out) == 0, "plain reversed BETWEEN must stay empty (no implicit wrap)")

        time_read = f"read_om('{timeseries_file}', dimensions := map(['value'], [['y','point','time']]), domain := 'ecmwf_ifs')"
        out = expect_ok(run_sql(f"SELECT count(*) FROM {time_read} WHERE {seam_filter};"),
                        "HRES time-series filter")
        require(int(out) == len(expected_seam) * 3,
                "HRES [1,point,time] layout must keep one filter result per time step")

        out = expect_ok(run_sql(
            f"SELECT value, lat, lon FROM {time_read} "
            "WHERE lat BETWEEN 89.9 AND 90 AND lon BETWEEN -0.001 AND 0.001 "
            "ORDER BY value LIMIT 3;"), "HRES polar time-series values")
        expected_first = sorted(((index % 4096) + t * 0.25 for index in range(20) for t in range(3)))[:3]
        values = [float(line.split(",")[0]) for line in out.splitlines()]
        require(len(values) == 3 and all(abs(a - b) < 1e-6 for a, b in zip(values, expected_first)),
                f"HRES polar row values differ: {values} vs {expected_first}")

        metadata_file = fixture_dir / "static-coordinates.om"
        metadata_read = f"read_om('{metadata_file}', domain := 'ecmwf_ifs', include_source := true)"
        out = expect_ok(run_sql(
            f"SELECT count(*), min(om_source.point_index), max(om_source.point_index) "
            f"FROM {metadata_read} WHERE lat > 89.9;"), "HRES embedded lat/lon profile")
        require(out == "20,0,19", "embedded lon must identify the O1280 flattened point axis")
        conflict = run_sql(
            f"SELECT * FROM read_om('{metadata_file}', dimensions := ['y','point'], "
            "domain := 'ecmwf_ifs') LIMIT 1;")
        require(conflict.returncode != 0 and "conflict" in conflict.stderr.lower(),
                "the registered axis alias must not bypass embedded metadata assertions")

        bad = run_sql(
            f"SELECT * FROM read_om('{static_file}', dimensions := map(['value'], [['point','y']]), "
            "domain := 'ecmwf_ifs') LIMIT 1;")
        require(bad.returncode != 0 and "point" in bad.stderr.lower(),
                "mis-ordered HRES axis declaration must be rejected")

    print("HRES O1280 domain checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
