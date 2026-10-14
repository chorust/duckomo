#!/usr/bin/env python3
"""Compare frozen real region reads with independent coordinate/value references."""

import argparse
import csv
import hashlib
import json
import math
import pathlib
import struct
import subprocess
import sys


class MissingInput(Exception):
    pass


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sql_string(value):
    return "'" + str(value).replace("'", "''") + "'"


def number(value):
    return float(value)


def same_number(left, right):
    left_text, right_text = str(left), str(right)
    left_null = left_text == "" or left_text.lower() == "null"
    right_null = right_text == "" or right_text.lower() == "null"
    if left_null or right_null:
        other = right_text if left_null else left_text
        try:
            return math.isnan(float(other))
        except ValueError:
            return False
    a, b = number(left), number(right)
    if math.isnan(a) or math.isnan(b):
        return math.isnan(a) and math.isnan(b)
    try:
        return struct.pack("!f", a) == struct.pack("!f", b)
    except OverflowError:
        return a == b


def inside_bounds(longitude, latitude, bounds):
    west, south, east, north = bounds
    return west <= longitude <= east and south <= latitude <= north


def read_coordinates(target, root):
    grid_id = target["grid_id"]
    bounds = target["region_bounds_lonlat"]
    result = {}
    if grid_id == "ncep_gfswave025":
        for row in range(721):
            latitude = -90.0 + row * 0.25
            for column in range(1440):
                longitude = -180.0 + column * 0.25
                if longitude >= 180.0:
                    longitude -= 360.0
                if inside_bounds(longitude, latitude, bounds):
                    spatial_index = row * 1440 + column
                    result[spatial_index] = (latitude, longitude)
        return result

    reference = target["independent_coordinate_reference"]
    path = root / reference["path"]
    if not path.is_file():
        raise MissingInput(str(path))
    if sha256(path) != reference["sha256"] or path.stat().st_size != reference["size_bytes"]:
        raise ValueError(f"coordinate reference identity mismatch for {grid_id}")
    with open(path, newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            longitude = number(row.get("longitude", row.get("lon")))
            latitude = number(row.get("latitude", row.get("lat")))
            if not inside_bounds(longitude, latitude, bounds):
                continue
            if grid_id == "ecmwf_ifs":
                spatial_index = int(row["logical_index"])
            else:
                spatial_index = int(row["spatial_index"])
            result[spatial_index] = (latitude, longitude)
    return result


def expected_positions(target, coordinates):
    shape = target["object"]["shape"]
    time_count = shape[-1] if target["object"]["axes"][-1] == "time" else 1
    positions = {}
    for spatial_index in coordinates:
        for time_index in range(time_count):
            position = spatial_index * time_count + time_index
            positions[position] = spatial_index
    return positions, time_count


def reference_paths(target, root):
    reference = target["official_value_reference"]
    if "variables" in reference:
        return {item["variable"].lstrip("/"): root / item["path"] for item in reference["variables"]}
    return {"value": root / reference["path"]}


def selected_values(path, positions, grid_id, variable):
    values = {}
    wanted = set(positions)
    with open(path, newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames or not {"index", "value"}.issubset(reader.fieldnames):
            raise ValueError(f"value reference has an unexpected CSV schema: {path}")
        for row in reader:
            index = int(row["index"])
            if index in wanted:
                values[index] = row["value"]
                if len(values) == len(wanted):
                    break
    if len(values) != len(wanted):
        raise ValueError(f"official value reference is missing selected indexes for {grid_id}/{variable}")
    return values


def make_sql(target, output_csv, metrics_csv, extension, spatial):
    query = target["queries"]["region_local_sql"]
    return "\n".join(
        [
            f"LOAD {sql_string(extension)};",
            f"LOAD {sql_string(spatial)};",
            "SET threads=1;",
            "COPY (SELECT *, om_source.logical_index AS source_index "
            f"FROM ({query}) AS region_rows ORDER BY source_index) TO {sql_string(output_csv)} "
            "(HEADER, DELIMITER ',');",
            "COPY (SELECT metrics::JSON AS metrics FROM duckomo_last_scan_metrics()) "
            f"TO {sql_string(metrics_csv)} (HEADER, DELIMITER ',');",
            "",
        ]
    )


def check_actual(target, coordinates, positions, values_by_variable, actual_path):
    grid_id = target["grid_id"]
    mismatches = []
    actual_positions = {}
    actual_variables = set()
    with open(actual_path, newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        required = {"source_index", "region_id", "lat", "lon"}
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            raise ValueError(f"region result has an unexpected CSV schema for {grid_id}")
        actual_variables = set(values_by_variable).intersection(reader.fieldnames)
        if actual_variables != set(values_by_variable):
            raise ValueError(f"region result value columns differ for {grid_id}")
        for row in reader:
            index = int(row["source_index"])
            if index in actual_positions:
                mismatches.append(f"duplicate source index {index}")
            actual_positions[index] = row
    if set(actual_positions) != set(positions):
        missing = sorted(set(positions) - set(actual_positions))[:5]
        extra = sorted(set(actual_positions) - set(positions))[:5]
        mismatches.append(f"source indexes differ (missing={missing}, extra={extra})")
    for index, row in actual_positions.items():
        if index not in positions:
            continue
        spatial_index = positions[index]
        expected_latitude, expected_longitude = coordinates[spatial_index]
        if row["region_id"] != grid_id:
            mismatches.append(f"source index {index} has unexpected region_id")
        tolerance = target["coordinate_tolerance_degrees"]
        if (abs(number(row["lat"]) - expected_latitude) > tolerance or
                abs(number(row["lon"]) - expected_longitude) > tolerance):
            mismatches.append(f"source index {index} differs from independent coordinates")
        for variable, expected in values_by_variable.items():
            if not same_number(row[variable], expected[index]):
                mismatches.append(f"source index {index} differs from official {variable} value")
    return mismatches


def run(args):
    root = args.root.resolve()
    evidence = args.output.resolve()
    main_manifest_path = args.manifest.resolve()
    main_manifest = json.loads(main_manifest_path.read_text(encoding="utf-8"))
    fixture = main_manifest["fixture"]
    fixture_path = root / "test/data/regions" / fixture["path"]
    if not fixture_path.is_file():
        raise MissingInput(str(fixture_path))
    if fixture_path.stat().st_size != fixture["size_bytes"] or sha256(fixture_path) != fixture["sha256"]:
        raise ValueError("teaching fixture hash/size mismatch")
    oracle_hashes = {}
    for name, path_key, hash_key in (
        ("membership", "expected_membership_path", "expected_membership_sha256"),
        ("statistics", "expected_group_statistics_path", "expected_group_statistics_sha256"),
    ):
        oracle_path = root / "test/data/regions" / main_manifest[path_key]
        if not oracle_path.is_file():
            raise MissingInput(str(oracle_path))
        if sha256(oracle_path) != main_manifest[hash_key]:
            raise ValueError(f"{name} oracle hash mismatch")
        oracle_hashes[name] = main_manifest[hash_key]
    membership_path = root / "test/data/regions" / main_manifest["expected_membership_path"]
    with open(membership_path, encoding="utf-8") as stream:
        if sum(1 for _ in stream) - 1 != main_manifest["expected_membership_rows"]:
            raise ValueError("synthetic membership oracle row count mismatch")

    real_manifest = root / "test/data/regions" / main_manifest["real_targets"]["path"]
    if not real_manifest.is_file():
        raise MissingInput(str(real_manifest))
    manifest = json.loads(real_manifest.read_text(encoding="utf-8"))
    if sha256(real_manifest) != main_manifest["real_targets"]["sha256"]:
        raise ValueError("frozen five-target manifest hash mismatch")
    evidence.mkdir(parents=True, exist_ok=True)
    targets = manifest["targets"]
    if len(targets) != 5:
        raise ValueError("the frozen real-target manifest must contain exactly five targets")

    report = {
        "schema_version": 1,
        "status": "pass",
        "region_manifest_sha256": sha256(main_manifest_path),
        "synthetic_fixture_sha256": fixture["sha256"],
        "synthetic_oracle_sha256": oracle_hashes,
        "real_targets_manifest_sha256": sha256(real_manifest),
        "targets": [],
    }
    for target in targets:
        grid_id = target["grid_id"]
        obj = target["object"]
        object_path = root / obj["path"]
        if not object_path.is_file():
            report["status"] = "not-run"
            report["missing_input"] = obj["path"]
            break
        if object_path.stat().st_size != obj["size_bytes"] or sha256(object_path) != obj["sha256"]:
            raise ValueError(f"frozen object hash/size mismatch for {grid_id}")
        region = target["region_wkb"]
        region_path = root / "test/data/regions" / region["path"]
        if not region_path.is_file():
            raise MissingInput(str(region_path))
        if region_path.stat().st_size != region["bytes"] or sha256(region_path) != region["sha256"]:
            raise ValueError(f"frozen WKB hash/size mismatch for {grid_id}")

        coordinates = read_coordinates(target, root)
        if len(coordinates) != target["coordinate_points_in_region"]:
            raise ValueError(f"independent coordinate membership count mismatch for {grid_id}")
        positions, time_count = expected_positions(target, coordinates)
        if len(positions) != target["expected_region_rows"]:
            raise ValueError(f"independent region row count mismatch for {grid_id}")

        values_by_variable = {}
        input_hashes = {"object_sha256": obj["sha256"], "wkb_sha256": region["sha256"]}
        for variable, path in reference_paths(target, root).items():
            ref = path
            if not ref.is_file():
                raise MissingInput(str(ref))
            ref_record = next(
                (item for item in target["official_value_reference"].get("variables", [])
                 if item["variable"].lstrip("/") == variable),
                target["official_value_reference"],
            )
            if ref.stat().st_size != ref_record["size_bytes"] or sha256(ref) != ref_record["sha256"]:
                raise ValueError(f"official value reference hash/size mismatch for {grid_id}/{variable}")
            values_by_variable[variable] = selected_values(ref, positions, grid_id, variable)
            input_hashes[f"value_{variable}_sha256"] = ref_record["sha256"]
        artifact_dir = evidence / grid_id
        artifact_dir.mkdir(parents=True, exist_ok=False)
        actual_csv = artifact_dir / "region-result.csv"
        metrics_csv = artifact_dir / "scan-metrics.csv"
        sql_path = artifact_dir / "query.sql"
        log_path = artifact_dir / "command.log"
        sql_path.write_text(
            make_sql(target, actual_csv, metrics_csv, args.extension.resolve(), args.spatial.resolve()),
            encoding="utf-8",
        )
        command = [str(args.duckdb.resolve()), "-unsigned", "-c", sql_path.read_text(encoding="utf-8")]
        result = subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log_path.write_text(result.stdout, encoding="utf-8")
        target_report = {
            "grid_id": grid_id,
            "command": command,
            "exit_code": result.returncode,
            "command_log": str(log_path.relative_to(evidence)),
            "command_log_sha256": sha256(log_path),
            "sql": str(sql_path.relative_to(evidence)),
            "sql_sha256": sha256(sql_path),
            "input_hashes": input_hashes,
            "expected_coordinate_points": len(coordinates),
            "expected_rows": len(positions),
            "time_count": time_count,
        }
        if result.returncode != 0:
            target_report["status"] = "fail"
            target_report["reason"] = "DuckDB real-target query failed"
            report["status"] = "fail"
            report["targets"].append(target_report)
            break
        if not actual_csv.is_file() or not metrics_csv.is_file():
            raise ValueError(f"query did not produce result and metrics artifacts for {grid_id}")

        metrics_rows = list(csv.DictReader(metrics_csv.open(newline="", encoding="utf-8")))
        if len(metrics_rows) != 1:
            raise ValueError(f"expected one scan metrics row for {grid_id}")
        metrics = json.loads(metrics_rows[0]["metrics"])
        mismatches = check_actual(target, coordinates, positions, values_by_variable, actual_csv)
        oracle = {
            "source_indexes": sorted(positions),
            "coordinates": [[index, *coordinates[spatial]] for index, spatial in sorted(positions.items())],
            "values": {name: [[index, values[index]] for index in sorted(values)]
                       for name, values in sorted(values_by_variable.items())},
        }
        oracle_bytes = json.dumps(oracle, sort_keys=True, separators=(",", ":")).encode()
        oracle_path = artifact_dir / "independent-oracle.json"
        oracle_path.write_bytes(oracle_bytes + b"\n")
        metrics_path = artifact_dir / "scan-metrics.json"
        metrics_path.write_text(json.dumps(metrics, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        target_report.update(
            {
                "status": "pass" if not mismatches else "fail",
                "result_path": str(actual_csv.relative_to(evidence)),
                "result_sha256": sha256(actual_csv),
                "result_size_bytes": actual_csv.stat().st_size,
                "oracle_path": str(oracle_path.relative_to(evidence)),
                "oracle_sha256": sha256(oracle_path),
                "metrics_path": str(metrics_path.relative_to(evidence)),
                "metrics_sha256": sha256(metrics_path),
                "scan_metrics": metrics,
                "mismatches": mismatches,
            }
        )
        if metrics.get("operation") != "read_om_regions" or metrics.get("regions", {}).get("match_count_complete") is not True:
            target_report["status"] = "fail"
            target_report["mismatches"].append("region scan metrics do not report a complete region scan")
        if target_report["status"] == "fail":
            report["status"] = "fail"
        report["targets"].append(target_report)

    report_path = evidence / "summary.json"
    report_path.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "summary": str(report_path),
                      "summary_sha256": sha256(report_path),
                      "targets_completed": len(report["targets"])}))
    return 0 if report["status"] == "pass" else 2 if report["status"] == "not-run" else 1


def self_check():
    assert inside_bounds(0.0, 0.0, [-1.0, -1.0, 1.0, 1.0])
    assert not inside_bounds(math.nextafter(1.0, math.inf), 0.0, [-1.0, -1.0, 1.0, 1.0])
    assert same_number("nan", "NaN")
    assert not same_number("inf", "-inf")
    assert expected_positions({"object": {"shape": [2, 3, 4], "axes": ["lat", "lon", "time"]}},
                              {5: (0.0, 0.0)}) == ({20: 5, 21: 5, 22: 5, 23: 5}, 4)
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path)
    parser.add_argument("--manifest", type=pathlib.Path)
    parser.add_argument("--duckdb", type=pathlib.Path)
    parser.add_argument("--extension", type=pathlib.Path)
    parser.add_argument("--spatial", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_check:
            return self_check()
        if any(value is None for value in (args.root, args.manifest, args.duckdb, args.extension, args.spatial, args.output)):
            parser.error("--root, --manifest, --duckdb, --extension, --spatial, and --output are required")
        return run(args)
    except MissingInput as error:
        print(f"region_real_reference: missing frozen input: {error}", file=sys.stderr)
        return 2
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"region_real_reference: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
