#!/usr/bin/env python3
"""Audit cross-version region evidence before allowing P6 to pass."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Any

VERSIONS = ("v1.5.4", "v1.5.5", "v1.5.6")
LOCAL_GATES = ("P0", "P1", "P2", "P3", "P4")
ALL_SOURCES = ("local", "http", "https", "s3")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
SHA1_RE = re.compile(r"^[0-9a-f]{40}$")
MAX_P5_SUMMARY_BYTES = 64 * 1024 * 1024


class EvidenceError(ValueError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise EvidenceError(f"cannot read evidence JSON {path}: {error}") from error


def read_p5_summary(path: Path, label: str) -> Any:
    try:
        if path.stat().st_size > MAX_P5_SUMMARY_BYTES:
            raise EvidenceError(f"{label} exceeds the bounded audit size")
    except OSError as error:
        raise EvidenceError(f"cannot stat evidence JSON {path}: {error}") from error
    return read_json(path)


def valid_sha(value: Any) -> bool:
    return isinstance(value, str) and SHA256_RE.fullmatch(value) is not None


def verify_file(path: Path, expected: Any, label: str) -> None:
    if not valid_sha(expected):
        raise EvidenceError(f"{label} does not contain a valid SHA-256")
    if not path.is_file() or sha256_file(path) != expected:
        raise EvidenceError(f"{label} file is missing or its SHA-256 does not match")


def safe_child(root: Path, relative: Any, label: str) -> Path:
    if not isinstance(relative, str) or not relative or Path(relative).is_absolute():
        raise EvidenceError(f"{label} must be a relative path")
    candidate = (root / relative).resolve()
    try:
        candidate.relative_to(root.resolve())
    except ValueError as error:
        raise EvidenceError(f"{label} escapes its evidence directory") from error
    return candidate


def ensure_safe_output(output: Path, evidence_root: Path) -> None:
    if output.is_symlink():
        raise EvidenceError("output directory must not be a symlink")
    output = output.resolve()
    evidence_root = evidence_root.resolve()
    if output == evidence_root or output in evidence_root.parents or evidence_root in output.parents:
        raise EvidenceError("output directory must not overlap the evidence input tree")
    if output.exists():
        if not output.is_dir() or any(output.iterdir()):
            raise EvidenceError("output directory must be empty")
    else:
        output.mkdir(parents=True)


def gate_manifest_incomplete(manifest: Any, expected_gates: tuple[str, ...]) -> bool:
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        return False
    requested = manifest.get("requested_gates")
    gates = manifest.get("gates")
    if not isinstance(requested, list) or set(requested) != set(expected_gates):
        return True
    if not isinstance(gates, list):
        return True
    ids = [gate.get("id") for gate in gates if isinstance(gate, dict)]
    return len(ids) != len(gates) or len(ids) != len(set(ids)) or set(ids) != set(expected_gates) or \
        manifest.get("status") in {"not-run", "blocked"} or any(
            gate.get("status") in {"not-run", "blocked"} for gate in gates if isinstance(gate, dict))


def gate_manifest_errors(manifest: Any, expected_gates: tuple[str, ...]) -> list[str]:
    errors: list[str] = []
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        return ["run manifest schema is missing or unsupported"]
    incomplete_status = manifest.get("status") in {"not-run", "blocked"}
    requested = manifest.get("requested_gates")
    gates = manifest.get("gates")
    if not isinstance(requested, list) or len(requested) != len(set(requested)):
        if not incomplete_status:
            errors.append("requested gate list is missing or contains duplicates")
    elif set(requested) != set(expected_gates) and not incomplete_status:
        errors.append("run does not request the complete expected gate set")
    if not isinstance(gates, list):
        if not incomplete_status:
            errors.append("run manifest has no completed gate records")
        return errors
    ids = [gate.get("id") for gate in gates if isinstance(gate, dict)]
    if len(ids) != len(gates) or len(ids) != len(set(ids)) or set(ids) != set(expected_gates):
        if not incomplete_status:
            errors.append("recorded gates do not exactly cover the expected gate set")
    if manifest.get("status") not in {"pass", "not-run", "blocked"}:
        errors.append("run manifest is not a passing terminal result")
    elif incomplete_status and any(not isinstance(gate, dict) or gate.get("status") == "fail" for gate in gates):
        errors.append("run manifest records a failed gate")
    for gate in gates:
        if not isinstance(gate, dict):
            continue
        gate_id = gate.get("id", "unknown")
        if gate.get("status") in {"not-run", "blocked"} and incomplete_status:
            continue
        if gate.get("status") != "pass":
            errors.append(f"{gate_id} did not pass")
            continue
        commands = gate.get("commands")
        if not isinstance(commands, list) or not commands:
            errors.append(f"{gate_id} lacks command evidence")
        elif any(not isinstance(command, dict) or command.get("exit_code") != 0
                 for command in commands):
            errors.append(f"{gate_id} contains an incomplete or failed command")
        if gate.get("reason"):
            errors.append(f"{gate_id} has a status reason despite being marked pass")
    return errors


def runtime_records(matrix: Any) -> tuple[dict[str, dict[str, Any]], list[str]]:
    errors: list[str] = []
    if not isinstance(matrix, dict) or not isinstance(matrix.get("runtimes"), list):
        return {}, ["runtime matrix has no runtimes list"]
    records: dict[str, dict[str, Any]] = {}
    for record in matrix["runtimes"]:
        if not isinstance(record, dict) or not isinstance(record.get("version"), str):
            errors.append("runtime matrix contains a malformed record")
            continue
        version = record["version"]
        if version in records:
            errors.append(f"runtime matrix duplicates {version}")
        records[version] = record
    if set(records) - set(VERSIONS):
        errors.append("runtime matrix contains an unsupported DuckDB version")
    for version in VERSIONS:
        record = records.get(version)
        if not record:
            continue
        probes = record.get("probes")
        artifact = record.get("spatial_artifact")
        source_sha = record.get("spatial_source_sha")
        if record.get("gate_status") != "ready_for_adapter_check" or record.get("source_identity_match") is not True:
            errors.append(f"{version} spatial source identity is not verified")
        if not isinstance(source_sha, str) or not SHA1_RE.fullmatch(source_sha):
            errors.append(f"{version} spatial source pin is malformed")
        if not isinstance(probes, dict) or not isinstance(artifact, dict):
            errors.append(f"{version} lacks runtime probes or spatial artifact identity")
            continue
        extension_version = probes.get("extension_version")
        if not isinstance(extension_version, str) or not isinstance(source_sha, str) or \
                not extension_version.startswith(source_sha[:7]):
            errors.append(f"{version} spatial probe does not match its verified source pin")
        for key in ("covers", "geom_from_wkb", "is_valid", "point", "boundary_covers"):
            if probes.get(key) is not True:
                errors.append(f"{version} spatial probe {key} did not pass")
        if not valid_sha(artifact.get("sha256")):
            errors.append(f"{version} spatial package hash is malformed")
        else:
            path = artifact.get("path")
            if not isinstance(path, str) or not Path(path).is_file() or sha256_file(Path(path)) != artifact["sha256"]:
                errors.append(f"{version} spatial package is missing or its hash changed")
        official = record.get("official_runtime")
        if not isinstance(official, dict):
            errors.append(f"{version} official runtime identities are missing")
        else:
            for kind in ("cli", "httpfs"):
                item = official.get(kind)
                if not isinstance(item, dict) or not valid_sha(item.get("sha256")):
                    errors.append(f"{version} {kind} package hash is missing")
                    continue
                path = item.get("path")
                if not isinstance(path, str) or not Path(path).is_file() or sha256_file(Path(path)) != item["sha256"]:
                    errors.append(f"{version} {kind} package is missing or its hash changed")
    return records, errors


def verify_run_manifest(run_dir: Path, expected_gates: tuple[str, ...], version: str,
                        runtime: dict[str, Any], feature_manifest: Path) -> tuple[dict[str, Any] | None, list[str]]:
    path = run_dir / "manifest.json"
    if not path.is_file():
        return None, []
    try:
        manifest = read_json(path)
    except EvidenceError as error:
        return None, [str(error)]
    errors = gate_manifest_errors(manifest, expected_gates)
    expected_region_hash = sha256_file(feature_manifest)
    if manifest.get("manifest_sha256") != expected_region_hash:
        errors.append(f"{version} run does not use the frozen region manifest")

    runtime_version = runtime.get("version")
    official = runtime.get("official_runtime", {})
    spatial = runtime.get("spatial_artifact", {})
    expected_hashes = {
        "duckdb_sha256": official.get("cli", {}).get("sha256"),
        "spatial_sha256": spatial.get("sha256"),
    }
    if manifest.get("runtime_version") != runtime_version:
        errors.append(f"{version} run reports a mismatched runtime version")
    for key, expected in expected_hashes.items():
        if not valid_sha(expected) or manifest.get(key) != expected:
            errors.append(f"{version} run has a mismatched {key}")
    for path_key, hash_key in (("duckdb_path", "duckdb_sha256"),
                               ("spatial_path", "spatial_sha256"),
                               ("duckomo_path", "duckomo_sha256"),
                               ("manifest_path", "manifest_sha256")):
        actual_path = manifest.get(path_key)
        expected_hash = manifest.get(hash_key)
        if not isinstance(actual_path, str) or not Path(actual_path).is_file() or \
                not valid_sha(expected_hash) or sha256_file(Path(actual_path)) != expected_hash:
            errors.append(f"{version} run {path_key} is missing or its recorded hash changed")

    for gate in manifest.get("gates", []) if isinstance(manifest.get("gates"), list) else []:
        if not isinstance(gate, dict):
            continue
        for command in gate.get("commands", []) if isinstance(gate.get("commands"), list) else []:
            if not isinstance(command, dict):
                errors.append(f"{version} command record is malformed")
                continue
            try:
                output = safe_child(run_dir, command.get("output_path"), "command output path")
                verify_file(output, command.get("output_sha256"), f"{version} command output")
            except EvidenceError as error:
                errors.append(str(error))
        report_name = gate.get("evidence_report")
        if report_name is not None:
            try:
                report = safe_child(run_dir, report_name, "gate evidence report")
                verify_file(report, gate.get("evidence_report_sha256"), f"{version} gate report")
            except EvidenceError as error:
                errors.append(str(error))
        elif gate.get("id") in {"P3", "P4", "P5"}:
            errors.append(f"{version} {gate.get('id')} lacks its structured evidence report")
    return manifest, errors


def check_resource_reports(run_dir: Path, manifest: dict[str, Any], version: str) -> list[str]:
    errors: list[str] = []
    gate_by_id = {gate.get("id"): gate for gate in manifest.get("gates", []) if isinstance(gate, dict)}
    p3_gate = gate_by_id.get("P3", {})
    p4_gate = gate_by_id.get("P4", {})
    try:
        p3_path = safe_child(run_dir, p3_gate.get("evidence_report"), "P3 report")
        p3 = read_json(p3_path)
        if p3.get("gate") != "P3" or p3.get("status") != "pass":
            errors.append(f"{version} P3 report is not a passing terminal report")
        if p3.get("small", {}).get("complete") is not True or p3.get("large", {}).get("complete") is not True or \
                p3.get("small", {}).get("owned_complete") is not True or p3.get("large", {}).get("owned_complete") is not True:
            errors.append(f"{version} P3 resource samples are incomplete")
        if p3.get("ratios", {}).get("owned_work_peak", float("inf")) > 2 or \
                p3.get("ratios", {}).get("isolated_process_peak_rss", float("inf")) > 2:
            errors.append(f"{version} P3 failed the independent owned-work and RSS bounds")
        subchecks = p3.get("subchecks", {})
        for key in ("zero_io_filters_parallel_fallback_and_legacy_metrics", "equal_narrow_result",
                    "owned_work_ratio_at_most_2", "process_rss_ratio_at_most_2"):
            if subchecks.get(key) is not True:
                errors.append(f"{version} P3 subcheck {key} did not pass")
        if p3.get("limitations", {}).get("spatial_internal_peak_bytes") is not None or \
                p3.get("limitations", {}).get("spatial_internal_memory_complete") is not False:
            errors.append(f"{version} P3 must preserve unknown spatial-internal memory as unknown")
        for key in ("sample_manifest_sha256", "resource_wkb_manifest_sha256", "resource_wkb_sha256",
                    "region_scan_source_sha256", "duckomo_sha256", "spatial_sha256"):
            if not valid_sha(p3.get("inputs", {}).get(key)):
                errors.append(f"{version} P3 input {key} lacks a SHA-256")
    except (EvidenceError, AttributeError, TypeError) as error:
        errors.append(f"{version} P3 report could not be audited: {error}")
    try:
        p4_path = safe_child(run_dir, p4_gate.get("evidence_report"), "P4 report")
        p4 = read_json(p4_path)
        if p4.get("gate") != "P4" or p4.get("status") != "pass":
            errors.append(f"{version} P4 report is not a passing terminal report")
        resources = p4.get("resources", {})
        if resources.get("owned_complete") is not True or resources.get("exact_complete") is not True or \
                resources.get("owned_work_peak_ratio", float("inf")) > 2 or \
                resources.get("isolated_process_peak_rss_ratio", float("inf")) > 2:
            errors.append(f"{version} P4 resource completeness or dual bound failed")
        for key in ("input_limits", "geometry_adapter_and_state", "cancel_shortread_decoder_objectchange_limit_multiscan_multiconnection_recovery",
                    "dependency_isolation", "ordinary_read_regression", "regions_sql_boundaries"):
            if p4.get("subchecks", {}).get(key) is not True:
                errors.append(f"{version} P4 subcheck {key} did not pass")
        if p4.get("limitations", {}).get("spatial_internal_peak_bytes") is not None or \
                p4.get("limitations", {}).get("spatial_internal_memory_complete") is not False:
            errors.append(f"{version} P4 must preserve unknown spatial-internal memory as unknown")
    except (EvidenceError, AttributeError, TypeError) as error:
        errors.append(f"{version} P4 report could not be audited: {error}")
    return errors


def metric_terminal_errors(metrics: Any, label: str, remote: bool, region: bool) -> list[str]:
    errors: list[str] = []
    if not isinstance(metrics, list) or len(metrics) != 1 or not isinstance(metrics[0], dict):
        return [f"{label} lacks exactly one terminal metrics record"]
    metric = metrics[0]
    outcome = metric.get("outcome", {})
    if outcome.get("status") != "success" or outcome.get("scan_complete") is not True or \
            outcome.get("terminal_published") is not True:
        errors.append(f"{label} is not a successful complete terminal scan")
    values = metric.get("reads", {}).get("value_totals", {})
    for key in ("data_bytes", "decoded_chunks"):
        value = values.get(key)
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            errors.append(f"{label} has unknown {key}; unknown values cannot be treated as zero")
    if values.get("decode_complete") is not True:
        errors.append(f"{label} has incomplete value decode metrics")
    transport = metric.get("transport", {})
    if remote:
        for key in ("response_body_bytes", "attempts", "responses"):
            if transport.get(key) is not None:
                errors.append(f"{label} must retain unknown client transport {key} as null")
        if transport.get("complete") is not False:
            errors.append(f"{label} must mark client transport incomplete")
    if region:
        exact = metric.get("regions", {})
        if exact.get("match_count_complete") is not True or not isinstance(exact.get("matched_pairs_exact"), int):
            errors.append(f"{label} cannot promote observed match counts to exact counts")
    return errors


def p5_summary_errors(report: Any, report_path: Path, targets: list[dict[str, Any]],
                      runtime: dict[str, Any], runtime_matrix_path: Path,
                      expected_hashes: dict[str, str], version: str) -> list[str]:
    errors: list[str] = []
    if not isinstance(report, dict) or report.get("gate") != "P5":
        return [f"{version} P5 report is malformed"]
    if report.get("status") != "pass":
        return [f"{version} P5 report is not a pass"]
    inputs = report.get("inputs", {})
    if report.get("comparison_scope") != "bounded_window" or report.get("full_domain_scanned") is not False or \
            report.get("full_domain_savings_claimed") is not False:
        errors.append(f"{version} P5 must explicitly declare bounded-window validation without full-domain claims")
    windows_path = runtime_matrix_path.parent / "remote-windows.json"
    if not windows_path.is_file() or inputs.get("remote_windows_sha256") != sha256_file(windows_path):
        errors.append(f"{version} P5 bounded-window input identity is missing or changed")
    if inputs.get("runtime_version") != version or inputs.get("runtime_matrix_sha256") != sha256_file(runtime_matrix_path):
        errors.append(f"{version} P5 runtime identity is inconsistent")
    for key, expected in expected_hashes.items():
        if inputs.get(key) != expected:
            errors.append(f"{version} P5 input {key} differs from the frozen artifact")
    target_ids = {target.get("grid_id") for target in targets}
    report_targets = report.get("targets")
    if not isinstance(report_targets, list) or {item.get("grid_id") for item in report_targets if isinstance(item, dict)} != target_ids:
        errors.append(f"{version} P5 does not cover all five frozen targets")
        return errors
    expected_by_id = {target["grid_id"]: target for target in targets}
    for target in report_targets:
        if not isinstance(target, dict):
            errors.append(f"{version} P5 contains a malformed target")
            continue
        grid_id = target.get("grid_id")
        expected = expected_by_id[grid_id]
        if target.get("status") != "pass" or target.get("object_sha256") != expected["object"].get("sha256") or \
                target.get("region_wkb_sha256") != expected["region_wkb"].get("sha256"):
            errors.append(f"{version} P5 target {grid_id} differs from its frozen object or region")
        sources = target.get("sources", {})
        if set(sources) != set(ALL_SOURCES):
            errors.append(f"{version} P5 target {grid_id} is missing a local/HTTP/HTTPS/S3 source")
            continue
        for source in ALL_SOURCES:
            record = sources[source]
            if not isinstance(record, dict) or record.get("status") != "pass":
                errors.append(f"{version} P5 target {grid_id}/{source} did not pass")
                continue
            scans = (record.get("full"), record.get("region"))
            if any(not isinstance(scan, dict) for scan in scans):
                errors.append(f"{version} P5 target {grid_id}/{source} lacks full/local scan records")
                continue
            full, region_scan = scans
            for scan_name, scan, is_region in (("full", full, False), ("region", region_scan, True)):
                result_path_value = scan.get("result_path")
                try:
                    result_path = Path(result_path_value).resolve()
                    result_path.relative_to(report_path.parent.resolve())
                    verify_file(result_path, scan.get("result_sha256"), f"{version} {grid_id}/{source}/{scan_name} result")
                except (EvidenceError, OSError, TypeError, ValueError) as error:
                    errors.append(f"{version} {grid_id}/{source}/{scan_name} result could not be verified: {error}")
                errors.extend(metric_terminal_errors(scan.get("scan_metrics"), f"{version} {grid_id}/{source}/{scan_name}",
                                                    source != "local", is_region))
            full_values = full.get("metrics", {})
            region_values = region_scan.get("metrics", {})
            if full_values.get("scan_complete") is not True or region_values.get("scan_complete") is not True or \
                    full_values.get("decode_complete") is not True or region_values.get("decode_complete") is not True:
                errors.append(f"{version} {grid_id}/{source} contains incomplete scans")
            if region_values.get("matched_pairs_exact") != expected.get("expected_region_rows") or \
                    region_scan.get("result_rows") != expected.get("expected_region_rows"):
                errors.append(f"{version} {grid_id}/{source} region result does not match the frozen exact count")
            if full.get("result_sha256") != full.get("local_result_sha256") or \
                    region_scan.get("result_sha256") != region_scan.get("local_result_sha256"):
                errors.append(f"{version} {grid_id}/{source} result hash differs from its local reference")
            if full.get("result_sha256") != region_scan.get("result_sha256") or \
                    full.get("result_rows") != expected.get("expected_region_rows"):
                errors.append(f"{version} {grid_id}/{source} bounded baseline does not match exact region rows")
            audit = region_scan.get("audit", {})
            full_audit = full.get("audit", {})
            if source != "local":
                for item in (audit, full_audit):
                    for key in ("requests", "sent_body_bytes"):
                        value = item.get(key)
                        if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
                            errors.append(f"{version} {grid_id}/{source} service audit has unknown {key}")
                    if item.get("incomplete_requests") != 0 or item.get("failed_requests") != 0:
                        errors.append(f"{version} {grid_id}/{source} service audit has failed or incomplete requests")
                if source in ("http", "s3"):
                    for label, full_value, region_value in (
                            ("value bytes", full_values.get("data_bytes"), region_values.get("data_bytes")),
                            ("decoded blocks", full_values.get("decoded_blocks"), region_values.get("decoded_blocks")),
                            ("service sent bytes", full_audit.get("sent_body_bytes"), audit.get("sent_body_bytes"))):
                        if not isinstance(full_value, int) or not isinstance(region_value, int) or full_value <= region_value:
                            errors.append(f"{version} {grid_id}/{source} savings do not strictly reduce {label}")
    service_logs = report.get("service_logs", {})
    if set(service_logs) != {"http", "s3"}:
        errors.append(f"{version} P5 service log coverage is incomplete")
    else:
        for name, item in service_logs.items():
            try:
                path = safe_child(report_path.parent, item.get("path"), f"{version} {name} service log path")
                verify_file(path, item.get("sha256"), f"{version} {name} service log")
                if item.get("bytes") != path.stat().st_size:
                    errors.append(f"{version} {name} service log byte count differs")
            except (EvidenceError, AttributeError) as error:
                errors.append(str(error))
    return errors


def audit_p6(root: Path, runtime_matrix_path: Path, evidence_root: Path,
             feature_manifest_path: Path) -> dict[str, Any]:
    root = root.resolve()
    runtime_matrix_path = runtime_matrix_path.resolve()
    evidence_root = evidence_root.resolve()
    feature_manifest_path = feature_manifest_path.resolve()
    errors: list[str] = []
    not_run: list[str] = []
    try:
        runtime_matrix = read_json(runtime_matrix_path)
        feature_manifest = read_json(feature_manifest_path)
    except EvidenceError as error:
        return {"schema_version": 1, "gate": "P6", "status": "not-run", "errors": [], "not_run": [str(error)]}
    records, runtime_errors = runtime_records(runtime_matrix)
    errors.extend(runtime_errors)
    frozen = feature_manifest.get("real_targets", {})
    targets_path_value = frozen.get("path") if isinstance(frozen, dict) else None
    if not isinstance(targets_path_value, str):
        return {"schema_version": 1, "gate": "P6", "status": "fail", "errors": ["feature manifest does not pin the five-target input"], "not_run": []}
    real_targets_path = (feature_manifest_path.parent / targets_path_value).resolve()
    try:
        verify_file(real_targets_path, frozen.get("sha256"), "frozen five-target input")
        targets_doc = read_json(real_targets_path)
        targets = targets_doc.get("targets", [])
        if len(targets) != 5:
            raise EvidenceError("frozen real-target manifest must contain exactly five targets")
    except EvidenceError as error:
        return {"schema_version": 1, "gate": "P6", "status": "fail", "errors": errors + [str(error)], "not_run": []}
    if not evidence_root.is_dir():
        return {"schema_version": 1, "gate": "P6", "status": "not-run", "errors": errors,
                "not_run": [f"evidence root is missing: {evidence_root}"]}

    versions: dict[str, Any] = {}
    for version in VERSIONS:
        runtime = records.get(version)
        if runtime is None:
            not_run.append(f"{version} runtime record is missing")
            continue
        version_root = evidence_root / version
        local_manifest, local_errors = verify_run_manifest(version_root / "P0-P4", LOCAL_GATES,
                                                             version, runtime, feature_manifest_path)
        errors.extend(local_errors)
        p5_manifest, p5_errors = verify_run_manifest(version_root / "P5", ("P5",),
                                                       version, runtime, feature_manifest_path)
        errors.extend(p5_errors)
        if local_manifest is None:
            not_run.append(f"{version} P0-P4 evidence is missing")
        elif gate_manifest_incomplete(local_manifest, LOCAL_GATES):
            not_run.append(f"{version} P0-P4 evidence is incomplete or not-run")
        else:
            errors.extend(check_resource_reports(version_root / "P0-P4", local_manifest, version))
        if p5_manifest is None:
            not_run.append(f"{version} P5 evidence is missing")
        elif gate_manifest_incomplete(p5_manifest, ("P5",)):
            not_run.append(f"{version} P5 gate is incomplete or not-run")
        else:
            gate = p5_manifest.get("gates", [{}])[0]
            report_name = gate.get("evidence_report")
            try:
                report_path = safe_child(version_root / "P5", report_name, f"{version} P5 report path")
                report = read_p5_summary(report_path, f"{version} P5 summary")
                expected = {
                    "region_manifest_sha256": sha256_file(real_targets_path),
                    "duckdb_sha256": runtime["official_runtime"]["cli"]["sha256"],
                    "httpfs_sha256": runtime["official_runtime"]["httpfs"]["sha256"],
                    "spatial_sha256": runtime["spatial_artifact"]["sha256"],
                }
                errors.extend(p5_summary_errors(report, report_path, targets, runtime,
                                                runtime_matrix_path, expected, version))
            except (EvidenceError, KeyError, IndexError, TypeError) as error:
                errors.append(f"{version} P5 report could not be audited: {error}")
        versions[version] = {
            "source_identity_match": runtime.get("source_identity_match") is True,
            "extension_version": runtime.get("probes", {}).get("extension_version") if isinstance(runtime.get("probes"), dict) else None,
            "install_path": runtime.get("probes", {}).get("install_path") if isinstance(runtime.get("probes"), dict) else None,
            "probes": runtime.get("probes"),
            "spatial_sha256": runtime.get("spatial_artifact", {}).get("sha256") if isinstance(runtime.get("spatial_artifact"), dict) else None,
            "p0_p4_manifest_sha256": sha256_file(version_root / "P0-P4" / "manifest.json") if (version_root / "P0-P4" / "manifest.json").is_file() else None,
            "p5_manifest_sha256": sha256_file(version_root / "P5" / "manifest.json") if (version_root / "P5" / "manifest.json").is_file() else None,
        }
    status = "fail" if errors else "not-run" if not_run else "pass"
    return {"schema_version": 1, "gate": "P6", "status": status, "versions": versions,
            "errors": errors, "not_run": not_run,
            "coverage": {version: {"gates": [*LOCAL_GATES, "P5"], "sources": list(ALL_SOURCES),
                                    "targets": [target["grid_id"] for target in targets]}
                         for version in versions}}


def render_p6_markdown(report: dict[str, Any]) -> str:
    lines = ["# Region P6 version matrix audit", "", f"Status: **{report['status']}**", "",
             "| Version | P0–P4/P5 evidence | Spatial source identity | spatial hash |", "|---|---|---|---|"]
    for version in VERSIONS:
        entry = report.get("versions", {}).get(version, {})
        lines.append(f"| {version} | {'recorded' if entry else 'not-run'} | "
                     f"{entry.get('source_identity_match', False)} | `{entry.get('spatial_sha256') or 'missing'}` |")
    if report.get("errors"):
        lines += ["", "## Audit errors", ""] + [f"- {error}" for error in report["errors"]]
    if report.get("not_run"):
        lines += ["", "## Not run", ""] + [f"- {reason}" for reason in report["not_run"]]
    lines += ["", "The audit does not sign P7. Independent reproduction remains a separate gate.", ""]
    return "\n".join(lines)


def audit_cli() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--audit-p6", action="store_true")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--runtime-matrix", type=Path, required=True)
    parser.add_argument("--evidence-root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    try:
        ensure_safe_output(output.parent, args.evidence_root)
        report = audit_p6(args.root, args.runtime_matrix, args.evidence_root, args.manifest)
        output.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        output.with_suffix(".md").write_text(render_p6_markdown(report), encoding="utf-8")
    except (EvidenceError, OSError) as error:
        print(f"region evidence audit: {error}", file=sys.stderr)
        return 2
    print(f"P6 evidence audit: {report['status']}; output={output}")
    return 0 if report["status"] == "pass" else 2 if report["status"] == "not-run" else 1


class RegionEvidenceTest(unittest.TestCase):
    def test_rejects_forged_artifact_hash(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "result.csv"
            path.write_bytes(b"actual result\n")
            with self.assertRaisesRegex(EvidenceError, "SHA-256 does not match"):
                verify_file(path, "0" * 64, "result artifact")

    def test_missing_target_version_stays_not_run(self):
        records, errors = runtime_records({"runtimes": [{"version": version} for version in VERSIONS[:-1]]})
        self.assertNotIn(VERSIONS[-1], records)
        self.assertFalse(any("cover exactly" in error for error in errors))

    def test_missing_version_and_gate_evidence_stays_not_run(self):
        root = Path(__file__).resolve().parents[2]
        with TemporaryDirectory() as directory:
            matrix = Path(directory) / "runtime-matrix.json"
            matrix.write_text(json.dumps({"runtimes": []}), encoding="utf-8")
            evidence_root = Path(directory) / "evidence"
            evidence_root.mkdir()
            report = audit_p6(root, matrix, evidence_root,
                              root / "test/data/regions/manifest.json")
            self.assertEqual(report["status"], "not-run")
            self.assertFalse(report["errors"])
            self.assertTrue(report["not_run"])

    def test_rejects_missing_p5_source(self):
        with TemporaryDirectory() as directory:
            matrix = Path(directory) / "runtime-matrix.json"
            matrix.write_text("{}", encoding="utf-8")
            target = {"grid_id": "grid", "object": {"sha256": "a" * 64},
                      "region_wkb": {"sha256": "b" * 64}, "expected_region_rows": 1}
            report = {"gate": "P5", "status": "pass",
                      "inputs": {"runtime_version": "v1.5.4",
                                 "runtime_matrix_sha256": sha256_file(matrix)},
                      "targets": [{"grid_id": "grid", "status": "pass",
                                   "object_sha256": "a" * 64, "region_wkb_sha256": "b" * 64,
                                   "sources": {source: {"status": "pass"} for source in ALL_SOURCES[:-1]}}]}
            errors = p5_summary_errors(report, Path(directory) / "summary.json", [target], {}, matrix, {}, "v1.5.4")
            self.assertTrue(any("missing a local/HTTP/HTTPS/S3 source" in error for error in errors))

    def test_rejects_full_domain_claim_or_changed_bounded_window_identity(self):
        with TemporaryDirectory() as directory:
            matrix = Path(directory) / "runtime-matrix.json"
            matrix.write_text("{}")
            windows = matrix.parent / "remote-windows.json"
            windows.write_text('{"scope":"bounded_window"}')
            report = {"gate": "P5", "status": "pass", "targets": [],
                      "comparison_scope": "bounded_window", "full_domain_scanned": False,
                      "full_domain_savings_claimed": False,
                      "inputs": {"runtime_version": "v1.5.4", "runtime_matrix_sha256": sha256_file(matrix),
                                 "remote_windows_sha256": sha256_file(windows)}}
            errors = p5_summary_errors(report, matrix.parent / "summary.json", [], {}, matrix, {}, "v1.5.4")
            self.assertFalse(any("bounded-window input identity" in item for item in errors))
            report["full_domain_scanned"] = True
            report["inputs"]["remote_windows_sha256"] = "0" * 64
            errors = p5_summary_errors(report, matrix.parent / "summary.json", [], {}, matrix, {}, "v1.5.4")
            self.assertTrue(any("without full-domain claims" in item for item in errors))
            self.assertTrue(any("bounded-window input identity" in item for item in errors))

    def test_rejects_oversized_p5_summary_before_loading_json(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "summary.json"
            with path.open("wb") as stream:
                stream.truncate(MAX_P5_SUMMARY_BYTES + 1)
            with self.assertRaisesRegex(EvidenceError, "bounded audit size"):
                read_p5_summary(path, "P5 summary")

    def test_unknown_bytes_and_incomplete_observed_region_count_stay_unknown(self):
        metrics = [{
            "outcome": {"status": "success", "scan_complete": True, "terminal_published": True},
            "reads": {"value_totals": {"data_bytes": None, "decoded_chunks": 0, "decode_complete": True}},
            "regions": {"match_count_complete": False, "matched_pairs_exact": 0},
        }]
        errors = metric_terminal_errors(metrics, "fixture", remote=False, region=True)
        self.assertTrue(any("unknown data_bytes" in error for error in errors))
        self.assertTrue(any("cannot promote observed match counts to exact counts" in error for error in errors))

    def test_rejects_owned_work_or_rss_resource_bound_failure(self):
        with TemporaryDirectory() as directory:
            run_dir = Path(directory)
            p3 = {"gate": "P3", "status": "pass",
                  "small": {"complete": True, "owned_complete": True},
                  "large": {"complete": True, "owned_complete": True},
                  "ratios": {"owned_work_peak": 10.0, "isolated_process_peak_rss": 10.0},
                  "subchecks": {key: True for key in ("zero_io_filters_parallel_fallback_and_legacy_metrics",
                                                        "equal_narrow_result", "owned_work_ratio_at_most_2",
                                                        "process_rss_ratio_at_most_2")},
                  "limitations": {"spatial_internal_peak_bytes": None,
                                  "spatial_internal_memory_complete": False},
                  "inputs": {key: "a" * 64 for key in ("sample_manifest_sha256", "resource_wkb_manifest_sha256",
                                                          "resource_wkb_sha256", "region_scan_source_sha256",
                                                          "duckomo_sha256", "spatial_sha256")}}
            p4 = {"gate": "P4", "status": "pass",
                  "resources": {"owned_complete": True, "exact_complete": True,
                                "owned_work_peak_ratio": 10.0, "isolated_process_peak_rss_ratio": 10.0},
                  "subchecks": {key: True for key in ("input_limits", "geometry_adapter_and_state",
                                                        "cancel_shortread_decoder_objectchange_limit_multiscan_multiconnection_recovery",
                                                        "dependency_isolation", "ordinary_read_regression",
                                                        "regions_sql_boundaries")},
                  "limitations": {"spatial_internal_peak_bytes": None,
                                  "spatial_internal_memory_complete": False}}
            (run_dir / "p3.json").write_text(json.dumps(p3), encoding="utf-8")
            (run_dir / "p4.json").write_text(json.dumps(p4), encoding="utf-8")
            manifest = {"gates": [{"id": "P3", "evidence_report": "p3.json"},
                                  {"id": "P4", "evidence_report": "p4.json"}]}

            errors = check_resource_reports(run_dir, manifest, "fixture")

            self.assertTrue(any("owned-work and RSS bounds" in error for error in errors))
            self.assertTrue(any("resource completeness or dual bound failed" in error for error in errors))

    def test_rejects_only_local_gate_report(self):
        manifest = {"schema_version": 1, "status": "pass", "requested_gates": ["P0", "P2"],
                    "gates": [{"id": "P0", "status": "pass", "commands": [{"exit_code": 0}], "reason": ""},
                              {"id": "P2", "status": "pass", "commands": [{"exit_code": 0}], "reason": ""}]}
        self.assertTrue(gate_manifest_errors(manifest, LOCAL_GATES))

    def test_rejects_nonempty_or_overlapping_output(self):
        with TemporaryDirectory() as directory:
            base = Path(directory)
            evidence = base / "evidence"
            evidence.mkdir()
            output = base / "output"
            output.mkdir()
            (output / "keep.txt").write_text("do not overwrite")
            with self.assertRaisesRegex(EvidenceError, "must be empty"):
                ensure_safe_output(output, evidence)
            with self.assertRaisesRegex(EvidenceError, "overlap"):
                ensure_safe_output(evidence / "nested", evidence)

    def test_accepts_exact_requested_gate_coverage(self):
        manifest = {"schema_version": 1, "status": "pass", "requested_gates": list(LOCAL_GATES),
                    "gates": [{"id": gate, "status": "pass", "reason": "",
                               "commands": [{"exit_code": 0}]} for gate in LOCAL_GATES]}
        self.assertEqual(gate_manifest_errors(manifest, LOCAL_GATES), [])


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--audit-p6":
        raise SystemExit(audit_cli())
    unittest.main()
