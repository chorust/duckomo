#!/usr/bin/env python3
"""Compare DuckOMO's complete sample scans with frozen independent references.

The coordinate scan projects no value column and is restricted to time index
zero. The value scan consumes the complete object and compares each original
OM logical index with the fixed official OM C export. Missing reference
targets are reported as not-run by the caller; this command only processes
samples that have both coordinate and official-value references.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import itertools
import json
import math
import mmap
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    "gem_rdps_10km": "rotated_v3",
    "gem_regional": "stereographic_v3",
    "aladin_central_europe_2km": "lambert_v3",
    "n160": "gaussian_n160_v3",
    "n320": "gaussian_n320_v3",
    "n320_ecmwf_aifs_europe_ensemble": "gaussian_n320_region_v3",
}
TARGET_ORDER = (
    "rotated_v3", "lambert_v3", "stereographic_v3",
    "gaussian_n160_v3", "gaussian_n320_v3", "gaussian_n320_region_v3",
)
DOMAIN_LAYOUT = {
    "gem_rdps_10km": "['y','x','time']",
    "gem_regional": "['y','x','time']",
    "aladin_central_europe_2km": "['y','x','time']",
}
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class ReferenceCompareError(ValueError):
    pass


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ReferenceCompareError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ReferenceCompareError(f"expected a JSON object in {path}")
    return value


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def resolve_file(value: Any, root: Path, field: str) -> Path:
    if not isinstance(value, str) or not value:
        raise ReferenceCompareError(f"{field} must be a non-empty path")
    path = Path(value)
    if not path.is_absolute():
        path = root / path
    path = path.resolve()
    if not path.is_file():
        raise ReferenceCompareError(f"{field} file is missing: {path}")
    return path


def checked_artifact(record: Any, root: Path, field: str) -> tuple[Path, dict[str, Any]]:
    if not isinstance(record, dict):
        raise ReferenceCompareError(f"{field} must contain path and sha256")
    path = resolve_file(record.get("path"), root, field)
    expected = record.get("sha256")
    if not isinstance(expected, str) or not SHA256_RE.fullmatch(expected):
        raise ReferenceCompareError(f"{field}.sha256 must be a lowercase SHA-256")
    actual = sha256(path)
    if actual != expected:
        raise ReferenceCompareError(f"{field} SHA-256 mismatch: expected {expected}, got {actual}")
    expected_size = record.get("size_bytes")
    if expected_size is not None and expected_size != path.stat().st_size:
        raise ReferenceCompareError(f"{field} size mismatch: expected {expected_size}, got {path.stat().st_size}")
    return path, {"path": str(path), "sha256": actual, "size_bytes": path.stat().st_size}


def csv_rows(path: Path, fieldnames: tuple[str, ...], label: str) -> Iterable[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        if tuple(reader.fieldnames or ()) != fieldnames:
            raise ReferenceCompareError(f"{label} columns must be {fieldnames}, got {reader.fieldnames}")
        yield from reader


def compare_coordinates(actual: Path, expected: Path, row_count: int, nx: int,
                        tolerance: float) -> dict[str, Any]:
    actual_rows = csv_rows(actual, ("spatial_index", "longitude", "latitude"), "DuckOMO coordinate output")
    expected_rows = csv_rows(expected,
                             ("spatial_index", "row", "column", "longitude", "latitude"),
                             "independent coordinate reference")
    count = 0
    maximum_error = 0.0
    worst_index: int | None = None
    first_exceeded_index: int | None = None
    positions_over_tolerance = 0
    for count, pair in enumerate(itertools.zip_longest(actual_rows, expected_rows), start=1):
        actual_row, expected_row = pair
        if actual_row is None or expected_row is None:
            raise ReferenceCompareError("DuckOMO coordinate output and independent reference have different row counts")
        try:
            actual_index = int(actual_row["spatial_index"])
            expected_index = int(expected_row["spatial_index"])
            expected_row_index = int(expected_row["row"])
            expected_column = int(expected_row["column"])
            actual_lon, actual_lat = float(actual_row["longitude"]), float(actual_row["latitude"])
            expected_lon, expected_lat = float(expected_row["longitude"]), float(expected_row["latitude"])
        except (TypeError, ValueError) as error:
            raise ReferenceCompareError(f"invalid coordinate row {count}: {error}") from error
        if actual_index != expected_index:
            raise ReferenceCompareError(
                f"coordinate source-position mismatch at row {count}: {actual_index} != {expected_index}"
            )
        if expected_index != expected_row_index * nx + expected_column:
            raise ReferenceCompareError(f"independent reference row/column mapping is inconsistent at {expected_index}")
        if expected_index != count - 1:
            raise ReferenceCompareError(f"coordinate reference index is not contiguous at row {count}: {expected_index}")
        if not all(math.isfinite(item) for item in (actual_lon, actual_lat, expected_lon, expected_lat)):
            raise ReferenceCompareError(f"non-finite coordinate at spatial index {expected_index}")
        if not all(-180.0 <= lon <= 180.0 for lon in (actual_lon, expected_lon)) or \
                not all(-90.0 <= lat <= 90.0 for lat in (actual_lat, expected_lat)):
            raise ReferenceCompareError(f"coordinate out of range at spatial index {expected_index}")
        lon_error = abs((actual_lon - expected_lon + 180.0) % 360.0 - 180.0)
        error = max(lon_error, abs(actual_lat - expected_lat))
        if error > maximum_error:
            maximum_error, worst_index = error, expected_index
        if error > tolerance:
            positions_over_tolerance += 1
            if first_exceeded_index is None:
                first_exceeded_index = expected_index
    if count != row_count:
        raise ReferenceCompareError(f"coordinate row count mismatch: {count} != {row_count}")
    return {"status": "fail" if positions_over_tolerance else "pass", "rows_compared": count,
            "positions_over_tolerance": positions_over_tolerance,
            "first_exceeded_spatial_index": first_exceeded_index,
            "maximum_error_degrees": maximum_error,
            "maximum_allowed_error_degrees": tolerance, "worst_spatial_index": worst_index}


def parse_float(value: str, label: str, index: int) -> float:
    if value.strip().lower() in {"nan", "+nan", "-nan"}:
        return math.nan
    try:
        number = float(value)
    except ValueError as error:
        raise ReferenceCompareError(f"invalid {label} value at index {index}: {value!r}") from error
    if math.isinf(number):
        raise ReferenceCompareError(f"infinite {label} value at index {index}")
    return number


def same_float32(left: float, right: float) -> bool:
    if math.isnan(left) or math.isnan(right):
        return math.isnan(left) and math.isnan(right)
    try:
        return struct.pack("!f", left) == struct.pack("!f", right)
    except OverflowError:
        return False


def is_missing(value: str) -> bool:
    normalized = value.strip().lower()
    return normalized in {"", "null", "nan", "+nan", "-nan"}


def compare_values(actual: Path, expected: Path, row_count: int, expected_nulls: int) -> dict[str, Any]:
    actual_rows = csv_rows(actual, ("logical_index", "value"), "DuckOMO value output")
    expected_rows = csv_rows(expected, ("index", "value"), "official OM C value reference")
    if row_count <= 0:
        raise ReferenceCompareError("value reference row count must be positive")

    # The serial DuckOMO scan order may be a permutation of the OM root-array
    # order (for example, one time slice at a time). Build a compact, temporary
    # Float32 index of the official CSV so each emitted logical_index can be
    # checked directly without sorting or materializing the DuckOMO output.
    with tempfile.TemporaryFile(mode="w+b") as reference_binary:
        reference_count = reference_null_count = 0
        for row in expected_rows:
            try:
                expected_index = int(row["index"])
            except (TypeError, ValueError) as error:
                raise ReferenceCompareError(
                    f"invalid official reference index at row {reference_count + 1}: {error}"
                ) from error
            if expected_index != reference_count:
                raise ReferenceCompareError(
                    f"official reference index is not contiguous at row {reference_count + 1}: "
                    f"{expected_index} != {reference_count}"
                )
            expected_value = row["value"]
            if is_missing(expected_value):
                reference_null_count += 1
                reference_binary.write(struct.pack("!f", math.nan))
            else:
                reference_binary.write(struct.pack("!f", parse_float(
                    expected_value, "official reference", expected_index)))
            reference_count += 1
        if reference_count != row_count:
            raise ReferenceCompareError(f"official reference row count mismatch: {reference_count} != {row_count}")
        if reference_null_count != expected_nulls:
            raise ReferenceCompareError(
                f"official reference null count mismatch: {reference_null_count} != {expected_nulls}"
            )

        reference_binary.flush()
        reference_map = mmap.mmap(reference_binary.fileno(), row_count * 4, access=mmap.ACCESS_READ)
        try:
            seen = bytearray((row_count + 7) // 8)
            count = null_count = 0
            for count, actual_row in enumerate(actual_rows, start=1):
                try:
                    actual_index = int(actual_row["logical_index"])
                except (TypeError, ValueError) as error:
                    raise ReferenceCompareError(f"invalid DuckOMO logical index at row {count}: {error}") from error
                if actual_index < 0 or actual_index >= row_count:
                    raise ReferenceCompareError(
                        f"DuckOMO logical index is out of range at row {count}: {actual_index}"
                    )
                byte_index, bit = divmod(actual_index, 8)
                mask = 1 << bit
                if seen[byte_index] & mask:
                    raise ReferenceCompareError(f"duplicate DuckOMO logical index: {actual_index}")
                seen[byte_index] |= mask

                actual_value = actual_row["value"]
                expected_value = struct.unpack_from("!f", reference_map, actual_index * 4)[0]
                if is_missing(actual_value):
                    if not math.isnan(expected_value):
                        raise ReferenceCompareError(f"missing-value mismatch at logical index {actual_index}")
                    null_count += 1
                else:
                    if math.isnan(expected_value) or not same_float32(
                        parse_float(actual_value, "DuckOMO", actual_index), expected_value
                    ):
                        raise ReferenceCompareError(f"Float32 value mismatch at logical index {actual_index}")
            if count != row_count:
                raise ReferenceCompareError(f"DuckOMO value row count mismatch: {count} != {row_count}")
            if null_count != expected_nulls:
                raise ReferenceCompareError(f"DuckOMO null count mismatch: {null_count} != {expected_nulls}")
        finally:
            reference_map.close()

    return {"status": "pass", "positions_compared": count, "null_count": null_count,
            "comparison": "exact Float32 bit values keyed by original logical_index; NaNs compared as missing",
            "scan_row_order": "arbitrary; unique, in-range logical indices required"}


def sql_literal(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def run_copy(duckdb: Path, extension: Path, sql_path: Path, cwd: Path) -> dict[str, Any]:
    command = [str(duckdb), "-unsigned", "-bail", "-batch", "-csv", ":memory:", "-f", str(sql_path)]
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True)
    return {"argv": command, "exit_code": result.returncode, "stdout": result.stdout,
            "stderr": result.stderr, "duckdb_sha256": sha256(duckdb),
            "extension_sha256": sha256(extension), "sql_sha256": sha256(sql_path)}


def run_sample(args: argparse.Namespace, sample: dict[str, Any], coordinate_record: dict[str, Any],
               value_record: dict[str, Any]) -> dict[str, Any]:
    root = args.root.resolve()
    output = args.output_dir.resolve()
    sample_id = sample["id"]
    definition_id = sample["grid_definition_id"]
    target = TARGETS[definition_id]
    if definition_id not in DOMAIN_LAYOUT:
        return {"target": target, "sample_id": sample_id, "status": "not-run",
                "reason": "no frozen object axis profile is available for a value/coordinate scan"}
    dimensions = DOMAIN_LAYOUT[definition_id]
    object_path = resolve_file(sample.get("local_copy", {}).get("path"), root, f"{sample_id} local object")
    if sha256(object_path) != sample.get("sha256"):
        raise ReferenceCompareError(f"sample object hash changed for {sample_id}")
    coordinate_path, coordinate_artifact = checked_artifact(
        coordinate_record.get("artifact"), root, f"{sample_id} coordinate oracle")
    value_path, value_artifact = checked_artifact(value_record.get("artifact"), root, f"{sample_id} value oracle")
    expected_input_hashes = {sample.get("sha256")}
    if not any(item.get("sha256") in expected_input_hashes for item in coordinate_record.get("inputs", [])):
        raise ReferenceCompareError(f"coordinate reference does not pin the sample object for {sample_id}")
    if not any(item.get("sha256") in expected_input_hashes for item in value_record.get("inputs", [])):
        raise ReferenceCompareError(f"value reference does not pin the sample object for {sample_id}")
    if value_record.get("method") != "official_om_c_api" or value_record.get("variable_path") != "/":
        raise ReferenceCompareError(f"value reference is not the expected official OM C root-array export for {sample_id}")
    tolerance = coordinate_record.get("tolerance_degrees")
    maximum = {"openmeteo_f32_v1": 1e-4, "float64_v1": 1e-8}.get(coordinate_record.get("numeric_policy"))
    if not isinstance(tolerance, (int, float)) or maximum is None or tolerance <= 0 or tolerance > maximum:
        raise ReferenceCompareError(f"coordinate tolerance is absent or exceeds the frozen policy for {sample_id}")

    actual_coordinates = output / f"{sample_id}.duckomo-coordinates.csv"
    actual_values = output / f"{sample_id}.duckomo-values.csv"
    coordinate_sql = output / f"{sample_id}.coordinates.sql"
    value_sql = output / f"{sample_id}.values.sql"
    common = (
        f"read_om({sql_literal(str(object_path))}, dimensions := map(['value'], [{dimensions}]), "
        f"domain := {sql_literal(definition_id)}, include_source := true)"
    )
    coordinate_common = (
        f"read_om({sql_literal(str(object_path))}, dimensions := map(['value'], [{dimensions}]), "
        f"domain := {sql_literal(definition_id)}, "
        "axes := {'time': {'axis':'time', 'start': TIMESTAMP '2000-01-01 00:00:00', "
        "'step': INTERVAL '1 hour'}}, include_source := true)"
    )
    coordinate_sql.write_text(
        f"LOAD {sql_literal(str(args.extension.resolve()))};\nSET threads = 1;\n"
        f"COPY (SELECT om_source.point_index AS spatial_index, lon AS longitude, lat AS latitude FROM {coordinate_common} "
        "WHERE valid_time = TIMESTAMP '2000-01-01 00:00:00') "
        f"TO {sql_literal(str(actual_coordinates))} "
        "(FORMAT CSV, HEADER true);\n", encoding="utf-8")
    value_sql.write_text(
        f"LOAD {sql_literal(str(args.extension.resolve()))};\nSET threads = 1;\n"
        # Preserve the scan's natural emission order. compare_values validates
        # each value by its original logical_index, independent of this order.
        f"COPY (SELECT om_source.logical_index, value FROM {common}) "
        f"TO {sql_literal(str(actual_values))} (FORMAT CSV, HEADER true);\n", encoding="utf-8")
    coordinate_command = run_copy(args.duckdb.resolve(), args.extension.resolve(), coordinate_sql, root)
    if coordinate_command["exit_code"] != 0:
        raise ReferenceCompareError(f"DuckOMO coordinate query failed for {sample_id}: {coordinate_command['stderr']}")
    value_command = run_copy(args.duckdb.resolve(), args.extension.resolve(), value_sql, root)
    if value_command["exit_code"] != 0:
        raise ReferenceCompareError(f"DuckOMO value query failed for {sample_id}: {value_command['stderr']}")

    coordinate_stats = compare_coordinates(actual_coordinates, coordinate_path,
                                            int(coordinate_record["row_count"]), int(sample["array"]["shape"][1]),
                                            float(tolerance))
    value_stats = compare_values(actual_values, value_path, int(value_record["row_count"]),
                                 int(value_record["null_count"]))
    coordinate_failure = coordinate_stats["status"] != "pass"
    return {
        "target": target,
        "sample_id": sample_id,
        "status": "fail" if coordinate_failure else "pass",
        "reason": (f"coordinate tolerance exceeded at {coordinate_stats['positions_over_tolerance']} positions; "
                   f"maximum error {coordinate_stats['maximum_error_degrees']} degrees")
                  if coordinate_failure else None,
        "scope": "expected [ny,nx,ntime] producer profile; coordinates are selected at a synthetic valid_time index zero; "
                  "source-axis mapping remains separately unverified",
        "input_object": {"path": str(object_path), "sha256": sample["sha256"]},
        "coordinate_reference": coordinate_artifact,
        "value_reference": value_artifact,
        "coordinate_output": {"path": str(actual_coordinates), "sha256": sha256(actual_coordinates),
                              "size_bytes": actual_coordinates.stat().st_size},
        "value_output": {"path": str(actual_values), "sha256": sha256(actual_values),
                         "size_bytes": actual_values.stat().st_size},
        "coordinates": coordinate_stats,
        "values": value_stats,
        "commands": {"coordinates": coordinate_command, "values": value_command},
    }


def run(args: argparse.Namespace) -> dict[str, Any]:
    root = args.root.resolve()
    sample_manifest = load_json(args.sample_manifest.resolve())
    coordinates = load_json(args.coordinate_reference.resolve())
    values = load_json(args.value_reference.resolve())
    samples = sample_manifest.get("public_open_meteo_samples", {}).get("objects", [])
    if not isinstance(samples, list):
        raise ReferenceCompareError("sample manifest public_open_meteo_samples.objects must be an array")
    sample_by_id = {row.get("id"): row for row in samples if isinstance(row, dict) and isinstance(row.get("id"), str)}
    coordinate_by_target = {row.get("target"): row for row in coordinates.get("references", [])
                            if isinstance(row, dict)}
    value_by_target = {row.get("target"): row for row in values.get("references", []) if isinstance(row, dict)}
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for target in TARGET_ORDER:
        coordinate_record = coordinate_by_target.get(target)
        value_record = value_by_target.get(target)
        if coordinate_record is None or value_record is None:
            results.append({"target": target, "status": "not-run",
                            "reason": "coordinate and official full-value references are both required"})
            continue
        sample_id = coordinate_record.get("sample_id")
        if sample_id != value_record.get("sample_id"):
            results.append({"target": target, "sample_id": sample_id, "status": "fail",
                            "reason": "coordinate and value references name different samples"})
            continue
        sample = sample_by_id.get(sample_id)
        if not sample or TARGETS.get(sample.get("grid_definition_id")) != target:
            results.append({"target": target, "sample_id": sample_id, "status": "fail",
                            "reason": "reference sample is absent or does not match its frozen target definition"})
            continue
        try:
            results.append(run_sample(args, sample, coordinate_record, value_record))
        except (OSError, KeyError, TypeError, ValueError, subprocess.SubprocessError) as error:
            results.append({"target": target, "sample_id": sample_id, "status": "fail", "reason": str(error)})
    covered = {row["target"] for row in results if row.get("status") == "pass"}
    missing = sorted(set(TARGET_ORDER) - covered)
    failed = any(row.get("status") == "fail" for row in results)
    status = "fail" if failed else "partial_pass" if covered else "not-run"
    return {
        "schema_version": 1,
        "status": status,
        "full_h1_status": "pass" if not missing and not any(row.get("status") != "pass" for row in results) else "not-run",
        "covered_targets": sorted(covered),
        "missing_targets": missing,
        "sample_manifest_sha256": sha256(args.sample_manifest.resolve()),
        "coordinate_reference_manifest_sha256": sha256(args.coordinate_reference.resolve()),
        "value_reference_manifest_sha256": sha256(args.value_reference.resolve()),
        "results": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--sample-manifest", type=Path, default=ROOT / "test/data/grids/sample-manifest.json")
    parser.add_argument("--coordinate-reference", type=Path, default=ROOT / "test/data/grids/coordinate-reference.json")
    parser.add_argument("--value-reference", type=Path, default=ROOT / "test/data/grids/value-reference-manifest.json")
    parser.add_argument("--duckdb", type=Path, required=True)
    parser.add_argument("--extension", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.root = args.root.resolve()
    args.sample_manifest = args.sample_manifest.resolve()
    args.coordinate_reference = args.coordinate_reference.resolve()
    args.value_reference = args.value_reference.resolve()
    args.duckdb = args.duckdb.resolve()
    args.extension = args.extension.resolve()
    try:
        report = run(args)
    except (OSError, ReferenceCompareError, json.JSONDecodeError, KeyError, TypeError) as error:
        report = {"schema_version": 1, "status": "fail", "full_h1_status": "not-run", "reason": str(error)}
    output_path = args.output_dir.resolve() / "h1-reference-comparison.json"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("status", "full_h1_status", "covered_targets",
                                                       "missing_targets", "reason") if key in report},
                     sort_keys=True))
    if report.get("status") == "fail":
        return 1
    if report.get("status") == "not-run":
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
