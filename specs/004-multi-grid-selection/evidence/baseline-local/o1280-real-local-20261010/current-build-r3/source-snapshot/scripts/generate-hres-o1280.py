#!/usr/bin/env python3
"""Freeze Open-Meteo's O1280 row table in definitions.json (no runtime fetch).

Latitude is the producer's Float32 approximation, NOT a Legendre root. The
point sequence is north-to-south, with each row starting at longitude zero.
Use --source-directory to verify downloaded files from the pinned commit.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "b06f4760fd1f997e5559bb380f64c5e496b4a509"
SOURCES = {
    "Sources/App/Domains/GaussianGrid.swift": "b913a4ec63b5889466d44ffffccd38aa4f9288d07c4c3b6786df9aa9f9c6d516",
    "Sources/App/EcmwfEcpds/EcmwfEcpdsDomain.swift": "70cccf553cbeef823d8ac7da7df9e686e1761871dc079a90be0dabcfea258805",
    "Sources/App/Helper/OmFileSplitter.swift": "23975b7e45bce942b85b6f03c661ccc75a7fd5720226a7f939fa78ace0d8328a",
    "Sources/App/Helper/Download/Curl+Grib.swift": "212e97efb147bf80c466ad3f19f1cbd94f08b92a4639f79c47de1ef937c2c593",
    "Sources/App/EcmwfEcpds/EcmwfEcpdsDownloader.swift": "17b3e4d2467fc165d982e8379430a3d7388f4ea48beba94e3549639221cbf9c3",
    "Sources/App/Helper/Writer/GenericVariableHandle.swift": "a20f1d77b5cffdfe7fcf1333a99d1fc12344790270d1eebc919bdcc3ce56480e",
}


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def definition() -> dict:
    n = 1280
    dy = f32(180.0 / f32(f32(2.0 * n) + 0.5))
    rows = []
    for y in range(2 * n):
        count = 20 + 4 * min(y, 2 * n - y - 1)
        rows.append({
            "latitude": f32(f32(f32(n - y - 1) * dy) + f32(dy / 2.0)),
            "point_count": count,
            "longitude_origin": 0.0,
            "longitude_step": f32(360.0 / count),
        })
    return {
        "id": "ecmwf_ifs",
        "kind": "reduced_gaussian",
        "source_path": "Sources/App/Domains/GaussianGrid.swift",
        "parameters": {
            "coordinate_rule_id": "openmeteo_gaussian_n1280_f32_v1",
            "numeric_policy": "openmeteo_f32_v1",
            "earth": {"kind": "wgs84_source", "semi_major_axis_m": 6378137.0,
                      "inverse_flattening": 298.257223563},
            "N": n,
            "latitude_rule": "openmeteo_approx_v1",
            "point_count": sum(row["point_count"] for row in rows),
            "rows": rows,
            "row_generation": "scripts/generate-hres-o1280.py; pinned GridType.nxOf/integral and getCoordinates Float32 operations",
            "source_files": [{"path": path, "sha256": digest} for path, digest in SOURCES.items()],
            "source_layout": {
                "static": [1, "point"],
                "time_series": [1, "point", "time"],
                "time_chunk_length": 504,
                "time_step_seconds": 3600,
                "time_chunk_origin": "Unix epoch; chunk k starts at k * 504 * 3600 seconds",
            },
        },
        "expected_spatial_axis_profile": {
            "layout": "flattened",
            "spatial_axis_count": 1,
            "axis_order": ["point"],
            "flattened_axis_alias": "lon",
            "object_profile_status": "source-derived O1280 point order; producer nx=6599680, ny=1; callers declare every original axis",
        },
        "evidence": {
            "level": "metadata-checked",
            "sample_id": "ecmwf_ifs_static_hsurf_reduced_gaussian_o1280",
            "source_uri": "s3://openmeteo/data/ecmwf_ifs/static/HSURF.om",
            "build_pair": None,
            "claims": "Frozen public HSURF and chunk_817 metadata are checked in sample-manifest.json. Local real-object checks in evidence/baseline-local/o1280-real-local-20261010/ cover all 6,599,680 HSURF values against official OM C Float32 decode and all coordinates against an independent Python port of pinned producer arithmetic, explicit/domain equivalence, spatial selection and 22 complete 504-slot time series with NULL positions. Build and artifact hashes are frozen per run; this is not an accepted matrix build_pair. HSURF embeds lat/lon axis names; its flattened lon alias is explicitly registered without overriding metadata assertions. These checks do not establish independent original-GRIB scanning/point-value correspondence or full time-series value coverage. Independent coordinate mapping, controlled remote full/local benefit, signed-S3 audit, full memory gate and H9 remain open. O1280 is the Gaussian-family acceptance target under contracts/gaussian-acceptance-20261010.md; owner-approved N-grid skips do not promote evidence levels.",
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--definitions", type=Path, default=ROOT / "test/data/grids/definitions.json")
    parser.add_argument("--source-directory", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    data = json.loads(args.definitions.read_text())
    if data["upstream"]["commit"] != COMMIT:
        raise ValueError("O1280 producer source commit differs from the grid registry")
    if args.source_directory:
        for path, expected in SOURCES.items():
            actual = hashlib.sha256((args.source_directory / Path(path).name).read_bytes()).hexdigest()
            if actual != expected:
                raise ValueError(f"pinned source hash differs: {path}")
    record = definition()
    old = next((item for item in data["definitions"] if item["id"] == record["id"]), None)
    if args.check:
        if old != record:
            raise ValueError("HRES O1280 definition is missing or stale")
        print("HRES O1280 producer row table verified")
        return 0
    if old is None:
        data["definitions"].append(record)
    else:
        data["definitions"][data["definitions"].index(old)] = record
    args.definitions.write_text(json.dumps(data, indent=2) + "\n")
    print(f"generated HRES O1280 definition: {args.definitions}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
