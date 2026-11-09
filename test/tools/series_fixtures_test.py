#!/usr/bin/env python3
"""Reproducibility and independent-oracle checks for series fixtures."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import tempfile
from datetime import datetime, timedelta, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GENERATOR_PATH = ROOT / "scripts/generate-series-fixtures.py"
FIXTURE_PATH = ROOT / "test/data/series/fixtures.json"


def load_generator():
    spec = importlib.util.spec_from_file_location("series_fixture_generator", GENERATOR_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load series fixture generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def independent_value(chunk_index: int, point_index: int, slot_index: int, array_offset: int = 0):
    """Acceptance oracle; intentionally separate from series planning code."""
    if chunk_index == 1 and point_index == 2 and slot_index == 1:
        return None
    return 1000 * chunk_index + 100 * point_index + slot_index + array_offset


def independent_time(chunk_index: int, slot_index: int) -> datetime:
    epoch = datetime(1970, 1, 1, tzinfo=timezone.utc)
    return epoch + timedelta(seconds=(chunk_index * 4 + slot_index) * 3600)


def test_fixture_definitions_and_independent_oracle() -> None:
    document = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
    rule = document["value_rule"]
    if rule["shape"] != [1, 6, 4] or rule["dimensions"] != ["lead", "point", "time"]:
        raise AssertionError("series fixture shape or dimension order changed")
    if rule["chunk_time_length"] != 4 or rule["step_seconds"] != 3600:
        raise AssertionError("series time oracle inputs changed")
    if independent_value(0, 5, 3) != 503 or independent_value(1, 2, 1) is not None:
        raise AssertionError("independent value oracle no longer covers values and NaN-to-NULL")
    if independent_value(-1, 0, 0) != -1000:
        raise AssertionError("negative chunk values are not based on the signed chunk index")
    if independent_time(-1, 3) != datetime(1969, 12, 31, 23, tzinfo=timezone.utc):
        raise AssertionError("negative chunk slot origin does not use absolute UTC time")
    if independent_time(2, 0) != datetime(1970, 1, 1, 8, tzinfo=timezone.utc):
        raise AssertionError("adjacent chunk boundary time changed")
    gap_ids = {item["id"] for item in document["gap_prefixes"]}
    if gap_ids != {"first_missing", "middle_missing", "last_missing", "all_missing"}:
        raise AssertionError("isolated first/middle/last/all gap prefixes are required")
    gaussian = document["reduced_gaussian_series"]
    if gaussian["registered_domain"] != "n160" or gaussian["classification"] != "synthetic_only":
        raise AssertionError("reduced-Gaussian fixtures must use the existing N160 identity and remain synthetic")
    if gaussian["shape"] != [1, 138346, 4] or gaussian["spatial_axis_profile"] != ["point"]:
        raise AssertionError("reduced-Gaussian series fixture no longer matches the registered flattened point profile")
    gaussian_objects = [item for item in document["writer_objects"]
                        if item["role"] == "reduced_gaussian_n160"]
    if [item["chunk_index"] for item in gaussian_objects] != [0, 1, 2]:
        raise AssertionError("reduced-Gaussian multi-chunk fixture must contain adjacent indices 0, 1, 2")
    if len({tuple(item["chunk_shape"]) for item in gaussian_objects}) != 3:
        raise AssertionError("reduced-Gaussian children must exercise differing compression block shapes")
    if any("compression" not in item for item in gaussian_objects):
        raise AssertionError("reduced-Gaussian fixture manifest must retain each OM compression codec")
    objects = {item["id"]: item for item in document["writer_objects"]}
    for identifier, expected_path, expected_index in (
        ("negative_-001", "data/negative/temperature/chunk_-001.om", -1),
        ("array_set_bad_0001", "data/mismatch/array-set/temperature/chunk_0001.om", 1),
    ):
        item = objects[identifier]
        if item["path"] != expected_path or item["chunk_index"] != expected_index:
            raise AssertionError(f"signed naming or mismatch fixture changed for {identifier}")
    expected_metadata = {
        "metadata_time_conflict_0000": {"valid_time": 3600},
        "dimension_order_bad_0000": {"coordinates": "lead time point"},
        "spatial_profile_bad_0000": {"coordinates": "lead latitude_axis time"},
        "grid_identity_bad_0000": {
            "coordinates": "lead point time",
            "crs_wkt": 'GEOGCRS["Reduced Gaussian Grid", DATUM["World Geodetic System 1984", '
            'ELLIPSOID["WGS 84",6378137,298.257223563]], CS[ellipsoidal,2], '
            'AXIS["latitude",north], AXIS["longitude",east], '
            'ANGLEUNIT["degree",0.0174532925199433], '
            'USAGE[SCOPE["grid"],BBOX[-90,-180.0,90,180]]]',
        },
    }
    for identifier, metadata in expected_metadata.items():
        if objects[identifier].get("metadata") != metadata:
            raise AssertionError(f"metadata mismatch fixture changed for {identifier}")
    if objects["declared_type_bad_0000"].get("data_type") != "DOUBLE_ARRAY":
        raise AssertionError("declared-type mismatch fixture must contain a real Double array")
    if objects["metadata_time_conflict_0000"]["array_paths"] != ["/temperature"]:
        raise AssertionError("embedded-time conflict fixture path changed")


def test_registry_equivalence_fixture_definitions() -> None:
    registry_path = ROOT / "test/data/series/models.json"
    registry = json.loads(registry_path.read_text(encoding="utf-8"))
    models = registry["models"]
    fixtures = load_generator()
    parsed, registry_hash = fixtures.load_registry_models(registry_path)
    if len(parsed) != 11 or registry_hash != hashlib.sha256(registry_path.read_bytes()).hexdigest():
        raise AssertionError("SC-004 fixtures must cover all eleven frozen registry entries and source hash")
    if [item["key"] for item in parsed] != [item["key"] for item in models]:
        raise AssertionError("SC-004 fixture order must follow the frozen model registry")
    for model in parsed:
        slot_count = model["chunk_time_length"]
        if slot_count != next(item["producer_source"]["chunk_time_length"]["value"]
                              for item in models if item["key"] == model["key"]):
            raise AssertionError(f"SC-004 fixture slot count differs for {model['key']}")
        expected_time = datetime(1970, 1, 1, tzinfo=timezone.utc) + timedelta(
            seconds=(slot_count - 1) * model["step_seconds"])
        if expected_time <= datetime(1970, 1, 1, tzinfo=timezone.utc):
            raise AssertionError(f"SC-004 fixture does not exercise a registered time step: {model['key']}")


def create_fake_writer(path: Path) -> None:
    source = """#!/usr/bin/env python3
import hashlib, json, sys
from pathlib import Path
if '--series-output' in sys.argv:
    root = Path(sys.argv[sys.argv.index('--series-output') + 1])
    spec = json.loads(Path('test/data/series/fixtures.json').read_text())
    objects = []
    for item in spec['writer_objects']:
        payload = ('deterministic-writer-stub:' + item['id'] + '\\n').encode()
        target = root / item['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        objects.append({**item, 'size_bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()})
    objects.sort(key=lambda item: item['id'])
    (root / 'series-writer-manifest.json').write_text(json.dumps({
        'schema_version': 1, 'objects': objects
    }, indent=2) + '\\n')
else:
    root = Path(sys.argv[sys.argv.index('--series-registry-output') + 1])
    models = [sys.argv[index + 1].split(':') for index, value in enumerate(sys.argv[:-1])
              if value == '--registry-model']
    objects = []
    for key, length, step in models:
        length, step = int(length), int(step)
        identifier = 'registry_' + key
        relative = f'data/registry-equivalence/{key}/chunk_0000.om'
        shape, chunks = [1, 6, length], [1, 3, min(2, length)]
        payload = ('registry-writer-stub:' + key + ':' + str(length) + '\\n').encode()
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        objects.append({'id': identifier, 'role': 'registry_equivalence', 'model_key': key,
                        'chunk_time_length': length, 'step_seconds': step, 'chunk_index': 0,
                        'path': relative, 'shape': shape, 'chunk_shape': chunks,
                        'compression': 'FPX_XOR2D', 'array_paths': ['/'],
                        'data_type': 'FLOAT_ARRAY', 'metadata': {}, 'size_bytes': len(payload),
                        'sha256': hashlib.sha256(payload).hexdigest()})
    (root / 'series-registry-writer-manifest.json').write_text(json.dumps({
        'schema_version': 1, 'objects': objects
    }, indent=2) + '\\n')
"""
    path.write_text(source, encoding="utf-8")
    path.chmod(0o755)


def test_generator_reproducibility_hashes_and_gap_isolation() -> None:
    generator = load_generator()
    with tempfile.TemporaryDirectory(prefix="duckomo-series-fixtures-") as temporary:
        temporary_root = Path(temporary)
        tool = temporary_root / "fake_fixture_tool.py"
        create_fake_writer(tool)
        output_a = temporary_root / "first"
        output_b = temporary_root / "second"
        first = generator.generate(tool, output_a, FIXTURE_PATH)
        second = generator.generate(tool, output_b, FIXTURE_PATH)
        manifest_a = (output_a / "series-fixtures.json").read_bytes()
        manifest_b = (output_b / "series-fixtures.json").read_bytes()
        if manifest_a != manifest_b:
            raise AssertionError("fixture manifest generation is not byte-for-byte deterministic")
        if first["fixture_definitions_sha256"] != second["fixture_definitions_sha256"]:
            raise AssertionError("fixture definition hash changed between deterministic runs")
        if first["registry_models_sha256"] != second["registry_models_sha256"]:
            raise AssertionError("frozen model registry hash changed between deterministic runs")
        if len(first["registry_equivalence_objects"]) != 11:
            raise AssertionError("registry fixture writer did not produce one object per frozen model")
        for item in first["registry_equivalence_objects"]:
            model = next(model for model in first["registry_equivalence_models"] if model["key"] == item["model_key"])
            if item["shape"] != [1, 6, model["chunk_time_length"]]:
                raise AssertionError(f"registry fixture does not use the actual slot count for {item['model_key']}")
            target = output_a / item["path"]
            if hashlib.sha256(target.read_bytes()).hexdigest() != item["sha256"]:
                raise AssertionError(f"registry fixture manifest does not match {item['model_key']}")

        gaps = {item["id"]: item for item in first["gap_prefixes"]}
        expected = {"first_missing": [1, 2], "middle_missing": [0, 2],
                    "last_missing": [0, 1], "all_missing": []}
        for identifier, chunk_indices in expected.items():
            gap = gaps[identifier]
            if gap["present_chunk_indices"] != chunk_indices:
                raise AssertionError(f"gap prefix {identifier} has the wrong present chunk set")
            directory = output_a / gap["prefix"]
            actual_names = sorted(path.name for path in directory.iterdir() if path.is_file())
            expected_names = [f"chunk_{index:04d}.om" for index in chunk_indices]
            if actual_names != expected_names:
                raise AssertionError(f"gap prefix {identifier} is not isolated: {actual_names}")
            for record in gap["objects"]:
                target = output_a / record["path"]
                actual_hash = hashlib.sha256(target.read_bytes()).hexdigest()
                if actual_hash != record["sha256"] or actual_hash != record["source_sha256"]:
                    raise AssertionError(f"copied gap object hash differs for {record['path']}")

        for object_record in first["objects"]:
            target = output_a / object_record["path"]
            actual_hash = hashlib.sha256(target.read_bytes()).hexdigest()
            if actual_hash != object_record["sha256"]:
                raise AssertionError(f"manifest does not match generated object hash: {object_record['path']}")

        if not generator.series_fixtures_ready(output_a, FIXTURE_PATH):
            raise AssertionError("a freshly generated fixture tree must count as complete")
        if not generator.series_fixtures_ready(output_b, FIXTURE_PATH):
            raise AssertionError("the second generated fixture tree must count as complete")
        sample = output_b / second["objects"][0]["path"]
        sample.unlink()
        if generator.series_fixtures_ready(output_b, FIXTURE_PATH):
            raise AssertionError("a missing writer object must not count as complete")
        empty_gap = output_a / "data/gaps/all-missing/temperature"
        empty_gap.rmdir()
        if generator.series_fixtures_ready(output_a, FIXTURE_PATH):
            raise AssertionError("the all-missing gap directory is part of the fixture set")
        manifest_path = output_a / "series-fixtures.json"
        stale = json.loads(manifest_path.read_text(encoding="utf-8"))
        stale["fixture_definitions_sha256"] = "0" * 64
        manifest_path.write_text(json.dumps(stale), encoding="utf-8")
        empty_gap.mkdir()
        if generator.series_fixtures_ready(output_a, FIXTURE_PATH):
            raise AssertionError("a fixture tree from a different definition hash must be regenerated")


def main() -> int:
    test_fixture_definitions_and_independent_oracle()
    test_registry_equivalence_fixture_definitions()
    test_generator_reproducibility_hashes_and_gap_isolation()
    print("series fixture reproducibility, hashes, independent oracle, and gap isolation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
