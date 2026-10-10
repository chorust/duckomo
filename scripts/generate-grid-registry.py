#!/usr/bin/env python3
"""Generate the checked-in metadata index for frozen grid definitions.

The JSON remains the source of truth. The generated C++ records preserve the
complete definition and axis profile as canonical compact JSON; they do not
make runtime queries depend on Python or fetch upstream data.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = ROOT / "test/data/grids/definitions.json"
DEFAULT_VECTORS = ROOT / "test/data/grids/canonical-vectors.json"
DEFAULT_OUTPUT = ROOT / "src/include/duckomo/generated_grid_registry.hpp"
DEFAULT_SAMPLE_MANIFEST = ROOT / "test/data/grids/sample-manifest.json"
DEFAULT_SAMPLE_QUERIES = ROOT / "test/data/grids/sample-queries.sql"


def u64(value: int) -> bytes:
    if value < 0 or value >= 1 << 64:
        raise ValueError(f"canonical integer is outside uint64: {value}")
    return value.to_bytes(8, "big")


def sized(value: bytes) -> bytes:
    return u64(len(value)) + value


class Encoder:
    def __init__(self, format_name: str):
        self.data = bytearray()
        self.string("format", format_name)

    def field(self, name: str, kind: str, value: bytes) -> None:
        self.data += sized(name.encode()) + kind.encode() + sized(value)

    def string(self, name: str, value: str) -> None:
        self.field(name, "s", value.encode())

    def integer(self, name: str, value: int) -> None:
        self.field(name, "u", str(value).encode())

    def boolean(self, name: str, value: bool) -> None:
        self.field(name, "b", b"1" if value else b"0")

    def real(self, name: str, value: float, policy: str) -> None:
        if policy == "openmeteo_f32_v1":
            value = struct.unpack(">f", struct.pack(">f", value))[0]
        elif policy != "float64_v1":
            raise ValueError(f"unsupported numeric policy: {policy}")
        if value == 0:
            value = 0.0
        bits = struct.unpack(">Q", struct.pack(">d", value))[0]
        self.field(name, "f", f"{bits:016x}".encode())

    def string_array(self, name: str, values: list[str]) -> None:
        payload = bytearray(u64(len(values)))
        for value in values:
            payload += sized(value.encode())
        self.field(name, "a", bytes(payload))

    def integer_array(self, name: str, values: list[int]) -> None:
        payload = bytearray(u64(len(values)))
        for value in values:
            payload += sized(str(value).encode())
        self.field(name, "A", bytes(payload))

    def record_array(self, name: str, values: list[bytes]) -> None:
        payload = bytearray(u64(len(values)))
        for value in values:
            payload += sized(value)
        self.field(name, "r", bytes(payload))


def encode_row(row: list[Any], policy: str) -> bytes:
    encoder = Encoder("duckomo-grid-row-v1")
    encoder.real("latitude", row[0], policy)
    encoder.integer("point_count", row[1])
    encoder.real("longitude_origin", row[2], policy)
    encoder.real("longitude_step", row[3], policy)
    return bytes(encoder.data)


def encode_segment(segment: list[int]) -> bytes:
    encoder = Encoder("duckomo-grid-region-segment-v1")
    encoder.integer("parent_row", segment[0])
    encoder.integer("parent_begin", segment[1])
    encoder.integer("count", segment[2])
    return bytes(encoder.data)


def encode_grid(definition: dict[str, Any]) -> bytes:
    version = definition["version"]
    policy = definition["numeric_policy"]
    earth = definition["earth"]
    geometry = definition["geometry"]
    encoder = Encoder("duckomo-grid-v1")
    encoder.integer("version", version)
    encoder.string("coordinate_rule_id", definition["coordinate_rule_id"])
    encoder.string("numeric_policy", policy)
    encoder.string("earth.kind", earth["kind"])
    if earth["kind"] == "sphere":
        encoder.real("earth.radius_m", earth["radius_m"], policy)
    else:
        encoder.real("earth.semi_major_axis_m", earth["semi_major_axis_m"], policy)
        encoder.real("earth.inverse_flattening", earth["inverse_flattening"], policy)

    kind = geometry["type"]
    if kind == "regular":
        encoder.string("geometry", "regular")
        for field in ("nx", "ny"):
            encoder.integer(field, geometry[field])
        for field in ("latitude_origin", "longitude_origin", "latitude_step", "longitude_step"):
            encoder.real(field, geometry[field], policy)
        encoder.string("order", geometry["order"])
        encoder.boolean("allow_out_of_range_latitude", geometry["allow_out_of_range_latitude"])
    elif kind == "reduced_gaussian":
        encoder.string("geometry", "reduced_gaussian")
        encoder.integer("N", geometry["N"])
        encoder.string("latitude_rule", geometry["latitude_rule"])
        encoder.record_array("rows", [encode_row(row, policy) for row in geometry["rows"]])
        encoder.record_array("subset_segments", [encode_segment(row) for row in geometry["subset_segments"]])
    elif kind in {"rotated_latlon", "lambert_conformal_conic", "stereographic"}:
        encoder.string("geometry", kind)
        for field in ("nx", "ny"):
            encoder.integer(field, geometry[field])
        for field in ("x0", "y0", "dx", "dy"):
            encoder.real(field, geometry[field], policy)
        encoder.string("order", geometry["order"])
        if kind == "rotated_latlon":
            for field in ("north_pole_latitude", "north_pole_longitude", "rotation"):
                encoder.real(field, geometry[field], policy)
        elif kind == "lambert_conformal_conic":
            for field in ("longitude_of_false_origin", "latitude_of_false_origin", "standard_parallel_1",
                          "standard_parallel_2", "radius_m", "false_easting_m", "false_northing_m"):
                encoder.real(field, geometry[field], policy)
        else:
            for field in ("latitude_of_origin", "longitude_of_origin", "radius_m", "scale_factor",
                          "false_easting_m", "false_northing_m"):
                encoder.real(field, geometry[field], policy)
    else:
        raise ValueError(f"canonical vector geometry is unsupported: {kind}")
    return bytes(encoder.data)


def source_grid_definition(item: dict[str, Any], by_id: dict[str, dict[str, Any]]) -> dict[str, Any]:
    parameters = item["parameters"]
    earth = parameters["earth"]
    common = {
        "version": 1,
        "coordinate_rule_id": parameters["coordinate_rule_id"],
        "numeric_policy": parameters["numeric_policy"],
        "earth": earth,
    }
    kind = item["kind"]
    if kind == "rotated_latlon":
        geometry = {
            "type": kind, "nx": parameters["nx"], "ny": parameters["ny"],
            "x0": parameters["x0"], "y0": parameters["y0"],
            "dx": parameters["dx"], "dy": parameters["dy"], "order": "separate",
            "north_pole_latitude": parameters["north_pole_latitude"],
            "north_pole_longitude": parameters["north_pole_longitude"], "rotation": parameters["rotation"],
        }
    elif kind == "stereographic":
        native = parameters["native_grid"]
        projection = parameters["projection"]
        geometry = {
            "type": kind, "nx": parameters["nx"], "ny": parameters["ny"],
            "x0": native["x0"], "y0": native["y0"], "dx": native["dx"], "dy": native["dy"],
            "order": "separate", "latitude_of_origin": projection["latitude_of_origin"],
            "longitude_of_origin": projection["longitude_of_origin"], "radius_m": projection["radius_m"],
            "scale_factor": projection["scale_factor"], "false_easting_m": projection["false_easting_m"],
            "false_northing_m": projection["false_northing_m"],
        }
    elif kind == "lambert_conformal_conic":
        geometry = {
            "type": kind, "nx": parameters["nx"], "ny": parameters["ny"],
            "x0": parameters["x0"], "y0": parameters["y0"],
            "dx": parameters["dx"], "dy": parameters["dy"], "order": parameters["order"],
            "longitude_of_false_origin": parameters["longitude_of_false_origin"],
            "latitude_of_false_origin": parameters["latitude_of_false_origin"],
            "standard_parallel_1": parameters["standard_parallel_1"],
            "standard_parallel_2": parameters["standard_parallel_2"],
            "radius_m": earth["radius_m"], "false_easting_m": parameters["false_easting_m"],
            "false_northing_m": parameters["false_northing_m"],
        }
    elif kind in {"reduced_gaussian", "reduced_gaussian_region"}:
        source_rows = parameters["rows"] if kind == "reduced_gaussian" else \
            by_id[parameters["parent_definition"]]["parameters"]["rows"]
        rows = [[row["latitude"], row["point_count"], row["longitude_origin"], row["longitude_step"]]
                for row in source_rows]
        segments = parameters["segments"] if kind == "reduced_gaussian_region" else []
        geometry = {
            "type": "reduced_gaussian", "N": parameters["N"],
            "latitude_rule": parameters["latitude_rule"], "rows": rows,
            "subset_segments": [[segment["parent_row"], segment["parent_begin"], segment["count"]]
                                for segment in segments],
        }
    else:
        raise ValueError(f"unsupported source grid kind: {kind}")
    return {**common, "geometry": geometry}


def source_grid_identities(definitions: list[dict[str, Any]]) -> dict[str, tuple[str, str]]:
    by_id = {item["id"]: item for item in definitions}
    result: dict[str, tuple[str, str]] = {}
    for item in definitions:
        canonical = source_grid_definition(item, by_id)
        grid_id = sha256_hex(encode_grid(canonical))
        parent_id = ""
        parent_name = item.get("parameters", {}).get("parent_definition", "")
        if parent_name:
            parent_id = result[parent_name][0]
        result[item["id"]] = (grid_id, parent_id)
    return result


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def cpp_real(value: Any) -> str:
    # Python's repr round-trips binary64 values and keeps generated literals
    # independent of locale and host formatting settings.
    return repr(float(value))


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def numeric_policy_cpp(policy: str) -> str:
    choices = {
        "float64_v1": "GridNumericPolicy::Float64V1",
        "openmeteo_f32_v1": "GridNumericPolicy::OpenMeteoF32V1",
    }
    try:
        return choices[policy]
    except KeyError as error:
        raise ValueError(f"unsupported numeric policy: {policy}") from error


def earth_cpp(earth: dict[str, Any]) -> str:
    kind = earth["kind"]
    if kind == "sphere":
        earth_kind = "GridEarthKind::Sphere"
        radius = cpp_real(earth["radius_m"])
        semi_major = "6378137.0"
        inverse_flattening = "298.257223563"
    elif kind == "wgs84_source":
        earth_kind = "GridEarthKind::Wgs84Source"
        radius = "6371229.0"
        semi_major = cpp_real(earth["semi_major_axis_m"])
        inverse_flattening = cpp_real(earth["inverse_flattening"])
    else:
        raise ValueError(f"unsupported source earth model: {kind}")
    return f"GridEarth{{{earth_kind}, {radius}, {semi_major}, {inverse_flattening}}}"


def row_vector_cpp(rows: list[dict[str, Any]]) -> str:
    values = ",\n".join(
        "        GaussianRow{" + ", ".join(
            [cpp_real(row["latitude"]), str(row["point_count"]), cpp_real(row["longitude_origin"]),
             cpp_real(row["longitude_step"])]
        ) + "}"
        for row in rows
    )
    return "std::vector<GaussianRow>{\n" + values + "\n    }"


def segment_vector_cpp(segments: list[dict[str, Any]]) -> str:
    values = ",\n".join(
        "        GaussianRegionSegment{" + ", ".join(
            [str(segment["parent_row"]), str(segment["parent_begin"]), str(segment["count"])]
        ) + "}"
        for segment in segments
    )
    return "std::vector<GaussianRegionSegment>{\n" + values + "\n    }"


def definition_expression(item: dict[str, Any], by_id: dict[str, dict[str, Any]]) -> str:
    parameters = item["parameters"]
    policy = numeric_policy_cpp(parameters["numeric_policy"])
    earth = earth_cpp(parameters["earth"])
    coordinate_rule = cpp_string(parameters["coordinate_rule_id"])
    kind = item["kind"]
    if kind == "rotated_latlon":
        geometry = (
            "ProjectedGrid(RotatedLatLonParameters{" + ", ".join(
                [str(parameters["nx"]), str(parameters["ny"]), cpp_real(parameters["x0"]),
                 cpp_real(parameters["y0"]), cpp_real(parameters["dx"]), cpp_real(parameters["dy"]),
                 cpp_real(parameters["north_pole_latitude"]), cpp_real(parameters["north_pole_longitude"]),
                 cpp_real(parameters["rotation"]), "GridStorageOrder::Separate"]
            ) + "})"
        )
    elif kind == "stereographic":
        native = parameters["native_grid"]
        projection = parameters["projection"]
        geometry = (
            "ProjectedGrid(StereographicParameters{" + ", ".join(
                [str(parameters["nx"]), str(parameters["ny"]), cpp_real(native["x0"]), cpp_real(native["y0"]),
                 cpp_real(native["dx"]), cpp_real(native["dy"]), cpp_real(projection["latitude_of_origin"]),
                 cpp_real(projection["longitude_of_origin"]), cpp_real(projection["radius_m"]),
                 cpp_real(projection["scale_factor"]), cpp_real(projection["false_easting_m"]),
                 cpp_real(projection["false_northing_m"]), "GridStorageOrder::Separate"]
            ) + "})"
        )
    elif kind == "lambert_conformal_conic":
        geometry = (
            "ProjectedGrid(LambertParameters{" + ", ".join(
                [str(parameters["nx"]), str(parameters["ny"]), cpp_real(parameters["x0"]),
                 cpp_real(parameters["y0"]), cpp_real(parameters["dx"]), cpp_real(parameters["dy"]),
                 cpp_real(parameters["longitude_of_false_origin"]), cpp_real(parameters["latitude_of_false_origin"]),
                 cpp_real(parameters["standard_parallel_1"]), cpp_real(parameters["standard_parallel_2"]),
                 cpp_real(parameters["earth"]["radius_m"]), cpp_real(parameters["false_easting_m"]),
                 cpp_real(parameters["false_northing_m"]), "GridStorageOrder::Separate"]
            ) + "})"
        )
    elif kind == "reduced_gaussian":
        geometry = (
            f"GaussianGrid({parameters['N']}, {cpp_string(parameters['latitude_rule'])}, "
            f"{row_vector_cpp(parameters['rows'])})"
        )
    elif kind == "reduced_gaussian_region":
        parent_id = parameters["parent_definition"]
        parent = by_id.get(parent_id)
        if parent is None or parent["kind"] != "reduced_gaussian":
            raise ValueError(f"Gaussian region {item['id']} references unknown full grid {parent_id}")
        # The build function creates full definitions before their regions.
        geometry = (
            f"GaussianGrid({parameters['N']}, {cpp_string(parameters['latitude_rule'])}, "
            f"std::get<GaussianGrid>(definitions.at({cpp_string(parent_id)}).geometry).Rows(), "
            f"{segment_vector_cpp(parameters['segments'])})"
        )
    else:
        raise ValueError(f"unsupported grid definition kind: {kind}")
    return f"GridDefinition({coordinate_rule}, {earth}, {policy}, {geometry})"


def profile_cpp(profile: dict[str, Any]) -> tuple[str, str, int, str]:
    layout = profile.get("layout", "")
    axes = profile.get("axis_order", [])
    if len(axes) > 2:
        raise ValueError("grid registry profiles support at most two ordered spatial axes")
    axis_slots = [cpp_string(axis) for axis in axes]
    axis_slots.extend(['""'] * (2 - len(axis_slots)))
    return (cpp_string(layout), "{" + ", ".join(axis_slots) + "}", len(axes),
            cpp_string(profile.get("object_profile_status", "")))


def canonical_vectors(vectors_path: Path) -> None:
    vectors = json.loads(vectors_path.read_text())
    for vector in vectors["vectors"]:
        if vector["kind"] == "grid":
            actual = sha256_hex(encode_grid(vector["definition"]))
            if actual != vector["grid_id"]:
                raise ValueError(f"canonical vector {vector['id']} grid_id differs: {actual}")
            if "parent_grid_id" in vector:
                parent = json.loads(json.dumps(vector["definition"]))
                parent["geometry"]["subset_segments"] = []
                actual_parent = sha256_hex(encode_grid(parent))
                if actual_parent != vector["parent_grid_id"]:
                    raise ValueError(f"canonical vector {vector['id']} parent_grid_id differs: {actual_parent}")
        elif vector["kind"] == "layout":
            actual_grid_id = sha256_hex(encode_grid(vector["definition"]))
            if actual_grid_id != vector["grid_id"]:
                raise ValueError(f"canonical vector {vector['id']} grid_id differs: {actual_grid_id}")
            encoder = Encoder("duckomo-layout-v1")
            encoder.string("grid_id", vector["grid_id"])
            encoder.string("geometry", vector["geometry"])
            encoder.integer_array("shape", vector["shape"])
            encoder.string_array("axes", vector["axes"])
            encoder.integer_array("strides", vector["strides"])
            encoder.integer_array("non_spatial_axes", vector["non_spatial_axes"])
            encoder.boolean("flattened", vector["flattened"])
            encoder.string("order", vector["order"])
            encoder.integer("latitude_axis", vector["latitude_axis"])
            encoder.integer("longitude_axis", vector["longitude_axis"])
            encoder.integer("point_axis", vector["point_axis"])
            actual = sha256_hex(bytes(encoder.data))
            if actual != vector["layout_id"]:
                raise ValueError(f"canonical vector {vector['id']} layout_id differs: {actual}")
        else:
            raise ValueError(f"unknown canonical vector kind: {vector['kind']}")


def generate_header(input_path: Path) -> str:
    manifest = json.loads(input_path.read_text())
    definitions = manifest["definitions"]
    ids = [entry["id"] for entry in definitions]
    if len(ids) != len(set(ids)):
        raise ValueError("grid definition IDs must be unique")
    by_id = {entry["id"]: entry for entry in definitions}
    identities = source_grid_identities(definitions)
    result_records = []
    definition_lines = []
    for item in definitions:
        definition_json = json.dumps(item, ensure_ascii=True, separators=(",", ":"), sort_keys=True)
        profile = item.get("expected_spatial_axis_profile", {})
        axes_json = json.dumps(profile, ensure_ascii=True, separators=(",", ":"), sort_keys=True)
        layout, axis_order, axis_count, profile_status = profile_cpp(profile)
        evidence = item.get("evidence", {})
        parent_definition = item.get("parameters", {}).get("parent_definition", "")
        name = item["id"]
        definition_lines.append(
            f"\tdefinitions.emplace({cpp_string(name)}, {definition_expression(item, by_id)});"
        )
        result_records.append(
            "\tresult.push_back(GridRegistryDefinitionRecord{" + ", ".join([
                cpp_string(name), cpp_string(item["kind"]), cpp_string(item.get("source_path", "")),
                cpp_string(definition_json), cpp_string(axes_json), cpp_string(identities[name][0]),
                cpp_string(identities[name][1]), layout, axis_order, str(axis_count),
                "false" if "identity vector only" in item.get("parameters", {}).get("status", "") else "true",
                profile_status,
                cpp_string(evidence.get("level", "definition-recorded")),
                cpp_string(evidence.get("sample_id") or ""),
                cpp_string(evidence.get("source_uri") or ""),
                cpp_string(evidence.get("build_pair") or ""),
                cpp_string(evidence.get("claims", "")),
                cpp_string(parent_definition), f"definitions.at({cpp_string(name)})",
            ]) + "});"
        )
    upstream_commit = manifest["upstream"]["commit"]
    header = """// Generated by scripts/generate-grid-registry.py; do not edit.
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

#include "duckomo/grid_definition.hpp"

namespace duckdb {
namespace duckomo {
namespace generated {

struct GridRegistryDefinitionRecord final {
\tstd::string_view id;
\tstd::string_view kind;
\tstd::string_view source_path;
\tstd::string_view definition_json;
\tstd::string_view axis_profile_json;
\tstd::string_view grid_id;
\tstd::string_view parent_grid_id;
\tstd::string_view expected_layout;
\tstd::array<std::string_view, 2> axis_order;
\tstd::size_t axis_count;
\tbool domain_bindable;
\tstd::string_view object_profile_status;
\tstd::string_view evidence_level;
\tstd::string_view evidence_sample_id;
\tstd::string_view evidence_source_uri;
\tstd::string_view evidence_build_pair;
\tstd::string_view evidence_claims;
\tstd::string_view parent_definition;
\tGridDefinition definition;
};

inline constexpr std::string_view GRID_REGISTRY_UPSTREAM_COMMIT = __UPSTREAM_COMMIT__;

inline std::vector<GridRegistryDefinitionRecord> BuildGridRegistryDefinitions() {
\tstd::unordered_map<std::string, GridDefinition> definitions;
\tdefinitions.reserve(__DEFINITION_COUNT__);
__DEFINITION_LINES__
\tstd::vector<GridRegistryDefinitionRecord> result;
\tresult.reserve(__RECORD_COUNT__);
__RECORD_LINES__
\treturn result;
}

} // namespace generated
} // namespace duckomo
} // namespace duckdb
"""
    return (header.replace("__UPSTREAM_COMMIT__", cpp_string(upstream_commit))
            .replace("__DEFINITION_COUNT__", str(len(definitions)))
            .replace("__RECORD_COUNT__", str(len(result_records)))
            .replace("__DEFINITION_LINES__", "\n".join(definition_lines))
            .replace("__RECORD_LINES__", "\n".join(result_records)))


def sql_quote(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def sql_literal(value: Any, indent: int = 0) -> str:
    """Render JSON data as a DuckDB STRUCT/LIST literal without losing field names."""
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, str):
        return sql_quote(value)
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if not (float("-inf") < value < float("inf")):
            raise ValueError("sample-query grid literals must contain only finite numbers")
        return repr(value)
    if isinstance(value, list):
        if not value:
            return "[]"
        inner_indent = indent + 4
        values = [" " * inner_indent + sql_literal(item, inner_indent) for item in value]
        return "[\n" + ",\n".join(values) + "\n" + " " * indent + "]"
    if isinstance(value, dict):
        if not value:
            return "{}"
        inner_indent = indent + 4
        fields = [
            " " * inner_indent + sql_quote(str(name)) + ": " + sql_literal(item, inner_indent)
            for name, item in value.items()
        ]
        return "{\n" + ",\n".join(fields) + "\n" + " " * indent + "}"
    raise ValueError(f"unsupported SQL literal type: {type(value).__name__}")


def sql_grid_definition(item: dict[str, Any], by_id: dict[str, dict[str, Any]]) -> dict[str, Any]:
    """Convert one frozen producer entry to the public version=1 SQL contract."""
    source = item["parameters"]
    kind = item["kind"]
    policy = source["numeric_policy"]
    earth_source = source["earth"]
    if earth_source["kind"] == "sphere":
        earth = {"model": "sphere", "radius_m": earth_source["radius_m"]}
    elif earth_source["kind"] == "wgs84_source":
        earth = {
            "model": "wgs84",
            "semi_major_m": earth_source["semi_major_axis_m"],
            "inverse_flattening": earth_source["inverse_flattening"],
        }
    else:
        raise ValueError(f"unsupported earth model in sample query: {earth_source['kind']}")

    if kind == "rotated_latlon":
        layout = {"nx": source["nx"], "ny": source["ny"], "order": "separate"}
        parameters = {
            "x0": source["x0"], "y0": source["y0"], "dx": source["dx"], "dy": source["dy"],
            "north_pole_latitude": source["north_pole_latitude"],
            "north_pole_longitude": source["north_pole_longitude"], "rotation": source["rotation"],
        }
    elif kind == "lambert_conformal_conic":
        layout = {"nx": source["nx"], "ny": source["ny"], "order": "separate"}
        parameters = {
            "x0": source["x0"], "y0": source["y0"], "dx": source["dx"], "dy": source["dy"],
            "central_meridian": source["longitude_of_false_origin"],
            "latitude_of_origin": source["latitude_of_false_origin"],
            "standard_parallel_1": source["standard_parallel_1"],
            "standard_parallel_2": source["standard_parallel_2"],
        }
    elif kind == "stereographic":
        native = source["native_grid"]
        projection = source["projection"]
        layout = {"nx": source["nx"], "ny": source["ny"], "order": "separate"}
        parameters = {
            "x0": native["x0"], "y0": native["y0"], "dx": native["dx"], "dy": native["dy"],
            "central_meridian": projection["longitude_of_origin"],
            "latitude_of_origin": projection["latitude_of_origin"],
            "scale_factor": projection["scale_factor"],
        }
        earth = {"model": "sphere", "radius_m": projection["radius_m"]}
    elif kind in {"reduced_gaussian", "reduced_gaussian_region"}:
        if kind == "reduced_gaussian_region":
            parent_id = source["parent_definition"]
            parent = by_id.get(parent_id)
            if parent is None or parent["kind"] != "reduced_gaussian":
                raise ValueError(f"Gaussian region {item['id']} has no full-grid definition")
            rows_source = parent["parameters"]["rows"]
            segments_source = source["segments"]
        else:
            rows_source = source["rows"]
            segments_source = []
        rows = [
            {
                "latitude": row["latitude"], "point_count": row["point_count"],
                "longitude_origin": row["longitude_origin"], "longitude_step": row["longitude_step"],
            }
            for row in rows_source
        ]
        parameters = {
            "n": source["N"], "latitude_rule": source["latitude_rule"], "rows": rows,
            "subset_segments": [
                {"parent_row": segment["parent_row"], "parent_begin": segment["parent_begin"],
                 "count": segment["count"]}
                for segment in segments_source
            ] if segments_source else None,
        }
        layout = {"order": "row_major"}
    else:
        raise ValueError(f"unsupported sample-query grid kind: {kind}")

    return {
        "version": 1,
        "type": "reduced_gaussian" if kind == "reduced_gaussian_region" else kind,
        "numeric_policy": policy,
        "earth": earth,
        "layout": layout,
        "parameters": parameters,
    }


def safe_identifier(value: str) -> str:
    result = "".join(character.lower() if character.isalnum() else "_" for character in value)
    while "__" in result:
        result = result.replace("__", "_")
    result = result.strip("_")
    if not result or result[0].isdigit():
        result = "grid_" + result
    return result


def dimensions_sql(variable_path: str, axes: list[str]) -> str:
    return f"map([{sql_quote(variable_path)}], [{sql_literal(axes)}])"


def query_view_sql(view: str, path: str, variable_path: str, axes: list[str], grid: dict[str, Any] | None,
                   domain: str | None) -> str:
    arguments = [f"{sql_quote(path)}", f"dimensions := {dimensions_sql(variable_path, axes)}"]
    if grid is not None:
        arguments.extend([f"grid := {sql_literal(grid, 8)}", "spatial_axes := ['y', 'x']" if len(axes) > 1 else
                          f"spatial_axes := [{sql_quote(axes[0])}]"])
    elif domain is not None:
        arguments.append(f"domain := {sql_quote(domain)}")
    arguments.append("include_source := true")
    return "CREATE TEMP VIEW " + view + " AS\nSELECT * FROM read_om(\n    " + ",\n    ".join(arguments) + "\n);\n"


def generate_sample_queries(sample_manifest_path: Path, definitions_path: Path) -> str:
    sample_manifest = json.loads(sample_manifest_path.read_text())
    registry = json.loads(definitions_path.read_text())
    definitions = registry["definitions"]
    by_id = {item["id"]: item for item in definitions}
    objects = sample_manifest.get("public_open_meteo_samples", {}).get("objects", [])
    object_by_definition = {
        item.get("grid_definition_id"): item
        for item in objects if item.get("grid_definition_id")
    }
    synthetic_n160 = next((item for item in sample_manifest.get("fixtures", [])
                           if item.get("id") == "gaussian_n160_identity"), None)
    sample_bindings: list[tuple[dict[str, Any], str, str, list[str], str]] = []
    for definition_id, sample in object_by_definition.items():
        if definition_id not in by_id:
            raise ValueError(f"sample {sample['id']} references unknown grid definition {definition_id}")
        array = sample.get("array", {})
        axes = array.get("axes")
        local_path = sample.get("local_copy", {}).get("path")
        if not axes or not local_path:
            continue
        # The frozen Open-Meteo README order is [y, x, time]; object axis labels
        # describe values but registry profiles use stable y/x roles.
        if len(axes) != 3 or axes[2] != "time":
            raise ValueError(f"sample {sample['id']} has an unsupported axis profile: {axes}")
        sample_bindings.append((by_id[definition_id], local_path, "value", ["y", "x", "time"], sample["id"]))
    if synthetic_n160 is not None:
        sample_bindings.append((by_id["n160"], "test/data/grids/" + synthetic_n160["path"],
                                "n160_identity/value", ["point"],
                                synthetic_n160["id"] + " (synthetic identity only)"))

    lines = [
        "-- Generated by scripts/generate-grid-registry.py; do not edit.",
        "-- Inputs are pinned by test/data/grids/sample-manifest.json and definitions.json.",
        f"-- sample-manifest sha256: {sha256_hex(sample_manifest_path.read_bytes())}",
        f"-- definitions sha256: {sha256_hex(definitions_path.read_bytes())}",
        "-- This file contains executable samples only where the manifest has a compatible object/fixture.",
        "-- Synthetic fixtures are labelled and cannot establish producer coordinate/value support.",
        "-- The O1280 ECMWF HRES objects are real OM v3, but have no verified N-grid identity or point order.",
        "SET threads = 1;",
        "",
    ]
    emitted = {item[0]["id"] for item in sample_bindings}
    for definition, path, variable_path, axes, sample_id in sample_bindings:
        definition_id = definition["id"]
        name = safe_identifier(definition_id)
        lines.extend([
            f"-- {definition_id}: {sample_id}; explicit and domain forms use the same frozen input.",
            query_view_sql(f"explicit_{name}", path, variable_path, axes,
                           sql_grid_definition(definition, by_id), None).rstrip(),
        ])
        if "identity vector only" not in definition.get("parameters", {}).get("status", ""):
            lines.append(query_view_sql(f"domain_{name}", path, variable_path, axes, None, definition_id).rstrip())
        lines.extend([
            f"CREATE TEMP TABLE baseline_{name} AS SELECT * FROM explicit_{name};",
            f"-- Full input was materialized before any restricted query for {definition_id}.",
            f"SELECT count(*) AS full_rows_{name} FROM explicit_{name};",
            f"SELECT count(*) AS materialized_rows_{name} FROM baseline_{name};",
        ])
        if "identity vector only" not in definition.get("parameters", {}).get("status", ""):
            lines.extend([
                f"SELECT count(*) AS domain_identity_difference_{name} FROM (",
                f"    (SELECT om_source.grid_id, om_source.layout_id FROM explicit_{name}",
                f"     EXCEPT ALL SELECT om_source.grid_id, om_source.layout_id FROM domain_{name})",
                "    UNION ALL",
                f"    (SELECT om_source.grid_id, om_source.layout_id FROM domain_{name}",
                f"     EXCEPT ALL SELECT om_source.grid_id, om_source.layout_id FROM explicit_{name})",
                ");",
            ])
        lines.extend([
            f"SELECT count(*) AS restricted_difference_{name} FROM (",
            f"    (SELECT * FROM baseline_{name} WHERE lat BETWEEN -10 AND 10 AND lon BETWEEN -10 AND 10",
            f"     EXCEPT ALL SELECT * FROM explicit_{name} WHERE lat BETWEEN -10 AND 10 AND lon BETWEEN -10 AND 10)",
            "    UNION ALL",
            f"    (SELECT * FROM explicit_{name} WHERE lat BETWEEN -10 AND 10 AND lon BETWEEN -10 AND 10",
            f"     EXCEPT ALL SELECT * FROM baseline_{name} WHERE lat BETWEEN -10 AND 10 AND lon BETWEEN -10 AND 10)",
            ");",
            f"SELECT count(*) AS seam_rows_{name} FROM explicit_{name} WHERE ",
            "    (lon BETWEEN 170 AND 180 OR lon BETWEEN -180 AND -170);",
            f"SELECT count(*) AS empty_rows_{name} FROM explicit_{name} WHERE lat > 90;",
            f"SELECT count(*) AS fallback_rows_{name} FROM explicit_{name} WHERE abs(lat) < 1;",
            f"SELECT om_source.logical_index, lon, lat,",
            "       (lon BETWEEN -1 AND 1 AND lat BETWEEN -1 AND 1) AS polygon_covers_point",
            f"FROM explicit_{name} -- POLYGON((-1 -1, 1 -1, 1 1, -1 1, -1 -1)), lon/lat order",
            "WHERE lon BETWEEN -1 AND 1 AND lat BETWEEN -1 AND 1 LIMIT 64;",
            "",
        ])

    required_ids = ["gem_rdps_10km", "gem_regional", "aladin_central_europe_2km", "n160", "n320",
                    "n320_ecmwf_aifs_europe_ensemble"]
    missing_ids = [identifier for identifier in required_ids if identifier not in emitted]
    if missing_ids:
        lines.extend([
            "-- Pending sample-backed explicit/domain views; no compatible frozen OM v3 input is recorded:",
            *[f"-- NOT RUN {identifier}: keep absent until a matching object and ordered-axis evidence are frozen."
              for identifier in missing_ids],
            "-- In particular, the real ECMWF HRES O1280 sample is not relabelled as N160 or N320.",
            "",
        ])

    hsurf = next((sample for sample in objects if sample["id"] == "ecmwf_ifs_static_hsurf_reduced_gaussian_o1280"),
                 None)
    if hsurf is not None:
        path = hsurf.get("local_copy", {}).get("path")
        if path:
            lines.extend([
                "-- Real ECMWF HRES/IFS OM v3 O1280 value-only supplement; no geographic mapping is asserted.",
                f"CREATE TEMP VIEW values_ecmwf_hres_o1280 AS SELECT * FROM read_om({sql_quote(path)});",
                "CREATE TEMP TABLE baseline_ecmwf_hres_o1280 AS",
                "    SELECT * FROM values_ecmwf_hres_o1280; -- materialize all 6,599,680 values first",
                "SELECT count(*) AS hres_o1280_value_count FROM values_ecmwf_hres_o1280;",
                "",
            ])
    lines.append("-- Spatial relation result is a smoke query only until the matching producer oracle is available.")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--vectors", type=Path, default=DEFAULT_VECTORS)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--sample-manifest", type=Path, default=DEFAULT_SAMPLE_MANIFEST)
    parser.add_argument("--sample-queries-output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    expected = generate_header(args.input)
    canonical_vectors(args.vectors)
    sample_queries = None
    if args.sample_queries_output is not None:
        sample_queries = generate_sample_queries(args.sample_manifest, args.input)
    if args.check:
        with tempfile.TemporaryDirectory(prefix="duckomo-grid-registry-") as temporary:
            first = Path(temporary) / "first.hpp"
            second = Path(temporary) / "second.hpp"
            first.write_text(expected)
            second.write_text(generate_header(args.input))
            if first.read_bytes() != second.read_bytes():
                raise ValueError("two generated grid registry headers differ")
        if not args.output.exists() or args.output.read_text() != expected:
            raise ValueError(f"generated grid registry is stale: {args.output}")
        if sample_queries is not None:
            if not args.sample_queries_output:
                raise ValueError("--check also requires --sample-queries-output to check generated SQL")
            if not args.sample_queries_output.exists() or args.sample_queries_output.read_text() != sample_queries:
                raise ValueError(f"generated sample queries are stale: {args.sample_queries_output}")
        print(f"grid registry and canonical vectors verified: {args.output}")
        if sample_queries is not None:
            print(f"sample queries verified: {args.sample_queries_output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(expected)
    print(f"generated {args.output}")
    if sample_queries is not None:
        args.sample_queries_output.parent.mkdir(parents=True, exist_ok=True)
        args.sample_queries_output.write_text(sample_queries)
        print(f"generated {args.sample_queries_output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise SystemExit(f"grid registry generation failed: {error}")
