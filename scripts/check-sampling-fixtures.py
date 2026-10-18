#!/usr/bin/env python3
"""Verify sampling fixture and independent reference manifest hashes."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import sys

PINNED_N1_PROFILE_SHA256 = {
    "locator.reference.csv": "ff1e5bc3aae2acbf6d76354521b0feb07e462c1852cdd8b23b8c02f01afb2ca1",
    "scientific.reference.csv": "bd1070959ba1a316f8e274c4d160bd693a49b1e6dd6b5f556105f2f27b7d9f33",
    "capability-boundaries.reference.csv": "8b1d16d3c661d0d8d413b0305527eda44623cbe4f787b4cab5b29e4e5f73ece0",
    "ncep-gfs025-profile.reference.csv": "99453febba1000b3bf141982b7c09c7a703956d147063e61ebbaa466c6bca231",
}

PINNED_REFERENCE_SHA256 = {
    # These lock the reviewed reference package independently of the manifest
    # being checked. Updating reference files requires an explicit pin review.
    "om-fixtures.json": "f7006f72bb33b02690deff4d9900587bae85e8ad7d622e5fea468faa85b2863e",
    "manifest.json": "ef0a882122959202cf504f41aa7df6c32ecdbc7097e689d3292a3570972582ab",
}


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def checked_path(root: Path, relative: str) -> Path:
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError(f"manifest path escapes sampling directory: {relative}")
    if not path.is_file():
        raise ValueError(f"manifest file is missing: {relative}")
    return path


def verify_pinned_reference_identity(output: Path) -> None:
    """Bind the reviewed official values and locator profiles outside their manifest."""
    output = output.resolve()
    for relative, expected in {**PINNED_REFERENCE_SHA256, **PINNED_N1_PROFILE_SHA256}.items():
        path = checked_path(output, relative)
        actual = digest(path)
        if actual != expected:
            raise ValueError(f"frozen reference hash mismatch: {relative}: {actual} != {expected}")


def verify(output: Path) -> dict:
    output = output.resolve()
    verify_pinned_reference_identity(output)
    om_manifest_path = checked_path(output, "om-fixtures.json")
    om_manifest = json.loads(om_manifest_path.read_text(encoding="utf-8"))
    seen_om: set[str] = set()
    seen_reference: set[str] = set()
    fixture_count = 0
    variable_count = 0
    for fixture in om_manifest.get("fixtures", []):
        fixture_count += 1
        relative = fixture["path"]
        if relative in seen_om:
            raise ValueError(f"duplicate OM path in manifest: {relative}")
        seen_om.add(relative)
        path = checked_path(output, relative)
        if path.stat().st_size != fixture["size_bytes"] or digest(path) != fixture["sha256"]:
            raise ValueError(f"OM size/hash mismatch: {relative}")
        for variable in fixture.get("variables", []):
            variable_count += 1
            reference_name = variable.get("reference_path")
            if reference_name is None:
                if variable.get("reference_sha256") is not None:
                    raise ValueError(f"reference hash without reference CSV: {relative}:{variable['path']}")
                continue
            if reference_name in seen_reference:
                raise ValueError(f"duplicate official reader CSV path: {reference_name}")
            seen_reference.add(reference_name)
            reference = checked_path(output, reference_name)
            if digest(reference) != variable["reference_sha256"]:
                raise ValueError(f"official reader CSV hash mismatch: {reference_name}")
            expected_rows = math.prod(variable["shape"])
            with reference.open(newline="", encoding="utf-8") as stream:
                reader = csv.DictReader(stream)
                if reader.fieldnames != ["index", "value"]:
                    raise ValueError(f"unexpected CSV schema: {reference_name}")
                rows = 0
                for row in reader:
                    if int(row["index"]) != rows:
                        raise ValueError(f"non-contiguous CSV index in {reference_name} at row {rows}")
                    float(row["value"])
                    rows += 1
            if rows != expected_rows:
                raise ValueError(f"CSV row count mismatch for {reference_name}: {rows} != {expected_rows}")
    ref_manifest_path = checked_path(output, "manifest.json")
    reference_manifest = json.loads(ref_manifest_path.read_text(encoding="utf-8"))
    om_identity = reference_manifest.get("om_fixture_manifest", {})
    if om_identity.get("path") != "om-fixtures.json" or om_identity.get("sha256") != digest(om_manifest_path):
        raise ValueError("independent reference manifest is not bound to the current OM manifest")
    locator_reference = reference_manifest.get("locator_reference_csv", {})
    locator_path = checked_path(output, locator_reference["path"])
    if digest(locator_path) != locator_reference.get("sha256"):
        raise ValueError("independent locator reference CSV hash mismatch")
    scientific_reference = reference_manifest.get("scientific_reference_csv", {})
    scientific_path = checked_path(output, scientific_reference.get("path", ""))
    if digest(scientific_path) != scientific_reference.get("sha256"):
        raise ValueError("independent scientific reference CSV hash mismatch")
    capability_reference = reference_manifest.get("capability_boundary_reference_csv", {})
    capability_path = checked_path(output, capability_reference.get("path", ""))
    if digest(capability_path) != capability_reference.get("sha256"):
        raise ValueError("independent capability-boundary reference CSV hash mismatch")
    with capability_path.open(newline="", encoding="utf-8") as stream:
        capability_reader = csv.DictReader(stream)
        if capability_reader.fieldnames != ["case_id", "nx", "ny", "lat0", "lon0", "dlat", "dlon",
                                            "earth_kind", "radius_m", "version",
                                            "allow_out_of_range_latitude", "geometry", "accepted", "reason"]:
            raise ValueError("unexpected capability-boundary reference CSV schema")
        capability_rows = list(capability_reader)
    if len(capability_rows) != capability_reference.get("rows") or not capability_rows:
        raise ValueError("capability-boundary reference row count mismatch")
    if any(row["accepted"] not in {"true", "false"} or not row["reason"] for row in capability_rows):
        raise ValueError("invalid independent capability-boundary classification")
    capability_classes = {row["reason"] for row in capability_rows if row["accepted"] == "false"}
    if len(capability_rows) != 377 or capability_classes != {
            "earth-radius", "earth-model", "definition-version", "latitude-policy", "grid-geometry",
            "positive-dimensions", "signed-point-count", "finite-axis-parameters",
            "exact-binary64-index", "finite-axis-endpoint", "nonzero-axis-step", "nonfinite-period", "latitude-range",
            "latitude-binary64-collapse", "longitude-binary64-collapse",
            "duplicate-normalized-longitude", "multiple-longitude-wraps"}:
        raise ValueError("capability-boundary reference does not cover every frozen reachable rejection class")
    full_span_case = next((row for row in capability_rows
                           if row["case_id"] == "reject-full-span-preempted-by-wrap-check"), None)
    if (full_span_case is None or full_span_case["accepted"] != "false" or
            full_span_case["reason"] != "multiple-longitude-wraps"):
        raise ValueError("full-span fixture must record its earlier reachable multiple-wrap rejection")
    if (sum(row["accepted"] == "true" for row in capability_rows) != 203 or
            sum(row["accepted"] == "false" for row in capability_rows) != 174):
        raise ValueError("capability-boundary acceptance/rejection counts differ from the frozen profile")
    profile_reference = reference_manifest.get("ncep_gfs025_profile_reference_csv", {})
    profile_path = checked_path(output, profile_reference.get("path", ""))
    if digest(profile_path) != profile_reference.get("sha256"):
        raise ValueError("independent ncep_gfs025 profile reference hash mismatch")
    with profile_path.open(newline="", encoding="utf-8") as stream:
        profile_reader = csv.DictReader(stream)
        expected_profile_fields = ["target_id", "target_lon", "target_lat", "source_point_index", "source_x",
                                   "source_y", "source_lon", "source_lat", "distance_m",
                                   "high_precision_distance_m", "tie_count", "coordinate_comparisons"]
        if profile_reader.fieldnames != expected_profile_fields:
            raise ValueError("unexpected ncep_gfs025 profile reference CSV schema")
        profile_rows = list(profile_reader)
    if len(profile_rows) != profile_reference.get("rows") or len(profile_rows) < 10:
        raise ValueError("ncep_gfs025 scientific profile row count is incomplete")
    expected_targets = {"south-near-pole", "south-west-interior", "southern-midlatitude",
                        "southern-equator-crossing", "equatorial-seam-east", "equatorial-seam-west",
                        "equatorial-four-way-midpoint", "equatorial-exact-source", "northern-midlatitude",
                        "north-near-pole", "north-pole-band", "wrapped-longitude"}
    if {row["target_id"] for row in profile_rows} != expected_targets:
        raise ValueError("ncep_gfs025 profile target set is incomplete or contains unexpected targets")
    expected_comparisons = profile_reference.get("nx", 0) * profile_reference.get("ny", 0)
    if (profile_reference.get("nx"), profile_reference.get("ny")) != (1440, 721) or \
            any(int(row["coordinate_comparisons"]) != expected_comparisons for row in profile_rows):
        raise ValueError("ncep_gfs025 profile reference is not a full 1440x721 exhaustive enumeration")
    if not any(row["target_id"] == "equatorial-four-way-midpoint" and int(row["tie_count"]) == 2
               for row in profile_rows):
        raise ValueError("ncep_gfs025 profile is missing its exact seam tie classification")
    with scientific_path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        expected_fields = ["id", "target_lon", "target_lat", "source_lon", "source_lat", "radius_m",
                           "distance_f64_m", "distance_high_precision_m", "bound"]
        if reader.fieldnames != expected_fields:
            raise ValueError("unexpected scientific reference CSV schema")
        scientific_rows = list(reader)
    if len(scientific_rows) != scientific_reference.get("rows") or not scientific_rows:
        raise ValueError("scientific reference row count mismatch")
    if any(row["bound"] not in {"ordinary", "singular"} for row in scientific_rows):
        raise ValueError("unknown scientific reference error-bound class")
    for geometry in reference_manifest.get("geometries", []):
        path = checked_path(output, geometry["wkb_path"])
        if path.stat().st_size != geometry["wkb_bytes"] or digest(path) != geometry["wkb_sha256"]:
            raise ValueError(f"WKB size/hash mismatch: {geometry['wkb_path']}")
    resource_fixtures = reference_manifest.get("resource_fixtures", {})
    skippable = resource_fixtures.get("skippable_values")
    if not isinstance(skippable, dict):
        raise ValueError("independent manifest is missing the skippable-values I/O acceptance fixture")
    skippable_path = checked_path(output, skippable["path"])
    if skippable_path.stat().st_size != skippable["size_bytes"] or digest(skippable_path) != skippable["sha256"]:
        raise ValueError("skippable-values OM size/hash mismatch")
    reference_path = checked_path(output, skippable["reference_path"])
    if digest(reference_path) != skippable["reference_sha256"]:
        raise ValueError("skippable-values official-reader CSV hash mismatch")
    skippable_fixture = next((item for item in om_manifest["fixtures"]
                              if item["path"] == skippable["path"]), None)
    if (skippable_fixture is None or
            skippable_fixture["size_bytes"] != skippable["size_bytes"] or
            skippable_fixture["sha256"] != skippable["sha256"]):
        raise ValueError("skippable-values resource fixture is not bound to the official reader manifest")
    skippable_variable = next((item for item in skippable_fixture["variables"]
                               if item["path"] == skippable["variable_path"]), None)
    if (skippable_variable is None or
            skippable_variable["shape"] != skippable["shape"] or
            skippable_variable["chunk_shape"] != skippable["chunk_shape"] or
            skippable_variable["reference_path"] != skippable["reference_path"] or
            skippable_variable["reference_sha256"] != skippable["reference_sha256"]):
        raise ValueError("skippable-values fixture shape/chunks/value identity is inconsistent")
    for row in reference_manifest.get("expected_nearest", []):
        if row.get("source_point_index") is not None:
            grid = next((item for item in reference_manifest["grids"] if item["id"] == row.get("grid_id")), None)
            if grid is None or not 0 <= row["source_point_index"] < grid["nx"] * grid["ny"]:
                raise ValueError(f"source point index is outside its grid: {row}")
            if row.get("status") not in {"ok", "beyond_max_distance"}:
                raise ValueError(f"non-candidate status unexpectedly carries a source point: {row}")
        elif row.get("distance_m") is not None or row.get("source_lon") is not None or row.get("source_lat") is not None:
            raise ValueError(f"status without a source candidate has distance/coordinates: {row}")
    if not fixture_count or not variable_count:
        raise ValueError("OM manifest contains no fixtures or variables")
    expected_nearest = reference_manifest.get("expected_nearest", [])
    if len(expected_nearest) < 1:
        raise ValueError("independent nearest reference manifest is empty")
    axis_counterexamples = [
        row for row in expected_nearest
        if row.get("status") == "ok" and row.get("axis_nearest_point_index") is not None
        and row.get("source_point_index") != row.get("axis_nearest_point_index")
    ]
    if len(axis_counterexamples) != 1 or axis_counterexamples[0].get("target_id") != "axis-nearest-counterexample":
        raise ValueError("independent manifest must bind exactly one spherical-vs-axis nearest counterexample")
    with locator_path.open(newline="", encoding="utf-8") as stream:
        locator_rows = list(csv.DictReader(stream))
    if not any(row.get("grid_id") == "axis-nearest-counterexample" and
               row.get("target_id") == "axis-nearest-counterexample" and
               row.get("source_point_index") == "0" and row.get("axis_nearest_point_index") == "2"
               for row in locator_rows):
        raise ValueError("independent locator CSV is missing the frozen spherical-vs-axis counterexample")
    return {"om_fixtures": fixture_count, "variables": variable_count,
            "geometries": len(reference_manifest.get("geometries", [])),
            "nearest_references": len(reference_manifest.get("expected_nearest", [])),
            "om_manifest_sha256": digest(om_manifest_path), "reference_manifest_sha256": digest(ref_manifest_path)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1] / "test/data/sampling")
    args = parser.parse_args()
    try:
        result = verify(args.root)
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError, csv.Error) as error:
        print(f"check-sampling-fixtures: {error}", file=sys.stderr)
        return 1
    print(json.dumps({"status": "pass", **result}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
