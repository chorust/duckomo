#!/usr/bin/env python3
"""Generate independent WKB and nearest-sampling references for feature 006.

This tool deliberately does not import or invoke DuckOMO locator code. It
recomputes source positions by exhaustive spherical distance evaluation and
reads value bytes only from official OM reader CSV references.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
from decimal import Decimal, localcontext

ROOT = Path(__file__).resolve().parents[1]
PI = math.pi
RADIUS_M = 6_371_229.0
DPI = Decimal("3.1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679")
DRADIUS = Decimal("6371229")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def ring(points: list[tuple[float, float]]) -> bytes:
    return struct.pack("<I", len(points)) + b"".join(struct.pack("<dd", x, y) for x, y in points)


def polygon_wkb(bounds: tuple[float, float, float, float]) -> bytes:
    xmin, ymin, xmax, ymax = bounds
    points = [(xmin, ymin), (xmax, ymin), (xmax, ymax), (xmin, ymax), (xmin, ymin)]
    return struct.pack("<BII", 1, 3, 1) + ring(points)


def multipolygon_wkb(polygons: list[tuple[float, float, float, float]]) -> bytes:
    members = [polygon_wkb(item) for item in polygons]
    return struct.pack("<BII", 1, 6, len(members)) + b"".join(members)


def polygon_rings_wkb(rings: list[list[tuple[float, float]]]) -> bytes:
    return struct.pack("<BII", 1, 3, len(rings)) + b"".join(ring(points) for points in rings)


def d_sin(value: Decimal) -> Decimal:
    value = value % (2 * DPI)
    if value > DPI:
        value -= 2 * DPI
    term = value
    total = value
    n = 1
    while True:
        term *= -(value * value) / Decimal((2 * n) * (2 * n + 1))
        updated = total + term
        if updated == total:
            return total
        total = updated
        n += 1


def d_cos(value: Decimal) -> Decimal:
    return d_sin(value + DPI / 2)


def d_atan(value: Decimal) -> Decimal:
    sign = Decimal(-1) if value < 0 else Decimal(1)
    x = abs(value)
    if x > 1:
        return sign * (DPI / 2 - d_atan(1 / x))
    if x > Decimal("0.4"):
        return sign * (DPI / 4 + d_atan((x - 1) / (x + 1)))
    term = x
    total = x
    n = 1
    while True:
        term *= -(x * x)
        updated = total + term / Decimal(2 * n + 1)
        if updated == total:
            return sign * total
        total = updated
        n += 1


def d_atan2(y: Decimal, x: Decimal) -> Decimal:
    if x > 0:
        return d_atan(y / x)
    if x < 0:
        return d_atan(y / x) + (DPI if y >= 0 else -DPI)
    if y > 0:
        return DPI / 2
    if y < 0:
        return -DPI / 2
    return Decimal(0)


def haversine_f64(lon0: float, lat0: float, lon1: float, lat1: float,
                  radius_m: float = RADIUS_M) -> float:
    if lat0 == lat1 and math.remainder(lon0 - lon1, 360.0) == 0.0:
        return 0.0
    if lat1 == 90.0:
        angle = (90.0 - lat0) * (PI / 180.0)
    elif lat1 == -90.0:
        angle = (90.0 + lat0) * (PI / 180.0)
    else:
        phi_t = lat0 * (PI / 180.0)
        phi_s = lat1 * (PI / 180.0)
        delta_phi = phi_s - phi_t
        delta_lon_degrees = math.remainder(lon1 - lon0, 360.0)
        delta_lambda = delta_lon_degrees * (PI / 180.0)
        s1 = math.sin(delta_phi / 2.0)
        s2 = math.sin(delta_lambda / 2.0)
        a = (s1 * s1) + ((math.cos(phi_t) * math.cos(phi_s)) * (s2 * s2))
        a = min(1.0, max(0.0, a))
        angle = 2.0 * math.atan2(math.sqrt(a), math.sqrt(1.0 - a))
    return radius_m * angle


def spherical_vector_distance_decimal(lon0: float, lat0: float, lon1: float, lat1: float,
                                      radius_m: float = RADIUS_M) -> Decimal:
    """Independent high-precision distance via 3D unit-vector cross/dot products.

    Uses atan2(norm(cross(u, v)), dot(u, v)), a different formulation from the
    production haversine kernel.
    """
    with localcontext() as context:
        context.prec = 90
        lon_t, lat_t, lon_s, lat_s, radius = map(
            lambda n: Decimal.from_float(n), (lon0, lat0, lon1, lat1, radius_m))
        lon_t *= DPI / Decimal(180)
        lat_t *= DPI / Decimal(180)
        lon_s *= DPI / Decimal(180)
        lat_s *= DPI / Decimal(180)
        target = (d_cos(lat_t) * d_cos(lon_t), d_cos(lat_t) * d_sin(lon_t), d_sin(lat_t))
        source = (d_cos(lat_s) * d_cos(lon_s), d_cos(lat_s) * d_sin(lon_s), d_sin(lat_s))
        cross = (target[1] * source[2] - target[2] * source[1],
                 target[2] * source[0] - target[0] * source[2],
                 target[0] * source[1] - target[1] * source[0])
        cross_norm = sum(component * component for component in cross).sqrt()
        dot = sum(left * right for left, right in zip(target, source))
        return +(radius * d_atan2(cross_norm, dot))


def write_csv(path: Path, header: list[str], rows) -> None:
    import csv
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(header)
        writer.writerows(rows)


def grid_definitions() -> list[dict]:
    result = []
    for name, lon0, dlon, lat0, dlat, nx, ny, layout in [
        ("small-separate", -2.0, 1.0, -2.0, 1.0, 5, 5, "lon_fastest"),
        ("small-interleaved", -2.0, 1.0, -2.0, 1.0, 5, 5, "lon_fastest"),
        ("small-reverse-lat", -2.0, 1.0, 2.0, -1.0, 5, 5, "lon_fastest"),
        ("small-reverse-lon", 2.0, -1.0, -2.0, 1.0, 5, 5, "lon_fastest"),
        ("small-reverse-both", 2.0, -1.0, 2.0, -1.0, 5, 5, "lon_fastest"),
        ("small-flat-lon", -2.0, 1.0, -2.0, 1.0, 5, 5, "lon_fastest"),
        ("small-flat-lat", -2.0, 1.0, -2.0, 1.0, 5, 5, "lat_fastest"),
    ]:
        flat = "flat" in name
        axes = (["member", "time", "level", "point"] if flat else
                ["member", "lat", "time", "level", "lon"] if "interleaved" in name else
                ["member", "time", "level", "lat", "lon"])
        if name == "small-flat-lat":
            layout = "lat_fastest"
        result.append({"id": name, "nx": nx, "ny": ny, "lon0": lon0, "dlon": dlon,
                       "lat0": lat0, "dlat": dlat, "layout": layout, "axes": axes,
                       "shape": [2, 3, 2, nx * ny] if flat else
                                ([2, ny, 3, 2, nx] if "interleaved" in name else [2, 3, 2, ny, nx]),
                       "variable_paths": [f"/{name}/temperature", f"/{name}/humidity"]})
    result.append({"id": "four-point-symmetric", "nx": 2, "ny": 2, "lon0": 0.0, "dlon": 1.0,
                   "lat0": -1.0, "dlat": 2.0, "layout": "lon_fastest", "axes": ["lat", "lon"],
                   "shape": [2, 2], "variable_paths": []})
    result.append({"id": "periodic-global", "nx": 8, "ny": 3, "lon0": -180.0, "dlon": 45.0,
                   "lat0": -90.0, "dlat": 90.0, "layout": "lon_fastest", "axes": ["lat", "lon"],
                   "shape": [3, 8], "variable_paths": []})
    result.append({"id": "axis-nearest-counterexample", "nx": 2, "ny": 2,
                   "lon0": -121.83048854140098, "dlon": 10.0,
                   "lat0": 23.30927414123454, "dlat": -1.0,
                   "layout": "lon_fastest", "axes": ["lat", "lon"],
                   "shape": [2, 2], "variable_paths": []})
    return result


def target_definitions() -> list[dict]:
    return [
        {"id": "exact-source", "bounds": [-0.25, -0.25, 0.25, 0.25]},
        {"id": "four-point-tie", "bounds": [0.25, -0.25, 0.75, 0.25]},
        {"id": "interior-gap", "bounds": [0.10, 0.10, 0.20, 0.20]},
        {"id": "near-equal", "bounds": [0.50000000000, -1e-11, 0.50000000002, 1e-11]},
        {"id": "outside-domain", "bounds": [10.0, 10.0, 11.0, 11.0]},
        {"id": "axis-nearest-counterexample",
         "bounds": [-117.5536203515942, 22.757592720108147,
                     -117.5536183515942, 22.757594720108147]},
    ]


def point_index(grid: dict, x: int, y: int) -> int:
    return x * grid["ny"] + y if grid["layout"] == "lat_fastest" else y * grid["nx"] + x


def ncep_gfs025_profile_vectors() -> list[dict]:
    """Independent spherical-nearest references across the full GFS025 profile."""
    nx, ny = 1440, 721
    lon0, lat0, step = -180.0, -90.0, 0.25
    targets = [
        ("south-near-pole", -180.0, -89.75),
        ("south-west-interior", -179.9, -89.7),
        ("southern-midlatitude", 73.123, -45.678),
        ("southern-equator-crossing", 179.91, -0.125),
        ("equatorial-seam-east", 179.9, 0.0),
        ("equatorial-seam-west", -179.9, 0.0),
        ("equatorial-four-way-midpoint", -179.875, 0.125),
        ("equatorial-exact-source", 0.0, 0.0),
        ("northern-midlatitude", -122.456, 63.789),
        ("north-near-pole", 179.75, 89.75),
        ("north-pole-band", 45.0, 89.999999),
        ("wrapped-longitude", 540.1, 21.375),
    ]
    rows = []
    for target_id, target_lon, target_lat in targets:
        winner = None
        tie_count = 0
        for y in range(ny):
            source_lat = lat0 + y * step
            for x in range(nx):
                source_lon = lon0 + x * step
                distance = haversine_f64(target_lon, target_lat, source_lon, source_lat)
                candidate = (distance, y * nx + x, x, y, source_lon, source_lat)
                if winner is None or candidate[:2] < winner[:2]:
                    winner = candidate
                    tie_count = 1
                elif distance == winner[0]:
                    tie_count += 1
        assert winner is not None
        rows.append({
            "target_id": target_id,
            "target_lon": target_lon,
            "target_lat": target_lat,
            "source_point_index": winner[1],
            "source_x": winner[2],
            "source_y": winner[3],
            "source_lon": winner[4],
            "source_lat": winner[5],
            "distance_m": winner[0],
            "high_precision_distance_m": str(
                spherical_vector_distance_decimal(target_lon, target_lat, winner[4], winner[5], RADIUS_M)),
            "tie_count": tie_count,
            "coordinate_comparisons": nx * ny,
        })
    return rows


def capability_boundary_cases() -> list[dict]:
    """Generate boundary representatives for binary64 qualification classes."""
    cases: list[dict] = []

    def add(case_id: str, *, nx: int = 4, ny: int = 2, lat0: float = -1.0,
            lon0: float = 0.0, dlat: float = 1.0, dlon: float = 1.0,
            earth_kind: str = "sphere", radius_m: float = RADIUS_M, version: int = 1,
            allow_out_of_range_latitude: bool = False, geometry: str = "regular") -> None:
        cases.append({"case_id": case_id, "nx": nx, "ny": ny, "lat0": lat0,
                      "lon0": lon0, "dlat": dlat, "dlon": dlon, "earth_kind": earth_kind,
                      "radius_m": radius_m, "version": version,
                      "allow_out_of_range_latitude": allow_out_of_range_latitude, "geometry": geometry})

    add("reject-zero-longitude-dimension", nx=0)
    add("reject-zero-latitude-dimension", ny=0)
    add("reject-signed-point-count-overflow", nx=1 << 62, ny=3)
    add("reject-longitude-index-beyond-exact-binary64", nx=(1 << 53) + 2, ny=1)
    add("reject-latitude-index-beyond-exact-binary64", nx=1, ny=(1 << 53) + 2, dlat=1e-300)
    add("valid-regional-positive")
    add("valid-regional-negative", lon0=3.0, dlon=-1.0)
    add("valid-global-period", nx=4, lon0=-180.0, dlon=90.0, lat0=-90.0, dlat=180.0)
    add("valid-latitude-band", nx=4, lon0=-180.0, dlon=90.0, lat0=-45.0, dlat=1.0)
    add("singleton-longitude", nx=1)
    add("singleton-latitude", ny=1)
    add("reject-latitude-below-domain", lat0=-90.00000000000001)
    add("accepted-latitude-upper-endpoint", lat0=89.0, dlat=1.0000000000000002)
    add("reject-latitude-collapse-subnormal", ny=2, lat0=90.0, dlat=math.ulp(0.0))
    add("reject-longitude-collapse-large-origin", nx=2, lon0=1e16, dlon=1.0)
    add("reject-duplicate-normalized-meridian", nx=2, dlon=360.0)
    add("reject-multiple-wraps", nx=5, dlon=100.0)
    add("valid-period-one-ulp-short", nx=4, lon0=-180.0, dlon=math.nextafter(90.0, 0.0))
    add("valid-regional-period-one-ulp-long", nx=4, lon0=-180.0,
        dlon=math.nextafter(90.0, math.inf))
    # The explicit non-periodic extent guard is unreachable after rejecting
    # full periods and multi-wrap axes. This case records its preempting wrap
    # check (raw span 360 degrees; computed period 450).
    add("reject-full-span-preempted-by-wrap-check", nx=5, dlon=90.0)
    add("reject-zero-longitude-step", nx=2, dlon=0.0)
    add("reject-negative-zero-longitude-step", nx=2, dlon=-0.0)
    add("reject-zero-latitude-step", ny=2, dlat=0.0)
    add("reject-negative-zero-latitude-step", ny=2, dlat=-0.0)
    add("reject-nan-longitude-origin", lon0=math.nan)
    add("reject-positive-infinity-longitude-step", dlon=math.inf)
    add("reject-negative-infinity-latitude-origin", lat0=-math.inf)
    add("reject-nan-latitude-step", dlat=math.nan)
    add("reject-displacement-overflow", nx=3, lon0=0.0, dlon=1e308)
    add("reject-nonfinite-longitude-period", nx=2, lon0=0.0, dlon=1e308)

    # Sweep representable spacing transitions at subnormal, normal, integer
    # precision, and large-exponent boundaries. Adjacent representable origins
    # and steps expose collapse, normalization duplicates, and overflow classes.
    exponents = [-1074, -1022, -1000, -500, -100, -1, 0, 1, 10, 32, 52, 53, 54, 100, 500, 900, 1020, 1023]
    for exponent in exponents:
        magnitude = math.ldexp(1.0, exponent) if exponent >= -1022 else math.ulp(0.0)
        for sign in (-1.0, 1.0):
            origin = sign * magnitude
            adjacent_origin = math.nextafter(origin, math.inf if sign > 0 else -math.inf)
            for step_name, step in (("subspacing", math.ulp(origin) / 2.0),
                                    ("spacing", math.ulp(origin)),
                                    ("twice-spacing", math.ulp(origin) * 2.0),
                                    ("adjacent-spacing", math.nextafter(math.ulp(origin), math.inf))):
                for origin_name, test_origin in (("origin", origin), ("adjacent-origin", adjacent_origin)):
                    cases.append({"case_id": f"ulp-e{exponent}-{sign:+.0f}-{step_name}-{origin_name}",
                                  "nx": 4, "ny": 2, "lat0": -1.0, "lon0": test_origin,
                                  "dlat": 1.0, "dlon": step})
    for exponent in range(-8, 9):
        step = math.ldexp(90.0, exponent)
        for step_value, label in ((step, "power"),
                                  (math.nextafter(step, 0.0), "below"),
                                  (math.nextafter(step, math.inf), "above")):
            cases.append({"case_id": f"period-e{exponent}-{label}", "nx": 4, "ny": 2,
                          "lat0": -1.0, "lon0": -180.0, "dlat": 1.0, "dlon": step_value})
    # Independent numeric inputs exercising qualification metadata and global
    # policy branches in addition to the finite axis-parameter CSV.
    add("reject-earth-radius-zero", radius_m=0.0)
    add("reject-earth-radius-negative", radius_m=-1.0)
    add("reject-earth-radius-nan", radius_m=math.nan)
    add("reject-earth-radius-infinity", radius_m=math.inf)
    add("reject-earth-ellipsoid", earth_kind="wgs84")
    add("reject-definition-version", version=2)
    add("reject-out-of-range-policy", lat0=-91.0, allow_out_of_range_latitude=True)
    add("reject-projected-geometry", geometry="projected")
    return cases


def independent_grid_qualification(case: dict) -> tuple[bool, str]:
    if case.get("version", 1) != 1:
        return False, "definition-version"
    if case.get("earth_kind", "sphere") != "sphere":
        return False, "earth-model"
    radius = case.get("radius_m", RADIUS_M)
    if not math.isfinite(radius) or radius <= 0:
        return False, "earth-radius"
    if case.get("geometry", "regular") != "regular":
        return False, "grid-geometry"
    if case.get("allow_out_of_range_latitude", False):
        return False, "latitude-policy"
    nx, ny = case["nx"], case["ny"]
    lat0, lon0, dlat, dlon = case["lat0"], case["lon0"], case["dlat"], case["dlon"]
    if nx <= 0 or ny <= 0:
        return False, "positive-dimensions"
    if nx * ny > (1 << 63) - 1:
        return False, "signed-point-count"
    if nx - 1 > (1 << 53) or ny - 1 > (1 << 53):
        return False, "exact-binary64-index"
    if not all(math.isfinite(value) for value in (lat0, lon0, dlat, dlon)):
        return False, "finite-axis-parameters"
    if dlat == 0.0 or dlon == 0.0:
        return False, "nonzero-axis-step"
    lat_end = lat0 + float(ny - 1) * dlat
    lon_end = lon0 + float(nx - 1) * dlon
    if not math.isfinite(lat_end) or not math.isfinite(lon_end):
        return False, "finite-axis-endpoint"
    lat_values = [lat0 + float(index) * dlat for index in range(ny)]
    lon_values = [lon0 + float(index) * dlon for index in range(nx)]
    if any(not math.isfinite(value) for value in lat_values + lon_values):
        return False, "finite-axis-coordinate"
    if any(value < -90.0 or value > 90.0 for value in (lat_values[0], lat_values[-1])):
        return False, "latitude-range"
    if ny > 1 and any((right <= left if dlat > 0 else right >= left)
                      for left, right in zip(lat_values, lat_values[1:])):
        return False, "latitude-binary64-collapse"
    if nx > 1 and any((right <= left if dlon > 0 else right >= left)
                      for left, right in zip(lon_values, lon_values[1:])):
        return False, "longitude-binary64-collapse"

    def normalize(longitude: float) -> float:
        result = math.fmod(longitude, 360.0)
        if result < -180.0:
            result += 360.0
        elif result >= 180.0:
            result -= 360.0
        return result

    # Match the first observable production rejection: adjacent normalized
    # duplicates are checked while walking the axis, before period/wrap limits.
    if nx > 1 and any(normalize(left) == normalize(right)
                      for left, right in zip(lon_values, lon_values[1:])):
        return False, "duplicate-normalized-longitude"
    full_span = float(nx) * abs(dlon) if nx > 1 else 0.0
    raw_span = abs(lon_values[-1] - lon_values[0])
    if not math.isfinite(full_span):
        return False, "nonfinite-period"
    if raw_span >= 360.0:
        return False, "multiple-longitude-wraps"
    periodic = nx >= 2 and full_span == 360.0 and raw_span < 360.0
    return True, "accepted-periodic" if periodic else "accepted-regional"


def value_rows(csv_path: Path) -> list[float]:
    with csv_path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    for index, row in enumerate(rows):
        if int(row["index"]) != index:
            raise ValueError(f"non-contiguous official reference indices in {csv_path}")
    return [float(row["value"]) for row in rows]


def logical_index(grid: dict, member: int, time: int, level: int, x: int, y: int) -> int:
    nx, ny = grid["nx"], grid["ny"]
    spatial = point_index(grid, x, y)
    axes, shape = grid["axes"], grid["shape"]
    indices = {"member": member, "time": time, "level": level, "point": spatial,
               "lat": y, "lon": x}
    value = 0
    for axis, size in zip(axes, shape):
        value = value * size + indices.get(axis, 0)
    return value


def build(output: Path) -> dict:
    generated_dir = output
    om_manifest_path = generated_dir / "om-fixtures.json"
    if not om_manifest_path.is_file():
        raise RuntimeError("generate official-reader OM fixtures first with duckomo_fixture_tool --sampling-output")
    om_manifest = json.loads(om_manifest_path.read_text())
    om_by_path = {}
    for fixture in om_manifest["fixtures"]:
        for variable in fixture["variables"]:
            om_by_path[(fixture["path"], variable["path"])] = variable

    geom_dir = output / "geometries"
    geom_dir.mkdir(parents=True, exist_ok=True)
    geometries = []
    targets = []
    for target in target_definitions():
        xmin, ymin, xmax, ymax = target["bounds"]
        lon, lat = xmin + (xmax - xmin) / 2.0, ymin + (ymax - ymin) / 2.0
        wkb = polygon_wkb(tuple(target["bounds"]))
        path = geom_dir / f"{target['id']}.wkb"
        path.write_bytes(wkb)
        geometries.append({"id": target["id"], "type": "Polygon", "wkb_path": path.relative_to(output).as_posix(),
                           "wkb_bytes": len(wkb), "wkb_sha256": sha256(path), "bounds": target["bounds"],
                           "representative": [lon, lat], "representative_rule": "analytic_rectangle_midpoint"})
        targets.append({"id": target["id"], "wkb_path": path.relative_to(output).as_posix(),
                        "representative_lon": lon, "representative_lat": lat})
    hole = polygon_rings_wkb([
        [(-2.0, -2.0), (2.0, -2.0), (2.0, 2.0), (-2.0, 2.0), (-2.0, -2.0)],
        [(-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0), (-1.0, -1.0)],
    ])
    hole_path = geom_dir / "polygon-with-hole.wkb"
    hole_path.write_bytes(hole)
    geometries.append({"id": "polygon-with-hole", "type": "Polygon",
                       "wkb_path": hole_path.relative_to(output).as_posix(), "wkb_bytes": len(hole),
                       "wkb_sha256": sha256(hole_path), "representative": None,
                       "representative_rule": "requires_pinned_spatial_ST_PointOnSurface"})
    empty = polygon_rings_wkb([])
    empty_path = geom_dir / "empty-polygon.wkb"
    empty_path.write_bytes(empty)
    geometries.append({"id": "empty-polygon", "type": "Polygon",
                       "wkb_path": empty_path.relative_to(output).as_posix(), "wkb_bytes": len(empty),
                       "wkb_sha256": sha256(empty_path), "representative": None,
                       "expected_status": "empty_geometry"})
    mp = multipolygon_wkb([(-2.0, -2.0, -1.0, -1.0), (1.0, 1.0, 2.0, 2.0)])
    mp_path = geom_dir / "two-component-multipolygon.wkb"
    mp_path.write_bytes(mp)
    geometries.append({"id": "two-component-multipolygon", "type": "MultiPolygon",
                       "wkb_path": mp_path.relative_to(output).as_posix(), "wkb_bytes": len(mp),
                       "wkb_sha256": sha256(mp_path), "representative": None,
                       "representative_rule": "requires_pinned_spatial_ST_PointOnSurface"})

    grids = grid_definitions()
    target_refs = []
    for grid in grids:
        nx, ny = grid["nx"], grid["ny"]
        for target in targets:
            lon, lat = target["representative_lon"], target["representative_lat"]
            lon_end = grid["lon0"] + (nx - 1) * grid["dlon"]
            lat_end = grid["lat0"] + (ny - 1) * grid["dlat"]
            is_periodic = grid["id"] == "periodic-global"
            lon_inside = is_periodic or min(grid["lon0"], lon_end) <= lon <= max(grid["lon0"], lon_end)
            lat_inside = min(grid["lat0"], lat_end) <= lat <= max(grid["lat0"], lat_end)
            is_inside = lon_inside and lat_inside
            if not is_inside:
                target_refs.append({"grid_id": grid["id"], "target_id": target["id"], "target_lon": lon,
                                    "target_lat": lat, "status": "outside_domain", "source_point_index": None,
                                    "source_x": None, "source_y": None, "source_lon": None, "source_lat": None,
                                    "distance_m": None, "high_precision_distance_m": None, "tie_count": 0,
                                    "distance_model": "sphere_haversine_f64_v1", "earth_radius_m": RADIUS_M})
                continue
            candidates = []
            for y in range(ny):
                source_lat = grid["lat0"] + y * grid["dlat"]
                for x in range(nx):
                    source_lon = grid["lon0"] + x * grid["dlon"]
                    distance = haversine_f64(lon, lat, source_lon, source_lat)
                    candidates.append((distance, point_index(grid, x, y), x, y, source_lon, source_lat))
            winner = min(candidates, key=lambda item: (item[0], item[1]))
            longitude_candidates = [
                (abs(math.remainder(grid["lon0"] + x * grid["dlon"] - lon, 360.0))
                 if is_periodic else abs(grid["lon0"] + x * grid["dlon"] - lon),
                 point_index(grid, x, 0), x)
                for x in range(nx)
            ]
            latitude_candidates = [
                (abs(grid["lat0"] + y * grid["dlat"] - lat), point_index(grid, 0, y), y)
                for y in range(ny)
            ]
            nearest_x = min(longitude_candidates)[2]
            nearest_y = min(latitude_candidates)[2]
            axis_nearest_index = point_index(grid, nearest_x, nearest_y)
            high_distance = spherical_vector_distance_decimal(lon, lat, winner[4], winner[5], RADIUS_M)
            item = {"grid_id": grid["id"], "target_id": target["id"],
                    "target_lon": lon, "target_lat": lat, "status": "ok",
                    "source_point_index": winner[1], "source_x": winner[2], "source_y": winner[3],
                    "axis_nearest_point_index": axis_nearest_index,
                    "source_lon": winner[4], "source_lat": winner[5], "distance_m": winner[0],
                    "high_precision_distance_m": str(high_distance),
                    "tie_count": sum(1 for candidate in candidates if candidate[0] == winner[0]),
                    "distance_model": "sphere_haversine_f64_v1", "earth_radius_m": RADIUS_M}
            if grid["id"] in {"small-separate", "small-interleaved", "small-reverse-lat", "small-reverse-lon",
                               "small-reverse-both", "small-flat-lon", "small-flat-lat"} and item["status"] == "ok":
                file = f"{grid['id']}.om"
                for variable_path in grid["variable_paths"]:
                    spec = om_by_path.get((file, variable_path))
                    if spec is None:
                        raise ValueError(f"official reader manifest lacks {file}:{variable_path}")
                    if spec.get("reference_path") is None:
                        continue
                    source_fixture = next(f for f in om_manifest["fixtures"] if f["path"] == file)
                    if source_fixture["size_bytes"] != (generated_dir / file).stat().st_size or \
                            source_fixture["sha256"] != sha256(generated_dir / file):
                        raise ValueError(f"official OM fixture hash/size mismatch: {file}")
                    reference_path = generated_dir / spec["reference_path"]
                    if sha256(reference_path) != spec["reference_sha256"]:
                        raise ValueError(f"official reader CSV hash mismatch: {spec['reference_path']}")
                    values = value_rows(reference_path)
                    combinations = []
                    for member in range(2):
                        for time in range(3):
                            for level in range(2):
                                index = logical_index(grid, member, time, level, winner[2], winner[3])
                                value = values[index]
                                combinations.append({"axis_indices": [member, time, level], "logical_index": index,
                                                    "source_point_index": winner[1],
                                                    "value": None if math.isnan(value) else value,
                                                    "variable_status": "source_null" if math.isnan(value) else "ok"})
                    item.setdefault("official_reader_values", {})[variable_path] = combinations
            target_refs.append(item)

    for grid in grids:
        if grid["id"] != "periodic-global":
            continue
        lon, lat = 179.9, 0.0
        candidates = []
        for y in range(grid["ny"]):
            source_lat = grid["lat0"] + y * grid["dlat"]
            for x in range(grid["nx"]):
                source_lon = grid["lon0"] + x * grid["dlon"]
                distance = haversine_f64(lon, lat, source_lon, source_lat)
                candidates.append((distance, point_index(grid, x, y), x, y, source_lon, source_lat))
        winner = min(candidates, key=lambda row: (row[0], row[1]))
        nearest_x = min((abs(math.remainder(grid["lon0"] + x * grid["dlon"] - lon, 360.0)),
                         point_index(grid, x, 0), x) for x in range(grid["nx"]))[2]
        nearest_y = min((abs(grid["lat0"] + y * grid["dlat"] - lat), point_index(grid, 0, y), y)
                        for y in range(grid["ny"]))[2]
        target_refs.append({"grid_id": grid["id"], "target_id": "periodic-seam", "target_lon": lon,
                            "target_lat": lat, "status": "ok", "source_point_index": winner[1],
                            "axis_nearest_point_index": point_index(grid, nearest_x, nearest_y),
                            "source_x": winner[2], "source_y": winner[3], "source_lon": winner[4],
                            "source_lat": winner[5], "distance_m": winner[0],
                            "high_precision_distance_m": str(
                                spherical_vector_distance_decimal(lon, lat, winner[4], winner[5], RADIUS_M)),
                            "tie_count": sum(candidate[0] == winner[0] for candidate in candidates),
                            "distance_model": "sphere_haversine_f64_v1", "earth_radius_m": RADIUS_M})

    scientific_vectors = [
        {"id": "exact-source", "target": [0.0, 0.0], "source": [0.0, 0.0], "bound": "ordinary"},
        {"id": "source-pole", "target": [10.0, 80.0], "source": [-170.0, 90.0], "bound": "ordinary"},
        {"id": "near-pole", "target": [12.0, 89.999], "source": [-167.0, 89.999999], "bound": "singular"},
        {"id": "antipodal", "target": [0.0, 0.0], "source": [180.0, 0.0], "bound": "singular"},
        {"id": "near-antipodal", "target": [0.0, 0.0], "source": [179.999999, 0.000001], "bound": "singular"},
        {"id": "ordinary-local", "target": [100.0, 20.0], "source": [100.25, 20.25], "bound": "ordinary"},
        {"id": "target-pole", "target": [10.0, 90.0], "source": [-170.0, 90.0], "bound": "ordinary"},
        {"id": "alternate-earth-radius", "target": [-30.0, 12.0], "source": [-29.75, 12.25],
         "radius_m": 6371000.0, "bound": "ordinary"},
        {"id": "small-positive-radius", "target": [0.0, 0.0], "source": [0.25, 0.0],
         "radius_m": 1e-307, "bound": "ordinary"},
        {"id": "large-finite-radius", "target": [0.0, 0.0], "source": [0.25, 0.0],
         "radius_m": 1e300, "bound": "ordinary"},
    ]
    for vector in scientific_vectors:
        vector.setdefault("radius_m", RADIUS_M)
        vector["distance_f64_m"] = haversine_f64(
            *vector["target"], *vector["source"], vector["radius_m"])
        vector["distance_high_precision_m"] = str(spherical_vector_distance_decimal(
            *vector["target"], *vector["source"], vector["radius_m"]))
    scientific_path = output / "scientific.reference.csv"
    write_csv(scientific_path,
              ["id", "target_lon", "target_lat", "source_lon", "source_lat", "radius_m",
               "distance_f64_m", "distance_high_precision_m", "bound"],
              [(row["id"], *row["target"], *row["source"], row["radius_m"], row["distance_f64_m"],
                row["distance_high_precision_m"], row["bound"]) for row in scientific_vectors])

    capability_cases = capability_boundary_cases()
    for case in capability_cases:
        case.setdefault("earth_kind", "sphere")
        case.setdefault("radius_m", RADIUS_M)
        case.setdefault("version", 1)
        case.setdefault("allow_out_of_range_latitude", False)
        case.setdefault("geometry", "regular")
    capability_path = output / "capability-boundaries.reference.csv"
    capability_rows = []
    capability_classifications = {}
    for case in capability_cases:
        accepted, reason = independent_grid_qualification(case)
        capability_classifications[case["case_id"]] = (accepted, reason)
        capability_rows.append((case["case_id"], case["nx"], case["ny"], case["lat0"], case["lon0"],
                                case["dlat"], case["dlon"], case["earth_kind"], case["radius_m"],
                                case["version"], str(case["allow_out_of_range_latitude"]).lower(),
                                case["geometry"], str(accepted).lower(), reason))
    preempted = capability_classifications["reject-full-span-preempted-by-wrap-check"]
    assert preempted == (False, "multiple-longitude-wraps"), preempted
    assert not any(not accepted and reason == "nonperiodic-full-span"
                   for accepted, reason in capability_classifications.values())
    write_csv(capability_path,
              ["case_id", "nx", "ny", "lat0", "lon0", "dlat", "dlon", "earth_kind", "radius_m",
               "version", "allow_out_of_range_latitude", "geometry", "accepted", "reason"],
              capability_rows)

    profile_rows = ncep_gfs025_profile_vectors()
    profile_path = output / "ncep-gfs025-profile.reference.csv"
    write_csv(profile_path,
              ["target_id", "target_lon", "target_lat", "source_point_index", "source_x", "source_y",
               "source_lon", "source_lat", "distance_m", "high_precision_distance_m", "tie_count",
               "coordinate_comparisons"],
              [(row["target_id"], row["target_lon"], row["target_lat"], row["source_point_index"],
                row["source_x"], row["source_y"], row["source_lon"], row["source_lat"], row["distance_m"],
                row["high_precision_distance_m"], row["tie_count"], row["coordinate_comparisons"])
               for row in profile_rows])

    # Generator self-checks: standard WKB framing, one exact source, and the
    # four-candidate integer tie resolved by the smallest original point index.
    assert struct.unpack_from("<BI", polygon_wkb((-1, -1, 1, 1))) == (1, 3)
    exact = next(row for row in target_refs if row["grid_id"] == "small-separate" and row["target_id"] == "exact-source")
    assert exact["distance_m"] == 0.0 and exact["source_point_index"] == 12
    tie = next(row for row in target_refs if row["grid_id"] == "four-point-symmetric" and row["target_id"] == "four-point-tie")
    assert tie["tie_count"] == 4 and tie["source_point_index"] == 0, tie
    flat_tie = next(row for row in target_refs if row["grid_id"] == "small-flat-lon" and row["target_id"] == "four-point-tie")
    assert flat_tie["tie_count"] == 2 and flat_tie["source_point_index"] == 12
    near_equal = next(row for row in target_refs if row["grid_id"] == "small-separate" and row["target_id"] == "near-equal")
    assert near_equal["tie_count"] == 1 and near_equal["source_point_index"] == 13, near_equal
    axis_counterexample = next(row for row in target_refs
                               if row["grid_id"] == "axis-nearest-counterexample" and
                                  row["target_id"] == "axis-nearest-counterexample")
    assert axis_counterexample["source_point_index"] == 0
    assert axis_counterexample["axis_nearest_point_index"] == 2, axis_counterexample
    for row in target_refs:
        if row.get("status") == "ok":
            row["max_distance_exact_m"] = row["distance_m"]
            row["threshold_below_m"] = math.nextafter(row["distance_m"], -math.inf)
            row["threshold_above_m"] = math.nextafter(row["distance_m"], math.inf)
    target_refs.append({"grid_id": None, "target_id": "empty-polygon", "target_lon": None, "target_lat": None,
                        "status": "empty_geometry", "source_point_index": None, "source_x": None,
                        "source_y": None, "source_lon": None, "source_lat": None, "distance_m": None,
                        "high_precision_distance_m": None, "tie_count": 0,
                        "geometry_path": "geometries/empty-polygon.wkb"})
    write_csv(output / "locator.reference.csv",
              ["grid_id", "target_id", "status", "source_point_index", "source_lon", "source_lat",
               "distance_m", "high_precision_distance_m", "tie_count", "axis_nearest_point_index"],
              [(row.get("grid_id"), row["target_id"], row["status"], row.get("source_point_index"),
                row.get("source_lon"), row.get("source_lat"), row.get("distance_m"),
                row.get("high_precision_distance_m"), row.get("tie_count", 0),
                row.get("axis_nearest_point_index")) for row in target_refs])
    manifest = {"schema_version": 1,
                "oracle": "independent exhaustive Python binary64 sphere_haversine_f64_v1 compatibility calculation plus 90-digit Decimal unit-vector cross/dot scientific distance; values from pinned official OM C reader",
                "numeric_contract": {"algorithm": "sphere_haversine_f64_v1", "earth_radius_m": RADIUS_M,
                                     "tie_break": ["distance_m ascending", "original source_point_index ascending"],
                                     "coordinate_arithmetic": "binary64 origin + index * step",
                                     "scientific_distance_precision": 90},
                "om_fixture_manifest": {"path": "om-fixtures.json", "sha256": sha256(om_manifest_path)},
                "resource_fixtures": {
                    "skippable_values": {
                        "path": "skippable-values.om",
                        "size_bytes": next(item["size_bytes"] for item in om_manifest["fixtures"]
                                            if item["path"] == "skippable-values.om"),
                        "sha256": next(item["sha256"] for item in om_manifest["fixtures"]
                                       if item["path"] == "skippable-values.om"),
                        "variable_path": "/skippable-values/temperature",
                        "shape": [1024, 1024], "chunk_shape": [16, 16],
                        "axes": ["lat", "lon"],
                        "reference_path": "skippable-values.temperature.csv",
                        "reference_sha256": next(
                            variable["reference_sha256"]
                            for item in om_manifest["fixtures"] if item["path"] == "skippable-values.om"
                            for variable in item["variables"]
                            if variable["path"] == "/skippable-values/temperature"),
                        "acceptance": "full baseline and nearest selected source share this exact variable and file"
                    }
                },
                "locator_reference_csv": {"path": "locator.reference.csv", "sha256": sha256(output / "locator.reference.csv")},
                "scientific_reference_csv": {"path": "scientific.reference.csv", "sha256": sha256(scientific_path),
                                             "rows": len(scientific_vectors),
                                             "method": "atan2(norm(cross(unit_target, unit_source)), dot(unit_target, unit_source))",
                                             "ordinary_error_bound": "max(1e-6 m, reference_distance * 1e-12)",
                                             "singular_error_bound_m": 2.0},
                "capability_boundary_reference_csv": {"path": "capability-boundaries.reference.csv",
                                                       "sha256": sha256(capability_path),
                                                       "rows": len(capability_rows),
                                                       "classes": sorted({row[-1] for row in capability_rows})},
                "ncep_gfs025_profile_reference_csv": {"path": "ncep-gfs025-profile.reference.csv",
                                                       "sha256": sha256(profile_path),
                                                       "rows": len(profile_rows), "nx": 1440, "ny": 721,
                                                       "coordinate_comparisons_per_target": 1440 * 721,
                                                       "oracle": "independent exhaustive Python enumeration; source index tie-break; 90-digit Decimal unit-vector cross/dot scientific distance"},
                "grids": grids, "geometries": geometries, "targets": targets,
                "expected_nearest": target_refs}
    path = output / "manifest.json"
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(manifest, indent=2, sort_keys=True, allow_nan=False) + "\n")
    temporary.replace(path)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "test/data/sampling")
    parser.add_argument("--self-check", action="store_true", help="generate references and validate internal invariants")
    args = parser.parse_args()
    try:
        manifest = build(args.output.resolve())
    except (OSError, ValueError, RuntimeError, AssertionError, json.JSONDecodeError) as error:
        print(f"generate-sampling-reference: {error}", file=sys.stderr)
        return 1
    print(json.dumps({"manifest": str((args.output.resolve() / "manifest.json")),
                      "grids": len(manifest["grids"]), "geometries": len(manifest["geometries"]),
                      "nearest_references": len(manifest["expected_nearest"]), "self_check": "passed"}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
