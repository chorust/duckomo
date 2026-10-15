#!/usr/bin/env python3
"""Freeze SEAS5/EC46 producer domains from the pinned Gaussian grid rules.

O320 is octahedral (421120 points), never an alias of N320. Product names
share the same spatial identity while time/member labels remain file axes.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "b06f4760fd1f997e5559bb380f64c5e496b4a509"
O320_DOMAINS = (
    "ecmwf_seas5", "ecmwf_seas5_ensemble_mean", "ecmwf_seas5_daily",
    "ecmwf_seas5_daily_ensemble_mean", "ecmwf_seas5_monthly", "ecmwf_ec46",
    "ecmwf_ec46_ensemble_mean", "ecmwf_ec46_weekly",
)
N160_DOMAINS = ("ecmwf_seas5_12hourly", "ecmwf_seas5_monthly_upper_level")
SOURCES = {
    "GaussianGrid.swift": "b913a4ec63b5889466d44ffffccd38aa4f9288d07c4c3b6786df9aa9f9c6d516",
    "EcmwfSeasDomain.swift": "2588451da26c1def64bde88dd0ba8af0e3257e4865bb4c78ac7ac3e7099d0d16",
}


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def definitions(document: dict) -> list[dict]:
    n160 = next(item for item in document["definitions"] if item["id"] == "n160")
    n = 320
    dy = f32(180.0 / f32(2.0 * n + 0.5))
    rows = []
    for y in range(2 * n):
        count = 20 + 4 * min(y, 2 * n - y - 1)
        rows.append({"latitude": f32(f32(f32(n - y - 1) * dy) + f32(dy / 2.0)),
                     "point_count": count, "longitude_origin": 0.0,
                     "longitude_step": f32(360.0 / count)})
    o320 = {"coordinate_rule_id": "openmeteo_gaussian_n320_f32_v1",
            "numeric_policy": "openmeteo_f32_v1", "earth": copy.deepcopy(n160["parameters"]["earth"]),
            "N": n, "latitude_rule": "openmeteo_approx_v1", "point_count": 421120, "rows": rows}
    result = []
    for name in O320_DOMAINS + N160_DOMAINS:
        is_o320 = name in O320_DOMAINS
        result.append({
            "id": name, "kind": "reduced_gaussian",
            "source_path": "Sources/App/EcmwfSeas/EcmwfSeasDomain.swift",
            "parameters": copy.deepcopy(o320) if name == "ecmwf_seas5" else {},
            **({} if name == "ecmwf_seas5" else
               {"grid_definition": "ecmwf_seas5" if is_o320 else "n160"}),
            "expected_spatial_axis_profile": {
                "layout": "flattened", "spatial_axis_count": 1, "axis_order": ["point"],
                "flattened_axis_alias": "lon",
                "object_profile_status": (
                    f"pinned producer {'O320: 421120' if is_o320 else 'N160: 138346'} points, ny=1; "
                    "lon is the flattened spatial axis in embedded lat/lon profiles; "
                    "time/member/level dimensions are retained, not inferred from the product name"
                ),
            },
            "evidence": {
                "level": "definition-recorded", "sample_id": None, "source_uri": None, "build_pair": None,
                "claims": (
                    "Grid and product mapping derived from pinned EcmwfSeasDomain.swift and GaussianGrid.swift. "
                    "Coordinates reproduce Open-Meteo Float32 arithmetic, not native ECMWF Legendre latitudes. "
                    "Synthetic full-shape spatial/time/member regression coverage is recorded separately; "
                    "real-object checks and public availability are product-specific."
                ),
            },
        })
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--definitions", type=Path, default=ROOT / "test/data/grids/definitions.json")
    parser.add_argument("--source-directory", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    document = json.loads(args.definitions.read_text())
    if document["upstream"]["commit"] != COMMIT:
        raise ValueError("seasonal producer commit differs from the registry")
    if args.source_directory:
        for name, digest in SOURCES.items():
            if hashlib.sha256((args.source_directory / name).read_bytes()).hexdigest() != digest:
                raise ValueError(f"pinned source hash differs: {name}")
    expected = definitions(document)
    old = [item for item in document["definitions"] if item["id"] in O320_DOMAINS + N160_DOMAINS]
    if args.check:
        if old != expected:
            raise ValueError("SEAS5/EC46 domain definitions are missing or stale")
        print("SEAS5/EC46 O320 and N160 definitions verified")
        return 0
    document["definitions"] = [item for item in document["definitions"]
                               if item["id"] not in O320_DOMAINS + N160_DOMAINS] + expected
    args.definitions.write_text(json.dumps(document, indent=2) + "\n")
    print("generated ten seasonal Gaussian domains")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
