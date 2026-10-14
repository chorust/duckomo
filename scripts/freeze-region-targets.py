#!/usr/bin/env python3
"""Freeze the five real grid targets and local regions before performance runs."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct
import sys
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "test/data/regions/real-targets.json"
O1280_EVIDENCE = ROOT / "specs/004-multi-grid-selection/evidence/baseline-local/o1280-real-local-20261010/current-build-r3"
O1280_OUTPUT = ROOT / "build/hres-o1280/validation-20261010-r3"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def wkb_rectangle(bounds: list[float]) -> bytes:
    min_x, min_y, max_x, max_y = bounds
    points = [(min_x, min_y), (max_x, min_y), (max_x, max_y),
              (min_x, max_y), (min_x, min_y)]
    return struct.pack("<BII", 1, 3, 1) + struct.pack("<I", len(points)) + b"".join(
        struct.pack("<dd", x, y) for x, y in points
    )


def count_and_first_position(path: Path, lon_name: str, lat_name: str, bounds: list[float],
                             position_name: str) -> tuple[int, dict | None]:
    min_lon, min_lat, max_lon, max_lat = bounds
    count = 0
    first = None
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        for row in reader:
            lon, lat = float(row[lon_name]), float(row[lat_name])
            if min_lon <= lon <= max_lon and min_lat <= lat <= max_lat:
                count += 1
                if first is None:
                    first = {"position": int(row[position_name]), "lon": lon, "lat": lat}
                    if "row" in row and "column" in row:
                        first["row"] = int(row["row"])
                        first["column"] = int(row["column"])
    return count, first


def sql_literal(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def freeze_queries(item: dict, variable_columns: list[str], include_time: bool) -> dict:
    projection = [*variable_columns, "lat", "lon"]
    if include_time:
        projection.append("valid_time")
    projection.append("om_source")
    source_uri = sql_literal(item["object"]["uri"])
    s3_uri = item["object"].get("s3_uri")
    local_path = sql_literal(item["object"]["path"])
    domain = sql_literal(item["grid_id"])
    # The three projected producer arrays have a registered y/x grid profile and
    # an additional time dimension; their source objects do not declare it.
    dimensions = ("dimensions := map(['value'], [['y','x','time']])"
                  if item["grid_id"] in {"gem_rdps_10km", "gem_regional", "aladin_central_europe_2km"}
                  else None)
    region_wkb = item["region_wkb"]["wkb_hex"]
    region_sql = "regions := [{'id': " + sql_literal(item["grid_id"]) + ", 'wkb': from_hex(" + sql_literal(region_wkb) + ")}], region_crs := 'grid_lonlat'"

    def query(function: str, path: str, regions: bool) -> str:
        arguments = [path]
        if regions:
            arguments.append(region_sql)
        if dimensions:
            arguments.append(dimensions)
        arguments.extend(["domain := " + domain, "include_source := true"])
        columns = ", ".join([*projection[:-1], "region_id", projection[-1]]) if regions else ", ".join(projection)
        return "SELECT " + columns + " FROM " + function + "(" + ", ".join(arguments) + ")"

    queries = {
        "projection": projection,
        "value_columns": variable_columns,
        "access_paths": ["local", "https"] + (["s3"] if s3_uri else []),
        "full_local_condition": "full object scan; no spatial predicate; source coordinates and all requested value columns materialized",
        "region_local_condition": "frozen Polygon covers point in declared grid_lonlat; no SQL bounding predicate or row limit",
        "full_remote_sql": query("read_om", source_uri, False),
        "region_remote_sql": query("read_om_regions", source_uri, True),
        "full_local_sql": query("read_om", local_path, False),
        "region_local_sql": query("read_om_regions", local_path, True),
    }
    if s3_uri:
        s3_path = sql_literal(s3_uri)
        queries["full_remote_s3_sql"] = query("read_om", s3_path, False)
        queries["region_remote_s3_sql"] = query("read_om_regions", s3_path, True)
    return queries


def existing_file(relative: str, expected_sha: str | None = None, expected_size: int | None = None) -> dict:
    path = ROOT / relative
    if not path.is_file():
        raise RuntimeError(f"frozen evidence input is missing: {relative}")
    actual_sha = sha256(path)
    actual_size = path.stat().st_size
    if expected_sha and actual_sha != expected_sha:
        raise RuntimeError(f"frozen hash changed for {relative}")
    if expected_size is not None and actual_size != expected_size:
        raise RuntimeError(f"frozen size changed for {relative}")
    return {"path": relative, "sha256": actual_sha, "size_bytes": actual_size}


def build(output: Path) -> dict:
    source_manifest = json.loads((ROOT / "test/data/grids/sample-manifest.json").read_text())
    sample_objects = {item["id"]: item for item in source_manifest["public_open_meteo_samples"]["objects"]}
    coordinates = json.loads((ROOT / "test/data/grids/coordinate-reference.json").read_text())
    coordinate_refs = {item["sample_id"]: item for item in coordinates["references"]}
    values = json.loads((ROOT / "test/data/grids/value-reference-manifest.json").read_text())
    value_refs = {item["sample_id"]: item for item in values["references"]}
    value_refs.update({item["sample_id"]: item for item in values["supplemental_references"]})

    projected = [
        ("gem_rdps_10km", "rotated_cmc_gem_rdps_10km_cape_chunk_4337",
         "rotated-cmc-gem-rdps-10km-cape-chunk-4337.coordinates.csv", [-124.65, -3.82, -124.48, -3.65]),
        ("gem_regional", "stereographic_cmc_gem_rdps_cape_chunk_4337",
         "stereographic-cmc-gem-rdps-cape-chunk-4337.coordinates.csv", [-143.02, 18.10, -142.76, 18.34]),
        ("aladin_central_europe_2km", "lambert_chmi_aladin_central_europe_2km_cape_chunk_4135",
         "lambert-chmi-aladin-ce-cape-chunk-4135.coordinates.csv", [1.30, 38.58, 1.40, 38.63]),
    ]
    targets = []
    for grid_id, sample_id, coordinate_file, bounds in projected:
        sample = sample_objects[sample_id]
        coordinate_ref = coordinate_refs[sample_id]
        value_ref = value_refs[sample_id]
        object_path = existing_file(sample["local_copy"]["path"], sample["sha256"], sample["size_bytes"])
        coordinate_artifact = coordinate_ref["artifact"]
        coordinate_path = existing_file(coordinate_artifact["path"], coordinate_artifact["sha256"],
                                        coordinate_artifact["size_bytes"])
        value_artifact = value_ref["artifact"]
        value_path = existing_file(value_artifact["path"], value_artifact["sha256"], value_artifact["size_bytes"])
        coordinate_count, first = count_and_first_position(
            ROOT / coordinate_path["path"], "longitude", "latitude", bounds, "spatial_index")
        if not coordinate_count or first is None:
            raise RuntimeError(f"frozen region does not cover a point in {sample_id}")
        if ("row" not in first or "column" not in first or
                first["position"] != first["row"] * int(sample["array"]["shape"][1]) + first["column"]):
            raise RuntimeError(f"producer reference position mapping changed for {sample_id}")
        target = {
            "grid_id": grid_id,
            "sample_id": sample_id,
            "object": {**object_path, "uri": sample["source_object"]["https_url"],
                       "s3_uri": sample["source_object"]["s3_uri"],
                       "variable_path": sample["array"]["variable_path"],
                       "shape": sample["array"]["shape"], "chunks": sample["array"]["chunk_shape"],
                       "axes": sample["array"]["axes"]},
            "region_bounds_lonlat": bounds,
            "region_wkb": None,
            "independent_coordinate_reference": coordinate_path,
            "coordinate_tolerance_degrees": coordinate_ref["tolerance_degrees"],
            "official_value_reference": value_path,
            "coordinate_points_in_region": coordinate_count,
            "coordinate_oracle_scope": "pinned producer Float32 coordinates; position identity from frozen [ny,nx,ntime] source layout",
            "first_source_position": first["position"],
        }
        targets.append(target)

    domain = json.loads((ROOT / "test/data/domain-manifest.json").read_text())
    ncep_sample = domain["sample"]
    ncep_url = urlparse(ncep_sample["url"])
    if ncep_url.netloc != "openmeteo.s3.amazonaws.com" or not ncep_url.path.startswith("/"):
        raise RuntimeError("the pinned NCEP URL no longer identifies its expected Open-Meteo S3 object")
    ncep_s3_uri = "s3://openmeteo" + ncep_url.path
    ncep_object = existing_file(ncep_sample["path"], ncep_sample["sha256"], ncep_sample["bytes"])
    ncep_bounds = [-0.4, -0.4, 0.4, 0.4]
    grid = domain["layout"]["grid"]
    ncep_points = [(col, row) for row in range(grid["ny"]) for col in range(grid["nx"])
                   if ncep_bounds[0] <= grid["lon0"] + col * grid["dlon"] <= ncep_bounds[2]
                   and ncep_bounds[1] <= grid["lat0"] + row * grid["dlat"] <= ncep_bounds[3]]
    if not ncep_points:
        raise RuntimeError("frozen ncep_gfswave025 polygon has no grid points")
    ncep_value_refs = []
    for variable in domain["oracle"]["variables"]:
        artifact = existing_file(str((Path(domain["oracle"]["reference_directory"]) /
                                      variable["reference_csv"])), variable["sha256"])
        ncep_value_refs.append({"variable": variable["path"], **artifact})
    ncep_targets = {
        "grid_id": domain["domain"],
        "sample_id": "ncep_gfswave025_2026-10-02T0300",
        "object": {**ncep_object, "uri": ncep_sample["url"], "s3_uri": ncep_s3_uri,
                   "domain": domain["domain"],
                   "shape": domain["layout"]["shape"], "chunks": domain["layout"]["chunk_shape"],
                   "axes": domain["layout"]["axes"], "variable_count": len(domain["oracle"]["variables"])},
        "region_bounds_lonlat": ncep_bounds,
        "region_wkb": None,
        "independent_coordinate_reference": {
            "method": "lat=-90+row*0.25; lon=normalize_180(-180+column*0.25)",
            "source": "test/data/domain-manifest.json and specs/002-spatial-pushdown/evidence/domain.md",
        },
        "official_value_reference": {
            "method": "fixed OM C API full-domain and per-variable prefix reference",
            "manifest": "test/data/domain-manifest.json",
            "evidence": "evidence/003-dimensions-remote-parallel/final.md",
            "variables": ncep_value_refs,
        },
        "coordinate_points_in_region": len(ncep_points),
        "coordinate_tolerance_degrees": 0.0,
    }
    ncep_targets["first_source_position"] = ncep_points[0][1] * grid["nx"] + ncep_points[0][0]
    targets.insert(0, ncep_targets)

    o1280_sample = sample_objects["ecmwf_ifs_static_hsurf_reduced_gaussian_o1280"]
    o1280_value_record = value_refs[o1280_sample["id"]]["official_reference"]
    o1280_value = {
        **existing_file(o1280_value_record["path"], o1280_value_record["sha256"],
                        o1280_value_record["size_bytes"]),
        "reader": o1280_value_record["reader"],
        "rows": o1280_value_record["rows"],
        "command": o1280_value_record["command"],
    }
    o1280_object = existing_file(o1280_sample["local_copy"]["path"], o1280_sample["sha256"],
                                 o1280_sample["size_bytes"])
    o1280_coordinate_manifest = json.loads((O1280_EVIDENCE / "manifest.json").read_text())
    o1280_check = o1280_coordinate_manifest["checks"]["static-official-values-and-producer-coordinates"]
    if o1280_check["status"] != "pass" or o1280_check["positions"] != 6599680:
        raise RuntimeError("O1280 static producer-coordinate/value baseline is not verified")
    o1280_bounds = [-0.25, 89.8, 0.25, 89.99]
    o1280_output = O1280_OUTPUT / "static-domain.csv"
    output_record = next(item for item in o1280_coordinate_manifest["artifacts"]
                         if item["path"] == "static-domain.csv")
    output_file = existing_file(str(o1280_output.relative_to(ROOT)), output_record["sha256"], output_record["bytes"])
    o1280_count, o1280_first = count_and_first_position(o1280_output, "lon", "lat", o1280_bounds, "point_index")
    if not o1280_count or o1280_first is None:
        raise RuntimeError("frozen O1280 region does not cover a coordinate-oracle point")
    o1280_target = {
        "grid_id": "ecmwf_ifs",
        "sample_id": o1280_sample["id"],
        "object": {**o1280_object, "uri": o1280_sample["source_object"]["https_url"],
                   "s3_uri": o1280_sample["source_object"]["s3_uri"],
                   "variable_path": o1280_sample["array"]["variable_path"],
                   "shape": o1280_sample["array"]["shape"], "chunks": o1280_sample["array"]["chunk_shape"],
                   "axes": ["point"]},
        "region_bounds_lonlat": o1280_bounds,
        "region_wkb": None,
        "independent_coordinate_reference": {
            **output_file,
            "method": "pinned producer Float32 Gaussian coordinate port",
            "scope": o1280_check["coordinate_oracle_scope"],
            "max_lat_error_degrees": o1280_check["max_lat_error_degrees"],
            "max_lon_error_degrees": o1280_check["max_lon_error_degrees"],
        },
        "official_value_reference": o1280_value,
        "coordinate_points_in_region": o1280_count,
        "coordinate_tolerance_degrees": max(
            o1280_check["max_lat_error_degrees"], o1280_check["max_lon_error_degrees"]),
        "first_source_position": o1280_first["position"],
    }
    targets.append(o1280_target)

    out_dir = output.parent / "geometries"
    out_dir.mkdir(parents=True, exist_ok=True)
    for item in targets:
        encoded = wkb_rectangle(item["region_bounds_lonlat"])
        path = out_dir / f"real_{item['grid_id']}.wkb"
        path.write_bytes(encoded)
        item["region_wkb"] = {
            "path": path.relative_to(output.parent).as_posix(),
            "sha256": hashlib.sha256(encoded).hexdigest(),
            "bytes": len(encoded),
            "wkb_hex": encoded.hex(),
        }
        if item["grid_id"] == "ncep_gfswave025":
            columns = [variable["path"].rsplit("/", 1)[-1] for variable in domain["oracle"]["variables"]]
            item["queries"] = freeze_queries(item, columns, include_time=False)
            item["full_output_rows"] = item["object"]["shape"][0] * item["object"]["shape"][1]
            item["expected_region_rows"] = item["coordinate_points_in_region"]
        elif item["grid_id"] == "ecmwf_ifs":
            item["queries"] = freeze_queries(item, ["value"], include_time=False)
            item["full_output_rows"] = item["object"]["shape"][1]
            item["expected_region_rows"] = item["coordinate_points_in_region"]
        else:
            item["queries"] = freeze_queries(item, ["value"], include_time=False)
            item["full_output_rows"] = item["object"]["shape"][0] * item["object"]["shape"][1] * item["object"]["shape"][2]
            item["expected_region_rows"] = item["coordinate_points_in_region"] * item["object"]["shape"][2]
    if len(targets) != 5:
        raise RuntimeError("the frozen target set must contain exactly five grids")

    document = {
        "schema_version": 1,
        "status": "frozen_before_region_performance_measurement",
        "platform": "Linux AArch64",
        "pinned_producer_source": "test/data/grids/source-manifest.json",
        "baseline_evidence": {
            "grid_samples": "test/data/grids/sample-manifest.json",
            "coordinate_references": "test/data/grids/coordinate-reference.json",
            "value_references": "test/data/grids/value-reference-manifest.json",
            "ncep_domain": "test/data/domain-manifest.json",
            "o1280_execution": str((O1280_EVIDENCE / "manifest.json").relative_to(ROOT)),
        },
        "target_count": 5,
        "targets": targets,
        "missing_inputs": [],
        "input_status": "all frozen local objects, producer coordinate exports, and official value references are present and hash-checked",
        "freeze_policy": "object, axes/chunks, independent coordinate/value references and local polygon WKB are fixed before any 005 region performance run",
        "limitations": [
            "O1280 coordinates use the pinned producer Float32 port; they are not an original GRIB scan.",
            "Public S3 identity is the frozen local object SHA; no remote VersionId is claimed.",
            "This artifact freezes correctness inputs and does not claim 005 region-query results or savings.",
        ],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")
    temporary.replace(output)
    write_baseline(ROOT / "specs/005-polygon-region-statistics/evidence/baseline.md", document)
    return document


def write_baseline(path: Path, document: dict) -> None:
    lines = [
        "# Frozen real-region baseline",
        "",
        "Status: `inputs_frozen_before_region_performance_measurement`. This records correctness inputs and comparable full/local SQL; it makes no region-query or performance claim.",
        "",
        "| Grid | Object SHA-256 / bytes | Variable(s) | Axes / chunks | Polygon points | Coordinate/value baseline |",
        "|---|---|---|---|---:|---|",
    ]
    for item in document["targets"]:
        obj = item["object"]
        values = ", ".join(item["queries"]["value_columns"])
        axes = ",".join(obj["axes"] or [])
        chunks = ",".join(str(value) for value in obj["chunks"])
        refs = item["official_value_reference"]
        if isinstance(refs, dict) and "artifact" in refs:
            value_ref = refs["artifact"]
        elif isinstance(refs, dict) and "official_reference" in refs:
            value_ref = refs["official_reference"]
        else:
            value_ref = refs
        if isinstance(refs, dict) and refs.get("variables"):
            value_sha = f"{len(refs['variables'])} per-variable CSVs; hashes in frozen manifest"
        else:
            value_sha = value_ref.get("sha256", "recorded by source manifest") if isinstance(value_ref, dict) else "recorded by source manifest"
        coord = item["independent_coordinate_reference"]
        coord_sha = coord.get("sha256", "formula") if isinstance(coord, dict) else "formula"
        lines.append(f"| `{item['grid_id']}` | `{obj['sha256']}` / {obj['size_bytes']} | `{values}` | `{axes}` / `{chunks}` | {item['coordinate_points_in_region']} | coordinate `{coord_sha}`; official value `{value_sha}` |")
    lines.extend([
        "",
        "## Frozen comparison SQL",
        "",
        "Each target stores complete local and remote HTTPS/S3 `read_om` full-scan and `read_om_regions` queries where the source advertises both access paths, selected columns, region WKB, and the full/local comparison conditions in `test/data/regions/real-targets.json`. Run them with one thread and no SQL row limit; use identical output projection, retaining `om_source` to compare original positions. The region query additionally emits `region_id`.",
        "",
        "## Baseline scope and limitations",
        "",
        "- Missing frozen inputs: none. Every local object, coordinate artifact, and applicable official value reference is hash-checked; NCEP pins all 15 per-variable CSVs in the manifest.",
        "- The three projected-grid coordinate references are pinned-producer Float32 exports with their frozen 0.0001-degree comparison tolerance; official value CSVs were produced by the fixed OM C API. This tolerance compares reported coordinates only and never expands polygon membership.",
        "- The three time-series objects have a frozen raw time dimension but no semantic timestamp coordinate in their source metadata; comparisons retain original `logical_index` and do not infer `valid_time` labels.",
        "- NCEP coordinates use the frozen domain formula and its official OM C API per-variable references.",
        "- O1280 uses the existing 004 full-domain check (6,599,680 positions, zero value bit mismatches, zero recorded coordinate error) and pinned producer Float32 coordinate port. The selected coordinate subset is recounted from the retained `static-domain.csv` artifact and hash-checked against the original run manifest.",
        "- A validator-development attempt exercised only the first two local region queries and failed comparison; no correctness or performance gate passed. The registered validator reruns all five frozen targets against the references.",
        "",
    ])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    try:
        result = build(args.output.resolve())
    except (OSError, KeyError, TypeError, ValueError, StopIteration, RuntimeError) as error:
        print(f"freeze-region-targets: {error}", file=sys.stderr)
        return 1
    print(json.dumps({"output": str(args.output.resolve()), "targets": result["target_count"],
                      "points": {item["grid_id"]: item["coordinate_points_in_region"] for item in result["targets"]}}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
