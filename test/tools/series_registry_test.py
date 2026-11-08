#!/usr/bin/env python3
"""Check frozen temporal model provenance and deterministic registry generation."""

from __future__ import annotations

import copy
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
REGISTRY_PATH = ROOT / "test/data/series/models.json"
GRID_SOURCE_MANIFEST = ROOT / "test/data/grids/source-manifest.json"
GRID_DEFINITIONS = ROOT / "test/data/grids/definitions.json"
GENERATOR = ROOT / "scripts/generate-series-registry.py"
GENERATED_HEADER = ROOT / "src/include/duckomo/generated_series_registry.hpp"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")

EXPECTED = {
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

SPATIAL_FIELDS = {"grid_id", "grid_definition", "grid_evidence_level", "spatial_evidence"}


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def check_frozen_registry() -> dict[str, Any]:
    data = load_json(REGISTRY_PATH)
    if data.get("schema_version") != 1 or data.get("feature") != "010-read-om-series":
        raise AssertionError("unexpected temporal registry schema or feature")

    source_manifest = load_json(GRID_SOURCE_MANIFEST)
    upstream = data["upstream"]
    if upstream.get("repository") != source_manifest.get("repository"):
        raise AssertionError("temporal and grid registries must share the exact upstream repository")
    if upstream.get("commit") != source_manifest.get("pinned_commit"):
        raise AssertionError("temporal registry pin differs from the pinned grid producer source")
    if upstream.get("pin_reference", {}).get("expected_commit") != upstream.get("commit"):
        raise AssertionError("recorded upstream pin reference does not match the temporal registry pin")

    for source_record in (upstream["key_mapping_source"], upstream["metadata_writer_source"]):
        if not source_record.get("path", "").startswith("Sources/App/") or not SHA256_RE.fullmatch(source_record.get("sha256", "")):
            raise AssertionError("missing frozen upstream mapping/writer source path or hash")
    writer = upstream["metadata_writer_source"]
    if writer.get("metadata_object_key_template") != "data/${domain.rawValue}/static/meta.json":
        raise AssertionError("metadata object key template differs from the pinned writer")
    if writer.get("metadata_file_domain_input") != "domain.domainRegistry":
        raise AssertionError("metadata writer must receive the exact DomainRegistry key")
    if writer.get("temporal_field_mapping") != {
        "chunk_time_length": "domain.omFileLength",
        "temporal_resolution_seconds": "domain.dtSeconds",
    }:
        raise AssertionError("metadata writer temporal fields changed")

    archive = data["archived_metadata_source"]
    audit_path = ROOT / archive["audit_path"]
    audit_bytes = audit_path.read_bytes()
    if hashlib.sha256(audit_bytes).hexdigest() != archive.get("audit_sha256"):
        raise AssertionError("archived metadata audit SHA-256 mismatch")
    audit = json.loads(audit_bytes)

    models = data.get("models")
    if not isinstance(models, list):
        raise AssertionError("models must be an ordered list")
    keys = [item.get("key") for item in models]
    if keys != list(EXPECTED):
        raise AssertionError(f"exact temporal model key/order mismatch: {keys}")
    if len(set(keys)) != len(keys):
        raise AssertionError("duplicate temporal model key")

    pinned_source_files: dict[str, str] = {}
    for item in source_manifest.get("source_files", []):
        pinned_source_files[item["path"]] = item["sha256"]
    legacy = source_manifest.get("legacy_reference", {})
    for item in legacy.get("source_files", []):
        pinned_source_files[item["path"]] = item["sha256"]

    for model in models:
        key = model["key"]
        expected_length, expected_step = EXPECTED[key]
        if model.get("data_key") != key:
            raise AssertionError(f"{key}: temporal key must map to the same exact data key")
        if model.get("metadata_object_key") != f"data/{key}/static/meta.json":
            raise AssertionError(f"{key}: incorrect metadata object key")
        if model.get("provenance_status") != "pinned_source_and_archived_metadata_match":
            raise AssertionError(f"{key}: source/snapshot provenance is not marked verified")
        if model.get("discrepancies") != []:
            raise AssertionError(f"{key}: temporal value or mapping discrepancy is recorded")
        if any(field in model for field in SPATIAL_FIELDS):
            raise AssertionError(f"{key}: temporal registry entry depends on spatial evidence")

        producer = model["producer_source"]
        producer_path = producer.get("path", "")
        producer_sha = producer.get("sha256", "")
        if not producer_path.startswith("Sources/App/") or not SHA256_RE.fullmatch(producer_sha):
            raise AssertionError(f"{key}: missing frozen producer source path/hash")
        if producer_path in pinned_source_files and producer_sha != pinned_source_files[producer_path]:
            raise AssertionError(f"{key}: producer hash differs from the existing pinned source manifest")
        if model.get("producer_domain", "").count(".") != 1:
            raise AssertionError(f"{key}: producer domain must name an exact type and case")

        length = producer["chunk_time_length"]
        step = producer["temporal_resolution_seconds"]
        for name, entry, expected_value in (
            ("chunk_time_length", length, expected_length),
            ("temporal_resolution_seconds", step, expected_step),
        ):
            value = entry.get("value")
            if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
                raise AssertionError(f"{key}: {name} must be a positive integer")
            if value != expected_value or not entry.get("source_expression") or not entry.get("source_symbol"):
                raise AssertionError(f"{key}: {name} differs from the frozen R3 value/source formula")

        snapshot = model["metadata_snapshot"]
        snapshot_path = ROOT / snapshot["path"]
        snapshot_bytes = snapshot_path.read_bytes()
        snapshot_sha = hashlib.sha256(snapshot_bytes).hexdigest()
        if not SHA256_RE.fullmatch(snapshot.get("sha256", "")) or snapshot_sha != snapshot["sha256"]:
            raise AssertionError(f"{key}: archived metadata snapshot SHA-256 mismatch")
        if snapshot.get("audit_entry_sha256_matches") is not True:
            raise AssertionError(f"{key}: archived audit hash match is not recorded")
        metadata = json.loads(snapshot_bytes)
        if metadata.get("chunk_time_length") != expected_length:
            raise AssertionError(f"{key}: archived chunk_time_length differs from R3")
        if metadata.get("temporal_resolution_seconds") != expected_step:
            raise AssertionError(f"{key}: archived temporal_resolution_seconds differs from R3")
        audit_entry = next((entry for entry in audit["objects"] if entry.get("key") == model["metadata_object_key"]), None)
        if audit_entry is None or audit_entry.get("status") != 200 or audit_entry.get("sha256") != snapshot_sha:
            raise AssertionError(f"{key}: snapshot does not match the archived HTTP 200 audit entry")

    return data


def check_spatial_independence(data: dict[str, Any]) -> None:
    grid_definitions = load_json(GRID_DEFINITIONS)
    spatial_ids = {item["id"] for item in grid_definitions["definitions"]}
    temporal_keys = {item["key"] for item in data["models"]}
    overlap = temporal_keys & spatial_ids
    if overlap != {"ecmwf_ifs"}:
        raise AssertionError(f"unexpected coupling between temporal keys and spatial definitions: {overlap}")
    if len(temporal_keys - spatial_ids) != 10:
        raise AssertionError("the ten temporal-only keys must remain registered without spatial definitions")
    if any("evidence" in item or "validation_level" in item for item in data["models"]):
        raise AssertionError("temporal entries must not inherit spatial evidence levels")


def invoke_generator(input_path: Path, output_path: Path, *, check: bool = False) -> subprocess.CompletedProcess[str]:
    command = [sys.executable, str(GENERATOR), "--input", str(input_path), "--output", str(output_path)]
    if check:
        command.append("--check")
    return subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False)


def expect_rejected(data: dict[str, Any], temporary: Path, name: str) -> None:
    input_path = temporary / f"{name}.json"
    output_path = temporary / f"{name}.hpp"
    input_path.write_text(json.dumps(data, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    result = invoke_generator(input_path, output_path)
    if result.returncode == 0:
        raise AssertionError(f"series registry generator accepted invalid input: {name}")


def check_generator(data: dict[str, Any]) -> None:
    with tempfile.TemporaryDirectory(prefix="duckomo-series-registry-") as directory:
        temporary = Path(directory)
        first = temporary / "first.hpp"
        second = temporary / "second.hpp"
        for output in (first, second):
            result = invoke_generator(REGISTRY_PATH, output)
            if result.returncode != 0:
                raise AssertionError(f"series registry generation failed: {result.stderr}")
        if first.read_bytes() != second.read_bytes():
            raise AssertionError("series registry generation is not byte-for-byte deterministic")
        generated_text = first.read_text(encoding="utf-8")
        for model in data["models"]:
            if f'"{model["key"]}"' not in generated_text:
                raise AssertionError(f"generated registry omitted temporal key {model['key']}")

        checked = invoke_generator(REGISTRY_PATH, GENERATED_HEADER, check=True)
        if checked.returncode != 0:
            raise AssertionError(f"generated series registry --check failed: {checked.stderr}")
        if first.read_bytes() != GENERATED_HEADER.read_bytes():
            raise AssertionError("checked-in series registry differs from deterministic generation")

        duplicate = copy.deepcopy(data)
        duplicate["models"].append(copy.deepcopy(duplicate["models"][0]))
        expect_rejected(duplicate, temporary, "duplicate-key")

        unsupported_key = copy.deepcopy(data)
        extra = copy.deepcopy(unsupported_key["models"][0])
        extra["key"] = "ecmwf_ifs_alias"
        extra["data_key"] = "ecmwf_ifs_alias"
        extra["metadata_object_key"] = "data/ecmwf_ifs_alias/static/meta.json"
        unsupported_key["models"].append(extra)
        expect_rejected(unsupported_key, temporary, "unsupported-key")

        invalid_length = copy.deepcopy(data)
        invalid_length["models"][0]["producer_source"]["chunk_time_length"]["value"] = 0
        expect_rejected(invalid_length, temporary, "zero-chunk-length")

        invalid_step = copy.deepcopy(data)
        invalid_step["models"][0]["producer_source"]["temporal_resolution_seconds"]["value"] = -3600
        expect_rejected(invalid_step, temporary, "negative-time-step")


def main() -> int:
    data = check_frozen_registry()
    check_spatial_independence(data)
    if GENERATOR.exists():
        check_generator(data)
        print("series registry provenance and deterministic generation checks passed")
    else:
        print("series registry provenance checks passed; deterministic generation checks deferred until T026")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
