#!/usr/bin/env python3
"""Generate the frozen C++ temporal model registry from models.json."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = ROOT / "test/data/series/models.json"
DEFAULT_OUTPUT = ROOT / "src/include/duckomo/generated_series_registry.hpp"
SOURCE_MANIFEST = ROOT / "test/data/grids/source-manifest.json"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
EXPECTED_VALUES = {
    "ecmwf_ifs": (504, 3600),
    "ecmwf_ifs025": (104, 10800),
    "ncep_gfs013": (481, 3600),
    "ncep_gfs025": (481, 3600),
    "dwd_icon": (253, 3600),
    "dwd_icon_eu": (193, 3600),
    "dwd_icon_d2": (121, 3600),
    "jma_gsm": (110, 21600),
    "cmc_gem_gdps": (110, 10800),
    "meteofrance_arpege_world025": (210, 3600),
    "ukmo_global_deterministic_10km": (193, 3600),
}
EXPECTED_KEYS = list(EXPECTED_VALUES)
MODEL_FIELDS = {
    "key",
    "data_key",
    "metadata_object_key",
    "producer_domain",
    "producer_source",
    "metadata_snapshot",
    "provenance_status",
    "discrepancies",
    "notes",
}
PRODUCER_FIELDS = {
    "path",
    "sha256",
    "chunk_time_length",
    "temporal_resolution_seconds",
}
TEMPORAL_FORMULA_FIELDS = {"source_symbol", "source_expression", "value"}
SNAPSHOT_FIELDS = {"path", "sha256", "audit_entry_sha256_matches"}


def require_object(value: Any, description: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{description} must be an object")
    return value


def require_fields(value: dict[str, Any], expected: set[str], description: str) -> None:
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        unsupported = sorted(actual - expected)
        raise ValueError(f"{description} fields differ (missing={missing}, unsupported={unsupported})")


def require_sha256(value: Any, description: str) -> str:
    if not isinstance(value, str) or not SHA256_RE.fullmatch(value):
        raise ValueError(f"{description} must be a lowercase SHA-256 hex digest")
    return value


def repo_file(relative_path: Any, description: str) -> Path:
    if not isinstance(relative_path, str) or not relative_path or Path(relative_path).is_absolute():
        raise ValueError(f"{description} must be a repository-relative path")
    candidate = (ROOT / relative_path).resolve()
    if not candidate.is_relative_to(ROOT):
        raise ValueError(f"{description} escapes the repository root")
    return candidate


def load_source_hashes() -> dict[str, str]:
    manifest = json.loads(SOURCE_MANIFEST.read_text(encoding="utf-8"))
    result: dict[str, str] = {}
    for item in manifest.get("source_files", []):
        result[item["path"]] = item["sha256"]
    for item in manifest.get("legacy_reference", {}).get("source_files", []):
        result[item["path"]] = item["sha256"]
    return result


def validate_registry(input_path: Path) -> tuple[dict[str, Any], list[tuple[str, int, int]]]:
    try:
        data = json.loads(input_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ValueError(f"invalid JSON: {error}") from error
    data = require_object(data, "registry")
    if data.get("schema_version") != 1 or data.get("feature") != "010-read-om-series":
        raise ValueError("unsupported temporal registry schema or feature")

    upstream = require_object(data.get("upstream"), "upstream")
    if upstream.get("repository") != "https://github.com/open-meteo/open-meteo":
        raise ValueError("unsupported upstream repository")
    source_manifest = json.loads(SOURCE_MANIFEST.read_text(encoding="utf-8"))
    commit = upstream.get("commit")
    if commit != source_manifest.get("pinned_commit"):
        raise ValueError("temporal registry commit differs from the frozen producer-source pin")
    pin_reference = require_object(upstream.get("pin_reference"), "upstream pin_reference")
    if pin_reference.get("path") != "test/data/grids/source-manifest.json" or \
            pin_reference.get("field") != "pinned_commit" or pin_reference.get("expected_commit") != commit:
        raise ValueError("recorded upstream pin reference does not match the registry commit")

    for description in ("key_mapping_source", "metadata_writer_source"):
        source = require_object(upstream.get(description), f"upstream {description}")
        source_path = source.get("path")
        if not isinstance(source_path, str) or not source_path.startswith("Sources/App/"):
            raise ValueError(f"upstream {description} has no producer source path")
        require_sha256(source.get("sha256"), f"upstream {description} sha256")
    writer = upstream["metadata_writer_source"]
    if writer.get("metadata_object_key_template") != "data/${domain.rawValue}/static/meta.json" or \
            writer.get("metadata_file_domain_input") != "domain.domainRegistry":
        raise ValueError("unsupported metadata writer key mapping")
    if writer.get("temporal_field_mapping") != {
        "chunk_time_length": "domain.omFileLength",
        "temporal_resolution_seconds": "domain.dtSeconds",
    }:
        raise ValueError("unsupported metadata writer temporal field mapping")

    archive = require_object(data.get("archived_metadata_source"), "archived_metadata_source")
    audit_path = repo_file(archive.get("audit_path"), "metadata audit path")
    audit_bytes = audit_path.read_bytes()
    audit_sha = require_sha256(archive.get("audit_sha256"), "metadata audit sha256")
    if hashlib.sha256(audit_bytes).hexdigest() != audit_sha:
        raise ValueError("archived metadata audit SHA-256 mismatch")
    audit = json.loads(audit_bytes)
    audit_by_key = {item.get("key"): item for item in audit.get("objects", []) if item.get("key")}

    models = data.get("models")
    if not isinstance(models, list):
        raise ValueError("models must be an array")
    keys: list[str] = []
    for item in models:
        model = require_object(item, "model entry")
        key = model.get("key")
        if not isinstance(key, str):
            raise ValueError("every model entry requires a string key")
        keys.append(key)
    if len(keys) != len(set(keys)):
        raise ValueError("duplicate temporal model key")
    if keys != EXPECTED_KEYS:
        unsupported = sorted(set(keys) - set(EXPECTED_KEYS))
        missing = sorted(set(EXPECTED_KEYS) - set(keys))
        raise ValueError(f"temporal model keys differ from frozen R3 (missing={missing}, unsupported={unsupported})")

    pinned_source_hashes = load_source_hashes()
    rows: list[tuple[str, int, int]] = []
    for model in models:
        key = model["key"]
        require_fields(model, MODEL_FIELDS, f"model {key}")
        if model["data_key"] != key:
            raise ValueError(f"{key}: data_key must equal the exact model key")
        metadata_key = f"data/{key}/static/meta.json"
        if model["metadata_object_key"] != metadata_key:
            raise ValueError(f"{key}: metadata object key must be {metadata_key}")
        if model["provenance_status"] != "pinned_source_and_archived_metadata_match":
            raise ValueError(f"{key}: producer/snapshot provenance is not verified")
        if model["discrepancies"] != []:
            raise ValueError(f"{key}: registry cannot be generated with unresolved temporal discrepancies")
        if not isinstance(model["notes"], list) or not all(isinstance(note, str) for note in model["notes"]):
            raise ValueError(f"{key}: notes must be an array of strings")
        producer = require_object(model["producer_source"], f"{key} producer_source")
        require_fields(producer, PRODUCER_FIELDS, f"{key} producer_source")
        source_path = producer["path"]
        if not isinstance(source_path, str) or not source_path.startswith("Sources/App/"):
            raise ValueError(f"{key}: producer source path must be repository-relative Sources/App path")
        source_sha = require_sha256(producer["sha256"], f"{key} producer source sha256")
        if source_path in pinned_source_hashes and source_sha != pinned_source_hashes[source_path]:
            raise ValueError(f"{key}: producer source hash differs from the frozen source manifest")

        values: list[int] = []
        for field, expected_value in zip(("chunk_time_length", "temporal_resolution_seconds"), EXPECTED_VALUES[key]):
            formula = require_object(producer.get(field), f"{key} {field}")
            require_fields(formula, TEMPORAL_FORMULA_FIELDS, f"{key} {field}")
            value = formula["value"]
            if type(value) is not int or value <= 0:
                raise ValueError(f"{key}: {field} must be a positive signed integer")
            if value != expected_value:
                raise ValueError(f"{key}: {field} differs from the frozen R3 value {expected_value}")
            if not isinstance(formula["source_symbol"], str) or not formula["source_symbol"] or \
                    not isinstance(formula["source_expression"], str) or not formula["source_expression"]:
                raise ValueError(f"{key}: {field} requires its pinned source formula and symbol")
            values.append(value)

        snapshot = require_object(model["metadata_snapshot"], f"{key} metadata_snapshot")
        require_fields(snapshot, SNAPSHOT_FIELDS, f"{key} metadata_snapshot")
        snapshot_path = repo_file(snapshot["path"], f"{key} metadata snapshot path")
        snapshot_bytes = snapshot_path.read_bytes()
        snapshot_sha = require_sha256(snapshot["sha256"], f"{key} metadata snapshot sha256")
        if hashlib.sha256(snapshot_bytes).hexdigest() != snapshot_sha:
            raise ValueError(f"{key}: archived metadata snapshot SHA-256 mismatch")
        if snapshot["audit_entry_sha256_matches"] is not True:
            raise ValueError(f"{key}: archived metadata audit hash match is not recorded")
        metadata = json.loads(snapshot_bytes)
        if metadata.get("chunk_time_length") != values[0] or \
                metadata.get("temporal_resolution_seconds") != values[1]:
            raise ValueError(f"{key}: archived metadata values differ from the producer formulas")
        audit_entry = audit_by_key.get(metadata_key)
        if audit_entry is None or audit_entry.get("status") != 200 or audit_entry.get("sha256") != snapshot_sha:
            raise ValueError(f"{key}: archived metadata does not match its HTTP 200 audit entry")
        rows.append((key, values[0], values[1]))

    return data, rows


def generate_header(input_path: Path) -> str:
    data, rows = validate_registry(input_path)
    commit = data["upstream"]["commit"]
    lines = [
        "// Generated by scripts/generate-series-registry.py; do not edit.",
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        "namespace duckdb {",
        "namespace duckomo {",
        "namespace generated {",
        "",
        "struct GeneratedSeriesModel final {",
        "\tstd::string_view key;",
        "\tstd::int64_t chunk_time_length;",
        "\tstd::int64_t temporal_resolution_seconds;",
        "};",
        "",
        f"inline constexpr std::string_view SERIES_REGISTRY_UPSTREAM_COMMIT = {json.dumps(commit)};",
        f"inline constexpr std::array<GeneratedSeriesModel, {len(rows)}> SERIES_MODELS {{{{",
    ]
    for key, chunk_length, step_seconds in rows:
        lines.append(f"\t{{{json.dumps(key)}, {chunk_length}, {step_seconds}}},")
    lines.extend([
        "}};",
        "",
        "} // namespace generated",
        "} // namespace duckomo",
        "} // namespace duckdb",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    expected = generate_header(args.input)
    if args.check:
        with tempfile.TemporaryDirectory(prefix="duckomo-series-registry-") as temporary:
            first = Path(temporary) / "first.hpp"
            second = Path(temporary) / "second.hpp"
            first.write_text(expected, encoding="utf-8")
            second.write_text(generate_header(args.input), encoding="utf-8")
            if first.read_bytes() != second.read_bytes():
                raise ValueError("two generated series registry headers differ")
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != expected:
            raise ValueError(f"generated series registry is stale: {args.output}")
        print(f"series registry verified: {args.output}")
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(expected, encoding="utf-8")
    print(f"generated {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise SystemExit(f"series registry generation failed: {error}")
