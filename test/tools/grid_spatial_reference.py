#!/usr/bin/env python3
"""Generate small, independent planar point/polygon reference cases.

The calculations below are standalone Python formula implementations. They do
not import DuckOMO or its registry generator; the output is an oracle for
angular-coordinate spatial examples, not a datum transform or physical-area
reference.
"""

from __future__ import annotations

import argparse
import csv
import copy
from datetime import datetime, timedelta
import gzip
import json
import math
import re
import subprocess
import sys
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / "test/data/grids/spatial-relations.json"
SAMPLE_VIEW_PATTERN = re.compile(r"(?ms)^CREATE TEMP VIEW\s+([A-Za-z_][A-Za-z_0-9]*)\s+AS\b.*?;")
READ_ARGUMENTS_PATTERN = re.compile(r"(?is)FROM\s+read_om\(\s*(.*?)\s*\)\s*;\s*$")
PI = math.pi
PUBLIC_COORDINATE_TARGETS = {
    "gem_rdps_10km": "rotated_v3",
    "gem_regional": "stereographic_v3",
    "aladin_central_europe_2km": "lambert_v3",
}
PUBLIC_SPATIAL_SELECTION_HALF_WIDTH_DEGREES = 0.2
PUBLIC_SOURCE_COLUMNS = (
    "object_id", "object_version", "version_strength", "content_verified",
    "grid_id", "layout_id", "logical_index", "point_index", "parent_point_index",
    "axis_indices", "latitude", "longitude",
)


CASES: tuple[dict[str, Any], ...] = (
    {
        "id": "rotated_latlon_small",
        "grid_type": "rotated_latlon",
        "definition": {
            "numeric_policy": "float64_v1",
            "layout": {"nx": 3, "ny": 2, "order": "separate"},
            "parameters": {
                "x0": -2.0, "y0": -1.0, "dx": 2.0, "dy": 2.0,
                "north_pole_latitude": 90.0, "north_pole_longitude": 0.0, "rotation": 0.0,
            },
        },
        "polygon": [[-0.5, -3.0], [0.5, -3.0], [0.5, 3.0], [-0.5, 3.0], [-0.5, -3.0]],
    },
    {
        "id": "lambert_small",
        "grid_type": "lambert_conformal_conic",
        "definition": {
            "numeric_policy": "float64_v1",
            "layout": {"nx": 3, "ny": 2, "order": "separate"},
            "earth": {"model": "sphere", "radius_m": 6371229.0},
            "parameters": {
                "x0": -10000.0, "y0": -10000.0, "dx": 10000.0, "dy": 10000.0,
                "central_meridian": 17.0, "latitude_of_origin": 46.244,
                "standard_parallel_1": 40.0, "standard_parallel_2": 50.0,
            },
        },
        "polygon": [[16.5, 45.5], [17.5, 45.5], [17.5, 47.0], [16.5, 45.5]],
    },
    {
        "id": "stereographic_small",
        "grid_type": "stereographic",
        "definition": {
            "numeric_policy": "float64_v1",
            "layout": {"nx": 3, "ny": 2, "order": "separate"},
            "earth": {"model": "sphere", "radius_m": 6371229.0},
            "parameters": {
                "x0": -10000.0, "y0": -5000.0, "dx": 10000.0, "dy": 10000.0,
                "central_meridian": 10.0, "latitude_of_origin": 45.0, "scale_factor": 1.0,
            },
        },
        "polygon": [[9.0, 44.0], [11.0, 44.0], [10.2, 46.0], [9.0, 44.0]],
    },
    {
        "id": "reduced_gaussian_small",
        "grid_type": "reduced_gaussian",
        "definition": {
            "numeric_policy": "float64_v1",
            "layout": {"order": "row_major"},
            "earth": {"model": "wgs84_source", "semi_major_axis_m": 6378137.0,
                      "inverse_flattening": 298.257223563},
            "parameters": {
                "n": 1,
                "latitude_rule": "explicit_v1",
                "rows": [
                    {"latitude": 60.0, "point_count": 4, "longitude_origin": 0.0, "longitude_step": 90.0},
                    {"latitude": -60.0, "point_count": 4, "longitude_origin": 0.0, "longitude_step": 90.0},
                ],
                "subset_segments": None,
            },
        },
        "polygon": [[-10.0, 30.0], [100.0, 30.0], [100.0, 80.0], [-10.0, 30.0]],
    },
)


def normalize_longitude(value: float) -> float:
    value = math.fmod(value, 360.0)
    if value < -180.0:
        value += 360.0
    if value >= 180.0:
        value -= 360.0
    return value


def rotated_coordinate(definition: dict[str, Any], x: int, y: int) -> tuple[float, float]:
    p = definition["parameters"]
    native_latitude = p["y0"] + y * p["dy"]
    native_longitude = p["x0"] + x * p["dx"] + p["rotation"]
    pole_lat = math.radians(p["north_pole_latitude"])
    pole_lon = math.radians(p["north_pole_longitude"])
    lat = math.radians(native_latitude)
    lon = math.radians(native_longitude)
    cp, sp = math.cos(pole_lat), math.sin(pole_lat)
    cl, sl = math.cos(pole_lon), math.sin(pole_lon)
    c = math.cos(lat)
    vx = c * math.cos(lon) * sp * cl - c * math.sin(lon) * sl + math.sin(lat) * cp * cl
    vy = c * math.cos(lon) * sp * sl + c * math.sin(lon) * cl + math.sin(lat) * cp * sl
    vz = -c * math.cos(lon) * cp + math.sin(lat) * sp
    return math.degrees(math.asin(max(-1.0, min(1.0, vz)))), normalize_longitude(math.degrees(math.atan2(vy, vx)))


def lambert_coordinate(definition: dict[str, Any], x: int, y: int) -> tuple[float, float]:
    p = definition["parameters"]
    radius = definition["earth"]["radius_m"]
    phi1, phi2 = math.radians(p["standard_parallel_1"]), math.radians(p["standard_parallel_2"])
    phi0, lambda0 = math.radians(p["latitude_of_origin"]), math.radians(p["central_meridian"])
    if p["standard_parallel_1"] == p["standard_parallel_2"]:
        n = math.sin(phi1)
    else:
        n = math.log(math.cos(phi1) / math.cos(phi2)) / math.log(
            math.tan(PI / 4 + phi2 / 2) / math.tan(PI / 4 + phi1 / 2)
        )
    f = math.cos(phi1) * math.tan(PI / 4 + phi1 / 2) ** n / n
    rho0 = radius * f / math.tan(PI / 4 + phi0 / 2) ** n
    native_x = p["x0"] + x * p["dx"]
    native_y = p["y0"] + y * p["dy"]
    theta = math.atan2(native_x / radius, rho0 / radius - native_y / radius)
    rho = math.hypot(native_x / radius, rho0 / radius - native_y / radius)
    signed_rho = math.copysign(rho, n)
    latitude = 2 * math.atan((f / signed_rho) ** (1 / n)) - PI / 2
    longitude = lambda0 + theta / n
    return math.degrees(latitude), normalize_longitude(math.degrees(longitude))


def stereographic_coordinate(definition: dict[str, Any], x: int, y: int) -> tuple[float, float]:
    p = definition["parameters"]
    radius = definition["earth"]["radius_m"]
    phi0 = math.radians(p["latitude_of_origin"])
    lambda0 = math.radians(p["central_meridian"])
    native_x = p["x0"] + x * p["dx"]
    native_y = p["y0"] + y * p["dy"]
    rho = math.hypot(native_x, native_y)
    if rho == 0:
        return p["latitude_of_origin"], normalize_longitude(p["central_meridian"])
    c = 2 * math.atan2(rho, 2 * radius * p["scale_factor"])
    latitude = math.asin(math.cos(c) * math.sin(phi0) + native_y * math.sin(c) * math.cos(phi0) / rho)
    longitude = lambda0 + math.atan2(
        native_x * math.sin(c), rho * math.cos(phi0) * math.cos(c) - native_y * math.sin(phi0) * math.sin(c)
    )
    return math.degrees(latitude), normalize_longitude(math.degrees(longitude))


def grid_points(case: dict[str, Any]) -> list[dict[str, Any]]:
    definition = case["definition"]
    p = definition["parameters"]
    nx = definition["layout"].get("nx")
    ny = definition["layout"].get("ny")
    points: list[dict[str, Any]] = []
    if case["grid_type"] == "reduced_gaussian":
        logical = 0
        for row_index, row in enumerate(p["rows"]):
            for x in range(row["point_count"]):
                lon = normalize_longitude(row["longitude_origin"] + x * row["longitude_step"])
                points.append({"logical_index": logical, "row": row_index, "column": x,
                               "longitude": lon, "latitude": row["latitude"]})
                logical += 1
        return points
    transforms = {
        "rotated_latlon": rotated_coordinate,
        "lambert_conformal_conic": lambert_coordinate,
        "stereographic": stereographic_coordinate,
    }
    transform = transforms[case["grid_type"]]
    logical = 0
    for y in range(ny):
        for x in range(nx):
            latitude, longitude = transform(definition, x, y)
            points.append({"logical_index": logical, "row": y, "column": x,
                           "longitude": longitude, "latitude": latitude})
            logical += 1
    return points


def on_segment(point: tuple[float, float], left: tuple[float, float], right: tuple[float, float]) -> bool:
    cross = (point[0] - left[0]) * (right[1] - left[1]) - (point[1] - left[1]) * (right[0] - left[0])
    if abs(cross) > 1e-12:
        return False
    dot = (point[0] - left[0]) * (point[0] - right[0]) + (point[1] - left[1]) * (point[1] - right[1])
    return dot <= 1e-12


def polygon_covers(point: tuple[float, float], polygon: list[list[float]]) -> bool:
    inside = False
    for index in range(len(polygon) - 1):
        left = (polygon[index][0], polygon[index][1])
        right = (polygon[index + 1][0], polygon[index + 1][1])
        if on_segment(point, left, right):
            return True
        crosses = (left[1] > point[1]) != (right[1] > point[1])
        if crosses:
            x_intersection = left[0] + (point[1] - left[1]) * (right[0] - left[0]) / (right[1] - left[1])
            if point[0] < x_intersection:
                inside = not inside
    return inside


def relation_case(case: dict[str, Any]) -> dict[str, Any]:
    points = grid_points(case)
    polygon = case["polygon"]
    shape = [len(points)] if case["grid_type"] == "reduced_gaussian" else [
        case["definition"]["layout"]["ny"], case["definition"]["layout"]["nx"]
    ]
    bbox = [min(point[0] for point in polygon), min(point[1] for point in polygon),
            max(point[0] for point in polygon), max(point[1] for point in polygon)]
    matches = []
    bbox_candidates = []
    point_relations = []
    for point in points:
        lon, lat = point["longitude"], point["latitude"]
        candidate = bbox[0] <= lon <= bbox[2] and bbox[1] <= lat <= bbox[3]
        covered = polygon_covers((lon, lat), polygon)
        if covered and not candidate:
            raise ValueError(f"bbox rejected a polygon match in {case['id']} at {point['logical_index']}")
        if candidate:
            bbox_candidates.append(point["logical_index"])
        point_relations.append({"logical_index": point["logical_index"], "covered": covered})
        if covered:
            matches.append(point)
    return {
        "id": case["id"],
        "grid_type": case["grid_type"],
        "definition": case["definition"],
        "shape": shape,
        "polygon_xy": polygon,
        "polygon_axis_order": ["longitude", "latitude"],
        "relation": "planar_covers_in_angular_degrees",
        "candidate_bbox_xy": bbox,
        "candidate_bbox_role": "conservative_candidate_only",
        "input_point_count": len(points),
        "bbox_candidate_positions": bbox_candidates,
        "oracle_matches": matches,
        "point_relations": point_relations,
    }


def generate() -> dict[str, Any]:
    return {
        "schema_version": 1,
        "reference_status": "synthetic_independent_planar_oracles",
        "coordinate_space": {
            "values": "declared grid longitude/latitude outputs in degrees",
            "planar_xy": ["longitude", "latitude"],
            "polygon_coordinates_are": "the same emitted angular coordinate pairs; no datum transform is applied",
            "longitude_normalization": "[-180,180)",
            "boundary_policy": "polygon boundary is covered",
            "runtime_dependency": "ordinary DuckOMO reads do not require a spatial extension",
            "claim_limits": ["no datum transformation", "no physical distance or area", "no cell boundary or adjacency"],
        },
        "oracle_method": {
            "coordinate_formulas": "standalone Python double-precision implementations of the declared fixture transforms",
            "point_polygon": "independent planar ray-crossing with inclusive segment check",
            "tested_kernel_imported": False,
            "scope": "small deterministic API examples; not real producer coordinate/value validation",
        },
        "cases": [relation_case(case) for case in CASES],
    }


def sql_string(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def validation_time_values_literal(count: int) -> str:
    if count <= 0:
        raise ValueError("validation time axis must have a positive length")
    start = datetime(2000, 1, 1)
    timestamps = [
        "TIMESTAMP " + sql_string((start + timedelta(seconds=index)).strftime("%Y-%m-%d %H:%M:%S"))
        for index in range(count)
    ]
    return "[" + ", ".join(timestamps) + "]"


def inject_validation_times(view_sql: str, timestamps_sql: str) -> str:
    match = READ_ARGUMENTS_PATTERN.search(view_sql)
    if match is None:
        raise ValueError("cannot find read_om arguments while adding validation time coordinates")
    arguments = match.group(1)
    if re.search(r"\bvalid_times\s*:=", arguments, flags=re.IGNORECASE):
        raise ValueError("public source view already declares valid_times")
    marker = re.search(r",\s*(?:grid|domain)\s*:=", arguments, flags=re.IGNORECASE)
    if marker is None:
        raise ValueError("public source view has no grid/domain argument for validation time insertion")
    arguments = arguments[:marker.start()] + ", valid_times := " + timestamps_sql + arguments[marker.start():]
    return view_sql[:match.start(1)] + arguments + view_sql[match.end(1):]


def duckdb_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, str):
        return sql_string(value)
    if isinstance(value, (int, float)):
        return repr(value)
    if isinstance(value, list):
        return "[" + ", ".join(duckdb_literal(item) for item in value) + "]"
    if isinstance(value, dict):
        return "{" + ", ".join(
            sql_string(str(key)) + ": " + duckdb_literal(item) for key, item in value.items()
        ) + "}"
    raise TypeError(f"cannot encode {type(value).__name__} as a DuckDB literal")


def validation_grid(case: dict[str, Any]) -> dict[str, Any]:
    definition = copy.deepcopy(case["definition"])
    parameters = definition["parameters"]
    if case["grid_type"] == "reduced_gaussian":
        parameters["subset_segments"] = None
    earth = definition.setdefault("earth", {"model": "sphere", "radius_m": 6371229.0})
    if earth.get("model") == "wgs84_source":
        earth["model"] = "wgs84"
        if "semi_major_axis_m" in earth:
            earth["semi_major_m"] = earth.pop("semi_major_axis_m")
    return {
        "version": 1,
        "type": case["grid_type"],
        "numeric_policy": definition["numeric_policy"],
        "earth": definition["earth"],
        "layout": definition["layout"],
        "parameters": parameters,
    }


def validation_read(case: dict[str, Any], root: Path) -> tuple[str, str]:
    if case["grid_type"] == "reduced_gaussian":
        path = root / "test/data/grids/spatial-relations-gaussian.om"
        variables = ["spatial_relations/gaussian_value"]
        axes = [["point"]]
        spatial_axes = ["point"]
    else:
        path = root / "test/data/raw.om"
        variables = ["value"]
        axes = [["row", "column"]]
        spatial_axes = ["row", "column"]
    arguments = (
        sql_string(str(path))
        + ", dimensions := map("
        + duckdb_literal(variables)
        + ", "
        + duckdb_literal(axes)
        + "), grid := "
        + duckdb_literal(validation_grid(case))
        + ", spatial_axes := "
        + duckdb_literal(spatial_axes)
    )
    return (
        "read_om(" + arguments + ", include_source := true)",
        "om_grid_info(" + arguments + ")",
    )


def sha256_file(path: Path) -> str:
    import hashlib

    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def resolve_public_coordinate_reference(root: Path, sample: dict[str, Any],
                                        manifest: dict[str, Any] | None = None) -> dict[str, Any]:
    grid_id = sample.get("grid_definition_id")
    target = PUBLIC_COORDINATE_TARGETS.get(grid_id)
    if target is None:
        raise ValueError(f"public source coordinate target is unsupported: {grid_id}")
    if manifest is None:
        manifest = json.loads((root / "test/data/grids/coordinate-reference.json").read_text(encoding="utf-8"))
    matches = [record for record in manifest.get("references", []) if record.get("target") == target]
    if len(matches) != 1:
        raise ValueError(f"pinned coordinate reference is missing or duplicated for {target}")
    reference = matches[0]
    if reference.get("method") != "pinned_producer_coordinates" or reference.get("sample_id") != sample.get("id"):
        raise ValueError(f"{target} coordinate reference is not pinned to this producer sample")
    shape = sample.get("array", {}).get("shape")
    if not isinstance(shape, list) or len(shape) < 2 or any(not isinstance(size, int) or size <= 0 for size in shape[:2]):
        raise ValueError(f"{target} sample has no positive spatial shape")
    expected_rows = shape[0] * shape[1]
    if reference.get("row_count") != expected_rows:
        raise ValueError(f"{target} coordinate reference row count does not match the frozen sample shape")
    numeric_policy = reference.get("numeric_policy")
    tolerance = reference.get("tolerance_degrees")
    if numeric_policy != "openmeteo_f32_v1" or not isinstance(tolerance, (int, float)) or \
            isinstance(tolerance, bool) or not 0 < tolerance <= 1e-4:
        raise ValueError(f"{target} coordinate reference does not use the frozen Float32 tolerance")
    artifact = reference.get("artifact")
    artifact_path_value = artifact.get("path") if isinstance(artifact, dict) else None
    expected_hash = artifact.get("sha256") if isinstance(artifact, dict) else None
    if not isinstance(artifact_path_value, str) or not isinstance(expected_hash, str):
        raise ValueError(f"{target} coordinate reference has no artifact path/hash")
    artifact_path = Path(artifact_path_value)
    if not artifact_path.is_absolute():
        artifact_path = root / artifact_path
    if not artifact_path.is_file() or sha256_file(artifact_path) != expected_hash:
        raise ValueError(f"{target} coordinate reference artifact is missing or has a hash mismatch")
    return {
        "target": target,
        "method": reference["method"],
        "path": str(artifact_path),
        "sha256": expected_hash,
        "row_count": expected_rows,
        "tolerance_degrees": float(tolerance),
        "numeric_policy": numeric_policy,
    }


def resolve_public_spatial_selection_bounds(coordinate_reference: dict[str, Any],
                                            spatial_count: int) -> dict[str, Any]:
    if spatial_count <= 0:
        raise ValueError("public spatial selection requires a positive spatial count")
    anchor_index = spatial_count // 2
    anchor: dict[str, str] | None = None
    with Path(coordinate_reference["path"]).open(encoding="utf-8", newline="") as stream:
        for row in csv.DictReader(stream):
            if int(row["spatial_index"]) == anchor_index:
                anchor = row
                break
    if anchor is None:
        raise ValueError(f"pinned coordinate reference has no center position {anchor_index}")
    latitude = float(anchor["latitude"])
    longitude = float(anchor["longitude"])
    if not math.isfinite(latitude) or not math.isfinite(longitude):
        raise ValueError("pinned center coordinate is not finite")
    half_width = PUBLIC_SPATIAL_SELECTION_HALF_WIDTH_DEGREES
    return {
        "anchor_spatial_index": anchor_index,
        "anchor_latitude": latitude,
        "anchor_longitude": longitude,
        "half_width_degrees": half_width,
        "latitude_min": max(-90.0, latitude - half_width),
        "latitude_max": min(90.0, latitude + half_width),
        "longitude_min": max(-180.0, longitude - half_width),
        "longitude_max": min(180.0, longitude + half_width),
        "selection_policy": "fixed closed 0.2-degree box around the center position in the hash-pinned producer coordinate reference",
    }


def sql_double(value: float) -> str:
    if not math.isfinite(value):
        raise ValueError("SQL spatial-selection bound must be finite")
    return repr(value)


def public_sample_source_uri(item: dict[str, Any], uri_kind: str) -> str | None:
    if uri_kind == "local":
        return None
    field = {"https": "https_url", "s3": "s3_uri"}.get(uri_kind)
    if field is None:
        raise ValueError(f"unsupported public source URI kind: {uri_kind}")
    source_object = item.get("source_object")
    uri = source_object.get(field) if isinstance(source_object, dict) else None
    expected_prefix = "https://" if uri_kind == "https" else "s3://"
    if not isinstance(uri, str) or not uri.startswith(expected_prefix):
        raise ValueError(f"{item.get('id', 'sample')} has no declared public {uri_kind} OM URI")
    return uri


def public_sample_s3_region(item: dict[str, Any]) -> str | None:
    source_object = item.get("source_object")
    https_url = source_object.get("https_url") if isinstance(source_object, dict) else None
    if not isinstance(https_url, str):
        return None
    host = urlsplit(https_url).hostname or ""
    match = re.search(r"(?:^|\.)s3[.-]([a-z0-9-]+)\.amazonaws\.com$", host, flags=re.IGNORECASE)
    return match.group(1).lower() if match else None


def replace_public_source_uri(view_sql: str, source_uri: str) -> str:
    source_pattern = re.compile(r"(?is)(FROM\s+read_om\(\s*)'((?:''|[^'])*)'")
    match = source_pattern.search(view_sql)
    if match is None:
        raise ValueError("cannot find the source path in the generated read_om view")
    escaped_uri = source_uri.replace("'", "''")
    return view_sql[:match.start(2) - 1] + "'" + escaped_uri + "'" + view_sql[match.end(2) + 1:]


def prepare_public_sample_queries(root: Path, output_dir: Path, *, full_positions: bool = False,
                                  first_valid_times: dict[str, str] | None = None,
                                  time_probe_only: bool = False,
                                  spatial_selection: bool = False,
                                  source_uri_kind: str = "local") -> tuple[list[str], list[dict[str, Any]], str]:
    if spatial_selection and not full_positions:
        return [], [], "public spatial-selection source check requires full materialized positions"
    manifest_path = root / "test/data/grids/sample-manifest.json"
    query_path = root / "test/data/grids/sample-queries.sql"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        query_text = query_path.read_text(encoding="utf-8")
        coordinate_manifest = json.loads(
            (root / "test/data/grids/coordinate-reference.json").read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as error:
        return [], [], f"public sample source/coordinate reference inputs are unavailable: {error}"
    views = {match.group(1): match.group(0) for match in SAMPLE_VIEW_PATTERN.finditer(query_text)}
    objects = manifest.get("public_open_meteo_samples", {}).get("objects", [])
    candidates = [
        item for item in objects
        if isinstance(item, dict)
        and item.get("classification") == "real_public_open_meteo_om_v3"
        and isinstance(item.get("grid_definition_id"), str)
        and isinstance(item.get("local_copy"), dict)
    ]
    projected = [item for item in candidates if item["grid_definition_id"] in
                 {"gem_rdps_10km", "gem_regional", "aladin_central_europe_2km"}]
    if len(projected) != 3:
        return [], [], "public local source smoke requires the three frozen real projected samples"

    statements: list[str] = []
    records: list[dict[str, Any]] = []
    for item in projected:
        grid_id = item["grid_definition_id"]
        try:
            coordinate_reference = resolve_public_coordinate_reference(root, item, coordinate_manifest)
        except (OSError, ValueError, KeyError, TypeError) as error:
            return [], [], f"public coordinate reference is unavailable for {grid_id}: {error}"
        expected_rows = coordinate_reference["row_count"]
        explicit_name = "explicit_" + grid_id
        domain_name = "domain_" + grid_id
        explicit = views.get(explicit_name)
        domain = views.get(domain_name)
        if explicit is None or domain is None:
            return [], [], f"generated explicit/domain views are missing for public sample {grid_id}"
        try:
            source_uri = public_sample_source_uri(item, source_uri_kind)
            if source_uri is not None:
                explicit = replace_public_source_uri(explicit, source_uri)
                domain = replace_public_source_uri(domain, source_uri)
        except ValueError as error:
            return [], [], str(error)
        local = item["local_copy"].get("path")
        expected_hash = item.get("sha256")
        path = Path(local) if isinstance(local, str) else Path()
        if not path.is_absolute():
            path = root / path
        if not path.is_file() or not isinstance(expected_hash, str) or sha256_file(path) != expected_hash:
            return [], [], f"public local source sample is missing or does not match its frozen hash: {local}"

        match = READ_ARGUMENTS_PATTERN.search(explicit)
        if match is None:
            return [], [], f"cannot extract read_om arguments from {explicit_name}"
        explicit_arguments = match.group(1)
        grid_marker = re.search(r",\s*grid\s*:=", explicit_arguments, flags=re.IGNORECASE)
        if grid_marker is None:
            return [], [], f"cannot isolate non-grid read_om arguments from {explicit_name}"
        time_probe_arguments = explicit_arguments[:grid_marker.start()]
        domain_match = READ_ARGUMENTS_PATTERN.search(domain)
        if domain_match is None:
            return [], [], f"cannot extract read_om arguments from {domain_name}"
        domain_arguments = domain_match.group(1)
        validation_times_sql: str | None = None
        if full_positions or time_probe_only:
            shape = item.get("array", {}).get("shape")
            if not isinstance(shape, list) or len(shape) != 3 or not isinstance(shape[2], int) or shape[2] <= 0:
                return [], [], f"public sample {grid_id} has no valid [y,x,time] shape for full source validation"
            validation_times_sql = validation_time_values_literal(shape[2])
            time_probe_arguments = time_probe_arguments.rstrip() + ", valid_times := " + validation_times_sql
            if not time_probe_only:
                explicit = inject_validation_times(explicit, validation_times_sql)
                domain = inject_validation_times(domain, validation_times_sql)
                explicit_arguments = READ_ARGUMENTS_PATTERN.search(explicit).group(1)
                domain_arguments = READ_ARGUMENTS_PATTERN.search(domain).group(1)
                record_time_source = "validator_declared_synthetic_labels_for_source_axis_index_selection"
            else:
                record_time_source = "validator_declared_synthetic_labels_for_source_axis_index_selection"
        explicit_info_arguments = re.sub(r",\s*include_source\s*:=\s*true\s*$", "", explicit_arguments,
                                         flags=re.IGNORECASE)
        domain_info_arguments = re.sub(r",\s*include_source\s*:=\s*true\s*$", "", domain_arguments,
                                       flags=re.IGNORECASE)
        slug = grid_id
        record: dict[str, Any] = {
            "id": item["id"],
            "grid_definition_id": grid_id,
            "local_path": str(path),
            "local_sha256": expected_hash,
            "source_uri_kind": source_uri_kind,
            "source_uri": source_uri,
            "s3_region": public_sample_s3_region(item) if source_uri_kind == "s3" else None,
            "remote_source_object_snapshot": item.get("source_object") if source_uri is not None else None,
            "shape": item["array"]["shape"],
            "coordinate_reference_csv": coordinate_reference["path"],
            "coordinate_reference_sha256": coordinate_reference["sha256"],
            "coordinate_reference_method": coordinate_reference["method"],
            "coordinate_tolerance_degrees": coordinate_reference["tolerance_degrees"],
            "explicit_view": explicit_name,
            "domain_view": domain_name,
            "explicit_source_csv": slug + ".explicit-source.csv",
            "explicit_source_metrics_csv": slug + ".explicit-source-metrics.csv",
            "domain_source_csv": slug + ".domain-source.csv",
            "domain_source_metrics_csv": slug + ".domain-source-metrics.csv",
            "explicit_info_csv": slug + ".explicit-info.csv",
            "explicit_info_metrics_csv": slug + ".explicit-info-metrics.csv",
            "domain_info_csv": slug + ".domain-info.csv",
            "domain_info_metrics_csv": slug + ".domain-info-metrics.csv",
            "source_identity_sql_csv": slug + ".source-identity.csv",
            "source_identity_sql_metrics_csv": slug + ".source-identity-metrics.csv",
            "first_valid_time_csv": slug + ".first-valid-time.csv",
            "first_valid_time_metrics_csv": slug + ".first-valid-time-metrics.csv",
        }
        if spatial_selection:
            record["explicit_spatial_source_csv"] = slug + ".explicit-spatial-source.csv"
            record["explicit_spatial_source_metrics_csv"] = slug + ".explicit-spatial-source-metrics.csv"
            record["domain_spatial_source_csv"] = slug + ".domain-spatial-source.csv"
            record["domain_spatial_source_metrics_csv"] = slug + ".domain-spatial-source-metrics.csv"
            record["spatial_identity_sql_csv"] = slug + ".spatial-source-identity.csv"
            record["spatial_identity_sql_metrics_csv"] = slug + ".spatial-source-identity-metrics.csv"
            record["spatial_selection_bounds"] = resolve_public_spatial_selection_bounds(
                coordinate_reference, expected_rows
            )
        if validation_times_sql is not None:
            record["validation_time_coordinate_source"] = record_time_source
            record["validation_time_coordinate_count"] = shape[2]
        if time_probe_only:
            probe_view = "time_probe_" + grid_id
            probe_csv = output_dir / record["first_valid_time_csv"]
            probe_metrics = output_dir / record["first_valid_time_metrics_csv"]
            statements.extend([
                "CREATE TEMP VIEW " + probe_view + " AS SELECT valid_time FROM read_om(" +
                time_probe_arguments + ");",
                "COPY (SELECT valid_time FROM " + probe_view + " LIMIT 1) TO " +
                sql_string(str(probe_csv)) + " (FORMAT CSV, HEADER true);",
                "COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() "
                "ORDER BY scan_id DESC LIMIT 1) TO " + sql_string(str(probe_metrics)) +
                " (FORMAT CSV, HEADER true);",
            ])
            records.append(record)
            continue

        statements.extend([
            explicit,
            domain,
            "CREATE TEMP VIEW info_" + explicit_name + " AS SELECT grid_id, layout::VARCHAR AS layout, "
            "object_id, object_version, version_strength, content_verified FROM om_grid_info(" +
            explicit_info_arguments + ");",
            "CREATE TEMP VIEW info_" + domain_name + " AS SELECT grid_id, layout::VARCHAR AS layout, "
            "object_id, object_version, version_strength, content_verified FROM om_grid_info(" +
            domain_info_arguments + ");",
        ])
        if full_positions:
            if first_valid_times is None or grid_id not in first_valid_times:
                return [], [], f"full public source comparison has no first valid_time for {grid_id}"
            record["explicit_full_source_csv"] = slug + ".explicit-full-source.csv.gz"
            record["explicit_full_source_metrics_csv"] = slug + ".explicit-full-source-metrics.csv"
            record["domain_full_source_csv"] = slug + ".domain-full-source.csv.gz"
            record["domain_full_source_metrics_csv"] = slug + ".domain-full-source-metrics.csv"
            time_literal = "TIMESTAMP " + sql_string(first_valid_times[grid_id])
        else:
            time_literal = ""
        # A raw LIMIT advances time first in [y,x,time] arrays. Select the
        # reference's time-zero plane without sorting/materializing the full
        # object; the validator still checks the exact spatial prefix.
        prefix_clause = " WHERE om_source.axis_indices[3] = 0 LIMIT 4"
        for mode, view_name in (("explicit", explicit_name), ("domain", domain_name)):
            source_csv = output_dir / record[mode + "_source_csv"]
            source_metrics = output_dir / record[mode + "_source_metrics_csv"]
            info_csv = output_dir / record[mode + "_info_csv"]
            info_metrics = output_dir / record[mode + "_info_metrics_csv"]
            statements.extend([
                "COPY (SELECT om_source.object_id, om_source.object_version, om_source.version_strength, "
                "om_source.content_verified, om_source.grid_id, om_source.layout_id, "
                "om_source.logical_index, om_source.point_index, om_source.parent_point_index, "
                "om_source.axis_indices, lat AS latitude, lon AS longitude FROM " + view_name +
                prefix_clause + ") TO " +
                sql_string(str(source_csv)) + " (FORMAT CSV, HEADER true, NULL 'NULL');",
                "COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() "
                "ORDER BY scan_id DESC LIMIT 1) TO " + sql_string(str(source_metrics)) +
                " (FORMAT CSV, HEADER true);",
                "COPY (SELECT * FROM info_" + view_name + ") TO " + sql_string(str(info_csv)) +
                " (FORMAT CSV, HEADER true, NULL 'NULL');",
                "COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() "
                "ORDER BY scan_id DESC LIMIT 1) TO " + sql_string(str(info_metrics)) +
                " (FORMAT CSV, HEADER true);",
            ])
            if full_positions:
                full_source_csv = output_dir / record[mode + "_full_source_csv"]
                full_source_metrics = output_dir / record[mode + "_full_source_metrics_csv"]
                statements.extend([
                    "COPY (SELECT om_source.object_id, om_source.object_version, om_source.version_strength, "
                    "om_source.content_verified, om_source.grid_id, om_source.layout_id, om_source.logical_index, "
                    "om_source.point_index, om_source.parent_point_index, om_source.axis_indices, "
                    "lat AS latitude, lon AS longitude FROM " + view_name + " WHERE valid_time = " + time_literal +
                    ") TO " + sql_string(str(full_source_csv)) +
                    " (FORMAT CSV, HEADER true, COMPRESSION 'gzip', NULL 'NULL');",
                    "COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() "
                    "ORDER BY scan_id DESC LIMIT 1) TO " + sql_string(str(full_source_metrics)) +
                    " (FORMAT CSV, HEADER true);",
                ])
            if spatial_selection:
                selection = record["spatial_selection_bounds"]
                selection_filter = (
                    "valid_time = " + time_literal +
                    " AND lat BETWEEN " + sql_double(selection["latitude_min"]) +
                    " AND " + sql_double(selection["latitude_max"]) +
                    " AND lon BETWEEN " + sql_double(selection["longitude_min"]) +
                    " AND " + sql_double(selection["longitude_max"])
                )
                spatial_source_csv = output_dir / record[mode + "_spatial_source_csv"]
                spatial_source_metrics = output_dir / record[mode + "_spatial_source_metrics_csv"]
                statements.extend([
                    "COPY (SELECT om_source.object_id, om_source.object_version, om_source.version_strength, "
                    "om_source.content_verified, om_source.grid_id, om_source.layout_id, "
                    "om_source.logical_index, om_source.point_index, om_source.parent_point_index, "
                    "om_source.axis_indices, lat AS latitude, lon AS longitude FROM " + view_name + " WHERE " +
                    selection_filter + ") TO " + sql_string(str(spatial_source_csv)) +
                    " (FORMAT CSV, HEADER true, NULL 'NULL');",
                    "COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() "
                    "ORDER BY scan_id DESC LIMIT 1) TO " + sql_string(str(spatial_source_metrics)) +
                    " (FORMAT CSV, HEADER true);",
                ])
        identity_sql = (
            "COPY (WITH explicit_prefix AS MATERIALIZED (SELECT om_source, lat AS latitude, lon AS longitude FROM " +
            explicit_name + prefix_clause + "), domain_prefix AS MATERIALIZED "
            "(SELECT om_source, lat AS latitude, lon AS longitude FROM " +
            domain_name + prefix_clause + ") SELECT count(*) AS joined_rows, "
            "count(*) FILTER (WHERE e.om_source IS NULL OR d.om_source IS NULL) AS unmatched_rows, "
            "count(*) FILTER (WHERE e.om_source IS DISTINCT FROM d.om_source OR "
            "e.latitude IS DISTINCT FROM d.latitude OR e.longitude IS DISTINCT FROM d.longitude) AS mismatch_rows, "
            "coalesce(bool_and(e.om_source IS NOT DISTINCT FROM d.om_source AND "
            "e.latitude IS NOT DISTINCT FROM d.latitude AND e.longitude IS NOT DISTINCT FROM d.longitude), "
            "false) AS all_equal FROM explicit_prefix AS e FULL OUTER JOIN domain_prefix AS d "
            "ON e.om_source.logical_index = d.om_source.logical_index) TO " +
            sql_string(str(output_dir / record["source_identity_sql_csv"])) +
            " (FORMAT CSV, HEADER true);"
        )
        statements.extend([
            identity_sql,
            "COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() "
            "ORDER BY scan_id DESC LIMIT 1) TO " +
            sql_string(str(output_dir / record["source_identity_sql_metrics_csv"])) +
            " (FORMAT CSV, HEADER true);",
        ])
        if spatial_selection:
            selection = record["spatial_selection_bounds"]
            selection_filter = (
                "valid_time = " + time_literal +
                " AND lat BETWEEN " + sql_double(selection["latitude_min"]) +
                " AND " + sql_double(selection["latitude_max"]) +
                " AND lon BETWEEN " + sql_double(selection["longitude_min"]) +
                " AND " + sql_double(selection["longitude_max"])
            )
            selection_identity_sql = (
                "COPY (WITH explicit_selected AS MATERIALIZED (SELECT om_source, lat AS latitude, lon AS longitude FROM " +
                explicit_name + " WHERE " + selection_filter + "), domain_selected AS MATERIALIZED "
                "(SELECT om_source, lat AS latitude, lon AS longitude FROM " + domain_name + " WHERE " + selection_filter + ") "
                "SELECT count(*) AS joined_rows, "
                "count(*) FILTER (WHERE e.om_source IS NULL OR d.om_source IS NULL) AS unmatched_rows, "
                "count(*) FILTER (WHERE e.om_source IS DISTINCT FROM d.om_source OR "
                "e.latitude IS DISTINCT FROM d.latitude OR e.longitude IS DISTINCT FROM d.longitude) AS mismatch_rows, "
                "coalesce(bool_and(e.om_source IS NOT DISTINCT FROM d.om_source AND "
                "e.latitude IS NOT DISTINCT FROM d.latitude AND e.longitude IS NOT DISTINCT FROM d.longitude), "
                "false) AS all_equal FROM explicit_selected AS e FULL OUTER JOIN domain_selected AS d "
                "ON e.om_source.logical_index = d.om_source.logical_index) TO " +
                sql_string(str(output_dir / record["spatial_identity_sql_csv"])) + " (FORMAT CSV, HEADER true);"
            )
            statements.extend([
                selection_identity_sql,
                "COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() "
                "ORDER BY scan_id DESC LIMIT 1) TO " +
                sql_string(str(output_dir / record["spatial_identity_sql_metrics_csv"])) +
                " (FORMAT CSV, HEADER true);",
            ])
        records.append(record)
    source_label = "three local" if source_uri_kind == "local" else f"three public {source_uri_kind} URI"
    reason = f"{source_label} projected samples are anchored to hash-pinned local OM v3 copies; first four spatial positions at time-axis index 0 are checked against pinned Float32 coordinate references"
    if full_positions:
        reason += ("; full spatial positions are streamed at time-axis index 0 using validator-declared synthetic "
                   "valid_time labels, without ordering or projecting values")
    if spatial_selection:
        reason += "; a spatially filtered source result is compared with the materialized full-scan source baseline"
    else:
        reason += ", while full OM axis mapping remains unverified"
    return statements, records, reason


def compare_public_source_coordinates(record: dict[str, Any], positions: list[dict[str, Any]]) -> float:
    nx = record["shape"][1]
    tolerance = record["coordinate_tolerance_degrees"]
    path = Path(record["coordinate_reference_csv"])
    maximum_error = 0.0
    with path.open(encoding="utf-8", newline="") as stream:
        expected_rows = csv.DictReader(stream)
        for spatial_index, actual in enumerate(positions):
            if spatial_index >= 4:
                break
            expected = next(expected_rows, None)
            if expected is None or int(expected["spatial_index"]) != spatial_index or \
                    int(expected["row"]) != spatial_index // nx or int(expected["column"]) != spatial_index % nx:
                raise ValueError(f"{record['id']} pinned coordinate reference is missing position {spatial_index}")
            longitude_error = abs(float(actual["longitude"]) - float(expected["longitude"]))
            latitude_error = abs(float(actual["latitude"]) - float(expected["latitude"]))
            error = max(longitude_error, latitude_error)
            maximum_error = max(maximum_error, error)
            if error > tolerance:
                raise ValueError(
                    f"{record['id']} source coordinate differs from pinned producer reference at "
                    f"{spatial_index}: {error} > {tolerance} degrees"
                )
    if len(positions) < 4:
        raise ValueError(f"{record['id']} public source returned fewer than four positions")
    return maximum_error


def validate_public_sample_source(record: dict[str, Any], output_dir: Path) -> dict[str, Any]:
    array_shape = record["shape"]
    if len(array_shape) != 3:
        raise ValueError(f"{record['id']} public source smoke expects a three-axis array")
    summaries: dict[str, Any] = {}
    for mode in ("explicit", "domain"):
        source_path = output_dir / record[mode + "_source_csv"]
        with source_path.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
        if len(rows) != 4:
            raise ValueError(f"{record['id']} {mode} source projection returned {len(rows)} rows; expected four")
        positions: list[dict[str, Any]] = []
        for spatial_ordinal, row in enumerate(rows):
            indices = json.loads(row["axis_indices"])
            logical = int(row["logical_index"])
            expected_indices = [0, spatial_ordinal, 0]
            expected_logical = spatial_ordinal * array_shape[2]
            expected_point = indices[0] * array_shape[1] + indices[1]
            if len(indices) != len(array_shape) or logical != expected_logical or indices != expected_indices or \
                    expected_point != spatial_ordinal or int(row["point_index"]) != expected_point or \
                    int(row["parent_point_index"]) != expected_point:
                raise ValueError(
                    f"{record['id']} {mode} identity does not reconstruct row-major [y,x,time] position "
                    f"{logical}: axes={indices}, point={row['point_index']}, parent={row['parent_point_index']}"
                )
            if not row["object_id"] or row["content_verified"].lower() != "false":
                raise ValueError(f"{record['id']} {mode} source evidence is missing or overstates verification")
            positions.append({
                "object_id": row["object_id"],
                "grid_id": row["grid_id"],
                "layout_id": row["layout_id"],
                "logical_index": logical,
                "point_index": int(row["point_index"]),
                "parent_point_index": int(row["parent_point_index"]),
                "axis_indices": indices,
                "longitude": float(row["longitude"]),
                "latitude": float(row["latitude"]),
            })
        if len({item["object_id"] for item in positions}) != 1 or \
                len({item["grid_id"] for item in positions}) != 1 or \
                len({item["layout_id"] for item in positions}) != 1:
            raise ValueError(f"{record['id']} {mode} source evidence changes within one public object")
        maximum_coordinate_error = compare_public_source_coordinates(record, positions)
        info_path = output_dir / record[mode + "_info_csv"]
        with info_path.open(encoding="utf-8", newline="") as stream:
            info_rows = list(csv.DictReader(stream))
        if len(info_rows) != 1 or info_rows[0]["grid_id"] != positions[0]["grid_id"] or \
                info_rows[0]["object_id"] != positions[0]["object_id"]:
            raise ValueError(f"{record['id']} {mode} source and grid_info object/grid identity differ")
        record[mode + "_source_value_reads_zero"] = assert_zero_value_reads(
            output_dir / record[mode + "_source_metrics_csv"], "read_om"
        )
        record[mode + "_grid_info_value_reads_zero"] = assert_zero_value_reads(
            output_dir / record[mode + "_info_metrics_csv"], "grid_info"
        )
        record[mode + "_pinned_coordinate_reference_matches"] = True
        record[mode + "_coordinate_positions_checked"] = 4
        record[mode + "_maximum_coordinate_error_degrees"] = maximum_coordinate_error
        summaries[mode] = {"positions": positions, "layout": json.loads(info_rows[0]["layout"])}
    if summaries["explicit"]["positions"] != summaries["domain"]["positions"]:
        raise ValueError(f"{record['id']} explicit/domain source identities or coordinates differ")
    if summaries["explicit"]["layout"] != summaries["domain"]["layout"]:
        raise ValueError(f"{record['id']} explicit/domain grid_info layout differs")
    identity_path = output_dir / record["source_identity_sql_csv"]
    with identity_path.open(encoding="utf-8", newline="") as stream:
        identity_rows = list(csv.DictReader(stream))
    if len(identity_rows) != 1:
        raise ValueError(f"{record['id']} explicit/domain SQL identity query did not return one audit row")
    identity_row = identity_rows[0]
    identity_matches = (
        int(identity_row["joined_rows"]) == 4
        and int(identity_row["unmatched_rows"]) == 0
        and int(identity_row["mismatch_rows"]) == 0
        and identity_row["all_equal"].lower() == "true"
    )
    if not identity_matches:
        raise ValueError(f"{record['id']} explicit/domain SQL source identity mismatch: {identity_row}")
    record["source_identity_sql_matches"] = True
    record["source_identity_sql_positions_checked"] = 4
    record["source_identity_sql_value_reads_zero"] = assert_zero_value_reads(
        output_dir / record["source_identity_sql_metrics_csv"], "read_om"
    )
    record["source_identity_matches_explicit_domain"] = True
    record["positions_checked"] = 4
    record["pinned_coordinate_reference_matches"] = True
    return record


def read_metrics(metrics_csv: Path, operation: str) -> dict[str, Any]:
    with metrics_csv.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 1:
        raise ValueError(f"expected one {operation} metrics row in {metrics_csv}")
    metrics = json.loads(rows[0]["metrics"])
    if metrics.get("operation") != operation or metrics.get("outcome", {}).get("status") != "success":
        raise ValueError(f"unexpected {operation} metrics outcome in {metrics_csv}")
    return metrics


def assert_full_zero_time_source_metrics(metrics_csv: Path, expected_spatial_positions: int) -> bool:
    metrics = read_metrics(metrics_csv, "read_om")
    totals = metrics.get("reads", {}).get("value_totals", {})
    names = ("index_bytes", "index_requests", "data_bytes", "data_requests", "decoded_chunks")
    if any(totals.get(name) != 0 for name in names):
        raise ValueError(f"full source-coordinate query read values: {totals}")
    for variable, detail in metrics.get("reads", {}).get("variables", {}).items():
        if any(detail.get(name) != 0 for name in names):
            raise ValueError(f"full source-coordinate query read values for {variable}: {detail}")
    selection = metrics.get("selection", {})
    if metrics.get("outcome", {}).get("scan_complete") is not True or \
            selection.get("exact_candidate_records") != expected_spatial_positions or \
            selection.get("candidate_upper_bound_records") != expected_spatial_positions or \
            selection.get("count_complete") is not True:
        raise ValueError(
            "full source-coordinate query did not complete exactly one spatial plane; "
            f"expected={expected_spatial_positions}, outcome={metrics.get('outcome')}, selection={selection}"
        )
    return True


def validate_full_public_sample_source(record: dict[str, Any], output_dir: Path,
                                       explicit_identity: dict[str, Any],
                                       domain_identity: dict[str, Any]) -> dict[str, Any]:
    shape = record["shape"]
    if len(shape) != 3 or shape[2] <= 0:
        raise ValueError(f"{record['id']} full source comparison requires [y,x,time] shape")
    ny, nx, ntime = shape
    expected_count = ny * nx
    tolerance = record["coordinate_tolerance_degrees"]
    coordinate_path = Path(record["coordinate_reference_csv"])
    explicit_path = output_dir / record["explicit_full_source_csv"]
    domain_path = output_dir / record["domain_full_source_csv"]
    source_columns = PUBLIC_SOURCE_COLUMNS
    maximum_coordinate_error = 0.0
    checked = 0
    with gzip.open(explicit_path, "rt", encoding="utf-8", newline="") as explicit_stream, \
            gzip.open(domain_path, "rt", encoding="utf-8", newline="") as domain_stream, \
            coordinate_path.open(encoding="utf-8", newline="") as coordinate_stream:
        explicit_rows = csv.DictReader(explicit_stream)
        domain_rows = csv.DictReader(domain_stream)
        expected_rows = csv.DictReader(coordinate_stream)
        for spatial_index in range(expected_count):
            explicit = next(explicit_rows, None)
            domain = next(domain_rows, None)
            expected = next(expected_rows, None)
            if explicit is None or domain is None or expected is None:
                raise ValueError(
                    f"{record['id']} full source comparison ended before position {spatial_index} "
                    f"of {expected_count}"
                )
            row, column = divmod(spatial_index, nx)
            if int(expected["spatial_index"]) != spatial_index or int(expected["row"]) != row or \
                    int(expected["column"]) != column:
                raise ValueError(f"{record['id']} pinned coordinate reference order changed at {spatial_index}")
            expected_axes = [row, column, 0]
            expected_logical = spatial_index * ntime
            for mode, actual, identity in (("explicit", explicit, explicit_identity),
                                           ("domain", domain, domain_identity)):
                axes = json.loads(actual["axis_indices"])
                logical = int(actual["logical_index"])
                if axes != expected_axes or logical != expected_logical or \
                        int(actual["point_index"]) != spatial_index or \
                        int(actual["parent_point_index"]) != spatial_index:
                    raise ValueError(
                        f"{record['id']} {mode} full source position {spatial_index} does not reconstruct "
                        f"[y,x,time=0]: logical={logical}, axes={axes}, point={actual['point_index']}, "
                        f"parent={actual['parent_point_index']}"
                    )
                if any(actual[name] != identity[name] for name in (
                    "object_id", "object_version", "version_strength", "content_verified", "grid_id", "layout_id"
                )):
                    raise ValueError(f"{record['id']} {mode} full source identity differs from its info row")
                longitude_error = abs(float(actual["longitude"]) - float(expected["longitude"]))
                latitude_error = abs(float(actual["latitude"]) - float(expected["latitude"]))
                error = max(longitude_error, latitude_error)
                maximum_coordinate_error = max(maximum_coordinate_error, error)
                if error > tolerance:
                    raise ValueError(
                        f"{record['id']} {mode} full source coordinate differs at position {spatial_index}: "
                        f"{error} > {tolerance} degrees"
                    )
            if any(explicit[column_name] != domain[column_name] for column_name in source_columns):
                raise ValueError(
                    f"{record['id']} explicit/domain full source rows differ at point {spatial_index}"
                )
            checked += 1
        if next(explicit_rows, None) is not None or next(domain_rows, None) is not None or \
                next(expected_rows, None) is not None:
            raise ValueError(f"{record['id']} full source/reference has more than {expected_count} positions")

    record["full_spatial_positions_checked"] = checked
    record["full_time_axis_index"] = 0
    record["full_source_coordinate_reference_matches"] = True
    record["full_source_explicit_domain_identity_matches"] = True
    record["full_source_identity_comparison"] = "streamed exact row comparison; no sort or in-memory position table"
    record["full_source_maximum_coordinate_error_degrees"] = maximum_coordinate_error
    for mode in ("explicit", "domain"):
        metrics_path = output_dir / record[mode + "_full_source_metrics_csv"]
        record[mode + "_full_source_value_reads_zero"] = assert_full_zero_time_source_metrics(
            metrics_path, expected_count
        )
        record[mode + "_full_source_csv_sha256"] = sha256_file(output_dir / record[mode + "_full_source_csv"])
    record["full_spatial_count"] = expected_count
    return record


def validate_public_spatial_selection(record: dict[str, Any], output_dir: Path,
                                      explicit_identity: dict[str, Any],
                                      domain_identity: dict[str, Any]) -> dict[str, Any]:
    bounds = record["spatial_selection_bounds"]
    expected_count = 0
    anchor_found = False
    explicit_path = output_dir / record["explicit_spatial_source_csv"]
    domain_path = output_dir / record["domain_spatial_source_csv"]
    full_path = output_dir / record["explicit_full_source_csv"]
    source_columns = PUBLIC_SOURCE_COLUMNS
    with gzip.open(full_path, "rt", encoding="utf-8", newline="") as full_stream, \
            explicit_path.open(encoding="utf-8", newline="") as explicit_stream, \
            domain_path.open(encoding="utf-8", newline="") as domain_stream:
        full_rows = csv.DictReader(full_stream)
        explicit_rows = csv.DictReader(explicit_stream)
        domain_rows = csv.DictReader(domain_stream)
        for baseline in full_rows:
            spatial_index = int(baseline["point_index"])
            longitude = float(baseline["longitude"])
            latitude = float(baseline["latitude"])
            selected_by_baseline = (
                bounds["latitude_min"] <= latitude <= bounds["latitude_max"]
                and bounds["longitude_min"] <= longitude <= bounds["longitude_max"]
            )
            if spatial_index == bounds["anchor_spatial_index"]:
                anchor_found = selected_by_baseline
            if not selected_by_baseline:
                continue
            explicit = next(explicit_rows, None)
            domain = next(domain_rows, None)
            if explicit is None or domain is None:
                raise ValueError(
                    f"{record['id']} spatial selection omitted baseline position {spatial_index}"
                )
            for mode, actual, identity in (("explicit", explicit, explicit_identity),
                                           ("domain", domain, domain_identity)):
                if any(actual[name] != baseline[name] for name in source_columns):
                    raise ValueError(
                        f"{record['id']} {mode} spatial source does not match full-scan baseline "
                        f"at logical position {baseline['logical_index']}"
                    )
                if any(actual[name] != identity[name] for name in (
                    "object_id", "object_version", "version_strength", "content_verified", "grid_id", "layout_id"
                )):
                    raise ValueError(
                        f"{record['id']} {mode} spatial source identity differs from its info row"
                    )
            expected_count += 1
        if next(explicit_rows, None) is not None or next(domain_rows, None) is not None:
            raise ValueError(f"{record['id']} spatial selection returned positions absent from the full-scan baseline")

    if expected_count == 0:
        raise ValueError(f"{record['id']} spatial selection unexpectedly returned no positions")
    if not anchor_found:
        raise ValueError(f"{record['id']} spatial selection omitted its pinned center anchor")

    identity_path = output_dir / record["spatial_identity_sql_csv"]
    with identity_path.open(encoding="utf-8", newline="") as stream:
        identity_rows = list(csv.DictReader(stream))
    if len(identity_rows) != 1:
        raise ValueError(f"{record['id']} spatial explicit/domain SQL identity query did not return one audit row")
    identity_row = identity_rows[0]
    if int(identity_row["joined_rows"]) != expected_count or \
            int(identity_row["unmatched_rows"]) != 0 or int(identity_row["mismatch_rows"]) != 0 or \
            identity_row["all_equal"].lower() != "true":
        raise ValueError(f"{record['id']} spatial explicit/domain SQL source identity mismatch: {identity_row}")

    for mode in ("explicit", "domain"):
        metrics_path = output_dir / record[mode + "_spatial_source_metrics_csv"]
        metrics = read_metrics(metrics_path, "read_om")
        totals = metrics.get("reads", {}).get("value_totals", {})
        names = ("index_bytes", "index_requests", "data_bytes", "data_requests", "decoded_chunks")
        if any(totals.get(name) != 0 for name in names):
            raise ValueError(f"{record['id']} {mode} spatial source query read values: {totals}")
        for variable, detail in metrics.get("reads", {}).get("variables", {}).items():
            if any(detail.get(name) != 0 for name in names):
                raise ValueError(f"{record['id']} {mode} spatial source query read values for {variable}: {detail}")
        selection = metrics.get("selection", {})
        if metrics.get("outcome", {}).get("scan_complete") is not True or \
                selection.get("exact_candidate_records") != expected_count or \
                selection.get("count_complete") is not True:
            raise ValueError(
                f"{record['id']} {mode} spatial source query did not complete exactly the baseline subset; "
                f"expected={expected_count}, outcome={metrics.get('outcome')}, selection={selection}"
            )
        record[mode + "_spatial_source_value_reads_zero"] = True
        record[mode + "_spatial_source_selection_metrics_match_baseline"] = True
        record[mode + "_spatial_source_csv_sha256"] = sha256_file(output_dir / record[mode + "_spatial_source_csv"])
    record["spatial_identity_sql_matches"] = True
    record["spatial_identity_sql_positions_checked"] = expected_count
    record["spatial_identity_sql_value_reads_zero"] = assert_zero_value_reads(
        output_dir / record["spatial_identity_sql_metrics_csv"], "read_om"
    )
    record["spatial_selection_positions_checked"] = expected_count
    record["spatial_selection_anchor_found"] = anchor_found
    record["spatial_selection_matches_full_scan_baseline"] = True
    record["spatial_selection_bounds_policy"] = "closed latitude/longitude bounds applied to materialized full-scan index-zero plane"
    return record


def compare_full_source_positions(local_csv_gz: Path, remote_csv_gz: Path) -> dict[str, Any]:
    compared = 0
    evidence_fields = ("object_id", "object_version", "version_strength", "content_verified")
    position_fields = (
        "logical_index", "point_index", "parent_point_index", "axis_indices",
        "grid_id", "layout_id", "latitude", "longitude",
    )
    local_evidence: dict[str, str] | None = None
    remote_evidence: dict[str, str] | None = None
    with gzip.open(local_csv_gz, "rt", encoding="utf-8", newline="") as local_stream, \
            gzip.open(remote_csv_gz, "rt", encoding="utf-8", newline="") as remote_stream:
        local_rows = csv.DictReader(local_stream)
        remote_rows = csv.DictReader(remote_stream)
        if not set(evidence_fields + position_fields).issubset(local_rows.fieldnames or ()) or \
                not set(evidence_fields + position_fields).issubset(remote_rows.fieldnames or ()):
            raise ValueError("local/remote source CSV is missing identity or original-position fields")
        while True:
            local = next(local_rows, None)
            remote = next(remote_rows, None)
            if local is None or remote is None:
                if local is not None or remote is not None:
                    raise ValueError("local and remote source scans returned different position counts")
                break
            if any(local[field] != remote[field] for field in position_fields):
                raise ValueError(f"local/remote source position differs at streamed row {compared}")
            current_local = {field: local[field] for field in evidence_fields}
            current_remote = {field: remote[field] for field in evidence_fields}
            if not current_local["object_id"] or not current_remote["object_id"]:
                raise ValueError("local or remote source object identity is empty")
            if local_evidence is None:
                local_evidence, remote_evidence = current_local, current_remote
            elif current_local != local_evidence or current_remote != remote_evidence:
                raise ValueError("object evidence changed within a local/remote source scan")
            compared += 1
    if compared == 0 or local_evidence is None or remote_evidence is None:
        raise ValueError("local/remote source comparison returned no positions")
    return {
        "positions_compared": compared,
        "positions_match": True,
        "local_object_evidence": local_evidence,
        "remote_object_evidence": remote_evidence,
        "object_id_equal": local_evidence["object_id"] == remote_evidence["object_id"],
        "object_id_equality_required": False,
    }


def run_public_source_validation(duckdb: Path, extension: Path, root: Path, output_dir: Path,
                                 full_positions: bool = False,
                                 spatial_selection: bool = False,
                                 source_uri_kind: str = "local",
                                 httpfs: Path | None = None,
                                 s3_region: str | None = None,
                                 compare_local_source_dir: Path | None = None) -> dict[str, Any]:
    output_dir.mkdir(parents=True, exist_ok=True)
    if source_uri_kind != "local" and (httpfs is None or not httpfs.is_file()):
        raise ValueError("public remote source validation requires the matching --httpfs extension")
    if compare_local_source_dir is not None:
        if source_uri_kind == "local" or not full_positions:
            raise ValueError("local/remote source comparison requires a remote URI and all public positions")
        if not compare_local_source_dir.is_dir():
            raise ValueError(f"local source comparison directory is missing: {compare_local_source_dir}")

    load_statements = []
    resolved_s3_region: str | None = None
    if source_uri_kind != "local":
        load_statements.append("LOAD " + sql_string(str(httpfs.resolve())) + ";")
    if source_uri_kind == "s3":
        manifest = json.loads((root / "test/data/grids/sample-manifest.json").read_text(encoding="utf-8"))
        projected_samples = [
            item for item in manifest.get("public_open_meteo_samples", {}).get("objects", [])
            if isinstance(item, dict) and item.get("grid_definition_id") in PUBLIC_COORDINATE_TARGETS
        ]
        inferred_regions = {public_sample_s3_region(item) for item in projected_samples}
        inferred_regions.discard(None)
        if s3_region is not None and inferred_regions and inferred_regions != {s3_region.lower()}:
            raise ValueError("--s3-region conflicts with the region in the public Open-Meteo object URLs")
        if s3_region is not None:
            resolved_s3_region = s3_region.lower()
        elif len(inferred_regions) == 1:
            resolved_s3_region = next(iter(inferred_regions))
        else:
            raise ValueError("S3 public source validation needs a region in the manifest HTTPS endpoints or --s3-region")
        load_statements.append("SET s3_region = " + sql_string(resolved_s3_region) + ";")
    load_statements.append("LOAD " + sql_string(str(extension)) + ";")
    load_statements.extend(["SET threads = 1;", "SET duckomo_max_threads = 1;"])
    first_valid_times: dict[str, str] | None = None
    probe_records: list[dict[str, Any]] = []
    probe_result: subprocess.CompletedProcess[str] | None = None
    if full_positions:
        probe_statements, probe_records, probe_reason = prepare_public_sample_queries(
            root, output_dir, time_probe_only=True, source_uri_kind=source_uri_kind
        )
        if len(probe_records) != 3:
            raise ValueError(probe_reason)
        probe_sql_path = output_dir / "public-source-time-probe.sql"
        probe_sql = load_statements + probe_statements
        probe_sql_path.write_text("\n".join(probe_sql) + "\n", encoding="utf-8")
        probe_result = subprocess.run(
            [str(duckdb), "-unsigned", "-bail", "-batch", "-f", str(probe_sql_path), ":memory:"],
            cwd=root, text=True, capture_output=True,
        )
        (output_dir / "time-probe.stdout.txt").write_text(probe_result.stdout, encoding="utf-8")
        (output_dir / "time-probe.stderr.txt").write_text(probe_result.stderr, encoding="utf-8")
        if probe_result.returncode != 0:
            raise ValueError(f"DuckDB valid_time probe failed with exit {probe_result.returncode}: "
                             f"{probe_result.stderr.strip()}")
        first_valid_times = {}
        for record in probe_records:
            probe_path = output_dir / record["first_valid_time_csv"]
            with probe_path.open(encoding="utf-8", newline="") as stream:
                rows = list(csv.DictReader(stream))
            if len(rows) != 1 or not rows[0].get("valid_time"):
                raise ValueError(f"{record['id']} did not return one finite first valid_time")
            value = rows[0]["valid_time"]
            if value.lower() == "null":
                raise ValueError(f"{record['id']} first valid_time is NULL")
            first_valid_times[record["grid_definition_id"]] = value
            record["first_valid_time"] = value
            record["first_valid_time_value_reads_zero"] = assert_zero_value_reads(
                output_dir / record["first_valid_time_metrics_csv"], "read_om"
            )

    public_statements, records, reason = prepare_public_sample_queries(
        root, output_dir, full_positions=full_positions, first_valid_times=first_valid_times,
        spatial_selection=spatial_selection, source_uri_kind=source_uri_kind
    )
    if len(records) != 3:
        raise ValueError(reason)

    sql_path = output_dir / "public-source.sql"
    statements = load_statements.copy()
    statements.extend(public_statements)
    sql_path.write_text("\n".join(statements) + "\n", encoding="utf-8")
    result = subprocess.run(
        [str(duckdb), "-unsigned", "-bail", "-batch", "-f", str(sql_path), ":memory:"],
        cwd=root, text=True, capture_output=True,
    )
    (output_dir / "runner.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (output_dir / "runner.stderr.txt").write_text(result.stderr, encoding="utf-8")
    if result.returncode != 0:
        raise ValueError(f"DuckDB public source query failed with exit {result.returncode}: {result.stderr.strip()}")

    samples = []
    for record in records:
        sample = validate_public_sample_source(record, output_dir)
        if full_positions:
            probe_record = next(item for item in probe_records
                                if item["grid_definition_id"] == record["grid_definition_id"])
            record["first_valid_time"] = probe_record["first_valid_time"]
            record["first_valid_time_value_reads_zero"] = probe_record["first_valid_time_value_reads_zero"]
            identities = {}
            for mode in ("explicit", "domain"):
                with (output_dir / record[mode + "_source_csv"]).open(encoding="utf-8", newline="") as stream:
                    prefix_rows = list(csv.DictReader(stream))
                info_path = output_dir / record[mode + "_info_csv"]
                with info_path.open(encoding="utf-8", newline="") as stream:
                    info_rows = list(csv.DictReader(stream))
                if not prefix_rows or len(info_rows) != 1:
                    raise ValueError(f"{record['id']} {mode} source/info identity is missing")
                identities[mode] = {
                    "object_id": prefix_rows[0]["object_id"],
                    "object_version": prefix_rows[0]["object_version"],
                    "version_strength": prefix_rows[0]["version_strength"],
                    "content_verified": prefix_rows[0]["content_verified"],
                    "grid_id": prefix_rows[0]["grid_id"],
                    "layout_id": prefix_rows[0]["layout_id"],
                }
                info_row = info_rows[0]
                for identity_name in ("object_id", "object_version", "version_strength", "content_verified", "grid_id"):
                    if info_row[identity_name] != identities[mode][identity_name]:
                        raise ValueError(
                            f"{record['id']} {mode} grid_info and source {identity_name} differ"
                        )
            sample = validate_full_public_sample_source(record, output_dir,
                                                        identities["explicit"], identities["domain"])
            if spatial_selection:
                sample = validate_public_spatial_selection(record, output_dir,
                                                          identities["explicit"], identities["domain"])
            if compare_local_source_dir is not None:
                comparisons = {}
                for mode in ("explicit", "domain"):
                    filename = record[mode + "_full_source_csv"]
                    comparison = compare_full_source_positions(
                        compare_local_source_dir / filename, output_dir / filename
                    )
                    comparisons[mode] = comparison
                sample["local_remote_source_positions"] = comparisons
        samples.append(sample)
    manifest_path = root / "test/data/grids/sample-manifest.json"
    coordinate_manifest_path = root / "test/data/grids/coordinate-reference.json"
    report = {
        "schema_version": 1,
        "status": ("public_source_full_" if full_positions else "public_source_") + source_uri_kind + "_pass",
        "source_uri_kind": source_uri_kind,
        "s3_region": resolved_s3_region,
        "scope": ("three hash-pinned public projected samples; all spatial points streamed at time-axis index 0 "
                  "using validator-declared synthetic valid_time labels, "
                  "explicit/domain source identity compared row-by-row, source/info zero-value-read checks, "
                  "and optional spatial subsets compared against the materialized unfiltered full plane"
                  + ("; full original positions compared to local hash-pinned copies with per-URI object evidence "
                     "recorded separately" if compare_local_source_dir is not None else "")
                  if full_positions else
                  f"three hash-pinned public projected samples via {source_uri_kind} source URIs; first four spatial positions at time-axis index 0, SQL explicit/domain source identity, and source/info/identity zero-value-read checks"),
        "reason": reason,
        "full_spatial_positions_requested": full_positions,
        "spatial_selection_requested": spatial_selection,
        "valid_time_probes": [
            {"grid_definition_id": record["grid_definition_id"],
             "first_valid_time": record["first_valid_time"],
             "time_coordinate_source": record["validation_time_coordinate_source"],
             "time_coordinate_count": record["validation_time_coordinate_count"],
             "time_index_selected": 0,
             "csv": record["first_valid_time_csv"],
             "metrics_csv": record["first_valid_time_metrics_csv"],
             "value_reads_zero": record["first_valid_time_value_reads_zero"]}
            for record in probe_records
        ] if full_positions else [],
        "sample_manifest_sha256": sha256_file(manifest_path),
        "coordinate_reference_manifest_sha256": sha256_file(coordinate_manifest_path),
        "samples": samples,
    }
    (output_dir / "public-source-report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return report


def run_validation(duckdb: Path, extension: Path, root: Path, output_dir: Path) -> dict[str, Any]:
    output_dir.mkdir(parents=True, exist_ok=True)
    cases = generate()["cases"]
    sql_path = output_dir / "h7-synthetic.sql"
    records: list[dict[str, Any]] = []
    statements = ["LOAD " + sql_string(str(extension)) + ";", "SET threads = 1;",
                  "SET duckomo_max_threads = 1;"]
    for case in cases:
        source_path = output_dir / (case["id"] + ".source.csv")
        source_metrics_path = output_dir / (case["id"] + ".source-metrics.csv")
        info_path = output_dir / (case["id"] + ".info.csv")
        info_metrics_path = output_dir / (case["id"] + ".info-metrics.csv")
        read, info = validation_read(case, root)
        statements.extend([
            "COPY (SELECT om_source.object_id, om_source.object_version, om_source.version_strength, "
            "om_source.content_verified, om_source.grid_id, om_source.layout_id, "
            "om_source.logical_index, om_source.point_index, om_source.parent_point_index, "
            "om_source.axis_indices, lon AS longitude, lat AS latitude FROM " + read +
            " ORDER BY om_source.logical_index) TO " + sql_string(str(source_path)) +
            " (FORMAT CSV, HEADER true, NULL 'NULL');",
            "COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() "
            "ORDER BY scan_id DESC LIMIT 1) TO " + sql_string(str(source_metrics_path)) +
            " (FORMAT CSV, HEADER true);",
            "COPY (SELECT grid_id, layout::VARCHAR AS layout, object_id, object_version, "
            "version_strength, content_verified FROM " + info + ") TO " +
            sql_string(str(info_path)) + " (FORMAT CSV, HEADER true, NULL 'NULL');",
            "COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() "
            "ORDER BY scan_id DESC LIMIT 1) TO " + sql_string(str(info_metrics_path)) +
            " (FORMAT CSV, HEADER true);",
        ])
        records.append({
            "id": case["id"],
            "grid_type": case["grid_type"],
            "source_csv": source_path.name,
            "source_metrics_csv": source_metrics_path.name,
            "grid_info_csv": info_path.name,
            "grid_info_metrics_csv": info_metrics_path.name,
        })
    public_statements, public_records, public_reason = prepare_public_sample_queries(root, output_dir)
    statements.extend(public_statements)
    sql_path.write_text("\n".join(statements) + "\n", encoding="utf-8")
    result = subprocess.run(
        [str(duckdb), "-unsigned", "-bail", "-batch", "-f", str(sql_path), ":memory:"],
        cwd=root, text=True, capture_output=True,
    )
    (output_dir / "runner.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (output_dir / "runner.stderr.txt").write_text(result.stderr, encoding="utf-8")
    if result.returncode != 0:
        raise ValueError(f"DuckDB H7 query failed with exit {result.returncode}: {result.stderr.strip()}")

    case_by_id = {case["id"]: case for case in cases}
    for record in records:
        case = case_by_id[record["id"]]
        observed: list[dict[str, Any]] = []
        with (output_dir / record["source_csv"]).open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
        expected_points = grid_points(case)
        if len(rows) != len(expected_points):
            raise ValueError(f"{case['id']} returned {len(rows)} source rows; expected {len(expected_points)}")
        identities = set()
        for expected, row in zip(expected_points, rows):
            logical = int(row["logical_index"])
            point_index = int(row["point_index"])
            parent_value = None if row["parent_point_index"] == "NULL" else int(row["parent_point_index"])
            axis_indices = json.loads(row["axis_indices"])
            longitude, latitude = float(row["longitude"]), float(row["latitude"])
            if logical != expected["logical_index"] or logical in identities or point_index != logical:
                raise ValueError(f"{case['id']} source logical/local positions are not a complete ordered set")
            identities.add(logical)
            if case["grid_type"] == "reduced_gaussian":
                expected_axes = [logical]
                expected_parent = logical
            else:
                expected_axes = [expected["row"], expected["column"]]
                expected_parent = logical
            if axis_indices != expected_axes or parent_value != expected_parent:
                raise ValueError(
                    f"{case['id']} source position {logical} differs: axes={axis_indices}, "
                    f"parent={parent_value}; expected axes={expected_axes}, parent={expected_parent}"
                )
            if row["content_verified"].lower() != "false" or not row["object_id"]:
                raise ValueError(f"{case['id']} source object evidence is missing or overstates verification")
            if row["version_strength"] != "unverifiable" or row["object_version"] not in ("NULL", ""):
                raise ValueError(f"{case['id']} synthetic local object version strength is not explicit")
            if not row["grid_id"] or not row["layout_id"]:
                raise ValueError(f"{case['id']} source grid/layout identity is missing")
            if abs(longitude - expected["longitude"]) > 1e-8 or abs(latitude - expected["latitude"]) > 1e-8:
                raise ValueError(f"{case['id']} coordinate differs from independent oracle at {logical}")
            observed.append({
                "logical_index": logical,
                "longitude": longitude,
                "latitude": latitude,
                "covered": polygon_covers((longitude, latitude), case["polygon_xy"]),
                "grid_id": row["grid_id"],
                "layout_id": row["layout_id"],
                "object_id": row["object_id"],
            })
        if identities != set(range(len(expected_points))):
            raise ValueError(f"{case['id']} source logical positions are incomplete")

        expected_relations = case["point_relations"]
        actual_relations = [
            {"logical_index": row["logical_index"], "covered": row["covered"]} for row in observed
        ]
        if actual_relations != expected_relations:
            raise ValueError(f"{case['id']} full point/polygon relation multiset differs from the oracle")
        expected_matches = [point["logical_index"] for point in case["oracle_matches"]]
        actual_matches = [row["logical_index"] for row in observed if row["covered"]]
        if actual_matches != expected_matches:
            raise ValueError(f"{case['id']} complete polygon match positions differ from the oracle")
        if len({row["object_id"] for row in observed}) != 1 or len({row["grid_id"] for row in observed}) != 1 or \
                len({row["layout_id"] for row in observed}) != 1:
            raise ValueError(f"{case['id']} source object/grid/layout evidence changes within one object")

        with (output_dir / record["grid_info_csv"]).open(encoding="utf-8", newline="") as stream:
            info_rows = list(csv.DictReader(stream))
        if len(info_rows) != 1:
            raise ValueError(f"{case['id']} om_grid_info returned {len(info_rows)} rows; expected one")
        info = info_rows[0]
        layout = json.loads(info["layout"])
        if info["grid_id"] != observed[0]["grid_id"] or info["object_id"] != observed[0]["object_id"]:
            raise ValueError(f"{case['id']} source and om_grid_info object/grid identities differ")
        if layout.get("layout_id") not in (None, observed[0]["layout_id"]):
            raise ValueError(f"{case['id']} source and om_grid_info layout identities differ")
        record["rows"] = len(rows)
        record["matched_positions"] = actual_matches
        record["grid_id"] = info["grid_id"]
        record["layout_id"] = observed[0]["layout_id"]
        record["object_id"] = info["object_id"]
        record["source_value_reads_zero"] = assert_zero_value_reads(
            output_dir / record["source_metrics_csv"], "read_om"
        )
        record["grid_info_value_reads_zero"] = assert_zero_value_reads(
            output_dir / record["grid_info_metrics_csv"], "grid_info"
        )
        record["full_relation_multiset_matches"] = True

    public_results = [validate_public_sample_source(record, output_dir) for record in public_records]
    public_status = "pass" if public_results else "not-run"
    report = {
        "schema_version": 1,
        "status": "synthetic_local_pass",
        "full_h7_status": "not-run",
        "scope": "four synthetic small grids and first four pinned-coordinate positions for three public projected samples; full OM axis mapping, required N160/N320 samples, and public remote source identity remain not-run",
        "public_local_source_smoke": {
            "status": public_status,
            "reason": public_reason,
            "scope": "local hash-pinned public projected samples; first four producer coordinate positions and source/info zero-read identities checked, without claiming full OM axis mapping",
            "samples": public_results,
        },
        "oracle_sha256": __import__("hashlib").sha256(DEFAULT_OUTPUT.read_bytes()).hexdigest(),
        "cases": records,
    }
    report_path = output_dir / "h7-synthetic-report.json"
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return report


def assert_zero_value_reads(metrics_csv: Path, operation: str) -> bool:
    with metrics_csv.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 1:
        raise ValueError(f"expected one {operation} metrics row in {metrics_csv}")
    metrics = json.loads(rows[0]["metrics"])
    if metrics.get("operation") != operation or metrics.get("outcome", {}).get("status") != "success":
        raise ValueError(f"unexpected {operation} metrics outcome")
    totals = metrics.get("reads", {}).get("value_totals", {})
    names = ("index_bytes", "index_requests", "data_bytes", "data_requests", "decoded_chunks")
    if any(totals.get(name) != 0 for name in names):
        raise ValueError(f"{operation} performed a value/index read: {totals}")
    for variable, detail in metrics.get("reads", {}).get("variables", {}).items():
        if any(detail.get(name) != 0 for name in names):
            raise ValueError(f"{operation} performed a value/index read for {variable}: {detail}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--validate", action="store_true", help="run four local H7 source/geometry checks")
    parser.add_argument("--validate-public-sources", action="store_true",
                         help="run H3 public projected source/info subchecks")
    parser.add_argument("--public-source-uri-kind", choices=("local", "https", "s3"), default="local",
                         help="read the existing Open-Meteo OM sample from its local copy, HTTPS URL, or S3 URI")
    parser.add_argument("--httpfs", type=Path,
                        help="matching HTTPFS extension required for HTTPS/S3 public source validation")
    parser.add_argument("--s3-region",
                        help="S3 region for public source validation; inferred from the manifest HTTPS endpoint when possible")
    parser.add_argument("--compare-local-source-dir", type=Path,
                        help="compare every remote source position to the local validator output directory")
    parser.add_argument("--all-public-source-positions", action="store_true",
                        help="compare every public projected spatial point at valid_time index 0")
    parser.add_argument("--public-spatial-selection", action="store_true",
                        help="compare a bounded spatial source subset against an unfiltered full-scan baseline")
    parser.add_argument("--duckdb", type=Path, help="matching DuckDB CLI for validation")
    parser.add_argument("--extension", type=Path, help="matching DuckOMO extension for validation")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--output-dir", type=Path, help="evidence directory for validation")
    args = parser.parse_args()
    expected = json.dumps(generate(), indent=2, sort_keys=True) + "\n"
    if args.all_public_source_positions and not args.validate_public_sources:
        raise SystemExit("--all-public-source-positions requires --validate-public-sources")
    if args.public_spatial_selection and not args.all_public_source_positions:
        raise SystemExit("--public-spatial-selection requires --all-public-source-positions")
    if args.public_source_uri_kind != "local" and not args.validate_public_sources:
        raise SystemExit("--public-source-uri-kind requires --validate-public-sources")
    if args.public_source_uri_kind != "local" and args.httpfs is None:
        raise SystemExit("HTTPS/S3 public source validation requires --httpfs")
    if args.compare_local_source_dir is not None and not args.all_public_source_positions:
        raise SystemExit("--compare-local-source-dir requires --all-public-source-positions")
    if args.validate or args.validate_public_sources:
        if args.validate and args.validate_public_sources:
            raise SystemExit("--validate and --validate-public-sources are mutually exclusive")
        if args.duckdb is None or args.extension is None or args.output_dir is None:
            raise SystemExit("validation requires --duckdb, --extension, and --output-dir")
        if args.validate and (not args.output.is_file() or args.output.read_text(encoding="utf-8") != expected):
            raise SystemExit(f"spatial reference is stale: {args.output}")
        try:
            if args.validate_public_sources:
                report = run_public_source_validation(args.duckdb.resolve(), args.extension.resolve(),
                                                      args.root.resolve(), args.output_dir.resolve(),
                                                      full_positions=args.all_public_source_positions,
                                                      spatial_selection=args.public_spatial_selection,
                                                      source_uri_kind=args.public_source_uri_kind,
                                                      httpfs=args.httpfs.resolve() if args.httpfs else None,
                                                      s3_region=args.s3_region,
                                                      compare_local_source_dir=(
                                                          args.compare_local_source_dir.resolve()
                                                          if args.compare_local_source_dir else None))
            else:
                report = run_validation(args.duckdb.resolve(), args.extension.resolve(), args.root.resolve(),
                                        args.output_dir.resolve())
        except (OSError, ValueError, TypeError, csv.Error, json.JSONDecodeError) as error:
            print(f"local spatial/source validation failed: {error}", file=sys.stderr)
            return 1
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    if args.check:
        if not args.output.is_file() or args.output.read_text(encoding="utf-8") != expected:
            raise SystemExit(f"spatial reference is stale: {args.output}")
        print(f"spatial relation oracle is current: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(expected, encoding="utf-8")
    print(f"generated {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
