#!/usr/bin/env python3
"""Generate deterministic read_om_series OM fixtures with the official writer."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FIXTURES = ROOT / "test/data/series/fixtures.json"
DEFAULT_REGISTRY = ROOT / "test/data/series/models.json"


class FixtureGenerationError(ValueError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def safe_relative_path(value: Any, field: str) -> Path:
    if not isinstance(value, str) or not value:
        raise FixtureGenerationError(f"{field} must be a non-empty relative path")
    parsed = PurePosixPath(value)
    if parsed.is_absolute() or ".." in parsed.parts or "\\" in value:
        raise FixtureGenerationError(f"{field} must stay inside the output root: {value!r}")
    return Path(*parsed.parts)


def load_spec(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise FixtureGenerationError(f"cannot read fixture definitions {path}: {error}") from error
    if not isinstance(document, dict) or document.get("schema_version") != 1:
        raise FixtureGenerationError("fixture definitions must be a schema_version 1 JSON object")
    objects = document.get("writer_objects")
    if not isinstance(objects, list) or not objects:
        raise FixtureGenerationError("writer_objects must be a non-empty array")
    identifiers: set[str] = set()
    paths: set[str] = set()
    for index, item in enumerate(objects):
        label = f"writer_objects[{index}]"
        if not isinstance(item, dict):
            raise FixtureGenerationError(f"{label} must be an object")
        identifier = item.get("id")
        if not isinstance(identifier, str) or not identifier or identifier in identifiers:
            raise FixtureGenerationError(f"{label}.id must be a unique non-empty string")
        identifiers.add(identifier)
        rel = safe_relative_path(item.get("path"), f"{label}.path").as_posix()
        if rel in paths:
            raise FixtureGenerationError(f"writer output path is repeated: {rel}")
        paths.add(rel)
        if not isinstance(item.get("role"), str) or not item["role"]:
            raise FixtureGenerationError(f"{label}.role must be a non-empty string")
        if not isinstance(item.get("chunk_index"), int) or isinstance(item.get("chunk_index"), bool):
            raise FixtureGenerationError(f"{label}.chunk_index must be an integer")
        shape = item.get("shape")
        if not isinstance(shape, list) or not shape or any(
            not isinstance(dim, int) or isinstance(dim, bool) or dim <= 0 for dim in shape
        ):
            raise FixtureGenerationError(f"{label}.shape must contain positive integer dimensions")
        array_paths = item.get("array_paths")
        if not isinstance(array_paths, list) or not array_paths or any(
            not isinstance(array_path, str) or not array_path.startswith("/") for array_path in array_paths
        ):
            raise FixtureGenerationError(f"{label}.array_paths must contain absolute OM array paths")
        if array_paths != sorted(set(array_paths)):
            raise FixtureGenerationError(f"{label}.array_paths must be unique and sorted")

    gaps = document.get("gap_prefixes")
    if not isinstance(gaps, list) or not gaps:
        raise FixtureGenerationError("gap_prefixes must be a non-empty array")
    gap_ids: set[str] = set()
    gap_paths: set[str] = set()
    for index, item in enumerate(gaps):
        label = f"gap_prefixes[{index}]"
        if not isinstance(item, dict):
            raise FixtureGenerationError(f"{label} must be an object")
        identifier = item.get("id")
        if not isinstance(identifier, str) or not identifier or identifier in gap_ids:
            raise FixtureGenerationError(f"{label}.id must be a unique non-empty string")
        gap_ids.add(identifier)
        prefix = safe_relative_path(item.get("prefix"), f"{label}.prefix").as_posix()
        if prefix in gap_paths:
            raise FixtureGenerationError(f"gap prefix is repeated: {prefix}")
        gap_paths.add(prefix)
        present = item.get("present_chunk_indices")
        if not isinstance(present, list) or any(
            not isinstance(chunk, int) or isinstance(chunk, bool) for chunk in present
        ) or len(present) != len(set(present)):
            raise FixtureGenerationError(f"{label}.present_chunk_indices must contain unique integer indices")
    return document


def validate_writer_manifest(spec: dict[str, Any], root: Path) -> list[dict[str, Any]]:
    path = root / "series-writer-manifest.json"
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise FixtureGenerationError(f"official writer did not produce a readable manifest: {error}") from error
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise FixtureGenerationError("official writer manifest has an unsupported schema")
    actual_objects = manifest.get("objects")
    expected_objects = spec["writer_objects"]
    if not isinstance(actual_objects, list) or len(actual_objects) != len(expected_objects):
        raise FixtureGenerationError("official writer object count differs from fixtures.json")
    actual_by_id = {item.get("id"): item for item in actual_objects if isinstance(item, dict)}
    if len(actual_by_id) != len(actual_objects):
        raise FixtureGenerationError("official writer manifest has missing or duplicate object ids")

    normalized: list[dict[str, Any]] = []
    for expected in expected_objects:
        actual = actual_by_id.get(expected["id"])
        if actual is None:
            raise FixtureGenerationError(f"official writer omitted fixture {expected['id']}")
        for field in ("id", "role", "chunk_index", "path", "shape", "array_paths"):
            if actual.get(field) != expected.get(field):
                raise FixtureGenerationError(
                    f"official writer metadata for {expected['id']}.{field} differs from fixtures.json"
                )
        for field in ("chunk_shape", "compression", "data_type", "metadata"):
            if field in expected and actual.get(field) != expected[field]:
                raise FixtureGenerationError(
                    f"official writer metadata for {expected['id']}.{field} differs from fixtures.json"
                )
        relative = safe_relative_path(actual["path"], f"{expected['id']}.path")
        object_path = root / relative
        if not object_path.is_file():
            raise FixtureGenerationError(f"official writer output is missing: {object_path}")
        actual_hash = sha256(object_path)
        if actual.get("sha256") != actual_hash:
            raise FixtureGenerationError(f"official writer hash mismatch for {expected['id']}")
        size = object_path.stat().st_size
        if actual.get("size_bytes") != size:
            raise FixtureGenerationError(f"official writer size mismatch for {expected['id']}")
        normalized.append({**expected, "size_bytes": size, "sha256": actual_hash})
    return normalized


def load_registry_models(path: Path) -> tuple[list[dict[str, Any]], str]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise FixtureGenerationError(f"cannot read temporal model registry {path}: {error}") from error
    models = document.get("models") if isinstance(document, dict) else None
    if not isinstance(models, list) or not models:
        raise FixtureGenerationError("temporal model registry must contain a non-empty models array")
    seen: set[str] = set()
    result: list[dict[str, Any]] = []
    for index, model in enumerate(models):
        label = f"models[{index}]"
        if not isinstance(model, dict):
            raise FixtureGenerationError(f"{label} must be an object")
        key = model.get("key")
        temporal = model.get("producer_source")
        length_record = temporal.get("chunk_time_length") if isinstance(temporal, dict) else None
        step_record = temporal.get("temporal_resolution_seconds") if isinstance(temporal, dict) else None
        length = length_record.get("value") if isinstance(length_record, dict) else None
        step = step_record.get("value") if isinstance(step_record, dict) else None
        if (not isinstance(key, str) or not key or not key.replace("_", "a").isalnum()
                or not key.isascii() or key in seen):
            raise FixtureGenerationError(f"{label}.key must be a unique ASCII model identifier")
        if (not isinstance(length, int) or isinstance(length, bool) or not 0 < length <= 4096
                or not isinstance(step, int) or isinstance(step, bool) or step <= 0):
            raise FixtureGenerationError(f"{label} must contain positive bounded temporal values")
        metadata = model.get("metadata_snapshot")
        producer = document.get("upstream", {})
        if (model.get("provenance_status") != "pinned_source_and_archived_metadata_match"
                or model.get("discrepancies") or not isinstance(metadata, dict)
                or metadata.get("audit_entry_sha256_matches") is not True
                or not isinstance(temporal.get("path"), str) or not isinstance(temporal.get("sha256"), str)):
            raise FixtureGenerationError(f"{label} lacks pinned producer and archived metadata provenance")
        seen.add(key)
        result.append({
            "key": key, "chunk_time_length": length, "step_seconds": step,
            "producer_domain": model.get("producer_domain"),
            "producer_source_path": temporal["path"], "producer_source_sha256": temporal["sha256"],
            "chunk_time_length_expression": length_record.get("source_expression"),
            "step_seconds_expression": step_record.get("source_expression"),
            "provenance_status": model["provenance_status"],
            "metadata_object_key": model.get("metadata_object_key"),
            "metadata_snapshot_path": metadata.get("path"),
            "metadata_snapshot_sha256": metadata.get("sha256"),
            "upstream_repository": producer.get("repository"), "upstream_commit": producer.get("commit"),
        })
    if len(result) != 11:
        raise FixtureGenerationError(f"expected eleven frozen temporal models, found {len(result)}")
    return result, sha256(path)


def validate_registry_writer_manifest(models: list[dict[str, Any]], root: Path) -> list[dict[str, Any]]:
    path = root / "series-registry-writer-manifest.json"
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise FixtureGenerationError(f"official writer did not produce registry fixture metadata: {error}") from error
    actual = manifest.get("objects") if isinstance(manifest, dict) else None
    if (not isinstance(manifest, dict) or manifest.get("schema_version") != 1
            or not isinstance(actual, list) or len(actual) != len(models)):
        raise FixtureGenerationError("registry fixture writer manifest has the wrong schema or object count")
    by_key = {item.get("model_key"): item for item in actual if isinstance(item, dict)}
    if len(by_key) != len(actual):
        raise FixtureGenerationError("registry fixture writer manifest has missing or duplicate model keys")
    normalized: list[dict[str, Any]] = []
    for model in models:
        key = model["key"]
        item = by_key.get(key)
        expected_length = model["chunk_time_length"]
        expected_path = f"data/registry-equivalence/{key}/chunk_0000.om"
        expected_shape = [1, 6, expected_length]
        expected_chunks = [1, 3, min(2, expected_length)]
        if (item is None or item.get("id") != f"registry_{key}" or item.get("role") != "registry_equivalence"
                or item.get("chunk_index") != 0 or item.get("path") != expected_path
                or item.get("chunk_time_length") != expected_length
                or item.get("step_seconds") != model["step_seconds"]
                or item.get("shape") != expected_shape or item.get("chunk_shape") != expected_chunks
                or item.get("array_paths") != ["/"] or item.get("data_type") != "FLOAT_ARRAY"):
            raise FixtureGenerationError(f"official writer metadata differs for registry model {key}")
        relative = safe_relative_path(item["path"], f"registry model {key} path")
        object_path = root / relative
        if not object_path.is_file() or item.get("sha256") != sha256(object_path):
            raise FixtureGenerationError(f"official registry fixture is missing or has a hash mismatch: {relative}")
        if item.get("size_bytes") != object_path.stat().st_size:
            raise FixtureGenerationError(f"official registry fixture size differs: {relative}")
        normalized.append({**item, "size_bytes": object_path.stat().st_size,
                           "sha256": sha256(object_path)})
    return normalized


def materialize_gap_prefixes(spec: dict[str, Any], objects: list[dict[str, Any]], root: Path) -> list[dict[str, Any]]:
    adjacent = {
        item["chunk_index"]: item
        for item in objects
        if item["role"] == "adjacent_root"
    }
    if sorted(adjacent) != [0, 1, 2]:
        raise FixtureGenerationError("the independent gap source must contain exactly adjacent chunks 0, 1, 2")
    results: list[dict[str, Any]] = []
    for gap in spec["gap_prefixes"]:
        prefix = safe_relative_path(gap["prefix"], f"gap {gap['id']} prefix")
        destination = root / prefix
        if destination.exists():
            shutil.rmtree(destination)
        destination.mkdir(parents=True)
        copied: list[dict[str, Any]] = []
        for chunk_index in gap["present_chunk_indices"]:
            source_record = adjacent.get(chunk_index)
            if source_record is None:
                raise FixtureGenerationError(f"gap {gap['id']} references unknown source chunk {chunk_index}")
            source = root / safe_relative_path(source_record["path"], "source object path")
            target = destination / source.name
            shutil.copyfile(source, target)
            copied.append({"chunk_index": chunk_index, "path": (prefix / source.name).as_posix(),
                           "size_bytes": target.stat().st_size, "sha256": sha256(target),
                           "source_path": source_record["path"], "source_sha256": source_record["sha256"]})
        actual_names = sorted(path.name for path in destination.iterdir() if path.is_file())
        expected_names = sorted(
            (root / safe_relative_path(adjacent[index]["path"], "source object path")).name
            for index in gap["present_chunk_indices"]
        )
        if actual_names != expected_names:
            raise FixtureGenerationError(f"gap prefix {gap['id']} contains unexpected objects")
        results.append({"id": gap["id"], "prefix": prefix.as_posix(),
                        "present_chunk_indices": gap["present_chunk_indices"], "objects": copied})
    return results


def _recorded_file_matches(root: Path, record: dict[str, Any]) -> bool:
    """True when a manifest record points at an existing file of the recorded size."""
    try:
        relative = safe_relative_path(record.get("path"), "path")
    except FixtureGenerationError:
        return False
    size = record.get("size_bytes")
    if isinstance(size, bool) or not isinstance(size, int) or size < 0:
        return False
    path = root / relative
    return path.is_file() and path.stat().st_size == size


def series_fixtures_ready(output_root: Path, fixtures_path: Path = DEFAULT_FIXTURES,
                          registry_path: Path = DEFAULT_REGISTRY) -> bool:
    """Return whether output_root already holds the fixture files SQL tests read.

    series-fixtures.json is written only after every object and gap prefix exists,
    so a matching manifest plus those files is enough to skip CMake. Definition
    hashes catch a spec change; file sizes catch a truncated or missing object.
    """
    output_root = Path(output_root)
    manifest_path = output_root / "series-fixtures.json"
    if not manifest_path.is_file():
        return False
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        spec = load_spec(fixtures_path)
        models, registry_sha = load_registry_models(registry_path)
    except (OSError, UnicodeError, json.JSONDecodeError, FixtureGenerationError):
        return False
    if (not isinstance(manifest, dict) or manifest.get("schema_version") != 1
            or manifest.get("fixture_definitions_sha256") != sha256(fixtures_path)
            or manifest.get("registry_models_sha256") != registry_sha):
        return False

    objects = manifest.get("objects")
    if not isinstance(objects, list):
        return False
    by_id: dict[str, dict[str, Any]] = {}
    for item in objects:
        if not isinstance(item, dict):
            return False
        identifier = item.get("id")
        if not isinstance(identifier, str) or identifier in by_id:
            return False
        by_id[identifier] = item
    expected_ids = [item["id"] for item in spec["writer_objects"]]
    expected_ids.extend(f"registry_{model['key']}" for model in models)
    if len(by_id) != len(expected_ids) or set(by_id) != set(expected_ids):
        return False
    for expected in spec["writer_objects"]:
        actual = by_id[expected["id"]]
        if actual.get("path") != expected["path"] or not _recorded_file_matches(output_root, actual):
            return False
    for model in models:
        actual = by_id[f"registry_{model['key']}"]
        expected_path = f"data/registry-equivalence/{model['key']}/chunk_0000.om"
        if actual.get("path") != expected_path or not _recorded_file_matches(output_root, actual):
            return False

    gaps = manifest.get("gap_prefixes")
    if not isinstance(gaps, list):
        return False
    gap_by_id: dict[str, dict[str, Any]] = {}
    for gap in gaps:
        if not isinstance(gap, dict):
            return False
        identifier = gap.get("id")
        if not isinstance(identifier, str) or identifier in gap_by_id:
            return False
        gap_by_id[identifier] = gap
    if set(gap_by_id) != {item["id"] for item in spec["gap_prefixes"]}:
        return False
    for expected_gap in spec["gap_prefixes"]:
        actual_gap = gap_by_id[expected_gap["id"]]
        if (actual_gap.get("prefix") != expected_gap["prefix"]
                or actual_gap.get("present_chunk_indices") != expected_gap["present_chunk_indices"]):
            return False
        try:
            prefix = output_root / safe_relative_path(expected_gap["prefix"], "gap prefix")
        except FixtureGenerationError:
            return False
        if not prefix.is_dir():
            return False
        copied = actual_gap.get("objects")
        if not isinstance(copied, list) or len(copied) != len(expected_gap["present_chunk_indices"]):
            return False
        recorded_names: list[str] = []
        for record in copied:
            if not isinstance(record, dict) or not _recorded_file_matches(output_root, record):
                return False
            recorded_names.append(Path(str(record["path"])).name)
        actual_names = sorted(path.name for path in prefix.iterdir() if path.is_file())
        if actual_names != sorted(recorded_names):
            return False
    return True


def generate(fixture_tool: Path, output_root: Path, fixtures_path: Path = DEFAULT_FIXTURES,
             registry_path: Path = DEFAULT_REGISTRY) -> dict[str, Any]:
    fixture_tool = fixture_tool.resolve()
    output_root = output_root.resolve()
    fixtures_path = fixtures_path.resolve()
    if not fixture_tool.is_file():
        raise FixtureGenerationError(f"fixture tool does not exist: {fixture_tool}")
    if not fixture_tool.stat().st_mode & 0o111:
        raise FixtureGenerationError(f"fixture tool is not executable: {fixture_tool}")
    spec = load_spec(fixtures_path)
    registry_models, registry_sha = load_registry_models(registry_path)
    output_root.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(fixture_tool), "--series-output", str(output_root)], cwd=ROOT, check=True)
    objects = validate_writer_manifest(spec, output_root)
    registry_arguments = [str(fixture_tool), "--series-registry-output", str(output_root)]
    for model in registry_models:
        registry_arguments.extend(["--registry-model", f"{model['key']}:{model['chunk_time_length']}:{model['step_seconds']}"])
    subprocess.run(registry_arguments, cwd=ROOT, check=True)
    registry_objects = validate_registry_writer_manifest(registry_models, output_root)
    gaps = materialize_gap_prefixes(spec, objects, output_root)
    writer_manifest = output_root / "series-writer-manifest.json"
    registry_writer_manifest = output_root / "series-registry-writer-manifest.json"
    result = {
        "schema_version": 1,
        "feature": "010-read-om-series",
        "fixture_definitions_sha256": sha256(fixtures_path),
        "registry_models_path": str(registry_path),
        "registry_models_sha256": registry_sha,
        "writer_manifest_sha256": sha256(writer_manifest),
        "registry_writer_manifest_sha256": sha256(registry_writer_manifest),
        "writer": "official OM C writer with official OM C decoder round-trip verification",
        "objects": objects + registry_objects,
        "registry_equivalence_models": registry_models,
        "registry_equivalence_objects": registry_objects,
        "gap_prefixes": gaps,
        "oracle_rule": spec["independent_oracle"],
    }
    result_path = output_root / "series-fixtures.json"
    result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"series fixtures generated: {result_path}")
    print(f"official writer objects={len(objects)} gap prefixes={len(gaps)}")
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-existing", action="store_true",
                        help="exit 0 when output-root already contains the complete fixture set")
    parser.add_argument("--fixture-tool", type=Path, help="built duckomo_fixture_tool executable")
    parser.add_argument("--output-root", required=True, type=Path, help="build-local generated fixture root")
    parser.add_argument("--fixtures", type=Path, default=DEFAULT_FIXTURES, help="fixture definition JSON")
    parser.add_argument("--registry", type=Path, default=DEFAULT_REGISTRY,
                        help="frozen temporal model registry used for SC-004 fixtures")
    args = parser.parse_args(argv)
    if args.check_existing:
        return 0 if series_fixtures_ready(args.output_root, args.fixtures, args.registry) else 1
    fixture_tool = args.fixture_tool
    if fixture_tool is None:
        parser.error("--fixture-tool is required to generate fixtures")
    try:
        generate(fixture_tool, args.output_root, args.fixtures, args.registry)
    except (FixtureGenerationError, OSError, subprocess.CalledProcessError) as error:
        print(f"series fixture generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
