#!/usr/bin/env python3
"""Run source-bound local read_om_series oracle checks and retain their evidence."""

from __future__ import annotations

import argparse
import errno
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
from collections import Counter
from datetime import datetime, timedelta, timezone
from email.utils import parsedate_to_datetime
from pathlib import Path, PurePosixPath
from typing import Any, Sequence
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import Request, urlopen
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
FEATURE = "010-read-om-series"
EXPECTED_SCHEMA = [
    {"name": "value", "type": "FLOAT"},
    {"name": "lat", "type": "DOUBLE"},
    {"name": "lon", "type": "DOUBLE"},
    {"name": "valid_time", "type": "TIMESTAMP"},
]
DIMENSIONS_SQL = "['lead','point','time']"
GRID_SQL = "{'nx':3,'ny':2,'lat0':10.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'lon_fastest'}"
SPATIAL_AXES_SQL = "['point']"


class DeferredTemporaryDirectory:
    """Keep service files available for caller cleanup after an exception."""

    def __init__(self, prefix: str):
        self._temporary = tempfile.TemporaryDirectory(prefix=prefix)

    def __enter__(self) -> str:
        return self._temporary.name

    def __exit__(self, *_: object) -> None:
        return None

    def cleanup(self) -> None:
        self._temporary.cleanup()


class ValidationError(ValueError):
    pass


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_row(row: Any) -> str:
    return json.dumps(row, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)


def safe_relative(value: Any, label: str) -> Path:
    if not isinstance(value, str) or not value:
        raise ValidationError(f"{label} must be a non-empty relative path")
    parsed = PurePosixPath(value)
    if parsed.is_absolute() or ".." in parsed.parts or "\\" in value:
        raise ValidationError(f"{label} must stay inside the fixture root")
    return Path(*parsed.parts)


def load_fixture_inventory(root: Path) -> dict[str, Any]:
    manifest_path = root / "series-fixtures.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValidationError(f"cannot read {manifest_path}: {error}") from error
    if not isinstance(manifest, dict) or manifest.get("feature") != FEATURE:
        raise ValidationError("fixture manifest has the wrong feature identity")
    objects = manifest.get("objects")
    gaps = manifest.get("gap_prefixes")
    if not isinstance(objects, list) or not objects or not isinstance(gaps, list):
        raise ValidationError("fixture manifest must contain objects and gap_prefixes arrays")

    checked_objects: list[dict[str, Any]] = []
    for item in objects:
        if not isinstance(item, dict):
            raise ValidationError("fixture object entries must be JSON objects")
        relative = safe_relative(item.get("path"), "fixture object path")
        checked_objects.append(check_object(root, relative, item))

    checked_gaps: list[dict[str, Any]] = []
    for gap in gaps:
        if not isinstance(gap, dict) or not isinstance(gap.get("objects"), list):
            raise ValidationError("gap entries must contain an objects array")
        checked_gap_objects = []
        for item in gap["objects"]:
            if not isinstance(item, dict):
                raise ValidationError("gap object entries must be JSON objects")
            relative = safe_relative(item.get("path"), "gap object path")
            checked_gap_objects.append(check_object(root, relative, item))
        checked_gaps.append({**gap, "objects": checked_gap_objects})

    by_id = {item.get("id"): item for item in checked_objects}
    adjacent = [by_id.get(f"adjacent_{index:04d}") for index in range(3)]
    if any(item is None for item in adjacent):
        raise ValidationError("fixture inventory must contain adjacent_root chunks 0000, 0001 and 0002")
    if any(item.get("role") != "adjacent_root" for item in adjacent if item is not None):
        raise ValidationError("adjacent chunk identities must use the adjacent_root role")
    if any(item.get("array_paths") != ["/"] for item in adjacent if item is not None):
        raise ValidationError("local S1 oracle currently requires the root-array adjacent fixtures")
    negative = {item.get("chunk_index"): item for item in checked_objects if item.get("role") == "negative_time"}
    if -1 not in negative or 0 not in negative:
        raise ValidationError("local S1 oracle requires negative-time chunks -1 and 0")
    reduced_gaussian = sorted(
        (item for item in checked_objects if item.get("role") == "reduced_gaussian_n160"),
        key=lambda item: item.get("chunk_index", 0),
    )
    if [item.get("chunk_index") for item in reduced_gaussian] != [0, 1, 2]:
        raise ValidationError("local S5 oracle requires reduced-Gaussian N160 chunks 0000, 0001 and 0002")
    if any(item.get("shape") != [1, 138346, 4] or item.get("array_paths") != ["/"]
           for item in reduced_gaussian):
        raise ValidationError("reduced-Gaussian fixtures must retain the synthetic N160 root-array shape")
    if len({tuple(item.get("chunk_shape", [])) for item in reduced_gaussian}) != 3:
        raise ValidationError("reduced-Gaussian fixtures must exercise three physical chunk shapes")

    models_path = ROOT / "test/data/series/models.json"
    if not models_path.is_file():
        raise ValidationError("frozen temporal model registry is missing")
    models_sha = sha256_file(models_path)
    if manifest.get("registry_models_sha256") != models_sha:
        raise ValidationError("registry-equivalence fixture registry hash differs from models.json")
    try:
        model_document = json.loads(models_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ValidationError(f"frozen temporal model registry is invalid: {error}") from error
    model_entries = model_document.get("models") if isinstance(model_document, dict) else None
    if not isinstance(model_entries, list) or len(model_entries) != 11:
        raise ValidationError("SC-004 requires fixtures for every one of the eleven initial model entries")
    registry_equivalence: list[dict[str, Any]] = []
    for model in model_entries:
        key = model.get("key")
        temporal = model.get("producer_source", {})
        length = temporal.get("chunk_time_length", {}).get("value")
        step = temporal.get("temporal_resolution_seconds", {}).get("value")
        item = by_id.get(f"registry_{key}")
        expected_path = f"data/registry-equivalence/{key}/chunk_0000.om"
        provenance = model.get("provenance_status") == "pinned_source_and_archived_metadata_match"
        metadata_snapshot = model.get("metadata_snapshot", {})
        producer_source = temporal.get("path")
        if (not provenance or model.get("discrepancies") or not metadata_snapshot.get("audit_entry_sha256_matches")
                or not isinstance(producer_source, str) or not isinstance(temporal.get("sha256"), str)):
            raise ValidationError(f"model registry lacks reproducible producer and archived metadata provenance for {key}")
        if (item is None or item.get("role") != "registry_equivalence" or item.get("model_key") != key
                or item.get("path") != expected_path or item.get("chunk_time_length") != length
                or item.get("step_seconds") != step or item.get("shape") != [1, 6, length]
                or item.get("array_paths") != ["/"]):
            raise ValidationError(f"registry-equivalence fixture does not match frozen model {key}")
        registry_equivalence.append({
            "key": key, "chunk_time_length": length, "step_seconds": step,
            "path": item["path"], "absolute_path": item["absolute_path"],
            "sha256": item["observed_sha256"], "size_bytes": item["observed_size_bytes"],
            "producer_domain": model.get("producer_domain"),
            "producer_source_path": producer_source,
            "producer_source_sha256": temporal.get("sha256"),
            "chunk_time_length_expression": temporal.get("chunk_time_length", {}).get("source_expression"),
            "step_seconds_expression": temporal.get("temporal_resolution_seconds", {}).get("source_expression"),
            "provenance_status": model.get("provenance_status"),
            "metadata_object_key": model.get("metadata_object_key"),
            "metadata_snapshot_path": metadata_snapshot.get("path"),
            "metadata_snapshot_sha256": metadata_snapshot.get("sha256"),
            "upstream_repository": model_document.get("upstream", {}).get("repository"),
            "upstream_commit": model_document.get("upstream", {}).get("commit"),
        })
    registry_fixture_items = [item for item in checked_objects if item.get("role") == "registry_equivalence"]
    if len(registry_fixture_items) != len(registry_equivalence):
        raise ValidationError("registry-equivalence fixtures contain extra model objects")

    definition_path = ROOT / "test/data/series/fixtures.json"
    expected_definition_hash = manifest.get("fixture_definitions_sha256")
    if not definition_path.is_file() or expected_definition_hash != sha256_file(definition_path):
        raise ValidationError("fixture definitions hash does not match the generated fixture manifest")
    definitions = json.loads(definition_path.read_text(encoding="utf-8"))
    gaussian_definition = definitions.get("reduced_gaussian_series", {})
    if gaussian_definition.get("classification") != "synthetic_only" or \
            gaussian_definition.get("registered_domain") != "n160" or \
            gaussian_definition.get("producer_validation", "").startswith("verified"):
        raise ValidationError("N160 series fixture provenance must remain explicitly synthetic-only")
    return {
        "manifest_path": str(manifest_path.resolve()),
        "manifest_sha256": sha256_file(manifest_path),
        "definition_path": str(definition_path.resolve()),
        "definition_sha256": expected_definition_hash,
        "registry_models_path": str(models_path.resolve()),
        "registry_models_sha256": models_sha,
        "registry_equivalence": registry_equivalence,
        "objects": checked_objects,
        "gap_prefixes": checked_gaps,
        "adjacent": adjacent,
        "reduced_gaussian": reduced_gaussian,
        "n160_explicit_grid_sql_source": str((ROOT / "test/sql/multi_grid.test").resolve()),
        "n160_explicit_grid_sql_source_sha256": sha256_file(ROOT / "test/sql/multi_grid.test"),
    }


def check_object(root: Path, relative: Path, item: dict[str, Any]) -> dict[str, Any]:
    path = root / relative
    if not path.is_file():
        raise ValidationError(f"fixture object is missing: {relative.as_posix()}")
    observed_hash = sha256_file(path)
    observed_size = path.stat().st_size
    if item.get("sha256") != observed_hash or item.get("size_bytes") != observed_size:
        raise ValidationError(f"fixture identity mismatch: {relative.as_posix()}")
    return {**item, "path": relative.as_posix(), "absolute_path": str(path.resolve()),
            "observed_sha256": observed_hash, "observed_size_bytes": observed_size}


def sql_string(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def with_extension_load(sql: str, extension_path: Path) -> str:
    return "LOAD " + sql_string(str(extension_path.expanduser().resolve())) + ";\n" + sql


def sql_timestamp(value: str) -> str:
    return f"TIMESTAMP {sql_string(value)}"


def axes_sql(chunk_index: int) -> str:
    origin_hour = chunk_index * 4
    origin = f"1970-01-01 {origin_hour:02d}:00:00" if origin_hour >= 0 else f"1969-12-31 {24 + origin_hour:02d}:00:00"
    return "{'time': {'axis':'time','start':" + sql_timestamp(origin) + ", 'step':INTERVAL '1 hour'}}"


def child_read_sql(path: str, chunk_index: int, spatial_arguments: str | None = None) -> str:
    spatial = spatial_arguments or ("grid := " + GRID_SQL + ", spatial_axes := " + SPATIAL_AXES_SQL)
    return (
        "SELECT * FROM read_om(" + sql_string(path)
        + ", dimensions := " + DIMENSIONS_SQL
        + ", " + spatial
        + ", axes := " + axes_sql(chunk_index) + ")"
    )


def series_call_sql(prefix: str, start: str, end: str, strict: bool = False,
                    model: str | None = None, chunk_time_length: int | None = 4,
                    time_step_sql: str | None = "INTERVAL '1 hour'",
                    spatial_arguments: str | None = None) -> str:
    arguments = [sql_string(prefix)]
    if model is not None:
        arguments.append("model := " + sql_string(model))
    if chunk_time_length is not None:
        arguments.append(f"chunk_time_length := {chunk_time_length}")
    if time_step_sql is not None:
        arguments.append("time_step := " + time_step_sql)
    spatial = spatial_arguments or ("grid := " + GRID_SQL + ", spatial_axes := " + SPATIAL_AXES_SQL)
    arguments.extend([
        "dimensions := " + DIMENSIONS_SQL,
        spatial,
        "valid_times := [" + sql_timestamp(start) + ", " + sql_timestamp(end) + "]",
        "strict := " + ("true" if strict else "false"),
    ])
    return "SELECT * FROM read_om_series(" + ", ".join(arguments) + ")"


def registry_equivalence_sql(prefix: str, model_key: str, chunk_time_length: int, step_seconds: int) -> str:
    start = datetime(1970, 1, 1, tzinfo=timezone.utc)
    end = start + timedelta(seconds=(chunk_time_length - 1) * step_seconds)
    start_text = start.strftime("%Y-%m-%d %H:%M:%S")
    end_text = end.strftime("%Y-%m-%d %H:%M:%S")
    registered_call = series_call_sql(
        prefix, start_text, end_text, model=model_key,
        chunk_time_length=None, time_step_sql=None,
    )
    explicit_call = series_call_sql(
        prefix, start_text, end_text, model=model_key,
        chunk_time_length=chunk_time_length,
        time_step_sql=f"INTERVAL '{step_seconds} seconds'",
    )
    return f"""CREATE TEMP TABLE registry_registered AS
{registered_call};
CREATE TEMP TABLE registry_registered_metrics AS
SELECT query_id, scan_id, metrics FROM duckomo_last_scan_metrics();
CREATE TEMP TABLE registry_explicit AS
{explicit_call};
CREATE TEMP TABLE registry_explicit_metrics AS
SELECT query_id, scan_id, metrics FROM duckomo_last_scan_metrics();
SELECT to_json(struct_pack(
    model_key := {sql_string(model_key)},
    chunk_time_length := {chunk_time_length},
    step_seconds := {step_seconds},
    expected_count := {chunk_time_length * 6},
    registered_count := (SELECT count(*) FROM registry_registered),
    explicit_count := (SELECT count(*) FROM registry_explicit),
    registered_only_count := (SELECT count(*) FROM
      (SELECT * FROM registry_registered EXCEPT ALL SELECT * FROM registry_explicit)),
    explicit_only_count := (SELECT count(*) FROM
      (SELECT * FROM registry_explicit EXCEPT ALL SELECT * FROM registry_registered)),
    registered_schema := (SELECT list(struct_pack(name := column_name, type := column_type) ORDER BY ordinal_position)
      FROM (SELECT row_number() OVER () AS ordinal_position, * FROM (DESCRIBE SELECT * FROM registry_registered))),
    explicit_schema := (SELECT list(struct_pack(name := column_name, type := column_type) ORDER BY ordinal_position)
      FROM (SELECT row_number() OVER () AS ordinal_position, * FROM (DESCRIBE SELECT * FROM registry_explicit))),
    registered_first_time := (SELECT min(valid_time) FROM registry_registered),
    registered_last_time := (SELECT max(valid_time) FROM registry_registered),
    explicit_first_time := (SELECT min(valid_time) FROM registry_explicit),
    explicit_last_time := (SELECT max(valid_time) FROM registry_explicit),
    registered_scan_metrics := (SELECT list(struct_pack(query_id := query_id, scan_id := scan_id, metrics := metrics)
      ORDER BY scan_id) FROM registry_registered_metrics),
    explicit_scan_metrics := (SELECT list(struct_pack(query_id := query_id, scan_id := scan_id, metrics := metrics)
      ORDER BY scan_id) FROM registry_explicit_metrics),
    engine_version := version()
)) AS validation_result;
"""


def compare_registry_equivalence(payload: dict[str, Any], model: dict[str, Any]) -> dict[str, Any]:
    length = model["chunk_time_length"]
    step = model["step_seconds"]
    expected_start = "1970-01-01 00:00:00"
    expected_end = (datetime(1970, 1, 1, tzinfo=timezone.utc)
                    + timedelta(seconds=(length - 1) * step)).strftime("%Y-%m-%d %H:%M:%S")
    registered_schema = payload.get("registered_schema") or []
    explicit_schema = payload.get("explicit_schema") or []
    schema_equal = registered_schema == explicit_schema == EXPECTED_SCHEMA
    expected_count = length * 6
    counts_equal = payload.get("registered_count") == payload.get("explicit_count") == expected_count
    differences_empty = payload.get("registered_only_count") == 0 and payload.get("explicit_only_count") == 0
    time_bounds_equal = (
        payload.get("registered_first_time") == payload.get("explicit_first_time") == expected_start
        and payload.get("registered_last_time") == payload.get("explicit_last_time") == expected_end
    )
    return {
        "status": "passed" if schema_equal and counts_equal and differences_empty and time_bounds_equal else "failed",
        "model_key": model["key"],
        "chunk_time_length": length,
        "step_seconds": step,
        "schema_equal": schema_equal,
        "registered_schema": registered_schema,
        "explicit_schema": explicit_schema,
        "registered_count": payload.get("registered_count"),
        "explicit_count": payload.get("explicit_count"),
        "expected_count": expected_count,
        "registered_only_count": payload.get("registered_only_count"),
        "explicit_only_count": payload.get("explicit_only_count"),
        "two_way_except_all_empty": differences_empty,
        "time_bounds_equal": time_bounds_equal,
        "expected_first_time": expected_start,
        "expected_last_time": expected_end,
        "registered_first_time": payload.get("registered_first_time"),
        "registered_last_time": payload.get("registered_last_time"),
        "explicit_first_time": payload.get("explicit_first_time"),
        "explicit_last_time": payload.get("explicit_last_time"),
        "registered_scan_metrics": payload.get("registered_scan_metrics") or [],
        "explicit_scan_metrics": payload.get("explicit_scan_metrics") or [],
        "engine_version": payload.get("engine_version"),
    }


def remote_actual_sql(prefix: str, start: str, end: str, strict: bool = False,
                      model: str | None = None, chunk_time_length: int | None = 4,
                      time_step_sql: str | None = "INTERVAL '1 hour'",
                      spatial_arguments: str | None = None,
                      outer_clause: str | None = None) -> str:
    call_sql = series_call_sql(prefix, start, end, strict, model, chunk_time_length, time_step_sql,
                               spatial_arguments)
    if outer_clause:
        call_sql += " " + outer_clause
    return f"""CREATE TEMP TABLE series_actual AS
{call_sql};
CREATE TEMP TABLE series_metrics AS
SELECT query_id, scan_id, metrics FROM duckomo_last_scan_metrics();
SELECT to_json(struct_pack(
    actual_schema := (SELECT list(struct_pack(name := column_name, type := column_type) ORDER BY ordinal_position)
                      FROM (SELECT row_number() OVER () AS ordinal_position, * FROM (DESCRIBE SELECT * FROM series_actual))),
    actual_count := (SELECT count(*) FROM series_actual),
    engine_version := version(),
    actual_rows := (SELECT list(struct_pack(value := value, lat := lat, lon := lon, valid_time := valid_time)
                                ORDER BY valid_time, lat, lon, value NULLS FIRST) FROM series_actual),
    actual_null_values := (SELECT count(*) FROM series_actual WHERE value IS NULL),
    scan_metrics := (SELECT list(struct_pack(query_id := query_id, scan_id := scan_id, metrics := metrics)
                                 ORDER BY scan_id) FROM series_metrics)
)) AS validation_result;
"""


def remote_expected_sql(uri_prefix: str, expected_indices: Sequence[int], start: str, end: str,
                        spatial_arguments: str | None = None) -> str:
    if expected_indices:
        children = [child_read_sql(
            uri_prefix.rstrip("/") + f"/chunk_{index:04d}.om", index, spatial_arguments
        ) for index in expected_indices]
        expected_relation = "SELECT * FROM (" + " UNION ALL ".join(children) + ") manual_children"
    else:
        expected_relation = (
            "SELECT CAST(NULL AS FLOAT) AS value, CAST(NULL AS DOUBLE) AS lat, "
            "CAST(NULL AS DOUBLE) AS lon, CAST(NULL AS TIMESTAMP) AS valid_time WHERE false"
        )
    return f"""CREATE TEMP TABLE series_expected AS
SELECT * FROM ({expected_relation}) expected_children
WHERE valid_time BETWEEN {sql_timestamp(start)} AND {sql_timestamp(end)};
SELECT to_json(struct_pack(
    expected_schema := (SELECT list(struct_pack(name := column_name, type := column_type) ORDER BY ordinal_position)
                        FROM (SELECT row_number() OVER () AS ordinal_position, * FROM (DESCRIBE SELECT * FROM series_expected))),
    expected_count := (SELECT count(*) FROM series_expected),
    expected_rows := (SELECT list(struct_pack(value := value, lat := lat, lon := lon, valid_time := valid_time)
                                  ORDER BY valid_time, lat, lon, value NULLS FIRST) FROM series_expected),
    expected_null_values := (SELECT count(*) FROM series_expected WHERE value IS NULL)
)) AS validation_result;
"""


def compare_remote_payloads(actual: dict[str, Any], expected: dict[str, Any]) -> dict[str, Any]:
    actual_schema = actual.get("actual_schema") or []
    expected_schema = expected.get("expected_schema") or []
    actual_rows = actual.get("actual_rows") or []
    expected_rows = expected.get("expected_rows") or []
    actual_multiset = Counter(canonical_row(row) for row in actual_rows)
    expected_multiset = Counter(canonical_row(row) for row in expected_rows)
    schema_equal = actual_schema == expected_schema == EXPECTED_SCHEMA
    equal = actual_multiset == expected_multiset
    return {
        "status": "passed" if schema_equal and equal else "failed",
        "schema_equal": schema_equal,
        "python_multiset_equal": equal,
        "sql_two_way_except_all_empty": None,
        "actual_count": actual.get("actual_count"),
        "expected_count": expected.get("expected_count"),
        "actual_null_values": actual.get("actual_null_values"),
        "expected_null_values": expected.get("expected_null_values"),
        "actual_schema": actual_schema,
        "expected_schema": expected_schema,
        "actual_rows": actual_rows,
        "expected_rows": expected_rows,
        "python_differences": {
            "actual_only": list((actual_multiset - expected_multiset).elements()),
            "expected_only": list((expected_multiset - actual_multiset).elements()),
        },
        "scan_metrics": actual.get("scan_metrics") or [],
    }


def summarize_remote_audit(events: Sequence[dict[str, Any]], planned_keys: Sequence[str]) -> dict[str, Any]:
    responses = [event for event in events if event.get("event") == "response"]
    allowed = set(planned_keys)
    list_events = [event for event in responses if event.get("list_request")]
    metadata_events = [event for event in responses if event.get("object") == "<unknown>"
                       or event.get("metadata_request")
                       or str(event.get("object", "")).lower().endswith("/meta.json")]
    witness_events = [event for event in responses if event.get("object") not in allowed
                      and event.get("object") not in {"<bucket-list>", "<unknown>"}]
    objects = sorted({str(event.get("object")) for event in responses})
    return {
        "response_count": len(responses),
        "object_keys": objects,
        "list_request_count": len(list_events),
        "metadata_or_unclassified_request_count": len(metadata_events),
        "witness_request_count": len(witness_events),
        "no_list_requests": not list_events,
        "no_metadata_or_unclassified_requests": not metadata_events,
        "no_witness_requests": not witness_events,
        "all_requests_within_planned_candidates": not witness_events and not metadata_events,
        "statuses": [event.get("status") for event in responses],
        "faults": [event.get("fault") for event in responses if event.get("fault")],
    }


def summarize_child_activity(planned_keys: Sequence[str], attempted_keys: Sequence[str],
                             events: Sequence[dict[str, Any]], scan_metrics: Sequence[dict[str, Any]],
                             query_invocations: int = 1,
                             bind_failure_keys: Sequence[str] = ()) -> dict[str, Any]:
    responses = [event for event in events if event.get("event") == "response"]
    bound_statuses = {200, 206, 304}
    successful_keys = {event.get("object") for event in responses if event.get("status") in bound_statuses}
    failed_bind_keys = set(bind_failure_keys)
    bound_keys = [key for key in attempted_keys if key in successful_keys and key not in failed_bind_keys]
    failure_signatures: Counter[tuple[Any, ...]] = Counter()
    for event in responses:
        status = event.get("status")
        if status not in bound_statuses:
            signature = (event.get("transport"), event.get("object"), event.get("method"),
                         event.get("range"), status, event.get("fault"))
            failure_signatures[signature] += 1
    repeated_failures = sum(count - 1 for count in failure_signatures.values() if count > 1)
    scan_ids: list[Any] = []
    nonexecuted_scan_ids: list[Any] = []
    for item in scan_metrics:
        if not isinstance(item, dict):
            continue
        raw_metric = item.get("metrics", {})
        if isinstance(raw_metric, str):
            try:
                metric = json.loads(raw_metric)
            except json.JSONDecodeError:
                metric = {}
        elif isinstance(raw_metric, dict):
            metric = raw_metric
        else:
            metric = {}
        reads = metric.get("reads", {}) if isinstance(metric, dict) else {}
        metadata = reads.get("metadata", {}) if isinstance(reads, dict) else {}
        coordinates = reads.get("coordinates", {}) if isinstance(reads, dict) else {}
        values = reads.get("value_totals", {}) if isinstance(reads, dict) else {}
        scan_io = any((metadata.get("scan_requests", 0), coordinates.get("physical_requests", 0),
                       values.get("index_requests", 0), values.get("data_requests", 0)))
        complete = metric.get("outcome", {}).get("scan_complete") is True if isinstance(metric, dict) else False
        target = scan_ids if complete or scan_io else nonexecuted_scan_ids
        target.append(item.get("scan_id"))
    return {
        "planned_children": list(planned_keys),
        "attempted_children": list(attempted_keys),
        "bound_children_inferred_from_successful_origin_responses": bound_keys,
        "scan_metric_count": len(scan_ids) + len(nonexecuted_scan_ids),
        "nonexecuted_scan_metric_ids": nonexecuted_scan_ids,
        "executed_child_scan_count": len(scan_ids),
        "executed_scan_ids": scan_ids,
        "execution_identity_limit": "existing scan metrics do not expose child URIs",
        "transport_activity": {
            "origin_request_count": len(responses),
            "repeated_failed_request_count_candidate_for_transport_retry": repeated_failures,
            "series_query_invocations": query_invocations,
            "series_restarts": max(0, query_invocations - 1),
            "classification": "repeated failed origin requests are reported separately; the audit cannot identify HTTPFS retry intent",
        },
    }


def summarize_post_bind_mutation(action: str, target: str,
                                 bound_events: Sequence[dict[str, Any]],
                                 post_events: Sequence[dict[str, Any]],
                                 exit_code: int, diagnostic: str) -> dict[str, Any]:
    bound_etag = next((event.get("etag") for event in bound_events
                       if event.get("event") == "response" and event.get("object") == target
                       and event.get("etag")), None)
    target_responses = [event for event in post_events
                        if event.get("event") == "response" and event.get("object") == target]
    if action == "remove":
        expected_response = any(event.get("status") == 404 for event in target_responses)
        post_etag = next((event.get("etag") for event in target_responses if event.get("etag")), None)
        identity_changed = False
        diagnostic_matches = exit_code != 0 and bool(diagnostic.strip())
        expected_status: int | str = 404
        expected_diagnostic = "post-bind object-not-found read error"
    elif action == "replace":
        replacement_response = next((event for event in target_responses
                                     if event.get("state") == "replacement" and event.get("status") == 200), None)
        post_etag = replacement_response.get("etag") if replacement_response else None
        identity_changed = bool(bound_etag and post_etag and bound_etag != post_etag)
        expected_response = replacement_response is not None and identity_changed
        diagnostic_matches = (exit_code != 0 and
                              re.search(r"object identity changed", diagnostic, re.IGNORECASE) is not None)
        expected_status = "HTTP 200 replacement HEAD with changed ETag"
        expected_diagnostic = "object identity changed"
    else:
        raise ValidationError(f"unsupported post-bind mutation: {action}")
    passed = expected_response and diagnostic_matches
    return {
        "passed": passed,
        "expected_post_mutation_status": expected_status,
        "observed_post_mutation_statuses": [event.get("status") for event in target_responses],
        "observed_expected_post_mutation_response": expected_response,
        "bound_etag": bound_etag,
        "post_mutation_etag": post_etag,
        "identity_changed": identity_changed,
        "expected_diagnostic": expected_diagnostic,
        "diagnostic_matches": diagnostic_matches,
    }


def read_audit_since(path: Path, offset: int) -> tuple[list[dict[str, Any]], int]:
    events: list[dict[str, Any]] = []
    if not path.is_file():
        return events, offset
    with path.open("rb") as stream:
        stream.seek(offset)
        for raw in stream:
            if raw.strip():
                events.append(json.loads(raw))
        return events, stream.tell()


def child_uri_key(prefix: str, index: int) -> str:
    return prefix.rstrip("/") + f"/chunk_{index:04d}.om"


def redact_remote_command(command: dict[str, Any], private_values: Sequence[str]) -> dict[str, Any]:
    for field in ("stdout", "stderr"):
        text = command.get(field, "")
        for value in private_values:
            if value:
                text = text.replace(value, "<redacted>")
        command[field] = text
    return command


def scenario_sql(name: str, prefix: Path, start: str, end: str,
                 candidate_indices: Sequence[int], endpoint_form: str = "valid_times") -> str:
    if endpoint_form == "valid_times":
        window_sql = "valid_times := [" + sql_timestamp(start) + ", " + sql_timestamp(end) + "]"
    elif endpoint_form == "start_end":
        window_sql = 'start := ' + sql_timestamp(start) + ', "end" := ' + sql_timestamp(end)
    else:
        raise ValidationError(f"unsupported endpoint form: {endpoint_form}")
    actual_sql = (
        "SELECT * FROM read_om_series(" + sql_string(str(prefix.resolve()))
        + ", chunk_time_length := 4, time_step := INTERVAL '1 hour'"
        + ", dimensions := " + DIMENSIONS_SQL
        + ", grid := " + GRID_SQL
        + ", spatial_axes := " + SPATIAL_AXES_SQL
        + ", " + window_sql + ")"
    )
    expected_children = []
    for index in candidate_indices:
        chunk = prefix / f"chunk_{index:04d}.om" if index >= 0 else prefix / f"chunk_{index:04d}.om"
        expected_children.append(child_read_sql(str(chunk.resolve()), index))
    expected_sql = " UNION ALL ".join(expected_children)
    if not expected_children:
        raise ValidationError("independent oracle candidate list cannot be empty")
    window_filter = (
        " WHERE valid_time BETWEEN " + sql_timestamp(start) + " AND " + sql_timestamp(end)
    )
    columns = "value, lat, lon, valid_time"
    return f"""-- Scenario: {name}
WITH actual AS (
    {actual_sql}
), expected AS (
    SELECT * FROM ({expected_sql}) manual_children{window_filter}
), actual_only AS (
    SELECT {columns} FROM actual EXCEPT ALL SELECT {columns} FROM expected
), expected_only AS (
    SELECT {columns} FROM expected EXCEPT ALL SELECT {columns} FROM actual
)
SELECT to_json(struct_pack(
    scenario := {sql_string(name)},
    engine_version := version(),
    actual_schema := (SELECT list(struct_pack(name := column_name, type := column_type) ORDER BY ordinal_position)
                      FROM (SELECT row_number() OVER () AS ordinal_position, * FROM (DESCRIBE SELECT * FROM actual))),
    expected_schema := (SELECT list(struct_pack(name := column_name, type := column_type) ORDER BY ordinal_position)
                        FROM (SELECT row_number() OVER () AS ordinal_position, * FROM (DESCRIBE SELECT * FROM expected))),
    actual_count := (SELECT count(*) FROM actual),
    expected_count := (SELECT count(*) FROM expected),
    actual_rows := (SELECT list(struct_pack(value := value, lat := lat, lon := lon, valid_time := valid_time)
                                ORDER BY valid_time, lat, lon, value NULLS FIRST) FROM actual),
    expected_rows := (SELECT list(struct_pack(value := value, lat := lat, lon := lon, valid_time := valid_time)
                                  ORDER BY valid_time, lat, lon, value NULLS FIRST) FROM expected),
    actual_only_rows := (SELECT list(struct_pack(value := value, lat := lat, lon := lon, valid_time := valid_time)
                                     ORDER BY valid_time, lat, lon, value NULLS FIRST) FROM actual_only),
    expected_only_rows := (SELECT list(struct_pack(value := value, lat := lat, lon := lon, valid_time := valid_time)
                                       ORDER BY valid_time, lat, lon, value NULLS FIRST) FROM expected_only),
    actual_null_values := (SELECT count(*) FROM actual WHERE value IS NULL),
    expected_null_values := (SELECT count(*) FROM expected WHERE value IS NULL)
)) AS validation_result;
"""


def parse_cli_result(stdout: str) -> dict[str, Any]:
    try:
        rows = json.loads(stdout)
    except json.JSONDecodeError as error:
        raise ValidationError(f"DuckDB CLI did not return JSON output: {error}") from error
    if not isinstance(rows, list) or len(rows) != 1 or not isinstance(rows[0], dict):
        raise ValidationError("DuckDB CLI returned an unexpected JSON result shape")
    raw = rows[0].get("validation_result")
    if isinstance(raw, dict):
        return raw
    if not isinstance(raw, str):
        raise ValidationError("DuckDB CLI result is missing validation_result JSON")
    try:
        payload = json.loads(raw)
    except json.JSONDecodeError as error:
        raise ValidationError(f"validation_result is not valid JSON: {error}") from error
    if not isinstance(payload, dict):
        raise ValidationError("validation_result must be a JSON object")
    return payload


def parse_cli_result_with_preface(stdout: str) -> dict[str, Any]:
    try:
        return parse_cli_result(stdout)
    except ValidationError as original_error:
        lines = stdout.splitlines()
        for index, line in enumerate(lines):
            if line.lstrip().startswith("["):
                try:
                    return parse_cli_result("\n".join(lines[index:]))
                except ValidationError:
                    continue
        raise original_error


def compare_payload(payload: dict[str, Any]) -> dict[str, Any]:
    actual_schema = payload.get("actual_schema") or []
    expected_schema = payload.get("expected_schema") or []
    actual_rows = payload.get("actual_rows") or []
    expected_rows = payload.get("expected_rows") or []
    actual_only = payload.get("actual_only_rows") or []
    expected_only = payload.get("expected_only_rows") or []
    schema_equal = actual_schema == expected_schema == EXPECTED_SCHEMA
    actual_multiset = Counter(canonical_row(row) for row in actual_rows)
    expected_multiset = Counter(canonical_row(row) for row in expected_rows)
    python_differences = {
        "actual_only": list((actual_multiset - expected_multiset).elements()),
        "expected_only": list((expected_multiset - actual_multiset).elements()),
    }
    sql_differences_empty = not actual_only and not expected_only
    passed = (
        schema_equal
        and actual_multiset == expected_multiset
        and sql_differences_empty
        and payload.get("actual_count") == payload.get("expected_count")
        and payload.get("actual_null_values") == payload.get("expected_null_values")
    )
    return {
        "status": "passed" if passed else "failed",
        "schema_equal": schema_equal,
        "sql_two_way_except_all_empty": sql_differences_empty,
        "python_multiset_equal": actual_multiset == expected_multiset,
        "actual_count": payload.get("actual_count"),
        "expected_count": payload.get("expected_count"),
        "actual_null_values": payload.get("actual_null_values"),
        "expected_null_values": payload.get("expected_null_values"),
        "python_differences": python_differences,
        "actual_only_rows": actual_only,
        "expected_only_rows": expected_only,
        "actual_schema": actual_schema,
        "expected_schema": expected_schema,
        "actual_rows": actual_rows,
        "expected_rows": expected_rows,
        "engine_version": payload.get("engine_version"),
    }


def run_command(argv: Sequence[str], cwd: Path, timeout: int = 120,
                env: dict[str, str] | None = None) -> dict[str, Any]:
    started_at = utc_now()
    start_clock = time.monotonic()
    completed = subprocess.run(list(argv), cwd=cwd, text=True, capture_output=True,
                               timeout=timeout, check=False, env=env)
    return {
        "argv": list(argv),
        "cwd": str(cwd),
        "started_at_utc": started_at,
        "ended_at_utc": utc_now(),
        "elapsed_seconds": round(time.monotonic() - start_clock, 3),
        "exit_code": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
    }


def n160_explicit_grid_macros_sql() -> str:
    source_path = ROOT / "test/sql/multi_grid.test"
    source = source_path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"^statement ok\s*\n(CREATE MACRO n160_(?:row_latitudes|row_counts|explicit_grid)\(\).*?;)\s*$",
        re.MULTILINE | re.DOTALL,
    )
    definitions = pattern.findall(source)
    if len(definitions) != 3:
        raise ValidationError("could not locate the three frozen N160 explicit-grid SQL macros")
    return "\n".join(definitions)


def n160_reduced_gaussian_matrix_sql(prefix: str, worker_count: int) -> str:
    prefix = prefix.rstrip("/")
    explicit_spatial = "grid := n160_explicit_grid(), spatial_axes := ['point']"
    series_spatial = "domain := 'n160'"
    start = "1970-01-01 00:00:00"
    end = "1970-01-01 04:00:00"
    children = [child_read_sql(f"{prefix}/chunk_{index:04d}.om", index, explicit_spatial)
                for index in (0, 1)]
    manual_relation = "SELECT * FROM (" + " UNION ALL ".join(children) + ") manual_children"
    manual_window = (
        f"valid_time BETWEEN {sql_timestamp(start)} AND {sql_timestamp(end)}"
    )
    actual_call = (
        "read_om_series(" + sql_string(prefix)
        + ", chunk_time_length := 4, time_step := INTERVAL '1 hour'"
        + ", dimensions := " + DIMENSIONS_SQL + ", " + series_spatial
        + ", valid_times := [" + sql_timestamp(start) + ", " + sql_timestamp(end) + "] )"
    )
    filtered_rows = "lat BETWEEN -0.3 AND -0.2 AND value > 0"
    value_free_filter = "TRUE"
    return n160_explicit_grid_macros_sql() + f"""
SET threads={worker_count};
SET duckomo_max_threads={worker_count};
CREATE TEMP TABLE series_actual AS
SELECT value, lat, lon, valid_time FROM {actual_call} WHERE {filtered_rows};
CREATE TEMP TABLE series_value_metrics AS
SELECT query_id, scan_id, metrics FROM duckomo_last_scan_metrics();
CREATE TEMP TABLE series_value_free_count AS
SELECT count(*) AS row_count FROM {actual_call} WHERE {value_free_filter};
CREATE TEMP TABLE series_value_free_metrics AS
SELECT query_id, scan_id, metrics FROM duckomo_last_scan_metrics();
CREATE TEMP TABLE manual_expected AS
SELECT value, lat, lon, valid_time FROM ({manual_relation}) expected_children
WHERE {manual_window} AND {filtered_rows};
CREATE TEMP TABLE manual_value_free_count AS
SELECT count(*) AS row_count FROM ({manual_relation}) expected_children
WHERE {manual_window} AND {value_free_filter};
SELECT to_json(struct_pack(
    actual_schema := (SELECT list(struct_pack(name := column_name, type := column_type) ORDER BY ordinal_position)
                      FROM (SELECT row_number() OVER () AS ordinal_position, * FROM (DESCRIBE SELECT * FROM series_actual))),
    expected_schema := (SELECT list(struct_pack(name := column_name, type := column_type) ORDER BY ordinal_position)
                        FROM (SELECT row_number() OVER () AS ordinal_position, * FROM (DESCRIBE SELECT * FROM manual_expected))),
    actual_count := (SELECT count(*) FROM series_actual),
    expected_count := (SELECT count(*) FROM manual_expected),
    actual_rows := (SELECT list(struct_pack(value := value, lat := lat, lon := lon, valid_time := valid_time)
                                ORDER BY valid_time, lat, lon, value NULLS FIRST) FROM series_actual),
    expected_rows := (SELECT list(struct_pack(value := value, lat := lat, lon := lon, valid_time := valid_time)
                                  ORDER BY valid_time, lat, lon, value NULLS FIRST) FROM manual_expected),
    actual_only_rows := (SELECT list(struct_pack(value := value, lat := lat, lon := lon, valid_time := valid_time)
                                     ORDER BY valid_time, lat, lon, value NULLS FIRST)
                         FROM (SELECT * FROM series_actual EXCEPT ALL SELECT * FROM manual_expected)),
    expected_only_rows := (SELECT list(struct_pack(value := value, lat := lat, lon := lon, valid_time := valid_time)
                                       ORDER BY valid_time, lat, lon, value NULLS FIRST)
                           FROM (SELECT * FROM manual_expected EXCEPT ALL SELECT * FROM series_actual)),
    actual_null_values := (SELECT count(*) FROM series_actual WHERE value IS NULL),
    expected_null_values := (SELECT count(*) FROM manual_expected WHERE value IS NULL),
    value_free_count := (SELECT row_count FROM series_value_free_count),
    expected_value_free_count := (SELECT row_count FROM manual_value_free_count),
    series_value_metrics := (SELECT list(struct_pack(query_id := query_id, scan_id := scan_id, metrics := metrics)
                                         ORDER BY scan_id) FROM series_value_metrics),
    series_value_free_metrics := (SELECT list(struct_pack(query_id := query_id, scan_id := scan_id, metrics := metrics)
                                              ORDER BY scan_id) FROM series_value_free_metrics),
    engine_version := version()
)) AS validation_result;
"""


def local_reduced_gaussian_matrix_sql(prefix: Path, worker_count: int) -> str:
    return n160_reduced_gaussian_matrix_sql(str(prefix.resolve()), worker_count)


def summarize_local_scan_metrics(records: Sequence[dict[str, Any]], expected_count: int,
                                 require_value_reads: bool, worker_cap: int | None = None) -> dict[str, Any]:
    parsed: list[dict[str, Any]] = []
    for record in records:
        raw = record.get("metrics", {}) if isinstance(record, dict) else {}
        if isinstance(raw, str):
            try:
                metric = json.loads(raw)
            except json.JSONDecodeError:
                metric = {}
        elif isinstance(raw, dict):
            metric = raw
        else:
            metric = {}
        reads = metric.get("reads", {}) if isinstance(metric, dict) else {}
        metadata = reads.get("metadata", {}) if isinstance(reads, dict) else {}
        values = reads.get("value_totals", {}) if isinstance(reads, dict) else {}
        value_activity = any(values.get(key, 0) for key in (
            "index_bytes", "index_requests", "data_bytes", "data_requests", "decoded_chunks"))
        legacy = metric.get("legacy_v3", {}) if isinstance(metric, dict) else {}
        tasks = legacy.get("tasks", {}) if isinstance(legacy, dict) else {}
        memory = metric.get("memory", {}) if isinstance(metric, dict) else {}
        max_active_workers = tasks.get("max_active_workers") if isinstance(tasks, dict) else None
        memory_peak = memory.get("query_owned_peak_bytes") if isinstance(memory, dict) else None
        memory_upper_bound = memory.get("query_owned_upper_bound_peak_bytes") if isinstance(memory, dict) else None
        parsed.append({
            "query_id": record.get("query_id"),
            "scan_id": record.get("scan_id"),
            "scan_complete": metric.get("outcome", {}).get("scan_complete") is True,
            "metadata_scan_requests": metadata.get("scan_requests", 0),
            "value_totals": values,
            "value_activity": value_activity,
            "max_active_workers": max_active_workers,
            "worker_cap_respected": (worker_cap is None or
                                     (isinstance(max_active_workers, int) and 0 < max_active_workers <= worker_cap)),
            "query_owned_peak_bytes": memory_peak,
            "query_owned_upper_bound_peak_bytes": memory_upper_bound,
            "query_memory_bound_respected": (isinstance(memory_peak, int)
                                              and isinstance(memory_upper_bound, int)
                                              and memory_peak <= memory_upper_bound),
        })
    scan_ids = [item.get("scan_id") for item in parsed]
    value_expectation_met = all(item["value_activity"] == require_value_reads for item in parsed)
    worker_caps_met = all(item["worker_cap_respected"] for item in parsed)
    memory_bounds_recorded = all(item["query_memory_bound_respected"] for item in parsed)
    completed = len(parsed) == expected_count and all(item["scan_complete"] for item in parsed)
    distinct = len(set(scan_ids)) == len(scan_ids)
    return {
        "status": "passed" if completed and distinct and value_expectation_met and worker_caps_met
        and memory_bounds_recorded else "failed",
        "scan_count": len(parsed),
        "scan_ids": scan_ids,
        "distinct_scan_ids": distinct,
        "all_scan_complete": all(item["scan_complete"] for item in parsed),
        "all_children_match_value_read_expectation": value_expectation_met,
        "all_children_respect_worker_cap": worker_caps_met,
        "worker_cap": worker_cap,
        "all_query_memory_peaks_within_recorded_upper_bounds": memory_bounds_recorded,
        "require_value_reads": require_value_reads,
        "children": parsed,
    }


def local_scenarios(fixtures: dict[str, Any]) -> list[dict[str, Any]]:
    adjacent_prefix = Path(fixtures["adjacent"][0]["absolute_path"]).parent
    negative_by_index = {
        int(item["chunk_index"]): item
        for item in fixtures["objects"] if item.get("role") == "negative_time"
    }
    negative_prefix = Path(negative_by_index[-1]["absolute_path"]).parent
    return [
        {"name": "one_chunk", "prefix": adjacent_prefix, "start": "1970-01-01 01:00:00",
         "end": "1970-01-01 02:00:00", "indices": [0]},
        {"name": "single_boundary_slot", "prefix": adjacent_prefix, "start": "1970-01-01 04:00:00",
         "end": "1970-01-01 04:00:00", "indices": [1]},
        {"name": "unaligned_cross_boundary", "prefix": adjacent_prefix, "start": "1970-01-01 03:00:00",
         "end": "1970-01-01 05:00:00", "indices": [0, 1]},
        {"name": "three_chunks_and_null", "prefix": adjacent_prefix, "start": "1970-01-01 00:00:00",
         "end": "1970-01-01 08:00:00", "indices": [0, 1, 2]},
        {"name": "alternate_start_end", "prefix": adjacent_prefix, "start": "1970-01-01 03:00:00",
         "end": "1970-01-01 05:00:00", "indices": [0, 1], "endpoint_form": "start_end"},
        {"name": "negative_epoch", "prefix": negative_prefix, "start": "1969-12-31 23:00:00",
         "end": "1970-01-01 00:00:00", "indices": [-1, 0]},
        {"name": "slotless_subsecond_window", "prefix": adjacent_prefix,
         "start": "1970-01-01 03:00:00.000001", "end": "1970-01-01 03:59:59.999999",
         "indices": [0]},
    ]


def summarize_extension_load(trace_text: str, extension_path: Path, exit_code: int) -> dict[str, Any]:
    expected_path = str(extension_path.expanduser().resolve())
    opened_exact_extension = expected_path in trace_text
    if exit_code != 0:
        status = "failed"
        summary = "DuckDB could not load and query the requested extension"
    elif not opened_exact_extension:
        status = "blocked"
        summary = "the CLI did not open the requested extension file; it may use a statically registered copy"
    else:
        status = "passed"
        summary = "strace observed DuckDB opening the exact requested extension file"
    return {"status": status, "summary": summary, "expected_extension_path": expected_path,
            "opened_exact_extension": opened_exact_extension, "command_exit_code": exit_code}


def verify_extension_load(args: argparse.Namespace, manifest: dict[str, Any], output: Path) -> dict[str, Any]:
    strace = shutil.which("strace")
    if not strace:
        return {"status": "blocked", "summary": "strace is unavailable; exact extension loading cannot be verified",
                "expected_extension_path": str(args.extension.expanduser().resolve()),
                "opened_exact_extension": False, "command_exit_code": None}
    trace_path = output / "extension-load.strace"
    command = [strace, "-f", "-qq", "-e", "trace=openat,openat2", "-o", str(trace_path), "--",
               str(args.duckdb.expanduser().resolve()), "-no-init", "-unsigned", "-bail", ":memory:", "-c",
               "LOAD " + sql_string(str(args.extension.expanduser().resolve()))
               + "; SELECT count(*) FROM duckomo_last_scan_metrics();"]
    execution = run_command(command, ROOT)
    execution["scenario"] = "verify_explicit_extension_load"
    execution["trace_path"] = str(trace_path)
    manifest["commands"].append(execution)
    trace_text = trace_path.read_text(encoding="utf-8", errors="replace") if trace_path.is_file() else ""
    result = summarize_extension_load(trace_text, args.extension, execution["exit_code"])
    result.update({"trace_path": str(trace_path) if trace_path.is_file() else None,
                   "trace_sha256": sha256_file(trace_path) if trace_path.is_file() else None})
    return result


def execute_local_reduced_gaussian_matrix(args: argparse.Namespace, fixtures: dict[str, Any],
                                          manifest: dict[str, Any], output: Path) -> tuple[str, str]:
    prefix = Path(fixtures["reduced_gaussian"][0]["absolute_path"]).parent
    all_row_checks = True
    all_metric_checks = True
    for worker_count in (1, 2, 4):
        name = f"s5-n160-domain-vs-explicit-worker-{worker_count}"
        query = local_reduced_gaussian_matrix_sql(prefix, worker_count)
        query_path = output / "queries" / f"{name}.sql"
        execution_sql = with_extension_load(query, args.extension)
        query_path.write_text(execution_sql, encoding="utf-8")
        command = [str(args.duckdb.resolve()), "-no-init", "-unsigned", "-json", "-bail",
                   ":memory:", "-f", str(query_path)]
        execution = run_command(command, ROOT)
        execution.update({"scenario": name, "sql_path": str(query_path),
                          "sql_sha256": hashlib.sha256(execution_sql.encode()).hexdigest(),
                          "query_sha256": hashlib.sha256(query.encode()).hexdigest(),
                          "worker_count": worker_count})
        manifest["commands"].append(execution)
        if execution["exit_code"] != 0:
            result = {"status": "failed", "summary": "DuckDB command exited non-zero",
                      "exit_code": execution["exit_code"], "stderr": execution["stderr"]}
            row_metrics = {"status": "failed", "summary": "query produced no metric payload"}
            count_metrics = {"status": "failed", "summary": "query produced no metric payload"}
        else:
            try:
                payload = parse_cli_result_with_preface(execution["stdout"])
                result = compare_payload(payload)
                value_count_equal = payload.get("value_free_count") == payload.get("expected_value_free_count")
                result.update({
                    "summary": "N160 domain series matches explicit-grid manual UNION ALL",
                    "value_free_count_equal": value_count_equal,
                    "value_free_count": payload.get("value_free_count"),
                    "expected_value_free_count": payload.get("expected_value_free_count"),
                    "engine_version": payload.get("engine_version"),
                    "grid_evidence_scope": "synthetic_only; producer N160 validation not claimed",
                    "explicit_grid_sql_source_sha256": fixtures["n160_explicit_grid_sql_source_sha256"],
                })
                row_metrics = summarize_local_scan_metrics(payload.get("series_value_metrics") or [], 2, True,
                                                           worker_count)
                count_metrics = summarize_local_scan_metrics(payload.get("series_value_free_metrics") or [], 2, False,
                                                             worker_count)
                if not value_count_equal:
                    result["status"] = "failed"
                if row_metrics["status"] != "passed" or count_metrics["status"] != "passed":
                    result["status"] = "failed"
            except ValidationError as error:
                result = {"status": "failed", "summary": str(error)}
                row_metrics = {"status": "failed", "summary": "payload comparison failed"}
                count_metrics = {"status": "failed", "summary": "payload comparison failed"}
        result["worker_count"] = worker_count
        result["value_scan_metrics"] = row_metrics
        result["value_free_count_metrics"] = count_metrics
        manifest["scenarios"][name] = result
        write_json(output / "scenarios" / f"{name}.json", result)
        all_row_checks &= result.get("status") == "passed"
        all_metric_checks &= row_metrics.get("status") == "passed" and count_metrics.get("status") == "passed"
    return ("passed" if all_row_checks else "failed", "passed" if all_metric_checks else "failed")


def parse_sql_logic_error_cases(text: str, name_prefix: str) -> list[dict[str, str]]:
    cases: list[dict[str, str]] = []
    blocks = re.split(r"(?m)^statement error\s*$", text)
    for block in blocks[1:]:
        sql, separator, expected = block.partition("\n----\n")
        if not separator:
            continue
        expected_line = next((line.strip() for line in expected.splitlines() if line.strip()), "")
        if expected_line.startswith("<REGEX>:"):
            error_pattern = expected_line[len("<REGEX>:"):]
        else:
            error_pattern = re.escape(expected_line)
        if not sql.strip() or not error_pattern:
            continue
        case_id = f"{name_prefix}_{len(cases) + 1:03d}"
        cases.append({"case_id": case_id, "sql": sql.strip(),
                      "expected_error": expected_line, "error_pattern": error_pattern})
    return cases


def s2_candidate_prefix(sql: str) -> Path:
    match = re.search(r"read_om_series\s*\(\s*'((?:''|[^'])*)'", sql, re.IGNORECASE | re.DOTALL)
    if not match:
        raise ValidationError("S2 SQL case does not have a literal read_om_series prefix")
    raw_prefix = match.group(1).replace("''", "'")
    prefix = Path(raw_prefix)
    if not prefix.is_absolute():
        prefix = ROOT / prefix
    return prefix.resolve()


def s2_case_label(case: dict[str, str]) -> tuple[str, list[int]]:
    sql = case["sql"]
    prefix = s2_candidate_prefix(sql)
    value = str(prefix)
    if "/mismatch/time-extent/" in value:
        return "time_extent_across_chunks", [0, 1]
    if "/mismatch/time-extent/" in value:
        return "time_extent_across_chunks", [0, 1]
    if "/mismatch/point-extent/" in value:
        return "point_extent_across_chunks", [0, 1]
    if "/mismatch/embedded-time/" in value:
        suffix = "slotless" if "start :=" in sql else "selected"
        return "embedded_time_conflict_" + suffix, [0]
    for directory, label in (
        ("/mismatch/declared-type/", "declared_type"),
        ("/mismatch/dimension-order/", "dimension_order"),
        ("/mismatch/spatial-profile/", "spatial_profile"),
        ("/mismatch/grid-identity/", "grid_identity"),
    ):
        if directory in value:
            return label, [0]
    return case["case_id"], [0]


def summarize_s2_access_trace(trace_text: str) -> dict[str, Any]:
    candidate_events = [line.strip() for line in trace_text.splitlines()
                        if "chunk_" in line and ".om" in line]
    candidate_paths = sorted(set(path for line in candidate_events
                                 for path in re.findall(r'"([^"\n]*chunk_[^"\n]*\.om)"', line)))
    network_events = [line.strip() for line in trace_text.splitlines()
                      if re.search(r"\b(?:connect|sendto|sendmsg)\(.*AF_INET6?", line)]
    return {"candidate_events": candidate_events, "candidate_paths": candidate_paths,
            "network_events": network_events}


def run_s2_validation_corpus(args: argparse.Namespace, extension_path: Path,
                             fixture_inventory: dict[str, Any], output: Path,
                             manifest: dict[str, Any]) -> dict[str, Any]:
    source_path = ROOT / "test/sql/read_om_series_validation.test"
    source = source_path.read_text(encoding="utf-8")
    metadata_marker = "# S2 metadata-dependent mismatches open the stated present candidate"
    pure_marker = "# Existing candidates are bound and reconciled before the logical result can execute."
    if metadata_marker not in source or pure_marker not in source:
        raise ValidationError("read_om_series_validation.test is missing the S2 corpus section markers")
    pure_cases = parse_sql_logic_error_cases(source.split(pure_marker, 1)[0], "preflight")
    metadata_cases = parse_sql_logic_error_cases(source.split(metadata_marker, 1)[1], "metadata")
    if not pure_cases or not metadata_cases:
        raise ValidationError("S2 SQLLogicTest corpus sections must both contain statement error cases")

    s2_root = output / "s2-corpus"
    query_root = s2_root / "queries"
    trace_root = s2_root / "traces"
    result_root = s2_root / "cases"
    for directory in (query_root, trace_root, result_root):
        directory.mkdir(parents=True, exist_ok=True)
    strace = shutil.which("strace")
    duckdb_path = Path(manifest["identities"]["duckdb"]["path"])
    known_object_paths = {item["absolute_path"] for item in fixture_inventory["objects"]}
    known_object_paths.update(item["absolute_path"] for gap in fixture_inventory["gap_prefixes"]
                              for item in gap["objects"])
    results: list[dict[str, Any]] = []

    def run_case(case: dict[str, str], expected_indices: list[int] | None) -> None:
        sql = case["sql"].replace("__WORKING_DIRECTORY__", str(ROOT.resolve()))
        name = case["case_id"]
        if expected_indices is not None:
            name, expected_indices = s2_case_label({**case, "sql": sql})
        if any(item["case_id"] == name for item in results):
            name += "_" + case["case_id"].rsplit("_", 1)[-1]
        query_path = query_root / f"{name}.sql"
        execution_sql = with_extension_load(sql, extension_path)
        query_path.write_text(execution_sql, encoding="utf-8")
        trace_path = trace_root / f"{name}.strace"
        command = [str(duckdb_path), "-no-init", "-unsigned", "-json", "-bail",
                   ":memory:", "-f", str(query_path)]
        if strace:
            traced_command = [strace, "-f", "-qq", "-e",
                              "trace=openat,openat2,newfstatat,statx,connect,sendto,sendmsg,recvfrom",
                              "-o", str(trace_path), "--", *command]
        else:
            traced_command = command
        execution = run_command(traced_command, ROOT)
        execution.update({"scenario": name, "sql_path": str(query_path),
                          "sql_sha256": hashlib.sha256(execution_sql.encode()).hexdigest(),
                          "query_sha256": hashlib.sha256(sql.encode()).hexdigest(),
                          "trace_path": str(trace_path) if strace else None})
        manifest["commands"].append(execution)
        diagnostic = (execution["stderr"] + "\n" + execution["stdout"]).strip()
        try:
            diagnostic_matches = execution["exit_code"] != 0 and re.search(
                case["error_pattern"], diagnostic, re.IGNORECASE | re.DOTALL) is not None
            regex_error = None
        except re.error as error:
            diagnostic_matches = False
            regex_error = str(error)

        trace_readable = bool(strace and trace_path.is_file())
        trace_text = trace_path.read_text(encoding="utf-8", errors="replace") if trace_readable else ""
        trace_summary = summarize_s2_access_trace(trace_text)
        candidate_events = trace_summary["candidate_events"]
        candidate_paths = trace_summary["candidate_paths"]
        network_events = trace_summary["network_events"]
        if expected_indices is None:
            expected_paths: list[str] = []
            access_matches = trace_readable and not candidate_events and not network_events
        else:
            prefix = s2_candidate_prefix(sql)
            expected_paths = [str(prefix / f"chunk_{index:04d}.om") for index in expected_indices]
            expected_are_fixtures = all(path in known_object_paths for path in expected_paths)
            access_matches = trace_readable and expected_are_fixtures and \
                sorted(candidate_paths) == sorted(expected_paths)

        if not diagnostic_matches:
            status = "failed"
            summary = "SQL error or diagnostic did not match the registered SQLLogicTest expectation"
        elif not trace_readable:
            status = "blocked"
            summary = "strace is unavailable or produced no readable per-case trace"
        elif not access_matches:
            status = "failed"
            summary = "observed candidate-object access differs from the case contract"
        else:
            status = "passed"
            summary = ("metadata mismatch rejected after opening each present candidate" if expected_indices is not None
                       else "invalid request rejected before candidate object or network access")
        result = {
            "case_id": name,
            "status": status,
            "summary": summary,
            "expected_error": case["expected_error"],
            "error_pattern": case["error_pattern"],
            "diagnostic_matches": diagnostic_matches,
            "diagnostic": re.sub(r"\s+", " ", diagnostic)[:2000],
            "regex_error": regex_error,
            "exit_code": execution["exit_code"],
            "query_path": str(query_path.relative_to(output)),
            "query_sha256": execution["sql_sha256"],
            "trace_path": str(trace_path.relative_to(output)) if trace_readable else None,
            "trace_readable": trace_readable,
            "expected_candidate_paths": expected_paths,
            "observed_candidate_paths": candidate_paths,
            "candidate_access_events": candidate_events,
            "network_events": network_events,
            "access_matches_contract": access_matches if trace_readable else None,
            "command": execution,
        }
        results.append(result)
        write_json(result_root / f"{name}.json", result)

    for case in pure_cases:
        run_case(case, None)
    for case in metadata_cases:
        run_case(case, [0])

    statuses = [item["status"] for item in results]
    if "failed" in statuses:
        overall = "failed"
    elif "blocked" in statuses or not results:
        overall = "blocked"
    else:
        overall = "passed"
    return {
        "status": overall,
        "summary": f"{len(pure_cases)} no-access cases and {len(metadata_cases)} metadata-access cases audited individually",
        "source_path": str(source_path.resolve()),
        "source_sha256": sha256_file(source_path),
        "strace_path": str(trace_root.resolve()),
        "strace_available": bool(strace),
        "preflight_case_count": len(pure_cases),
        "metadata_case_count": len(metadata_cases),
        "passed_case_count": sum(item["status"] == "passed" for item in results),
        "failed_case_count": sum(item["status"] == "failed" for item in results),
        "blocked_case_count": sum(item["status"] == "blocked" for item in results),
        "cases": results,
    }


def prerequisite_record(label: str, path: Path, required: bool = True) -> dict[str, Any]:
    resolved = path.expanduser().resolve()
    exists = resolved.is_file()
    record: dict[str, Any] = {"name": label, "path": str(resolved), "required": required,
                              "status": "available" if exists else ("missing" if required else "unavailable")}
    if exists:
        record.update({"sha256": sha256_file(resolved), "size_bytes": resolved.stat().st_size})
    return record


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")


def render_controlled_remote_report(manifest: dict[str, Any]) -> str:
    lines = [
        "# read_om_series Controlled Remote Validation",
        "",
        f"- Feature: `{FEATURE}`",
        f"- Run status: **{manifest['status']}**",
        f"- Started: `{manifest['started_at_utc']}`",
        f"- Platform: `{manifest['platform']['system']} {manifest['platform']['machine']}`",
        "- Mode: `controlled-remote`",
        "",
        "## Transport gates",
        "",
    ]
    for transport, result in manifest.get("transports", {}).items():
        lines.append(f"- **{transport}: {result['status']}** — {result.get('summary', '')}")
    lines.extend(["", "## Feature gates", ""])
    for gate, result in manifest.get("gates", {}).items():
        lines.append(f"- **{gate}: {result['status']}** — {result.get('summary', '')}")
    extension_load = manifest.get("extension_load_check", {})
    if extension_load:
        lines.extend(["", "## Extension load identity", "",
                      f"- Status: **{extension_load.get('status')}** — {extension_load.get('summary')}"])
        if extension_load.get("expected_extension_path"):
            lines.append(f"- Requested extension: `{extension_load['expected_extension_path']}`")
    lines.extend(["", "## Scenario outcomes", ""])
    for name, result in manifest.get("scenarios", {}).items():
        lines.append(f"- `{name}`: **{result.get('status')}** — {result.get('summary', '')}")
        if "worker_count" in result:
            value_metrics = result.get("value_scan_metrics", {})
            free_metrics = result.get("value_free_count_metrics", {})
            lines.append(f"  - N160 worker cap `{result['worker_count']}`; rows "
                         f"{result.get('actual_count')} vs {result.get('expected_count')}; "
                         f"value scans `{value_metrics.get('status')}`, value-free COUNT scans "
                         f"`{free_metrics.get('status')}`; worker caps respected "
                         f"`{value_metrics.get('all_children_respect_worker_cap')}`/"
                         f"`{free_metrics.get('all_children_respect_worker_cap')}`; memory upper bounds "
                         f"respected `{value_metrics.get('all_query_memory_peaks_within_recorded_upper_bounds')}`/"
                         f"`{free_metrics.get('all_query_memory_peaks_within_recorded_upper_bounds')}`; COUNT "
                         f"{result.get('value_free_count')} vs {result.get('expected_value_free_count')}")
        activity = result.get("child_activity", {})
        if activity:
            lines.append("  - children: attempted "
                         f"{len(activity.get('attempted_children', []))}, bound "
                         f"{len(activity.get('bound_children_inferred_from_successful_origin_responses', []))}, "
                         f"executed scans {activity.get('executed_child_scan_count', 0)}; "
                         f"series restarts {activity.get('transport_activity', {}).get('series_restarts', 0)}")
        audit = result.get("request_audit", {})
        if audit:
            lines.append("  - audit: "
                         f"list {audit.get('list_request_count')}, metadata/unclassified "
                         f"{audit.get('metadata_or_unclassified_request_count')}, witness "
                         f"{audit.get('witness_request_count')}")
    lines.extend([
        "", "## Identities and retained evidence", "",
        f"- DuckDB CLI SHA-256: `{manifest.get('identities', {}).get('duckdb', {}).get('sha256', 'unavailable')}`",
        f"- DuckOMO extension SHA-256: `{manifest.get('identities', {}).get('extension', {}).get('sha256', 'unavailable')}`",
        f"- Official HTTPFS SHA-256: `{manifest.get('identities', {}).get('httpfs', {}).get('sha256', 'unavailable')}`",
        f"- Fixture manifest SHA-256: `{manifest.get('identities', {}).get('fixtures', {}).get('manifest_sha256', 'unavailable')}`",
        f"- Temporal model registry SHA-256: `{manifest.get('identities', {}).get('fixtures', {}).get('registry_models_sha256', 'unavailable')}`",
        "- Request audit contains sanitized origin events; private service configuration was kept in temporary storage.",
        "- Repeated failed origin requests are counted separately from runner query invocations; the adapter cannot identify HTTPFS retry intent.",
        "",
    ])
    return "\n".join(lines)


def controlled_remote_main(args: argparse.Namespace) -> int:
    output = args.output.expanduser().resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "queries").mkdir(parents=True, exist_ok=True)
    started = utc_now()
    system = platform.system()
    machine = platform.machine()
    platform_ok = system == "Linux" and machine.lower() in {"aarch64", "arm64"}
    duckdb_record = prerequisite_record("duckdb", args.duckdb)
    extension_record = prerequisite_record("extension", args.extension)
    httpfs_record = (prerequisite_record("httpfs", args.httpfs) if args.httpfs else
                     {"name": "httpfs", "required": True, "status": "missing", "reason": "--httpfs was not supplied"})
    fixtures_path = args.fixtures.expanduser().resolve()
    fixture_inventory: dict[str, Any] | None = None
    fixture_problem: str | None = None
    prerequisites = [duckdb_record, extension_record, httpfs_record]
    if fixtures_path.is_dir():
        try:
            fixture_inventory = load_fixture_inventory(fixtures_path)
            prerequisites.append({"name": "fixtures", "path": str(fixtures_path), "status": "available",
                                 "manifest_sha256": fixture_inventory["manifest_sha256"]})
        except ValidationError as error:
            fixture_problem = str(error)
            prerequisites.append({"name": "fixtures", "path": str(fixtures_path), "status": "invalid",
                                 "reason": fixture_problem})
    else:
        fixture_problem = f"fixture directory does not exist: {fixtures_path}"
        prerequisites.append({"name": "fixtures", "path": str(fixtures_path), "status": "missing",
                             "reason": fixture_problem})
    tls_args = (args.tls_cert, args.tls_key, args.tls_ca)
    tls_configured = all(tls_args)
    tls_partial = any(tls_args) and not tls_configured
    if tls_partial:
        prerequisites.append({"name": "https_tls", "status": "invalid",
                              "reason": "--tls-cert, --tls-key and --tls-ca must be supplied together"})
    elif tls_configured:
        for label, path in (("tls_cert", args.tls_cert), ("tls_key", args.tls_key), ("tls_ca", args.tls_ca)):
            prerequisites.append(prerequisite_record(label, path))
    if not platform_ok:
        prerequisites.append({"name": "linux_arm64", "status": "unsupported",
                              "reason": "acceptance evidence requires Linux AArch64"})

    manifest: dict[str, Any] = {
        "schema_version": 1,
        "feature": FEATURE,
        "mode": "controlled-remote",
        "status": "blocked",
        "started_at_utc": started,
        "ended_at_utc": None,
        "platform": {"system": system, "machine": machine, "python": sys.version.split()[0]},
        "prerequisites": prerequisites,
        "identities": {
            "duckdb": duckdb_record,
            "extension": extension_record,
            "httpfs": httpfs_record,
            "runner": prerequisite_record("validator", Path(__file__)),
            "remote_adapter": prerequisite_record("remote_adapter", ROOT / "scripts/series_remote.py"),
            "fixtures": ({"root": str(fixtures_path), "manifest_path": fixture_inventory["manifest_path"],
                          "manifest_sha256": fixture_inventory["manifest_sha256"],
                          "definition_sha256": fixture_inventory["definition_sha256"],
                          "registry_models_path": fixture_inventory["registry_models_path"],
                          "registry_models_sha256": fixture_inventory["registry_models_sha256"],
                          "registry_equivalence": fixture_inventory["registry_equivalence"]}
                         if fixture_inventory else {"root": str(fixtures_path), "error": fixture_problem}),
        },
        "commands": [],
        "scenarios": {},
        "transports": {},
        "gates": {
            "S3": {"status": "blocked", "summary": "controlled initial-missing matrix was not run"},
            "S4": {"status": "blocked", "summary": "controlled transport-fault and request-audit matrix was not run"},
            "SC-004": {"status": "blocked", "summary": "all-model runtime equivalence and no-discovery origin audit was not run"},
            "S5": {"status": "blocked", "summary": "remote N160 domain comparison across transports and worker caps was not run"},
            "S6": {"status": "blocked", "summary": "remote per-child scan metric checks were not run"},
        },
    }

    required_ready = (duckdb_record["status"] == "available" and extension_record["status"] == "available"
                      and httpfs_record["status"] == "available" and fixture_inventory is not None
                      and platform_ok and not tls_partial)
    if not required_ready:
        manifest["ended_at_utc"] = utc_now()
        write_json(output / "manifest.json", manifest)
        write_json(output / "commands.json", manifest["commands"])
        (output / "report.md").write_text(render_controlled_remote_report(manifest), encoding="utf-8")
        print(f"blocked: controlled-remote prerequisite missing; evidence written to {output}", file=sys.stderr)
        return 2

    assert fixture_inventory is not None and args.httpfs is not None
    duckdb_path = Path(duckdb_record["path"])
    extension_path = Path(extension_record["path"])
    httpfs_path = Path(httpfs_record["path"])
    version_command = run_command([str(duckdb_path), "-no-init", "-version"], ROOT)
    manifest["commands"].append(version_command)
    manifest["identities"]["duckdb"]["version"] = version_command["stdout"].strip()
    if version_command["exit_code"] != 0:
        manifest["status"] = "failed"
        manifest["gates"]["S3"] = {"status": "failed", "summary": "DuckDB version command failed"}
        manifest["gates"]["S4"] = {"status": "failed", "summary": "DuckDB version command failed"}
        manifest["gates"]["S5"] = {"status": "failed", "summary": "DuckDB version command failed"}
        manifest["gates"]["S6"] = {"status": "failed", "summary": "DuckDB version command failed"}
        manifest["gates"]["SC-004"] = {"status": "failed", "summary": "DuckDB version command failed"}
        manifest["ended_at_utc"] = utc_now()
        write_json(output / "manifest.json", manifest)
        write_json(output / "commands.json", manifest["commands"])
        (output / "report.md").write_text(render_controlled_remote_report(manifest), encoding="utf-8")
        return 1

    extension_load_check = verify_extension_load(args, manifest, output)
    manifest["extension_load_check"] = extension_load_check
    if extension_load_check["status"] != "passed":
        gate_status = "failed" if extension_load_check["status"] == "failed" else "blocked"
        for gate in ("S3", "S4", "SC-004", "S5", "S6"):
            manifest["gates"][gate] = {
                "status": gate_status,
                "summary": "controlled remote queries were not run because the requested loadable extension was not verified",
            }
        manifest["status"] = gate_status
        manifest["ended_at_utc"] = utc_now()
        write_json(output / "manifest.json", manifest)
        write_json(output / "commands.json", manifest["commands"])
        (output / "report.md").write_text(render_controlled_remote_report(manifest), encoding="utf-8")
        print(f"{gate_status}: requested extension load was not verified; evidence: {output}", file=sys.stderr)
        return 1 if gate_status == "failed" else 2

    setup_failed = False
    service_audit_copy = output / "remote-audit.jsonl"
    temporary_context: DeferredTemporaryDirectory | None = None
    try:
        temporary_context = DeferredTemporaryDirectory(prefix="duckomo-series-remote-")
        with temporary_context as temporary:
            temporary_root = Path(temporary)
            service_dir = temporary_root / "service"
            service_start = [sys.executable, str(ROOT / "scripts/series_remote.py"), "start",
                             "--fixtures", str(fixtures_path), "--output", str(service_dir),
                             "--httpfs", str(httpfs_path)]
            if tls_configured:
                service_start.extend(["--tls-cert", str(Path(args.tls_cert).resolve()),
                                      "--tls-key", str(Path(args.tls_key).resolve()),
                                      "--tls-ca", str(Path(args.tls_ca).resolve())])
            start_result = run_command(service_start, ROOT, timeout=30)
            start_result["purpose"] = "start controlled fixture services"
            manifest["commands"].append(start_result)
            if start_result["exit_code"] != 0:
                setup_failed = True
                manifest["prerequisites"].append({"name": "controlled_fixture_service", "status": "failed",
                                                  "reason": start_result["stderr"].strip() or start_result["stdout"].strip()})
                raise RuntimeError("controlled fixture service failed to start")

            service_manifest_path = service_dir / "run-manifest.json"
            service_manifest = json.loads(service_manifest_path.read_text(encoding="utf-8"))
            manifest["identities"]["remote_service"] = {
                "fixture_manifest_sha256": service_manifest.get("fixture_manifest_sha256"),
                "fixture_object_count": service_manifest.get("fixture_object_count"),
                "https_ca_sha256": service_manifest.get("https_ca_sha256"),
                "s3_style": service_manifest.get("s3_style"),
                "credentials_in_run_manifest": service_manifest.get("credentials_in_run_manifest"),
            }
            private_config = json.loads((service_dir / "private/config.json").read_text(encoding="utf-8"))
            private_values = [str(private_config.get(name, "")) for name in ("control_token", "s3_key_id", "s3_secret_key")]
            base_env = {key: value for key, value in os.environ.items()
                        if key.lower() not in {"http_proxy", "https_proxy", "all_proxy"}}
            audit_path = service_dir / "logs/requests.jsonl"
            http_base = service_manifest["http_base"]
            s3_base = service_manifest["s3_base"]
            transport_bases = {"http": http_base, "s3": s3_base}
            if service_manifest.get("https_base"):
                transport_bases["https"] = service_manifest["https_base"]
            else:
                manifest["transports"]["https"] = {
                    "status": "blocked",
                    "summary": "HTTPS certificate and CA inputs were not supplied",
                }

            init_paths: dict[str, Path] = {}
            for transport in transport_bases:
                init_path = temporary_root / f"{transport}-init.sql"
                init_sql = "LOAD " + sql_string(str(httpfs_path)) + ";\n"
                if transport == "https":
                    init_sql += "SET ca_cert_file = " + sql_string(str(Path(args.tls_ca).resolve())) + ";\n"
                if transport == "s3":
                    init_sql += (service_dir / "s3-setup.sql").read_text(encoding="utf-8")
                init_path.write_text(init_sql, encoding="utf-8")
                init_path.chmod(0o600)
                init_paths[transport] = init_path

            def controlled_command(name: str, transport: str, sql: str) -> dict[str, Any]:
                query_path = output / "queries" / f"{name}.sql"
                execution_sql = with_extension_load(sql, extension_path)
                query_path.write_text(execution_sql, encoding="utf-8")
                argv = [str(duckdb_path), "-unsigned", "-json", "-bail", "-init", str(init_paths[transport]),
                        ":memory:", "-f", str(query_path)]
                env = dict(base_env)
                if transport == "https":
                    ca_path = str(Path(args.tls_ca).resolve())
                    env["CURL_CA_BUNDLE"] = ca_path
                    env["SSL_CERT_FILE"] = ca_path
                try:
                    record = run_command(argv, ROOT, timeout=120, env=env)
                except subprocess.TimeoutExpired as error:
                    stdout = error.stdout.decode(errors="replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
                    stderr = error.stderr.decode(errors="replace") if isinstance(error.stderr, bytes) else (error.stderr or "")
                    record = {"argv": argv, "cwd": str(ROOT), "started_at_utc": utc_now(),
                              "ended_at_utc": utc_now(), "elapsed_seconds": 120.0, "exit_code": 124,
                              "stdout": stdout, "stderr": stderr, "timed_out": True}
                redact_remote_command(record, private_values)
                record.update({"scenario": name, "transport": transport, "sql_path": str(query_path),
                               "sql_sha256": hashlib.sha256(execution_sql.encode()).hexdigest(),
                               "query_sha256": hashlib.sha256(sql.encode()).hexdigest()})
                manifest["commands"].append(record)
                return record

            def control(transport: str, key: str, action: str, fault: str | None = None,
                        replacement: str | None = None) -> None:
                argv = [sys.executable, str(ROOT / "scripts/series_remote.py"), "control",
                        "--output", str(service_dir), "--transport", transport,
                        "--object", key, "--action", action]
                if fault:
                    argv.extend(["--fault", fault])
                if replacement:
                    argv.extend(["--replacement", replacement])
                record = run_command(argv, ROOT, timeout=15)
                record["purpose"] = f"{action} {transport} object state"
                redact_remote_command(record, private_values)
                manifest["commands"].append(record)
                if record["exit_code"] != 0:
                    raise RuntimeError(f"controlled fixture {action} failed for {transport} {key}")

            def execute_registry_equivalence(transport: str, base: str,
                                             model: dict[str, Any]) -> dict[str, Any]:
                name = f"{transport}-sc004-registered-vs-explicit-{model['key']}"
                key_prefix = f"data/registry-equivalence/{model['key']}"
                uri_prefix = base.rstrip("/") + "/" + key_prefix
                planned_keys = [child_uri_key(key_prefix, 0)]
                offset = audit_path.stat().st_size
                query = registry_equivalence_sql(uri_prefix, model["key"],
                                                 model["chunk_time_length"], model["step_seconds"])
                command = controlled_command(name, transport, query)
                events, _ = read_audit_since(audit_path, offset)
                request_audit = summarize_remote_audit(events, planned_keys)
                if command["exit_code"] != 0:
                    result: dict[str, Any] = {
                        "status": "failed", "summary": "registered-vs-explicit query exited non-zero",
                        "exit_code": command["exit_code"], "diagnostic": command["stderr"].strip()[:1200],
                    }
                else:
                    try:
                        payload = parse_cli_result_with_preface(command["stdout"])
                        result = compare_registry_equivalence(payload, model)
                        result["summary"] = "registered defaults match explicit temporal parameters by two-way EXCEPT ALL"
                    except ValidationError as error:
                        payload = {}
                        result = {"status": "failed", "summary": str(error)}
                audit_ok = (request_audit["response_count"] > 0 and request_audit["no_list_requests"]
                            and request_audit["no_metadata_or_unclassified_requests"]
                            and request_audit["no_witness_requests"]
                            and request_audit["all_requests_within_planned_candidates"])
                if not audit_ok:
                    result["status"] = "failed"
                    result["summary"] = "runtime equivalence or no-discovery origin audit failed"
                result.update({
                    "transport": transport,
                    "model_key": model["key"],
                    "chunk_time_length": model["chunk_time_length"],
                    "step_seconds": model["step_seconds"],
                    "fixture_path": model["path"],
                    "fixture_sha256": model["sha256"],
                    "query_sha256": hashlib.sha256(query.encode()).hexdigest(),
                    "registered_call": "read_om_series with model and registered temporal defaults",
                    "explicit_call": "same registered model with explicit chunk_time_length and time_step parameters",
                    "comparison": "two-way SQL EXCEPT ALL with schema, row count and time-bound checks",
                    "request_audit": request_audit,
                    "request_events": events,
                })
                manifest["scenarios"][name] = result
                write_json(output / "scenarios" / f"{name}.json", result)
                return result

            def execute_zero_access_argument_error(transport: str, base: str) -> dict[str, Any]:
                name = f"{transport}-argument-error-zero-origin-requests"
                glob_prefix = base.rstrip("/") + "/data/synthetic/*/temperature"
                query = remote_actual_sql(glob_prefix, "1970-01-01 00:00:00", "1970-01-01 08:00:00")
                offset = audit_path.stat().st_size
                command = controlled_command(name, transport, query)
                events, _ = read_audit_since(audit_path, offset)
                request_audit = summarize_remote_audit(events, [])
                diagnostic = (command["stderr"] + "\n" + command["stdout"]).lower()
                expected_error = command["exit_code"] != 0 and "glob" in diagnostic
                no_requests = request_audit["response_count"] == 0
                result = {
                    "status": "passed" if expected_error and no_requests else "failed",
                    "summary": ("pure prefix argument error returned before any origin object request"
                                if expected_error and no_requests
                                else "pure argument error did not meet the zero-request contract"),
                    "transport": transport,
                    "exit_code": command["exit_code"],
                    "diagnostic": command["stderr"].strip()[:1200],
                    "expected_error": "glob-containing series prefix rejected during argument binding",
                    "query_sha256": hashlib.sha256(query.encode()).hexdigest(),
                    "request_audit": request_audit,
                    "request_events": events,
                }
                manifest["scenarios"][name] = result
                write_json(output / "scenarios" / f"{name}.json", result)
                return result

            def execute_post_bind_mutation(transport: str, base: str, action: str,
                                           replacement: str | None = None) -> dict[str, Any]:
                name = f"{transport}-post-bind-{action}"
                key_prefix = "data/synthetic/temperature"
                target_key = child_uri_key(key_prefix, 0)
                uri_prefix = base.rstrip("/") + "/" + key_prefix
                planned_keys = [target_key]
                gate_path = output / "barriers" / f"{name}.fifo"
                gate_path.parent.mkdir(parents=True, exist_ok=True)
                gate_path.unlink(missing_ok=True)
                os.mkfifo(gate_path, 0o600)
                start_text = "1970-01-01 00:00:00"
                prepared_query = series_call_sql(
                    uri_prefix, start_text, start_text, strict=True, model="ecmwf_ifs",
                    chunk_time_length=4, time_step_sql="INTERVAL '1 hour'",
                )
                sql = (f"PREPARE series_post_bind AS\n{prepared_query};\n"
                       f"SELECT count(*) FROM read_csv({sql_string(str(gate_path))}, header=false, "
                       "columns={'gate':'VARCHAR'});\n"
                       "EXECUTE series_post_bind;\n")
                query_path = output / "queries" / f"{name}.sql"
                execution_sql = with_extension_load(sql, extension_path)
                query_path.write_text(execution_sql, encoding="utf-8")
                argv = [str(duckdb_path), "-unsigned", "-json", "-bail", "-init", str(init_paths[transport]),
                        ":memory:", "-f", str(query_path)]
                env = dict(base_env)
                if transport == "https":
                    ca_path = str(Path(args.tls_ca).resolve())
                    env["CURL_CA_BUNDLE"] = ca_path
                    env["SSL_CERT_FILE"] = ca_path
                bind_offset = audit_path.stat().st_size
                started_at = utc_now()
                start_clock = time.monotonic()
                process = subprocess.Popen(argv, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                           text=True, env=env)
                fifo_fd: int | None = None
                fifo_gate_reached = False
                control_applied = False
                deadline = start_clock + 90
                bind_events: list[dict[str, Any]] = []
                bind_audit: dict[str, Any] = {}
                post_events: list[dict[str, Any]] = []
                post_audit: dict[str, Any] = {}
                post_offset = bind_offset
                try:
                    while time.monotonic() < deadline and process.poll() is None:
                        try:
                            fifo_fd = os.open(gate_path, os.O_WRONLY | os.O_NONBLOCK)
                            fifo_gate_reached = True
                            break
                        except OSError as error:
                            if error.errno != errno.ENXIO:
                                raise
                            time.sleep(0.05)
                    if fifo_fd is None:
                        raise RuntimeError(f"DuckDB did not reach the post-bind gate for {name}")
                    bind_events, _ = read_audit_since(audit_path, bind_offset)
                    bind_audit = summarize_remote_audit(bind_events, planned_keys)
                    bind_confirmed = any(event.get("event") == "response" and event.get("object") == target_key
                                         and event.get("status") in {200, 206}
                                         for event in bind_events)
                    bind_audit_ok = (bind_confirmed and bind_audit["no_list_requests"]
                                     and bind_audit["no_metadata_or_unclassified_requests"]
                                     and bind_audit["no_witness_requests"]
                                     and bind_audit["all_requests_within_planned_candidates"])
                    if bind_audit_ok:
                        control(transport, target_key, action, replacement=replacement)
                        control_applied = True
                    post_offset = audit_path.stat().st_size
                    os.write(fifo_fd, b"go\n")
                    os.close(fifo_fd)
                    fifo_fd = None
                    try:
                        stdout, stderr = process.communicate(timeout=120)
                        exit_code = process.returncode
                    except subprocess.TimeoutExpired as error:
                        process.kill()
                        stdout, stderr = process.communicate()
                        exit_code = 124
                        stderr += "\npost-bind query exceeded 120 seconds"
                    post_events, _ = read_audit_since(audit_path, post_offset)
                    post_audit = summarize_remote_audit(post_events, planned_keys)
                    mutation_check = summarize_post_bind_mutation(
                        action, target_key, bind_events, post_events, exit_code, stderr + "\n" + stdout)
                    status_ok = (post_audit["no_list_requests"] and post_audit["no_metadata_or_unclassified_requests"]
                                 and post_audit["no_witness_requests"]
                                 and post_audit["all_requests_within_planned_candidates"])
                    passed = bind_audit_ok and control_applied and mutation_check["passed"] and status_ok
                    result = {
                        "scenario": name,
                        "status": "passed" if passed else "failed",
                        "summary": ("prepared scan failed after the bound object was " +
                                    ("removed" if action == "remove" else "replaced with a changed ETag")
                                    if passed else "post-bind mutation did not produce the expected audited query failure"),
                        "transport": transport,
                        "mutation": action,
                        "replacement_object": replacement,
                        "target_object": target_key,
                        "prepare_completed_before_control": fifo_gate_reached,
                        "control_applied_after_bind": control_applied,
                        "bind_confirmed_by_successful_origin_response": bind_confirmed,
                        "bind_request_audit": bind_audit,
                        "post_mutation_request_audit": post_audit,
                        **mutation_check,
                        "query_exit_code": exit_code,
                        "query_diagnostic": stderr.strip()[:1600],
                        "post_mutation_request_events": post_events,
                        "series_query_invocations": 1,
                        "series_restarts": 0,
                        "query_path": str(query_path.relative_to(output)),
                        "sql_sha256": hashlib.sha256(execution_sql.encode()).hexdigest(),
                        "query_sha256": hashlib.sha256(sql.encode()).hexdigest(),
                        "started_at_utc": started_at,
                        "ended_at_utc": utc_now(),
                        "elapsed_seconds": time.monotonic() - start_clock,
                        "argv": argv,
                        "cwd": str(ROOT),
                        "stdout": stdout,
                        "stderr": stderr,
                    }
                finally:
                    if fifo_fd is not None:
                        try:
                            os.close(fifo_fd)
                        except OSError:
                            pass
                    if process.poll() is None:
                        process.kill()
                        process.communicate()
                    gate_path.unlink(missing_ok=True)
                if control_applied:
                    control(transport, target_key, "restore")
                    recovery_name = f"{name}-recovery"
                    recovery = execute_positive(transport, recovery_name, uri_prefix, key_prefix,
                                                [0, 1, 2], [0, 1, 2])
                    result["recovery_scenario"] = recovery_name
                    if recovery["status"] != "passed":
                        result["status"] = "failed"
                        result["summary"] += "; recovery after restoration failed"
                result["bind_request_events"] = bind_events
                manifest["commands"].append({
                    "scenario": name, "transport": transport, "argv": argv, "cwd": str(ROOT),
                    "started_at_utc": started_at, "ended_at_utc": result["ended_at_utc"],
                    "elapsed_seconds": result["elapsed_seconds"], "exit_code": result["query_exit_code"],
                    "stdout": result["stdout"], "stderr": result["stderr"],
                    "sql_path": str(query_path), "sql_sha256": result["sql_sha256"],
                    "query_sha256": result["query_sha256"],
                    "control_applied_after_prepare": control_applied,
                })
                manifest["scenarios"][name] = result
                write_json(output / "scenarios" / f"{name}.json", result)
                return result

            def execute_positive(transport: str, scenario_name: str, uri_prefix: str, key_prefix: str,
                                 candidate_indices: Sequence[int], expected_indices: Sequence[int],
                                 strict: bool = False, attempted_indices: Sequence[int] | None = None,
                                 model: str | None = None, chunk_time_length: int | None = 4,
                                 time_step_sql: str | None = "INTERVAL '1 hour'") -> dict[str, Any]:
                planned_keys = [key_prefix.rstrip("/") + f"/chunk_{index:04d}.om" for index in candidate_indices]
                attempted = list(attempted_indices if attempted_indices is not None else candidate_indices)
                attempted_keys = [key_prefix.rstrip("/") + f"/chunk_{index:04d}.om" for index in attempted]
                actual_offset = audit_path.stat().st_size
                actual_command = controlled_command(
                    scenario_name + "-actual", transport,
                    remote_actual_sql(uri_prefix, "1970-01-01 00:00:00", "1970-01-01 08:00:00", strict,
                                      model, chunk_time_length, time_step_sql))
                actual_events, _ = read_audit_since(audit_path, actual_offset)
                if actual_command["exit_code"] == 0:
                    try:
                        actual_payload = parse_cli_result_with_preface(actual_command["stdout"])
                        expected_command = controlled_command(
                            scenario_name + "-oracle", transport,
                            remote_expected_sql(uri_prefix, expected_indices,
                                                "1970-01-01 00:00:00", "1970-01-01 08:00:00"))
                        if expected_command["exit_code"] != 0:
                            raise ValidationError("manual read_om oracle command exited non-zero")
                        expected_payload = parse_cli_result_with_preface(expected_command["stdout"])
                        comparison = compare_remote_payloads(actual_payload, expected_payload)
                    except ValidationError as error:
                        comparison = {"status": "failed", "summary": str(error)}
                        actual_payload = {}
                else:
                    comparison = {"status": "failed", "summary": "series query command exited non-zero",
                                  "exit_code": actual_command["exit_code"],
                                  "stderr": actual_command["stderr"]}
                    actual_payload = {}
                audit_summary = summarize_remote_audit(actual_events, planned_keys)
                scan_metrics = actual_payload.get("scan_metrics") or []
                child_activity = summarize_child_activity(planned_keys, attempted_keys, actual_events, scan_metrics)
                status = comparison.get("status", "failed")
                if not audit_summary["all_requests_within_planned_candidates"] or not audit_summary["no_list_requests"]:
                    status = "failed"
                result = {
                    **comparison,
                    "status": status,
                    "summary": ("schema and full row multiset match" if status == "passed"
                                else comparison.get("summary", "schema, rows or request audit failed")),
                    "transport": transport,
                    "strict": strict,
                    "temporal_arguments": {"model": model, "chunk_time_length_override": chunk_time_length,
                                           "time_step_override": time_step_sql},
                    "expected_children": list(expected_indices),
                    "child_activity": child_activity,
                    "request_audit": audit_summary,
                    "request_events": actual_events,
                    "actual_command_scenario": scenario_name + "-actual",
                }
                manifest["scenarios"][scenario_name] = result
                write_json(output / "scenarios" / f"{scenario_name}.json", result)
                return result

            def execute_expected_failure(transport: str, scenario_name: str, uri_prefix: str,
                                         key_prefix: str, candidate_indices: Sequence[int],
                                         stop_index: int, expected_fault: str | None = None,
                                         outer_clause: str | None = None) -> dict[str, Any]:
                planned_keys = [key_prefix.rstrip("/") + f"/chunk_{index:04d}.om" for index in candidate_indices]
                attempted_indices = [index for index in candidate_indices if index <= stop_index]
                attempted_keys = [key_prefix.rstrip("/") + f"/chunk_{index:04d}.om" for index in attempted_indices]
                offset = audit_path.stat().st_size
                command = controlled_command(
                    scenario_name, transport,
                    remote_actual_sql(uri_prefix, "1970-01-01 00:00:00", "1970-01-01 08:00:00",
                                      strict=expected_fault is None, outer_clause=outer_clause))
                events, _ = read_audit_since(audit_path, offset)
                request_audit = summarize_remote_audit(events, planned_keys)
                expected_response = None
                if expected_fault is None:
                    missing_key = key_prefix.rstrip("/") + f"/chunk_{stop_index:04d}.om"
                    expected_response = any(event.get("event") == "response" and event.get("object") == missing_key
                                            and event.get("status") == 404 for event in events)
                else:
                    expected_status = {"http_403": 403, "http_404": 404, "http_412": 412,
                                       "body_404_status_403": 403, "http_500": 500,
                                       "http_503": 503, "drop": None, "corrupt": 200}.get(expected_fault)
                    expected_response = any(event.get("event") == "response" and event.get("fault") == expected_fault
                                            and event.get("status") == expected_status for event in events)
                # Pass means only a nonzero query exit plus the expected origin response in the audit.
                # It does not assert error category/text or parity with ordinary read_om.
                passed = command["exit_code"] != 0 and bool(expected_response)
                scan_metrics: list[dict[str, Any]] = []
                child_activity = summarize_child_activity(
                    planned_keys, attempted_keys, events, scan_metrics,
                    bind_failure_keys=(attempted_keys[-1:] if expected_fault == "corrupt" else ()))
                result = {
                    "status": "passed" if passed and request_audit["all_requests_within_planned_candidates"]
                    and request_audit["no_list_requests"] else "failed",
                    "summary": ("expected initial absence/fault propagated during bind" if passed
                                else "query did not fail with the expected audited response"),
                    "transport": transport,
                    "expected_failure": "initial_missing" if expected_fault is None else expected_fault,
                    "outer_clause": outer_clause,
                    "exit_code": command["exit_code"],
                    "diagnostic": command["stderr"].strip()[:1200],
                    "planned_children": planned_keys,
                    "child_activity": child_activity,
                    "request_audit": request_audit,
                    "request_events": events,
                }
                manifest["scenarios"][scenario_name] = result
                write_json(output / "scenarios" / f"{scenario_name}.json", result)
                return result

            def execute_reduced_gaussian(transport: str, base: str, worker_count: int) -> dict[str, Any]:
                name = f"{transport}-s5-n160-domain-vs-explicit-worker-{worker_count}"
                key_prefix = "data/synthetic/reduced-gaussian-n160/temperature"
                uri_prefix = base.rstrip("/") + "/" + key_prefix
                planned_keys = [key_prefix + f"/chunk_{index:04d}.om" for index in (0, 1)]
                audit_offset = audit_path.stat().st_size
                query = n160_reduced_gaussian_matrix_sql(uri_prefix, worker_count)
                command = controlled_command(name, transport, query)
                events, _ = read_audit_since(audit_path, audit_offset)
                row_metrics: dict[str, Any] = {"status": "failed", "summary": "query produced no metric payload"}
                count_metrics: dict[str, Any] = {"status": "failed", "summary": "query produced no metric payload"}
                child_activity: dict[str, Any] = {}
                if command["exit_code"] != 0:
                    result: dict[str, Any] = {
                        "status": "failed", "summary": "DuckDB command exited non-zero",
                        "exit_code": command["exit_code"], "stderr": command["stderr"],
                    }
                    payload: dict[str, Any] = {}
                else:
                    try:
                        payload = parse_cli_result_with_preface(command["stdout"])
                        result = compare_payload(payload)
                        value_count_equal = payload.get("value_free_count") == payload.get("expected_value_free_count")
                        result.update({
                            "summary": "N160 domain series matches explicit-grid manual UNION ALL",
                            "value_free_count_equal": value_count_equal,
                            "value_free_count": payload.get("value_free_count"),
                            "expected_value_free_count": payload.get("expected_value_free_count"),
                            "engine_version": payload.get("engine_version"),
                            "grid_evidence_scope": "synthetic_only; producer N160 validation not claimed",
                            "explicit_grid_sql_source_sha256": fixture_inventory["n160_explicit_grid_sql_source_sha256"],
                        })
                        row_records = payload.get("series_value_metrics") or []
                        count_records = payload.get("series_value_free_metrics") or []
                        row_metrics = summarize_local_scan_metrics(row_records, 2, True, worker_count)
                        count_metrics = summarize_local_scan_metrics(count_records, 2, False, worker_count)
                        child_activity = summarize_child_activity(
                            planned_keys, planned_keys, events, row_records)
                        if not value_count_equal:
                            result["status"] = "failed"
                        if row_metrics["status"] != "passed" or count_metrics["status"] != "passed":
                            result["status"] = "failed"
                    except ValidationError as error:
                        result = {"status": "failed", "summary": str(error)}
                        payload = {}
                audit = summarize_remote_audit(events, planned_keys)
                audit_ok = (audit["no_list_requests"] and audit["no_metadata_or_unclassified_requests"]
                            and audit["no_witness_requests"] and audit["all_requests_within_planned_candidates"])
                if not audit_ok:
                    result["status"] = "failed"
                    result["summary"] = "N160 comparison or request audit failed"
                result.update({
                    "transport": transport,
                    "worker_count": worker_count,
                    "value_scan_metrics": row_metrics,
                    "value_free_count_metrics": count_metrics,
                    "child_activity": child_activity,
                    "request_audit": audit,
                    "request_events": events,
                    "command_scenario": name,
                })
                manifest["scenarios"][name] = result
                write_json(output / "scenarios" / f"{name}.json", result)
                return result

            for transport, base in transport_bases.items():
                transport_result = {"status": "passed", "summary": "skip, strict, model and fault checks completed",
                                    "scenarios": []}
                manifest["transports"][transport] = transport_result
                argument_name = f"{transport}-argument-error-zero-origin-requests"
                argument_result = execute_zero_access_argument_error(transport, base)
                transport_result["scenarios"].append(argument_name)
                if argument_result["status"] != "passed":
                    transport_result["status"] = "failed"

                for model in fixture_inventory["registry_equivalence"]:
                    registry_name = f"{transport}-sc004-registered-vs-explicit-{model['key']}"
                    registry_result = execute_registry_equivalence(transport, base, model)
                    transport_result["scenarios"].append(registry_name)
                    if registry_result["status"] != "passed":
                        transport_result["status"] = "failed"

                registered_name = f"{transport}-registered-partial-override"
                registered = execute_positive(
                    transport, registered_name, base.rstrip("/") + "/data/synthetic/temperature",
                    "data/synthetic/temperature", [0, 1, 2], [0, 1, 2],
                    model="ecmwf_ifs", chunk_time_length=4, time_step_sql=None)
                transport_result["scenarios"].append(registered_name)
                if registered["status"] != "passed":
                    transport_result["status"] = "failed"

                gap_by_id = {item["id"]: item for item in fixture_inventory["gap_prefixes"]}
                gap_names = ("first_missing", "middle_missing", "last_missing", "all_missing")
                for gap_name in gap_names:
                    gap = gap_by_id[gap_name]
                    key_prefix = gap["prefix"]
                    uri_prefix = base.rstrip("/") + "/" + key_prefix
                    candidate_indices = [0, 1, 2]
                    present = set(gap["present_chunk_indices"])
                    scenario_name = f"{transport}-{gap_name}-skip"
                    result = execute_positive(transport, scenario_name, uri_prefix, key_prefix,
                                              candidate_indices, [index for index in candidate_indices if index in present])
                    transport_result["scenarios"].append(scenario_name)
                    if result["status"] != "passed":
                        transport_result["status"] = "failed"
                    missing_indices = [index for index in candidate_indices if index not in present]
                    stop_index = missing_indices[0]
                    strict_name = f"{transport}-{gap_name}-strict"
                    strict_result = execute_expected_failure(transport, strict_name, uri_prefix, key_prefix,
                                                             candidate_indices, stop_index)
                    transport_result["scenarios"].append(strict_name)
                    if strict_result["status"] != "passed":
                        transport_result["status"] = "failed"

                first_gap = gap_by_id["first_missing"]
                first_key_prefix = first_gap["prefix"]
                first_uri_prefix = base.rstrip("/") + "/" + first_key_prefix
                for clause, label in (("LIMIT 0", "limit-zero"), ("WHERE false", "where-false")):
                    outer_name = f"{transport}-first-missing-strict-{label}"
                    outer_result = execute_expected_failure(
                        transport, outer_name, first_uri_prefix, first_key_prefix,
                        [0, 1, 2], 0, outer_clause=clause)
                    transport_result["scenarios"].append(outer_name)
                    if outer_result["status"] != "passed":
                        transport_result["status"] = "failed"

                adjacent_object = next(item for item in fixture_inventory["objects"] if item["id"] == "adjacent_0000")
                adjacent_key_prefix = str(Path(adjacent_object["path"]).parent.as_posix())
                adjacent_uri_prefix = base.rstrip("/") + "/" + adjacent_key_prefix
                candidate_indices = [0, 1, 2]
                fault_cases = [
                    ("http_404", "skip", [1, 2]),
                    ("http_403", "fail", []),
                    ("body_404_status_403", "fail", []),
                    ("http_412", "fail", []),
                    ("http_500", "fail", []),
                    ("http_503", "fail", []),
                    ("corrupt", "fail", []),
                    ("drop", "fail", []),
                ]
                for fault_name, expectation, expected_indices in fault_cases:
                    target_key = adjacent_key_prefix + "/chunk_0000.om"
                    control(transport, target_key, "fault", fault=fault_name)
                    scenario_name = f"{transport}-fault-{fault_name}"
                    if expectation == "skip":
                        # A structured 404 should follow default initial-missing policy.
                        result = execute_positive(transport, scenario_name, adjacent_uri_prefix,
                                                  adjacent_key_prefix, candidate_indices, expected_indices)
                        event_ok = any(event.get("fault") == fault_name and event.get("status") == 404
                                       for event in result.get("request_events", []))
                        if not event_ok:
                            result["status"] = "failed"
                            result["summary"] = "structured 404 was not observed in the origin audit"
                            write_json(output / "scenarios" / f"{scenario_name}.json", result)
                    else:
                        result = execute_expected_failure(transport, scenario_name, adjacent_uri_prefix,
                                                          adjacent_key_prefix, candidate_indices, 0,
                                                          expected_fault=fault_name)
                    control(transport, target_key, "restore")
                    transport_result["scenarios"].append(scenario_name)
                    if result["status"] != "passed":
                        transport_result["status"] = "failed"

                recovery_name = f"{transport}-post-fault-recovery"
                recovery = execute_positive(transport, recovery_name, adjacent_uri_prefix,
                                            adjacent_key_prefix, candidate_indices, candidate_indices)
                transport_result["scenarios"].append(recovery_name)
                if recovery["status"] != "passed":
                    transport_result["status"] = "failed"

                for action, replacement in (("remove", None),
                                            ("replace", "data/synthetic/temperature/chunk_0001.om")):
                    mutation_result = execute_post_bind_mutation(transport, base, action, replacement)
                    transport_result["scenarios"].append(mutation_result["scenario"])
                    if mutation_result.get("recovery_scenario"):
                        transport_result["scenarios"].append(mutation_result["recovery_scenario"])
                    if mutation_result["status"] != "passed":
                        transport_result["status"] = "failed"

                for worker_count in (1, 2, 4):
                    matrix_result = execute_reduced_gaussian(transport, base, worker_count)
                    transport_result["scenarios"].append(matrix_result["command_scenario"])
                    if matrix_result["status"] != "passed":
                        transport_result["status"] = "failed"

                if transport_result["status"] == "failed":
                    transport_result["summary"] = "one or more remote skip/strict/fault/N160 checks failed"

            stop_result = run_command([sys.executable, str(ROOT / "scripts/series_remote.py"), "stop",
                                       "--output", str(service_dir)], ROOT, timeout=15)
            stop_result["purpose"] = "stop controlled fixture services"
            manifest["commands"].append(stop_result)
            if stop_result["exit_code"] != 0:
                setup_failed = True
                manifest["prerequisites"].append({"name": "controlled_fixture_cleanup", "status": "failed",
                                                  "reason": stop_result["stderr"].strip()})
            if audit_path.is_file():
                shutil.copyfile(audit_path, service_audit_copy)
    except (OSError, RuntimeError, ValueError, ValidationError, json.JSONDecodeError) as error:
        setup_failed = True
        manifest["service_error"] = str(error)
        if not manifest["transports"]:
            manifest["transports"]["service"] = {"status": "failed", "summary": str(error)}

    if temporary_context is not None:
        temporary_root = Path(temporary_context._temporary.name)
        service_dir = temporary_root / "service"
        pid_file = service_dir / "service-pids.json"
        if pid_file.is_file():
            stop_result = run_command([sys.executable, str(ROOT / "scripts/series_remote.py"), "stop",
                                       "--output", str(service_dir)], ROOT, timeout=15)
            stop_result["purpose"] = "stop controlled fixture services after interrupted validation"
            manifest["commands"].append(stop_result)
            if stop_result["exit_code"] != 0:
                setup_failed = True
                manifest["prerequisites"].append({"name": "controlled_fixture_cleanup", "status": "failed",
                                                  "reason": stop_result["stderr"].strip()})
        audit_path = service_dir / "logs/requests.jsonl"
        if audit_path.is_file() and not service_audit_copy.is_file():
            shutil.copyfile(audit_path, service_audit_copy)
        temporary_context.cleanup()

    expected_matrix_names = [f"{transport}-s5-n160-domain-vs-explicit-worker-{worker_count}"
                            for transport in ("http", "https", "s3") for worker_count in (1, 2, 4)]
    matrix_results = [manifest["scenarios"].get(name) for name in expected_matrix_names]
    matrix_missing = any(result is None for result in matrix_results)
    matrix_failed = any(result is not None and result.get("status") == "failed" for result in matrix_results)
    matrix_blocked = any(result is not None and result.get("status") == "blocked" for result in matrix_results)
    s5_status = "failed" if matrix_failed else ("blocked" if matrix_missing or matrix_blocked or setup_failed else "passed")
    s6_failed = any(result is not None and (
        result.get("value_scan_metrics", {}).get("status") == "failed"
        or result.get("value_free_count_metrics", {}).get("status") == "failed") for result in matrix_results)
    s6_missing = any(result is None or "value_scan_metrics" not in result
                     or "value_free_count_metrics" not in result for result in matrix_results)
    s6_status = "failed" if s6_failed else ("blocked" if s6_missing or matrix_blocked or setup_failed else "passed")
    manifest["gates"]["S5"] = {
        "status": s5_status,
        "summary": "N160 remote domain scans matched explicit-grid manual UNION ALL across HTTP, HTTPS and path-style S3 at 1/2/4 workers",
    }
    manifest["gates"]["S6"] = {
        "status": s6_status,
        "summary": "remote per-child metrics showed completed value scans and zero value I/O for COUNT(*) across all transports",
    }
    registry_case_names = [
        f"{transport}-sc004-registered-vs-explicit-{model['key']}"
        for transport in ("http", "https", "s3")
        for model in fixture_inventory["registry_equivalence"]
    ]
    registry_results = [manifest["scenarios"].get(name) for name in registry_case_names]
    registry_failed = any(result is not None and result.get("status") == "failed" for result in registry_results)
    registry_complete = all(result is not None and result.get("status") == "passed" for result in registry_results)
    manifest["gates"]["SC-004"] = {
        "status": "failed" if registry_failed else ("passed" if registry_complete and not setup_failed else "blocked"),
        "summary": ("all eleven models matched explicit temporal parameters on HTTP, HTTPS and S3 with no discovery requests"
                    if registry_complete and not setup_failed else
                    "one or more model comparisons or origin audits failed" if registry_failed else
                    "SC-004 is incomplete because one or more controlled transports were unavailable or blocked"),
        "expected_model_transport_cells": len(registry_case_names),
        "completed_model_transport_cells": sum(result is not None for result in registry_results),
        "source_registry_sha256": fixture_inventory["registry_models_sha256"],
    }

    def scenario_gate_status(names: Sequence[str]) -> str:
        results = [manifest["scenarios"].get(name) for name in names]
        if any(result is not None and result.get("status") == "failed" for result in results):
            return "failed"
        if setup_failed or any(result is None or result.get("status") != "passed" for result in results):
            return "blocked"
        return "passed"

    s3_case_names = [
        f"{transport}-registered-partial-override"
        for transport in ("http", "https", "s3")
    ] + [
        f"{transport}-{gap}-{suffix}"
        for transport in ("http", "https", "s3")
        for gap in ("first_missing", "middle_missing", "last_missing", "all_missing")
        for suffix in ("skip", "strict")
    ] + [
        f"{transport}-first-missing-strict-{suffix}"
        for transport in ("http", "https", "s3") for suffix in ("limit-zero", "where-false")
    ]
    s4_case_names = [
        f"{transport}-argument-error-zero-origin-requests"
        for transport in ("http", "https", "s3")
    ] + [
        f"{transport}-fault-{fault}"
        for transport in ("http", "https", "s3")
        for fault in ("http_404", "http_403", "body_404_status_403", "http_412",
                      "http_500", "http_503", "corrupt", "drop")
    ] + [
        f"{transport}-post-fault-recovery"
        for transport in ("http", "https", "s3")
    ] + [
        f"{transport}-post-bind-{action}"
        for transport in ("http", "https", "s3") for action in ("remove", "replace")
    ] + [
        f"{transport}-post-bind-{action}-recovery"
        for transport in ("http", "https", "s3") for action in ("remove", "replace")
    ]
    s3_status = scenario_gate_status(s3_case_names)
    s4_status = scenario_gate_status(s4_case_names)
    manifest["gates"]["S3"] = {
        "status": s3_status,
        "summary": "first/middle/last/all-missing skip and strict semantics, including LIMIT 0 and WHERE false, recorded on all transports",
    }
    manifest["gates"]["S4"] = {
        "status": s4_status,
        "summary": "zero-request argument errors, structured and unclassified faults, post-bind mutation and recovery recorded on all transports",
    }
    transport_statuses = [item.get("status") for item in manifest["transports"].values()]
    failed = setup_failed or "failed" in transport_statuses or any(
        result.get("status") == "failed" for result in manifest["gates"].values())
    blocked = "blocked" in transport_statuses or any(
        result.get("status") == "blocked" for result in manifest["gates"].values())
    if manifest["gates"]["S5"]["status"] == "failed":
        manifest["gates"]["S5"]["summary"] = "one or more remote N160 row or value-free COUNT comparisons failed"
    if manifest["gates"]["S6"]["status"] == "failed":
        manifest["gates"]["S6"]["summary"] = "one or more remote per-child I/O metric assertions failed"
    manifest["status"] = "failed" if failed else ("blocked" if blocked else "passed")
    manifest["identities"]["remote_audit"] = (
        {"path": str(service_audit_copy), "sha256": sha256_file(service_audit_copy),
         "size_bytes": service_audit_copy.stat().st_size} if service_audit_copy.is_file() else None)
    manifest["ended_at_utc"] = utc_now()
    write_json(output / "manifest.json", manifest)
    write_json(output / "commands.json", manifest["commands"])
    (output / "report.md").write_text(render_controlled_remote_report(manifest), encoding="utf-8")
    print(f"{manifest['status']}: controlled remote evidence written to {output}")
    return 0 if manifest["status"] == "passed" else (1 if manifest["status"] == "failed" else 2)


def normalize_utc_datetime(value: str) -> str:
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        try:
            parsed = parsedate_to_datetime(value)
        except (TypeError, ValueError) as error:
            raise ValidationError(f"invalid UTC timestamp: {value}") from error
    if parsed.tzinfo is None:
        raise ValidationError(f"timestamp has no timezone: {value}")
    return parsed.astimezone(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


def parse_head_snapshot(path: Path) -> dict[str, Any]:
    status: int | None = None
    headers: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("HTTP/"):
            parts = line.split()
            status = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else None
        elif ":" in line:
            name, value = line.split(":", 1)
            headers[name.strip().lower()] = value.strip()
    try:
        size_bytes = int(headers["content-length"])
    except (KeyError, ValueError) as error:
        raise ValidationError(f"HEAD snapshot is missing a valid Content-Length: {path}") from error
    if status != 200 or not headers.get("etag") or not headers.get("last-modified"):
        raise ValidationError(f"HEAD snapshot is missing HTTP 200, ETag or Last-Modified: {path}")
    return {"status": status, "etag": headers["etag"], "size_bytes": size_bytes,
            "last_modified": normalize_utc_datetime(headers["last-modified"])}


def load_real_manifest(path: Path) -> dict[str, Any]:
    manifest_path = path.expanduser().resolve()
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValidationError(f"cannot read valid real-object manifest {manifest_path}: {error}") from error
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1 or manifest.get("feature") != FEATURE:
        raise ValidationError("real-object manifest has the wrong schema or feature identity")

    snapshot_records = manifest.get("source_snapshots")
    required_snapshots = {
        "open_data_readme", "live_listing", "live_metadata", "head_chunk_1594",
        "head_chunk_1595", "manual_union_oracle", "series_boundary_oracle",
    }
    if not isinstance(snapshot_records, dict) or not required_snapshots.issubset(snapshot_records):
        raise ValidationError("real-object manifest must identify all source, HEAD and oracle snapshots")
    snapshots: dict[str, dict[str, Any]] = {}
    for name in sorted(required_snapshots):
        record = snapshot_records[name]
        if not isinstance(record, dict):
            raise ValidationError(f"real-object snapshot record is invalid: {name}")
        relative = safe_relative(record.get("path"), f"real-object snapshot {name} path")
        snapshot_path = manifest_path.parent / relative
        if not snapshot_path.is_file():
            raise ValidationError(f"real-object snapshot is missing: {relative.as_posix()}")
        observed_hash = sha256_file(snapshot_path)
        if record.get("sha256") != observed_hash:
            raise ValidationError(f"real-object snapshot SHA-256 mismatch: {relative.as_posix()}")
        snapshots[name] = {"path": str(snapshot_path.resolve()), "sha256": observed_hash,
                           "size_bytes": snapshot_path.stat().st_size}

    producer = manifest.get("producer_provenance")
    if not isinstance(producer, dict):
        raise ValidationError("real-object manifest is missing producer provenance")
    models_relative = safe_relative(producer.get("models_path"), "producer models path")
    models_path = ROOT / models_relative
    if not models_path.is_file() or sha256_file(models_path) != producer.get("models_sha256"):
        raise ValidationError("temporal registry source hash does not match the frozen real-object manifest")
    models_data = json.loads(models_path.read_text(encoding="utf-8"))
    upstream = models_data.get("upstream", {})
    if upstream.get("commit") != producer.get("upstream_commit"):
        raise ValidationError("producer source commit differs from the temporal registry")
    model_key = producer.get("model_key")
    model_entries = [item for item in models_data.get("models", []) if item.get("key") == model_key]
    if len(model_entries) != 1:
        raise ValidationError("real-object model key is not unique in the temporal registry")
    model_entry = model_entries[0]
    if model_entry.get("provenance_status") != "pinned_source_and_archived_metadata_match":
        raise ValidationError("real-object temporal model lacks pinned producer provenance")
    temporal = manifest.get("temporal")
    if not isinstance(temporal, dict):
        raise ValidationError("real-object manifest is missing temporal defaults")
    producer_source = model_entry.get("producer_source", {})
    expected_length = producer_source.get("chunk_time_length", {}).get("value")
    expected_step = producer_source.get("temporal_resolution_seconds", {}).get("value")
    if (temporal.get("chunk_time_length") != expected_length
            or temporal.get("temporal_resolution_seconds") != expected_step):
        raise ValidationError("real-object temporal defaults differ from the pinned producer registry")
    live_metadata = json.loads(Path(snapshots["live_metadata"]["path"]).read_text(encoding="utf-8"))
    if (live_metadata.get("chunk_time_length") != expected_length
            or live_metadata.get("temporal_resolution_seconds") != expected_step):
        raise ValidationError("live metadata snapshot differs from the pinned temporal defaults")

    spatial = manifest.get("spatial_configuration")
    if not isinstance(spatial, dict):
        raise ValidationError("real-object manifest is missing spatial configuration")
    domain = spatial.get("domain")
    domain_relative = safe_relative(producer.get("domain_source_path"), "spatial domain source path")
    domain_path = ROOT / domain_relative
    if (not domain_path.is_file() or sha256_file(domain_path) != producer.get("domain_source_sha256")
            or not domain or f'"{domain}"' not in domain_path.read_text(encoding="utf-8")):
        raise ValidationError("spatial domain provenance is missing, changed or unregistered")

    readme = Path(snapshots["open_data_readme"]["path"]).read_text(encoding="utf-8", errors="replace")
    if not readme.strip() or "ecmwf_ifs025" not in readme:
        raise ValidationError("open-data source snapshot does not identify the selected model")
    try:
        listing_root = ET.parse(snapshots["live_listing"]["path"]).getroot()
    except ET.ParseError as error:
        raise ValidationError(f"live S3 listing snapshot is not valid XML: {error}") from error
    namespace = {"s3": "http://s3.amazonaws.com/doc/2006-03-01/"}
    listing_entries = {
        item.findtext("s3:Key", namespaces=namespace): item
        for item in listing_root.findall("s3:Contents", namespace)
    }

    objects = manifest.get("objects")
    if not isinstance(objects, list) or len(objects) < 2:
        raise ValidationError("real-object manifest must freeze at least two adjacent producer objects")
    indexes = [item.get("chunk_index") if isinstance(item, dict) else None for item in objects]
    if not all(isinstance(index, int) for index in indexes) or indexes != list(range(indexes[0], indexes[0] + len(indexes))):
        raise ValidationError("real-object chunk indexes must be contiguous and ordered")
    prefix_key = manifest.get("object_prefix_key")
    base_url = manifest.get("bucket_base_url")
    parsed_base = urlsplit(base_url if isinstance(base_url, str) else "")
    if (parsed_base.scheme != "https" or parsed_base.hostname != "openmeteo.s3.us-west-2.amazonaws.com"
            or parsed_base.path not in {"", "/"} or parsed_base.query or parsed_base.fragment):
        raise ValidationError("real-object manifest must use the certificate-verified Open-Meteo S3 endpoint")
    if not isinstance(prefix_key, str) or not prefix_key.endswith("/") or ".." in PurePosixPath(prefix_key).parts:
        raise ValidationError("real-object prefix key is invalid")

    axis_starts = manifest.get("manual_axis_starts")
    if not isinstance(axis_starts, dict):
        raise ValidationError("real-object manual oracle is missing per-chunk axis starts")
    epoch = datetime(1970, 1, 1)
    step_seconds = temporal["temporal_resolution_seconds"]
    chunk_length = temporal["chunk_time_length"]
    checked_objects: list[dict[str, Any]] = []
    for item in objects:
        index = item["chunk_index"]
        expected_key = prefix_key + f"chunk_{index:04d}.om"
        expected_url = parsed_base.scheme + "://" + parsed_base.netloc + "/" + expected_key
        if item.get("key") != expected_key or item.get("url") != expected_url:
            raise ValidationError(f"real-object key or URL does not match chunk index {index}")
        listed = listing_entries.get(expected_key)
        if listed is None:
            raise ValidationError(f"live S3 listing snapshot does not contain {expected_key}")
        listed_identity = {
            "etag": listed.findtext("s3:ETag", namespaces=namespace),
            "size_bytes": int(listed.findtext("s3:Size", namespaces=namespace)),
            "last_modified": normalize_utc_datetime(listed.findtext("s3:LastModified", namespaces=namespace)),
        }
        for field, value in listed_identity.items():
            if item.get(field) != value:
                raise ValidationError(f"live S3 listing identity differs for {expected_key}: {field}")
        expected_axis_start = (epoch + timedelta(seconds=index * chunk_length * step_seconds)).isoformat(sep=" ")
        if axis_starts.get(str(index)) != expected_axis_start:
            raise ValidationError(f"manual axis start is not derived from the registered slot count for {expected_key}")
        head_name = f"head_chunk_{index}"
        if head_name not in snapshots:
            raise ValidationError(f"real-object manifest has no frozen HEAD snapshot for {expected_key}")
        head_identity = parse_head_snapshot(Path(snapshots[head_name]["path"]))
        if any(head_identity[field] != item.get(field) for field in ("etag", "size_bytes", "last_modified")):
            raise ValidationError(f"frozen HEAD identity differs from the S3 listing for {expected_key}")
        checked_objects.append({**item, "frozen_head": head_identity})

    window = manifest.get("boundary_window")
    if not isinstance(window, dict):
        raise ValidationError("real-object manifest is missing its boundary window")
    point = spatial.get("point", {})
    expected_start = (epoch + timedelta(seconds=(indexes[1] * chunk_length - 1) * step_seconds)).isoformat(sep=" ")
    expected_end = axis_starts[str(indexes[1])]
    if window.get("start") != expected_start or window.get("end") != expected_end:
        raise ValidationError("frozen validation window does not span the adjacent chunk boundary")
    frozen_oracle = manifest.get("frozen_oracle")
    if not isinstance(frozen_oracle, dict) or frozen_oracle.get("schema") != EXPECTED_SCHEMA:
        raise ValidationError("frozen boundary oracle schema differs from the public series schema")
    expected_rows = frozen_oracle.get("rows")
    if not isinstance(expected_rows, list) or len(expected_rows) != 2:
        raise ValidationError("frozen boundary oracle must contain exactly the two boundary rows")
    expected_times = [window["start"], window["end"]]
    for row, timestamp in zip(expected_rows, expected_times):
        if (not isinstance(row, dict) or row.get("valid_time") != timestamp
                or row.get("lat") != point.get("lat") or row.get("lon") != point.get("lon")):
            raise ValidationError("frozen oracle rows do not match the declared boundary point and timestamps")
    for snapshot_name in ("manual_union_oracle", "series_boundary_oracle"):
        try:
            rows = json.loads(Path(snapshots[snapshot_name]["path"]).read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            raise ValidationError(f"frozen oracle snapshot is not valid JSON: {snapshot_name}") from error
        if Counter(canonical_row(row) for row in rows) != Counter(canonical_row(row) for row in expected_rows):
            raise ValidationError(f"frozen oracle snapshot differs from manifest rows: {snapshot_name}")

    return {"manifest": manifest, "path": str(manifest_path), "sha256": sha256_file(manifest_path),
            "snapshots": snapshots, "models_manifest_path": str(models_path.resolve()),
            "models_manifest_sha256": producer["models_sha256"], "model_entry": model_entry,
            "domain_source_path": str(domain_path.resolve()), "domain_source_sha256": producer["domain_source_sha256"],
            "objects": checked_objects}


def fetch_head_identity(url: str, timeout: int = 30) -> dict[str, Any]:
    started_at = utc_now()
    request = Request(url, method="HEAD", headers={"User-Agent": "duckomo-series-validator/1"})
    try:
        with urlopen(request, timeout=timeout) as response:
            headers = response.headers
            return {"url": url, "status": response.status, "etag": headers.get("ETag"),
                    "size_bytes": int(headers["Content-Length"]) if headers.get("Content-Length") else None,
                    "last_modified": normalize_utc_datetime(headers["Last-Modified"])
                    if headers.get("Last-Modified") else None, "started_at_utc": started_at,
                    "ended_at_utc": utc_now()}
    except HTTPError as error:
        return {"url": url, "status": error.code, "error": "HTTPError", "started_at_utc": started_at,
                "ended_at_utc": utc_now()}
    except (URLError, OSError, TimeoutError, ValidationError) as error:
        return {"url": url, "status": None, "error": f"{type(error).__name__}: {error}",
                "started_at_utc": started_at, "ended_at_utc": utc_now()}


def compare_real_payload(payload: dict[str, Any], frozen_rows: Sequence[dict[str, Any]],
                         frozen_schema: Sequence[dict[str, Any]] = EXPECTED_SCHEMA) -> dict[str, Any]:
    actual_schema = payload.get("actual_schema") or []
    manual_schema = payload.get("manual_schema") or []
    actual_rows = payload.get("actual_rows") or []
    manual_rows = payload.get("manual_rows") or []
    actual_multiset = Counter(canonical_row(row) for row in actual_rows)
    manual_multiset = Counter(canonical_row(row) for row in manual_rows)
    frozen_multiset = Counter(canonical_row(row) for row in frozen_rows)
    actual_only = payload.get("actual_only_rows") or []
    manual_only = payload.get("manual_only_rows") or []
    schema_equal = actual_schema == manual_schema == list(frozen_schema) == EXPECTED_SCHEMA
    sql_equal = not actual_only and not manual_only
    actual_manual_equal = actual_multiset == manual_multiset
    actual_frozen_equal = actual_multiset == frozen_multiset
    manual_frozen_equal = manual_multiset == frozen_multiset
    passed = (schema_equal and sql_equal and actual_manual_equal and actual_frozen_equal
              and manual_frozen_equal and payload.get("actual_count") == payload.get("manual_count")
              and payload.get("actual_count") == len(frozen_rows))
    return {"status": "passed" if passed else "failed", "schema_equal": schema_equal,
            "sql_two_way_except_all_empty": sql_equal, "actual_matches_manual_union": actual_manual_equal,
            "actual_matches_frozen_oracle": actual_frozen_equal,
            "manual_union_matches_frozen_oracle": manual_frozen_equal,
            "actual_count": payload.get("actual_count"), "manual_count": payload.get("manual_count"),
            "frozen_count": len(frozen_rows), "actual_schema": actual_schema, "manual_schema": manual_schema,
            "actual_rows": actual_rows, "manual_rows": manual_rows, "frozen_rows": list(frozen_rows),
            "actual_only_rows": actual_only, "manual_only_rows": manual_only,
            "engine_version": payload.get("engine_version")}


def real_child_read_sql(item: dict[str, Any], axis_start: str, dimensions_sql: str,
                        domain: str, step_seconds: int) -> str:
    axes = ("{'time': {'axis':'time','start':" + sql_timestamp(axis_start)
            + ", 'step':INTERVAL '" + str(step_seconds) + " seconds'}}")
    return ("SELECT value, lat, lon, valid_time FROM read_om(" + sql_string(item["url"])
            + ", dimensions := " + dimensions_sql + ", domain := " + sql_string(domain)
            + ", axes := " + axes + ")")


def real_validation_sql(real_manifest: dict[str, Any]) -> str:
    objects = real_manifest["objects"]
    spatial = real_manifest["spatial_configuration"]
    temporal = real_manifest["temporal"]
    window = real_manifest["boundary_window"]
    point = spatial["point"]
    domain = spatial["domain"]
    model = real_manifest["producer_provenance"]["model_key"]
    dimensions = spatial["dimensions"]
    dimensions_sql = "[" + ",".join(sql_string(name) for name in dimensions) + "]"
    point_filter = f"lat = {float(point['lat'])!r} AND lon = {float(point['lon'])!r}"
    valid_filter = ("valid_time BETWEEN " + sql_timestamp(window["start"])
                    + " AND " + sql_timestamp(window["end"]))
    actual = ("SELECT value, lat, lon, valid_time FROM read_om_series("
              + sql_string(real_manifest["object_prefix_url"])
              + ", model := " + sql_string(model)
              + ", dimensions := " + dimensions_sql
              + ", domain := " + sql_string(domain)
              + ", valid_times := [" + sql_timestamp(window["start"]) + ", "
              + sql_timestamp(window["end"]) + "]"
              + ", strict := true) WHERE " + point_filter + " AND " + valid_filter)
    children = [real_child_read_sql(item, real_manifest["manual_axis_starts"][str(item["chunk_index"])],
                                     dimensions_sql, domain, temporal["temporal_resolution_seconds"])
                for item in objects]
    manual = ("SELECT value, lat, lon, valid_time FROM (" + " UNION ALL ".join(children)
              + ") manual_children WHERE " + point_filter + " AND " + valid_filter)
    row_projection = "struct_pack(value := value, lat := lat, lon := lon, valid_time := valid_time)"
    order_by = "valid_time, lat, lon, value NULLS FIRST"
    return f"""CREATE TEMP TABLE series_actual AS
{actual};
CREATE TEMP TABLE manual_expected AS
{manual};
SELECT to_json(struct_pack(
    engine_version := version(),
    actual_schema := (SELECT list(struct_pack(name := column_name, type := column_type) ORDER BY ordinal_position)
                      FROM (SELECT row_number() OVER () AS ordinal_position, * FROM (DESCRIBE SELECT * FROM series_actual))),
    manual_schema := (SELECT list(struct_pack(name := column_name, type := column_type) ORDER BY ordinal_position)
                      FROM (SELECT row_number() OVER () AS ordinal_position, * FROM (DESCRIBE SELECT * FROM manual_expected))),
    actual_count := (SELECT count(*) FROM series_actual),
    manual_count := (SELECT count(*) FROM manual_expected),
    actual_rows := (SELECT list({row_projection} ORDER BY {order_by}) FROM series_actual),
    manual_rows := (SELECT list({row_projection} ORDER BY {order_by}) FROM manual_expected),
    actual_only_rows := (SELECT list({row_projection} ORDER BY {order_by})
                         FROM (SELECT * FROM series_actual EXCEPT ALL SELECT * FROM manual_expected)),
    manual_only_rows := (SELECT list({row_projection} ORDER BY {order_by})
                         FROM (SELECT * FROM manual_expected EXCEPT ALL SELECT * FROM series_actual))
)) AS validation_result;
"""


def verify_httpfs_load(args: argparse.Namespace, manifest: dict[str, Any], output: Path) -> dict[str, Any]:
    strace = shutil.which("strace")
    expected_path = str(args.httpfs.expanduser().resolve()) if args.httpfs else None
    if not strace or not expected_path:
        return {"status": "blocked", "summary": "strace or the matching HTTPFS extension is unavailable",
                "expected_extension_path": expected_path, "opened_exact_extension": False}
    trace_path = output / "httpfs-load.strace"
    command = [strace, "-f", "-qq", "-e", "trace=openat,openat2", "-o", str(trace_path), "--",
               str(args.duckdb.expanduser().resolve()), "-no-init", "-unsigned", "-bail", ":memory:", "-c",
               "LOAD " + sql_string(expected_path) + "; SELECT 1;"]
    execution = run_command(command, ROOT)
    execution["scenario"] = "verify_explicit_httpfs_load"
    execution["trace_path"] = str(trace_path)
    manifest["commands"].append(execution)
    trace_text = trace_path.read_text(encoding="utf-8", errors="replace") if trace_path.is_file() else ""
    loaded = expected_path in trace_text
    status = "failed" if execution["exit_code"] != 0 else ("passed" if loaded else "blocked")
    result = {"status": status,
              "summary": "strace observed DuckDB opening the exact requested HTTPFS extension file"
              if status == "passed" else "the requested HTTPFS extension was not verified",
              "expected_extension_path": expected_path, "opened_exact_extension": loaded,
              "command_exit_code": execution["exit_code"],
              "trace_path": str(trace_path) if trace_path.is_file() else None,
              "trace_sha256": sha256_file(trace_path) if trace_path.is_file() else None}
    return result


def render_real_report(manifest: dict[str, Any]) -> str:
    gate = manifest.get("gates", {}).get("S7", {})
    lines = ["# read_om_series Real Producer Object Validation", "",
             f"- Feature: `{FEATURE}`", f"- Run status: **{manifest.get('status')}**",
             f"- Started: `{manifest.get('started_at_utc')}`", "- Mode: `real`", "",
             "## S7 gate", "", f"- Status: **{gate.get('status', 'blocked')}** — {gate.get('summary', '')}", "",
             "## Frozen object identities", ""]
    for item in manifest.get("object_checks", []):
        expected = item.get("expected", {})
        observed = item.get("observed", {})
        lines.append(f"- `{item.get('key')}`: **{item.get('status')}**; ETag `{expected.get('etag')}`, "
                     f"size `{expected.get('size_bytes')}`, last modified `{expected.get('last_modified')}`")
        if observed:
            lines.append(f"  - live HEAD: status `{observed.get('status')}`, ETag `{observed.get('etag')}`, "
                         f"size `{observed.get('size_bytes')}`, last modified `{observed.get('last_modified')}`")
    provenance = manifest.get("producer_provenance", {})
    if provenance:
        lines.extend(["", "## Provenance", "",
                      f"- Model key: `{provenance.get('model_key')}`; producer commit `{provenance.get('upstream_commit')}`",
                      f"- Temporal registry SHA-256: `{provenance.get('models_sha256')}`",
                      f"- Live metadata SHA-256: `{manifest.get('identities', {}).get('source_snapshots', {}).get('live_metadata', {}).get('sha256')}`",
                      f"- Domain: `{manifest.get('spatial_configuration', {}).get('domain')}`; point `{manifest.get('spatial_configuration', {}).get('point')}`"])
    comparison = manifest.get("comparison")
    if comparison:
        lines.extend(["", "## Oracle comparison", "",
                      f"- Schema equal: `{comparison.get('schema_equal')}`; SQL two-way EXCEPT ALL empty: "
                      f"`{comparison.get('sql_two_way_except_all_empty')}`",
                      f"- Series/manual multiset equal: `{comparison.get('actual_matches_manual_union')}`; "
                      f"series/frozen oracle equal: `{comparison.get('actual_matches_frozen_oracle')}`; rows "
                      f"`{comparison.get('actual_count')}` vs `{comparison.get('manual_count')}` vs "
                      f"`{comparison.get('frozen_count')}`",
                      f"- Engine: `{comparison.get('engine_version')}`",
                      f"- Frozen rows: `{json.dumps(comparison.get('frozen_rows', []), sort_keys=True)}`"])
    lines.extend(["", "## Input retention", "",
                  "- The validator issues identity HEAD requests and uses DuckDB HTTPFS for the two selected object reads.",
                  "- No OM object bytes are written to the repository; evidence retains listing/metadata/HEAD snapshots, SQL and selected rows only.",
                  "- The result covers one registered IFS025 domain point and one adjacent-chunk boundary window.", ""])
    return "\n".join(lines)


def finish_real_run(manifest: dict[str, Any], output: Path, exit_code: int) -> int:
    manifest["ended_at_utc"] = utc_now()
    write_json(output / "manifest.json", manifest)
    write_json(output / "commands.json", manifest.get("commands", []))
    (output / "report.md").write_text(render_real_report(manifest), encoding="utf-8")
    print(f"{manifest['status']}: S7={manifest.get('gates', {}).get('S7', {}).get('status')}; evidence: {output}")
    return exit_code


def real_main(args: argparse.Namespace) -> int:
    output = args.output.expanduser().resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "queries").mkdir(parents=True, exist_ok=True)
    started = utc_now()
    system = platform.system()
    machine = platform.machine()
    platform_ok = system == "Linux" and machine.lower() in {"aarch64", "arm64"}
    duckdb_record = prerequisite_record("duckdb", args.duckdb)
    extension_record = prerequisite_record("extension", args.extension)
    httpfs_record = (prerequisite_record("httpfs", args.httpfs) if args.httpfs else
                     {"name": "httpfs", "required": True, "status": "missing", "reason": "--httpfs was not supplied"})
    manifest_record = prerequisite_record("real_object_manifest", args.manifest)
    real_data: dict[str, Any] | None = None
    manifest_error: str | None = None
    if manifest_record["status"] == "available":
        try:
            real_data = load_real_manifest(args.manifest)
        except (OSError, ValueError, ValidationError, json.JSONDecodeError) as error:
            manifest_error = str(error)
            manifest_record["status"] = "invalid"
            manifest_record["reason"] = manifest_error
    if real_data:
        manifest_record["sha256"] = real_data["sha256"]
    prerequisites = [duckdb_record, extension_record, httpfs_record, manifest_record,
                     {"name": "linux_arm64", "status": "available" if platform_ok else "unsupported",
                      "reason": None if platform_ok else "real S7 evidence requires Linux AArch64"}]
    manifest: dict[str, Any] = {
        "schema_version": 1, "feature": FEATURE, "mode": "real", "status": "blocked",
        "started_at_utc": started, "ended_at_utc": None,
        "platform": {"system": system, "machine": machine, "python": sys.version.split()[0]},
        "prerequisites": prerequisites, "manifest_validation_error": manifest_error,
        "real_manifest": ({"path": real_data["path"], "sha256": real_data["sha256"],
                            "schema_version": real_data["manifest"].get("schema_version")}
                           if real_data else {"path": str(args.manifest.expanduser().resolve()), "error": manifest_error}),
        "identities": {"duckdb": duckdb_record, "extension": extension_record, "httpfs": httpfs_record,
                       "source_snapshots": real_data["snapshots"] if real_data else {}},
        "commands": [], "object_checks": [], "comparison": None,
        "producer_provenance": real_data["manifest"].get("producer_provenance", {}) if real_data else {},
        "spatial_configuration": real_data["manifest"].get("spatial_configuration", {}) if real_data else {},
        "gates": {"S7": {"status": "blocked", "summary": "real-object prerequisites were not verified"}},
    }
    required_ready = (duckdb_record["status"] == "available" and extension_record["status"] == "available"
                      and httpfs_record["status"] == "available" and manifest_record["status"] == "available"
                      and real_data is not None and platform_ok)
    if not required_ready:
        manifest["gates"]["S7"] = {"status": "blocked", "summary": "one or more real-object prerequisites are missing or invalid"}
        return finish_real_run(manifest, output, 2)

    assert real_data is not None and args.httpfs is not None
    real_manifest = real_data["manifest"]
    duckdb_path = Path(duckdb_record["path"])
    version_command = run_command([str(duckdb_path), "-no-init", "-version"], ROOT)
    manifest["commands"].append(version_command)
    manifest["identities"]["duckdb"]["version"] = version_command["stdout"].strip()
    if version_command["exit_code"] != 0:
        manifest["status"] = "failed"
        manifest["gates"]["S7"] = {"status": "failed", "summary": "DuckDB version command failed"}
        return finish_real_run(manifest, output, 1)

    extension_load = verify_extension_load(args, manifest, output)
    manifest["extension_load_check"] = extension_load
    httpfs_load = verify_httpfs_load(args, manifest, output)
    manifest["httpfs_load_check"] = httpfs_load
    if extension_load["status"] != "passed" or httpfs_load["status"] != "passed":
        statuses = (extension_load["status"], httpfs_load["status"])
        failed = "failed" in statuses
        manifest["status"] = "failed" if failed else "blocked"
        manifest["gates"]["S7"] = {"status": manifest["status"],
                                   "summary": "exact DuckOMO and official HTTPFS extension loads were not both verified"}
        return finish_real_run(manifest, output, 1 if failed else 2)

    all_heads_match = True
    for item in real_data["objects"]:
        observed = fetch_head_identity(item["url"])
        matches = (observed.get("status") == 200 and observed.get("etag") == item.get("etag")
                   and observed.get("size_bytes") == item.get("size_bytes")
                   and observed.get("last_modified") == item.get("last_modified"))
        check = {"key": item["key"], "url": item["url"], "status": "passed" if matches else "blocked",
                 "expected": {field: item.get(field) for field in ("etag", "size_bytes", "last_modified")},
                 "observed": observed, "identity_matches": matches}
        manifest["object_checks"].append(check)
        all_heads_match &= matches
    if not all_heads_match:
        manifest["status"] = "blocked"
        manifest["gates"]["S7"] = {"status": "blocked",
                                   "summary": "live HEAD identity did not match the frozen adjacent objects; SQL scans were not run"}
        return finish_real_run(manifest, output, 2)

    init_path = output / "httpfs-init.sql"
    init_path.write_text("LOAD " + sql_string(str(args.httpfs.resolve())) + ";\n", encoding="utf-8")
    query = real_validation_sql(real_manifest)
    query_path = output / "queries" / "real-boundary-series-vs-manual-union.sql"
    execution_sql = with_extension_load(query, args.extension)
    query_path.write_text(execution_sql, encoding="utf-8")
    command = [str(duckdb_path), "-no-init", "-unsigned", "-json", "-bail", "-init", str(init_path),
               ":memory:", "-f", str(query_path)]
    try:
        execution = run_command(command, ROOT, timeout=300)
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout.decode(errors="replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
        stderr = error.stderr.decode(errors="replace") if isinstance(error.stderr, bytes) else (error.stderr or "")
        execution = {"argv": command, "cwd": str(ROOT), "started_at_utc": started, "ended_at_utc": utc_now(),
                     "elapsed_seconds": 300.0, "exit_code": 124, "stdout": stdout, "stderr": stderr,
                     "timed_out": True}
    execution.update({"scenario": "real_adjacent_boundary", "sql_path": str(query_path),
                      "sql_sha256": hashlib.sha256(execution_sql.encode()).hexdigest(),
                      "query_sha256": hashlib.sha256(query.encode()).hexdigest(),
                      "init_sql_path": str(init_path), "httpfs_sha256": httpfs_record["sha256"]})
    manifest["commands"].append(execution)
    if execution["exit_code"] != 0:
        diagnostic = (execution.get("stderr", "") + "\n" + execution.get("stdout", "")).strip()
        transient = any(marker in diagnostic.lower() for marker in (
            "[transport]", "timed out", "timeout", "connection reset", "name resolution", "could not resolve",
            "http error 404", "http 404", "remote file disappeared"))
        manifest["status"] = "blocked" if transient else "failed"
        manifest["gates"]["S7"] = {"status": manifest["status"],
                                   "summary": "live series/manual query did not complete: "
                                   + re.sub(r"\s+", " ", diagnostic)[:1000]}
        return finish_real_run(manifest, output, 2 if transient else 1)
    try:
        payload = parse_cli_result_with_preface(execution["stdout"])
        comparison = compare_real_payload(payload, real_manifest["frozen_oracle"]["rows"],
                                           real_manifest["frozen_oracle"]["schema"])
    except ValidationError as error:
        manifest["status"] = "failed"
        manifest["gates"]["S7"] = {"status": "failed", "summary": str(error)}
        return finish_real_run(manifest, output, 1)
    manifest["comparison"] = comparison
    if comparison["status"] == "passed":
        manifest["status"] = "passed"
        manifest["gates"]["S7"] = {"status": "passed",
                                   "summary": "live read_om_series, manual read_om UNION ALL and frozen boundary rows matched"}
        return finish_real_run(manifest, output, 0)
    manifest["status"] = "failed"
    manifest["gates"]["S7"] = {"status": "failed",
                               "summary": "live series, manual UNION ALL and frozen boundary oracle differed"}
    return finish_real_run(manifest, output, 1)


def render_report(manifest: dict[str, Any]) -> str:
    if manifest.get("mode") == "real":
        return render_real_report(manifest)
    if manifest.get("mode") == "controlled-remote":
        return render_controlled_remote_report(manifest)
    lines = [
        "# read_om_series Local Validation",
        "",
        f"- Feature: `{FEATURE}`",
        f"- Run status: **{manifest['status']}**",
        f"- Started: `{manifest['started_at_utc']}`",
        f"- Platform: `{manifest['platform']['system']} {manifest['platform']['machine']}`",
        f"- Mode: `{manifest['mode']}`",
        "",
        "## Gate mapping",
        "",
    ]
    for gate, result in manifest["gates"].items():
        lines.append(f"- **{gate}: {result['status']}** — {result['summary']}")
    extension_load = manifest.get("extension_load_check", {})
    lines.extend(["", "## Extension load identity", "",
                  f"- Status: **{extension_load.get('status', 'blocked')}** — {extension_load.get('summary', 'not verified')}"])
    if extension_load.get("expected_extension_path"):
        lines.append(f"- Requested extension: `{extension_load['expected_extension_path']}`")
    if extension_load.get("trace_path"):
        lines.append(f"- Open trace: `{extension_load['trace_path']}`")
    lines.extend(["", "## Local S1 scenarios", ""])
    for name, result in manifest.get("scenarios", {}).items():
        if name.startswith("s5-"):
            continue
        lines.append(f"- `{name}`: **{result['status']}**, rows {result.get('actual_count')} vs {result.get('expected_count')}, "
                     f"schema equal: `{result.get('schema_equal')}`, SQL EXCEPT ALL empty: `{result.get('sql_two_way_except_all_empty')}`")
    lines.extend(["", "## Synthetic N160 S5/S6 worker matrix", ""])
    for name, result in manifest.get("scenarios", {}).items():
        if not name.startswith("s5-"):
            continue
        value_metrics = result.get("value_scan_metrics", {})
        free_metrics = result.get("value_free_count_metrics", {})
        lines.append(f"- `{name}`: **{result.get('status')}**, rows {result.get('actual_count')} vs "
                     f"{result.get('expected_count')}, value scans `{value_metrics.get('status')}`, "
                     f"value-free COUNT scans `{free_metrics.get('status')}`; worker caps "
                     f"`{value_metrics.get('all_children_respect_worker_cap')}`/"
                     f"`{free_metrics.get('all_children_respect_worker_cap')}`, memory bounds "
                     f"`{value_metrics.get('all_query_memory_peaks_within_recorded_upper_bounds')}`/"
                     f"`{free_metrics.get('all_query_memory_peaks_within_recorded_upper_bounds')}`")
    invalid = manifest.get("s2_corpus", {})
    lines.extend(["", "## S2 per-case access audit", "",
                  f"- Status: **{invalid.get('status', 'blocked')}** — {invalid.get('summary', 'not run')}",
                  f"- Preflight cases: `{invalid.get('preflight_case_count', 0)}`; metadata cases: "
                  f"`{invalid.get('metadata_case_count', 0)}`; passed/failed/blocked: "
                  f"`{invalid.get('passed_case_count', 0)}/{invalid.get('failed_case_count', 0)}/"
                  f"{invalid.get('blocked_case_count', 0)}`",
                  f"- SQLLogicTest source SHA-256: `{invalid.get('source_sha256', 'unavailable')}`"])
    for case in invalid.get("cases", []):
        access = ("no candidate access" if not case.get("expected_candidate_paths")
                  else "opened " + ", ".join(Path(item).name for item in case["expected_candidate_paths"]))
        lines.append(f"- `{case.get('case_id')}`: **{case.get('status')}** — {access}; "
                     f"diagnostic matched `{case.get('diagnostic_matches')}`; trace `"
                     f"{case.get('trace_path', 'unavailable')}`")
    lines.extend(["", "## Identity and outputs", "",
                  f"- DuckDB CLI SHA-256: `{manifest.get('identities', {}).get('duckdb', {}).get('sha256', 'unavailable')}`",
                  f"- Extension SHA-256: `{manifest.get('identities', {}).get('extension', {}).get('sha256', 'unavailable')}`",
                  f"- Fixture manifest SHA-256: `{manifest.get('identities', {}).get('fixtures', {}).get('manifest_sha256', 'unavailable')}`",
                  "- Full commands, outputs, query files, diagnostics and per-case access traces are retained beside this report.",
                  "- Controlled HTTP/HTTPS/S3 and real producer evidence remain separate gates and are not inferred from local S2 results.",
                  ""])
    return "\n".join(lines)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duckdb", required=True, type=Path, help="matching DuckDB CLI executable")
    parser.add_argument("--extension", required=True, type=Path, help="local duckomo loadable extension")
    parser.add_argument("--fixtures", type=Path, help="generated build/series-fixtures directory")
    parser.add_argument("--mode", choices=["local", "controlled-remote", "real"], required=True)
    parser.add_argument("--manifest", type=Path, help="frozen adjacent producer object manifest (real mode)")
    parser.add_argument("--httpfs", type=Path, help="matching official HTTPFS extension (remote modes)")
    parser.add_argument("--tls-cert", type=Path, help="certificate for the controlled HTTPS origin")
    parser.add_argument("--tls-key", type=Path, help="private key for the controlled HTTPS origin")
    parser.add_argument("--tls-ca", type=Path, help="CA certificate trusted by DuckDB HTTPFS")
    parser.add_argument("--output", required=True, type=Path, help="run evidence directory")
    args = parser.parse_args(argv)
    if args.mode == "real":
        if args.manifest is None:
            parser.error("--manifest is required in real mode")
        return real_main(args)
    if args.fixtures is None:
        parser.error("--fixtures is required for local and controlled-remote modes")
    if args.mode == "controlled-remote":
        return controlled_remote_main(args)

    output = args.output.expanduser().resolve()
    output.mkdir(parents=True, exist_ok=True)
    queries_dir = output / "queries"
    queries_dir.mkdir(parents=True, exist_ok=True)
    started = utc_now()
    machine = platform.machine().lower()
    system = platform.system()
    platform_ok = system == "Linux" and machine in {"aarch64", "arm64"}

    duckdb_record = prerequisite_record("duckdb", args.duckdb)
    extension_record = prerequisite_record("extension", args.extension)
    fixtures_path = args.fixtures.expanduser().resolve()
    prerequisites = [duckdb_record, extension_record]
    fixture_inventory: dict[str, Any] | None = None
    fixture_problem: str | None = None
    if fixtures_path.is_dir():
        try:
            fixture_inventory = load_fixture_inventory(fixtures_path)
            prerequisites.append({"name": "fixtures", "path": str(fixtures_path), "status": "available",
                                 "manifest_sha256": fixture_inventory["manifest_sha256"]})
        except ValidationError as error:
            fixture_problem = str(error)
            prerequisites.append({"name": "fixtures", "path": str(fixtures_path), "status": "invalid",
                                 "reason": fixture_problem})
    else:
        fixture_problem = f"fixture directory does not exist: {fixtures_path}"
        prerequisites.append({"name": "fixtures", "path": str(fixtures_path), "status": "missing",
                             "reason": fixture_problem})
    if not platform_ok:
        prerequisites.append({"name": "linux_arm64", "status": "unsupported",
                              "reason": "acceptance evidence requires Linux AArch64"})

    manifest: dict[str, Any] = {
        "schema_version": 1,
        "feature": FEATURE,
        "mode": args.mode,
        "status": "blocked",
        "started_at_utc": started,
        "ended_at_utc": None,
        "platform": {"system": system, "machine": platform.machine(), "python": sys.version.split()[0]},
        "prerequisites": prerequisites,
        "identities": {
            "duckdb": duckdb_record,
            "extension": extension_record,
            "fixtures": ({"root": str(fixtures_path), "manifest_path": fixture_inventory["manifest_path"],
                          "manifest_sha256": fixture_inventory["manifest_sha256"],
                          "definition_sha256": fixture_inventory["definition_sha256"],
                          "registry_models_path": fixture_inventory["registry_models_path"],
                          "registry_models_sha256": fixture_inventory["registry_models_sha256"],
                          "registry_equivalence": fixture_inventory["registry_equivalence"],
                          "n160_explicit_grid_sql_source": fixture_inventory["n160_explicit_grid_sql_source"],
                          "n160_explicit_grid_sql_source_sha256": fixture_inventory["n160_explicit_grid_sql_source_sha256"],
                          "objects": [{"id": item.get("id"), "path": item["path"],
                                       "sha256": item["observed_sha256"], "size_bytes": item["observed_size_bytes"]}
                                      for item in fixture_inventory["objects"]]}
                         if fixture_inventory else {"root": str(fixtures_path), "error": fixture_problem}),
        },
        "commands": [],
        "scenarios": {},
        "s2_preflight": {"status": "blocked", "summary": "not run because prerequisites are unavailable"},
        "gates": {
            "S1": {"status": "blocked", "summary": "local scenarios were not run"},
            "S2": {"status": "blocked", "summary": "full per-case invalid-input and metadata mismatch corpus was not run"},
            "SC-004": {"status": "blocked", "summary": "all-entry registered-vs-explicit runtime equivalence was not run"},
            "S5": {"status": "blocked", "summary": "reduced-Gaussian and worker matrix was not run"},
            "S6": {"status": "blocked", "summary": "per-child metric checks were not run"},
        },
    }

    required_ready = duckdb_record["status"] == "available" and extension_record["status"] == "available" \
        and fixture_inventory is not None and platform_ok
    if not required_ready:
        manifest["ended_at_utc"] = utc_now()
        write_json(output / "manifest.json", manifest)
        write_json(output / "commands.json", manifest["commands"])
        (output / "report.md").write_text(render_report(manifest), encoding="utf-8")
        print(f"blocked: prerequisite missing; evidence written to {output}", file=sys.stderr)
        return 2

    assert fixture_inventory is not None
    duckdb_path = Path(duckdb_record["path"])
    extension_path = Path(extension_record["path"])
    version_command = run_command([str(duckdb_path), "-no-init", "-version"], ROOT)
    manifest["commands"].append(version_command)
    manifest["identities"]["duckdb"]["version_command_exit_code"] = version_command["exit_code"]
    manifest["identities"]["duckdb"]["version"] = version_command["stdout"].strip()
    if version_command["exit_code"] != 0:
        manifest["prerequisites"].append({"name": "duckdb_version", "status": "failed",
                                           "reason": version_command["stderr"].strip()})
        manifest["status"] = "failed"
        manifest["gates"]["S1"] = {"status": "failed", "summary": "DuckDB version command failed"}
        manifest["ended_at_utc"] = utc_now()
        write_json(output / "manifest.json", manifest)
        write_json(output / "commands.json", manifest["commands"])
        (output / "report.md").write_text(render_report(manifest), encoding="utf-8")
        return 1

    extension_load_check = verify_extension_load(args, manifest, output)
    manifest["extension_load_check"] = extension_load_check
    if extension_load_check["status"] != "passed":
        gate_status = "failed" if extension_load_check["status"] == "failed" else "blocked"
        for gate in ("S1", "S2", "S5", "S6"):
            manifest["gates"][gate] = {
                "status": gate_status,
                "summary": "local scans were not run because the requested loadable extension was not verified",
            }
        manifest["status"] = gate_status
        manifest["ended_at_utc"] = utc_now()
        write_json(output / "manifest.json", manifest)
        write_json(output / "commands.json", manifest["commands"])
        (output / "report.md").write_text(render_report(manifest), encoding="utf-8")
        print(f"{gate_status}: requested extension load was not verified; evidence: {output}", file=sys.stderr)
        return 1 if gate_status == "failed" else 2

    all_scenarios_passed = True
    for scenario in local_scenarios(fixture_inventory):
        name = scenario["name"]
        query_path = queries_dir / f"{name}.sql"
        query = scenario_sql(name, scenario["prefix"], scenario["start"], scenario["end"],
                             scenario["indices"], scenario.get("endpoint_form", "valid_times"))
        execution_sql = with_extension_load(query, extension_path)
        query_path.write_text(execution_sql, encoding="utf-8")
        command = [str(duckdb_path), "-no-init", "-unsigned", "-json", "-bail",
                   ":memory:", "-f", str(query_path)]
        execution = run_command(command, ROOT)
        execution.update({"scenario": name, "sql_path": str(query_path),
                          "sql_sha256": hashlib.sha256(execution_sql.encode()).hexdigest(),
                          "query_sha256": hashlib.sha256(query.encode()).hexdigest()})
        manifest["commands"].append(execution)
        if execution["exit_code"] != 0:
            scenario_result = {"status": "failed", "summary": "DuckDB command exited non-zero",
                               "exit_code": execution["exit_code"], "stdout": execution["stdout"],
                               "stderr": execution["stderr"]}
        else:
            try:
                payload = parse_cli_result_with_preface(execution["stdout"])
                scenario_result = compare_payload(payload)
                scenario_result["summary"] = "schema and full row multiset match" if scenario_result["status"] == "passed" else "schema or row multiset differs"
            except ValidationError as error:
                scenario_result = {"status": "failed", "summary": str(error),
                                   "stdout": execution["stdout"], "stderr": execution["stderr"]}
        scenario_result["sql_path"] = str(query_path.relative_to(output))
        scenario_result["sql_sha256"] = execution["sql_sha256"]
        manifest["scenarios"][name] = scenario_result
        write_json(output / "scenarios" / f"{name}.json", scenario_result)
        if scenario_result["status"] != "passed":
            all_scenarios_passed = False

    s1_status = "passed" if all_scenarios_passed else "failed"
    manifest["gates"]["S1"] = {
        "status": s1_status,
        "summary": "all local one/two/three-chunk, endpoint, negative-time, NULL and slotless oracles matched"
        if all_scenarios_passed else "one or more local schema or multiset comparisons failed",
    }

    registry_equivalence_passed = True
    for model in fixture_inventory["registry_equivalence"]:
        name = f"sc004-registered-vs-explicit-{model['key']}"
        prefix = str(Path(model["absolute_path"]).parent)
        query = registry_equivalence_sql(prefix, model["key"], model["chunk_time_length"], model["step_seconds"])
        query_path = queries_dir / f"{name}.sql"
        execution_sql = with_extension_load(query, extension_path)
        query_path.write_text(execution_sql, encoding="utf-8")
        command = [str(duckdb_path), "-no-init", "-unsigned", "-json", "-bail",
                   ":memory:", "-f", str(query_path)]
        execution = run_command(command, ROOT)
        execution.update({"scenario": name, "sql_path": str(query_path),
                          "sql_sha256": hashlib.sha256(execution_sql.encode()).hexdigest(),
                          "query_sha256": hashlib.sha256(query.encode()).hexdigest()})
        manifest["commands"].append(execution)
        if execution["exit_code"] != 0:
            result = {"status": "failed", "summary": "registered-vs-explicit SQL exited non-zero",
                      "exit_code": execution["exit_code"], "stderr": execution["stderr"],
                      "model_key": model["key"]}
        else:
            try:
                payload = parse_cli_result_with_preface(execution["stdout"])
                result = compare_registry_equivalence(payload, model)
                result["summary"] = "registered defaults match explicit equivalent parameters by two-way EXCEPT ALL"
            except ValidationError as error:
                result = {"status": "failed", "summary": str(error), "model_key": model["key"]}
        result.update({"sql_path": str(query_path.relative_to(output)),
                       "sql_sha256": execution["sql_sha256"],
                       "fixture_path": model["path"], "fixture_sha256": model["sha256"]})
        manifest["scenarios"][name] = result
        write_json(output / "scenarios" / f"{name}.json", result)
        registry_equivalence_passed &= result["status"] == "passed"
    manifest["gates"]["SC-004"] = {
        "status": "passed" if registry_equivalence_passed else "failed",
        "summary": ("all eleven registered model entries matched explicit equivalent temporal parameters"
                    if registry_equivalence_passed else "one or more registry equivalence checks failed"),
        "model_count": len(fixture_inventory["registry_equivalence"]),
        "source_registry_sha256": fixture_inventory["registry_models_sha256"],
    }

    s5_status, s6_status = execute_local_reduced_gaussian_matrix(args, fixture_inventory, manifest, output)
    manifest["gates"]["S5"] = {
        "status": s5_status,
        "summary": "synthetic N160 domain series matched an explicit-grid manual UNION ALL at 1/2/4 workers"
        if s5_status == "passed" else "one or more reduced-Gaussian row or value-free-count comparisons failed",
    }
    manifest["gates"]["S6"] = {
        "status": s6_status,
        "summary": "each executed child retained a distinct completed scan record with useful or zero value I/O as requested"
        if s6_status == "passed" else "one or more per-child scan metric assertions failed",
    }

    s2_result = run_s2_validation_corpus(args, extension_path, fixture_inventory, output, manifest)
    manifest["s2_corpus"] = s2_result
    manifest["s2_preflight"] = {
        "status": s2_result["status"],
        "summary": s2_result["summary"],
        "preflight_case_count": s2_result["preflight_case_count"],
        "metadata_case_count": s2_result["metadata_case_count"],
        "passed_case_count": s2_result["passed_case_count"],
        "failed_case_count": s2_result["failed_case_count"],
        "blocked_case_count": s2_result["blocked_case_count"],
        "source_path": s2_result["source_path"],
        "source_sha256": s2_result["source_sha256"],
    }
    manifest["gates"]["S2"] = {
        "status": s2_result["status"],
        "summary": s2_result["summary"] if s2_result["status"] == "passed"
        else "S2 corpus " + s2_result["status"] + "; see per-case SQL, diagnostics and access traces",
    }

    manifest["status"] = "failed" if s1_status == "failed" or s2_result["status"] == "failed" \
        or manifest["gates"]["SC-004"]["status"] == "failed" \
        or s5_status == "failed" or s6_status == "failed" else "blocked"
    manifest["ended_at_utc"] = utc_now()
    write_json(output / "manifest.json", manifest)
    write_json(output / "commands.json", manifest["commands"])
    (output / "report.md").write_text(render_report(manifest), encoding="utf-8")
    print(f"{manifest['status']}: S1={s1_status}, S2={manifest['gates']['S2']['status']}; evidence: {output}")
    return 0 if manifest["status"] == "passed" else (1 if manifest["status"] == "failed" else 2)


if __name__ == "__main__":
    raise SystemExit(main())
