#!/usr/bin/env python3
"""Generate frozen 2D WKB, independent membership, and SQL statistics references."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
from typing import Iterable

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "test/data/regions"
GRID_POINTS = tuple((100 + x, 20 + y, y, x) for y in range(5) for x in range(5))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ring(min_x: float, min_y: float, max_x: float, max_y: float) -> list[tuple[float, float]]:
    return [(min_x, min_y), (max_x, min_y), (max_x, max_y), (min_x, max_y), (min_x, min_y)]


def polygon(outer: Iterable[tuple[float, float]], *holes: Iterable[tuple[float, float]]) -> dict:
    return {"type": "Polygon", "rings": [list(outer), *(list(hole) for hole in holes)]}


REGIONS = [
    ("A", polygon(ring(100, 20, 102, 22))),
    ("B", polygon(ring(101, 21, 103, 23))),
    ("Hole", polygon(ring(100, 20, 104, 24), ring(101, 21, 103, 23))),
    ("Empty", {"type": "Polygon", "rings": []}),
    ("Outside", polygon(ring(110, 30, 111, 31))),
    ("Multi", {"type": "MultiPolygon", "polygons": [polygon(ring(100, 20, 101, 21)),
                                                           polygon(ring(103, 23, 104, 24))]}),
    ("CopyA", polygon(ring(100, 20, 102, 22))),
    ("Narrow", polygon(ring(100, 20, 100.2, 24))),
    ("Gap", polygon(ring(100.1, 20.1, 100.2, 20.2))),
    ("Corner", polygon(ring(102, 22, 104, 24))),
]


def encode_ring(points: list[tuple[float, float]]) -> bytes:
    return struct.pack("<I", len(points)) + b"".join(struct.pack("<dd", *point) for point in points)


def encode_polygon(geometry: dict) -> bytes:
    if geometry["type"] != "Polygon":
        raise ValueError("expected Polygon")
    rings = geometry["rings"]
    return struct.pack("<BI", 1, 3) + struct.pack("<I", len(rings)) + b"".join(encode_ring(item) for item in rings)


def encode_wkb(geometry: dict) -> bytes:
    if geometry["type"] == "Polygon":
        return encode_polygon(geometry)
    if geometry["type"] == "MultiPolygon":
        polygons = geometry["polygons"]
        return struct.pack("<BII", 1, 6, len(polygons)) + b"".join(encode_polygon(item) for item in polygons)
    raise ValueError("only standard 2D Polygon/MultiPolygon WKB is supported")


def ring_relation(points: list[tuple[float, float]], x: float, y: float) -> str:
    inside = False
    for index in range(len(points) - 1):
        x1, y1 = points[index]
        x2, y2 = points[index + 1]
        cross = (x - x1) * (y2 - y1) - (y - y1) * (x2 - x1)
        if cross == 0 and min(x1, x2) <= x <= max(x1, x2) and min(y1, y2) <= y <= max(y1, y2):
            return "boundary"
        if (y1 > y) != (y2 > y):
            crossing_x = x1 + (y - y1) * (x2 - x1) / (y2 - y1)
            if x < crossing_x:
                inside = not inside
    return "inside" if inside else "outside"


def polygon_covers(geometry: dict, x: float, y: float) -> bool:
    if geometry["type"] == "MultiPolygon":
        return any(polygon_covers(item, x, y) for item in geometry["polygons"])
    rings = geometry["rings"]
    if not rings:
        return False
    outer = ring_relation(rings[0], x, y)
    if outer == "outside":
        return False
    for hole in rings[1:]:
        if ring_relation(hole, x, y) == "inside":
            return False
    return True


def geometry_edge_cases() -> tuple[dict, bytes]:
    shell = ring(0.0, 0.0, 2.0, 2.0)
    hole = ring(0.5, 0.5, 1.5, 1.5)
    geometry = polygon(shell, hole)
    cases = [
        ("outer_boundary", 0.0, 1.0, True),
        ("hole_boundary", 0.5, 1.0, True),
        ("hole_interior", 1.0, 1.0, False),
        ("just_inside_outer", math.nextafter(0.0, math.inf), 1.0, True),
        ("just_outside_outer", math.nextafter(0.0, -math.inf), 1.0, False),
        ("outer_vertex", 0.0, 0.0, True),
    ]
    results = []
    for name, x, y, expected in cases:
        actual = polygon_covers(geometry, x, y)
        assert actual is expected, (name, x, y, actual, expected)
        results.append({"id": name, "point": [x, y], "covers": expected})
    return {"id": "square_with_hole_and_nextafter", "geometry_wkb_hex": encode_wkb(geometry).hex(),
            "cases": results}, encode_wkb(geometry)


def fixture_values(path: Path) -> list[float]:
    values = []
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            index, value = row["index"], row["value"]
            assert int(index) == len(values)
            values.append(float(value))
    assert len(values) == 150
    return values


def write_csv(path: Path, header: list[str], rows: Iterable[Iterable[object]]) -> None:
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(header)
        writer.writerows(rows)


def build(output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    (output / "geometries").mkdir(exist_ok=True)
    object_path = output / "regions.om"
    ref_path = output / "regions.reference.csv"
    if not object_path.is_file() or not ref_path.is_file():
        raise RuntimeError("generate and install official OM fixtures before generating the region reference")

    geometries = []
    memberships = []
    expected_counts = {"A": 9, "B": 9, "Hole": 24, "Empty": 0, "Outside": 0,
                       "Multi": 8, "CopyA": 9, "Narrow": 5, "Gap": 0, "Corner": 9}
    values = fixture_values(ref_path)
    groups: dict[tuple[str, int, int], list[float]] = {}
    for region_id, geometry in REGIONS:
        encoded = encode_wkb(geometry)
        wkb_path = output / "geometries" / f"{region_id.lower()}.wkb"
        wkb_path.write_bytes(encoded)
        point_memberships = []
        for lon, lat, y, x in GRID_POINTS:
            if polygon_covers(geometry, lon, lat):
                point_memberships.append((lon, lat, y, x))
        assert len(point_memberships) == expected_counts[region_id], (region_id, point_memberships)
        geometries.append({
            "id": region_id,
            "type": geometry["type"],
            "wkb_path": wkb_path.relative_to(output).as_posix(),
            "wkb_sha256": sha256(wkb_path),
            "wkb_bytes": len(encoded),
            "vertex_count": sum(len(r) for r in geometry.get("rings", [])) if geometry["type"] == "Polygon"
                            else sum(len(r) for item in geometry["polygons"] for r in item["rings"]),
            "spatial_point_count": len(point_memberships),
        })
        for member in range(2):
            for time_index in range(3):
                group = []
                for lon, lat, y, x in point_memberships:
                    logical_index = member * 75 + time_index * 25 + y * 5 + x
                    value = values[logical_index]
                    is_null = math.isnan(value)
                    memberships.append((region_id, logical_index, member, time_index, lat, lon,
                                         "" if is_null else format(value, ".9g")))
                    if not is_null:
                        group.append(value)
                groups[(region_id, member, time_index)] = group

    assert sum(expected_counts.values()) * 2 * 3 == len(memberships) == 438
    write_csv(output / "regions-membership.reference.csv",
              ["region_id", "logical_index", "member", "time_index", "lat", "lon", "value"], memberships)
    group_records = []
    for region_id, member, time_index in sorted(groups):
        matching = [row for row in memberships if row[0] == region_id and row[2] == member and row[3] == time_index]
        vals = groups[(region_id, member, time_index)]
        total = sum(vals) if vals else None
        group_records.append({
            "region_id": region_id,
            "member": member,
            "time_index": time_index,
            "sample_count": len(matching),
            "valid_count": len(vals),
            "sum": total,
            "mean": total / len(vals) if vals else None,
            "min": min(vals) if vals else None,
            "max": max(vals) if vals else None,
        })
    a0 = next(row for row in group_records if (row["region_id"], row["member"], row["time_index"]) == ("A", 0, 0))
    assert (a0["sample_count"], a0["valid_count"], a0["sum"], a0["mean"], a0["min"], a0["max"]) == (9, 8, 77, 9.625, 0, 21)
    assert sum(row["sample_count"] for row in group_records) == 438
    assert len(group_records) == 60
    statistics_path = output / "regions-statistics.reference.csv"
    write_csv(statistics_path,
              ["region_id", "member", "time_index", "sample_count", "valid_count", "sum", "mean", "min", "max"],
              [(row["region_id"], row["member"], row["time_index"], row["sample_count"], row["valid_count"],
                row["sum"], row["mean"], row["min"], row["max"]) for row in group_records])

    edge_case, edge_wkb = geometry_edge_cases()
    edge_path = output / "geometries" / "edge-cases.wkb"
    edge_path.write_bytes(edge_wkb)
    real_targets = output / "real-targets.json"
    region_id_conflict = output / "regions_region_id_conflict.om"
    if not region_id_conflict.is_file():
        raise RuntimeError("generate the region_id name-conflict fixture with duckomo_fixture_tool first")
    real_target_summary = {"status": "not-frozen; see T004"}
    if real_targets.is_file():
        target_data = json.loads(real_targets.read_text())
        real_target_summary = {
            **target_data,
            "status": target_data["status"],
            "path": real_targets.relative_to(output).as_posix(),
            "sha256": sha256(real_targets),
            "target_count": len(target_data["targets"]),
        }
    record = {
        "schema_version": 1,
        "classification": "synthetic_and_frozen_real_targets" if real_targets.is_file() else "synthetic_only",
        "oracle": "independent Python ray casting and exact segment boundary checks; values decoded by pinned official OM C reader",
        "om_format_version": 3,
        "fixture": {
            "path": "regions.om",
            "sha256": sha256(object_path),
            "size_bytes": object_path.stat().st_size,
            "official_reference_path": "regions.reference.csv",
            "official_reference_sha256": sha256(ref_path),
            "shape": [2, 3, 5, 5],
            "chunks": [1, 1, 2, 3],
            "axes": ["member", "time", "lat", "lon"],
            "value_formula": "1000*member + 100*time + 10*y + x; time index 2 and spatial (y=2,x=2) are NaN",
        },
        "supporting_fixtures": [{
            "path": region_id_conflict.name,
            "sha256": sha256(region_id_conflict),
            "size_bytes": region_id_conflict.stat().st_size,
            "purpose": "binding rejection when a source value column is named region_id",
        }],
        "regions": geometries,
        "expected_membership_path": "regions-membership.reference.csv",
        "expected_membership_sha256": sha256(output / "regions-membership.reference.csv"),
        "expected_membership_rows": len(memberships),
        "expected_group_statistics": group_records,
        "expected_group_statistics_path": statistics_path.relative_to(output).as_posix(),
        "expected_group_statistics_sha256": sha256(statistics_path),
        "edge_case_reference": edge_case,
        "edge_case_wkb_path": edge_path.relative_to(output).as_posix(),
        "edge_case_wkb_sha256": sha256(edge_path),
        "real_targets": real_target_summary,
    }
    manifest_path = output / "manifest.json"
    temporary = manifest_path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(record, indent=2, sort_keys=True, allow_nan=False) + "\n")
    temporary.replace(manifest_path)
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    try:
        result = build(args.output_dir.resolve())
    except (OSError, ValueError, RuntimeError, AssertionError) as error:
        print(f"generate-region-reference: {error}", file=sys.stderr)
        return 1
    print(json.dumps({"manifest": str((args.output_dir.resolve() / "manifest.json")),
                      "regions": len(result["regions"]), "membership_rows": result["expected_membership_rows"],
                      "groups": len(result["expected_group_statistics"])}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
