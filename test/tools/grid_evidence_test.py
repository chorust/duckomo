#!/usr/bin/env python3
"""Unit checks that frozen grid evidence cannot promote missing or partial gates."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import unittest
from datetime import datetime, timezone
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Any


EVIDENCE_LEVELS = (
    "definition-recorded",
    "metadata-checked",
    "coordinate-value-validated",
    "remote-benefit-validated",
)
SUCCESSFUL_GATE = "pass"


def valid_utc(value: Any) -> bool:
    if not isinstance(value, str) or not value.endswith("Z"):
        return False
    try:
        parsed = datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError:
        return False
    return parsed.tzinfo == timezone.utc


def checked_artifact(artifact: Any, root: Path) -> bool:
    if not isinstance(artifact, dict):
        return False
    path_value, expected = artifact.get("path"), artifact.get("sha256")
    if not isinstance(path_value, str) or not isinstance(expected, str) or len(expected) != 64:
        return False
    if any(character not in "0123456789abcdef" for character in expected):
        return False
    path = Path(path_value)
    if not path.is_absolute():
        path = root / path
    if not path.is_file():
        return False
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    return digest == expected


def file_record_hash(inputs: Any, name: str) -> str | None:
    record = inputs.get(name) if isinstance(inputs, dict) else None
    if not isinstance(record, dict):
        return None
    digest = record.get("sha256")
    return digest if isinstance(digest, str) and re.fullmatch(r"[0-9a-f]{64}", digest) else None


def attempt_key(attempt: Any) -> tuple[Any, Any] | None:
    if not isinstance(attempt, dict):
        return None
    request_id, attempt_id = attempt.get("request_id"), attempt.get("attempt")
    if not isinstance(request_id, int) or isinstance(request_id, bool) or \
            not isinstance(attempt_id, int) or isinstance(attempt_id, bool):
        return None
    return request_id, attempt_id


def valid_attempt_audit(audit: Any) -> bool:
    if not isinstance(audit, dict) or audit.get("complete") is not True:
        return False
    client = audit.get("client_attempts")
    server = audit.get("server_attempts")
    if not isinstance(client, list) or not isinstance(server, list) or not client:
        return False
    client_keys = [attempt_key(attempt) for attempt in client]
    server_keys = [attempt_key(attempt) for attempt in server]
    if any(key is None for key in client_keys + server_keys) or sorted(client_keys) != sorted(server_keys):
        return False
    for attempt in client + server:
        body = attempt.get("body_bytes") if isinstance(attempt, dict) else None
        if not isinstance(body, int) or isinstance(body, bool) or body < 0:
            return False
    return sum(item["body_bytes"] for item in client) == sum(item["body_bytes"] for item in server)


def gate_errors(gate: Any, root: Path) -> list[str]:
    if not isinstance(gate, dict):
        return ["gate entry is not an object"]
    status = gate.get("status")
    if not isinstance(status, str) or status not in {"pass", "fail", "not-run"}:
        return ["gate status is not pass, fail, or not-run"]
    if status == "not-run":
        return [] if isinstance(gate.get("reason"), str) and gate["reason"].strip() else [
            "not-run gate needs a concrete reason"
        ]
    if status == "fail":
        return [] if isinstance(gate.get("reason"), str) and gate["reason"].strip() else [
            "failed gate needs a concrete reason"
        ]

    errors: list[str] = []
    snapshot = gate.get("input_snapshot")
    if not isinstance(snapshot, dict) or not valid_utc(snapshot.get("frozen_at_utc")):
        errors.append("passing gate lacks a valid frozen input timestamp")
    commands = gate.get("commands")
    if not isinstance(commands, list) or not commands:
        errors.append("passing gate lacks exact command records")
    elif any(not isinstance(command, dict) or not isinstance(command.get("argv"), list) or
             not command["argv"] or command.get("exit_code") != 0 for command in commands):
        errors.append("passing gate has a missing command or non-zero exit code")
    artifacts = gate.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts or any(not checked_artifact(item, root) for item in artifacts):
        errors.append("passing gate lacks a verified result/oracle artifact hash")

    if gate.get("id") == "H8":
        errors.extend(h8_summary_errors(gate, root))
        return errors

    scans = gate.get("scans")
    if not isinstance(scans, list) or not scans:
        errors.append("passing gate lacks per-scan metrics v4 snapshots")
    else:
        for scan in scans:
            if not isinstance(scan, dict) or scan.get("schema_version") != 4:
                errors.append("gate scan is missing a v4 snapshot")
                continue
            if gate.get("requires_cost") is True:
                if gate.get("contract") != "official-httpfs-20261008":
                    transport = scan.get("transport")
                    if not isinstance(transport, dict) or transport.get("complete") is not True or \
                            not isinstance(transport.get("response_body_bytes"), int) or \
                            isinstance(transport.get("response_body_bytes"), bool):
                        errors.append("cost gate has unknown or incomplete transport byte accounting")
                memory = scan.get("memory")
                if not isinstance(memory, dict) or memory.get("query_owned_complete") is not True:
                    errors.append("cost gate has unknown or incomplete query-owned memory accounting")
                outcome = scan.get("outcome")
                if not isinstance(outcome, dict) or outcome.get("status") != "success" or \
                        outcome.get("scan_complete") is not True or outcome.get("terminal_published") is not True:
                    errors.append("cost gate includes an unsuccessful or early-stopped scan")

    if gate.get("id") == "H6" and gate.get("contract") != "official-httpfs-20261008" and not valid_attempt_audit(gate.get("server_audit")):
        errors.append("H6 passing evidence does not reconcile all client/server physical attempts")
    if gate.get("contract") == "official-httpfs-20261008" and gate.get("requires_cost") is True:
        audit = gate.get("server_audit", {})
        if audit.get("scope") != "service sent bytes" or audit.get("complete") is not True:
            errors.append("official cost gate lacks independently complete service-sent audit")
        for scan in gate.get("scans", []):
            transport = scan.get("transport", {})
            if transport.get("complete") is not False or any(transport.get(key) is not None for key in ("response_body_bytes", "attempts", "responses")):
                errors.append("official remote transport must remain unknown")
    return errors


def h8_summary_errors(gate: dict[str, Any], root: Path) -> list[str]:
    artifacts = gate.get("artifacts")
    if not isinstance(artifacts, list) or len(artifacts) != 1 or not checked_artifact(artifacts[0], root):
        return ["H8 lacks one hash-verified matrix summary"]
    summary_path = Path(artifacts[0]["path"])
    if not summary_path.is_absolute():
        summary_path = root / summary_path
    try:
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return ["H8 matrix summary is missing or invalid JSON"]
    if not isinstance(summary, dict) or summary.get("gate") != "H8" or \
            summary.get("status") != "pass" or summary.get("identity_status") != "pass":
        return ["H8 matrix summary does not report a passing identity and gate status"]
    if summary.get("contract") == "official-httpfs-20261008":
        records = summary.get("pairs", [])
        if {record.get("pair_id") for record in records} != {"v1.5.4", "v1.5.5", "v1.5.6"}:
            return ["H8 requires all three official releases"]
        if not all(record.get("identity_status") == "pass" and record.get("full_grid_gates") == "pass" for record in records):
            return ["H8 official runtime identity alone cannot replace full grid gate evidence"]
        return []
    pair_records = summary.get("pairs")
    if not isinstance(pair_records, list) or {pair.get("pair_id") for pair in pair_records
                                              if isinstance(pair, dict)} != {
                                                  "baseline-1.5.4", "prerelease-2.0-dev"
                                              } or len(pair_records) != 2:
        return ["H8 matrix summary does not contain both fixed version pairs"]
    common_snapshot = summary.get("frozen_input_set_sha256")
    if not isinstance(common_snapshot, str) or not re.fullmatch(r"[0-9a-f]{64}", common_snapshot):
        return ["H8 matrix summary lacks a shared frozen input-set hash"]
    errors: list[str] = []
    matrix_hash = summary.get("matrix_sha256")
    sample_hash = summary.get("sample_manifest_sha256")
    if not isinstance(matrix_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", matrix_hash) or \
            not isinstance(sample_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", sample_hash):
        errors.append("H8 matrix summary lacks frozen matrix/sample hashes")
    for pair in pair_records:
        if not isinstance(pair, dict) or pair.get("identity_status") != "pass" or \
                pair.get("compatibility_status") != "pass":
            errors.append("H8 pair build identity or compatibility checks did not pass")
            continue
        run_record = pair.get("run_evidence", {}).get("complete_h0_h7_run") \
            if isinstance(pair.get("run_evidence"), dict) else None
        if not isinstance(run_record, dict) or run_record.get("accepted_as_complete_h0_h7") is not True or \
                run_record.get("frozen_input_set_sha256") != common_snapshot:
            errors.append("H8 pair lacks an audited H0-H7 run over the shared frozen inputs")
            continue
        run_path_value = run_record.get("path")
        if not isinstance(run_path_value, str):
            errors.append("H8 pair run manifest path is missing")
            continue
        run_path = Path(run_path_value)
        if not run_path.is_absolute():
            run_path = root / run_path
        try:
            run_manifest = json.loads(run_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            errors.append("H8 pair run manifest is missing or invalid JSON")
            continue
        errors.extend(f"H8 {pair.get('pair_id')} run: {error}" for error in validate_run(run_manifest, root))
        if not isinstance(run_manifest, dict) or run_manifest.get("status") != "success" or \
                set(run_manifest.get("requested_gates", [])) != {f"H{index}" for index in range(8)} or \
                run_manifest.get("frozen_input_set_sha256") != common_snapshot:
            errors.append("H8 pair run does not prove all H0-H7 passed on the shared frozen inputs")
            continue
        run_inputs = run_manifest.get("inputs")
        if file_record_hash(run_inputs, "matrix") != matrix_hash or \
                file_record_hash(run_inputs, "sample_manifest") != sample_hash:
            errors.append("H8 pair run does not use the matrix/sample inputs frozen in its summary")
        audit_path = run_path.parent / "evidence-audit.json"
        try:
            audit = json.loads(audit_path.read_text(encoding="utf-8"))
            manifest_digest = hashlib.sha256(run_path.read_bytes()).hexdigest()
        except (OSError, json.JSONDecodeError):
            errors.append("H8 pair run has no readable evidence-audit record")
            continue
        if not isinstance(audit, dict) or audit.get("status") != "pass" or \
                audit.get("manifest_sha256") != manifest_digest:
            errors.append("H8 pair run evidence audit does not match its manifest")
    return errors


def sample_errors(sample: Any, root: Path) -> list[str]:
    if not isinstance(sample, dict):
        return ["sample entry is not an object"]
    level = sample.get("evidence_level")
    if level not in EVIDENCE_LEVELS:
        return ["sample evidence level is unknown"]
    errors: list[str] = []
    if sample.get("classification") == "real_public_open_meteo_om_v3" and level in EVIDENCE_LEVELS[2:]:
        for field in ("coordinate_reference", "official_value_reference"):
            reference = sample.get(field)
            if not isinstance(reference, dict) or not checked_artifact(reference, root):
                errors.append(f"real sample claims coordinate/value validation without a hashed {field}")
    if sample.get("inherits_evidence_from"):
        errors.append("support evidence cannot be inherited from another domain")
    tolerance = sample.get("coordinate_tolerance_degrees")
    policy = sample.get("numeric_policy")
    maximum = {"openmeteo_f32_v1": 1e-4, "float64_v1": 1e-8}.get(policy)
    if level in EVIDENCE_LEVELS[2:] and (
            not isinstance(tolerance, (int, float)) or isinstance(tolerance, bool) or
            maximum is None or tolerance <= 0 or tolerance > maximum):
        errors.append("validated sample lacks a frozen tolerance within its numeric-policy limit")
    return errors


def strict_cost_benefit(full_scan: Any, local_scan: Any) -> bool:
    """Require complete, successful counters and strict decline in every declared cost."""
    def values(scan: Any) -> tuple[int, int, int] | None:
        if not isinstance(scan, dict) or scan.get("schema_version") != 4:
            return None
        outcome, transport, memory, totals = (scan.get("outcome"), scan.get("transport"),
                                              scan.get("memory"), scan.get("value_totals"))
        if not isinstance(outcome, dict) or outcome.get("status") != "success" or \
                outcome.get("scan_complete") is not True or outcome.get("terminal_published") is not True:
            return None
        if not isinstance(transport, dict) or transport.get("complete") is not True or \
                not isinstance(transport.get("response_body_bytes"), int):
            return None
        if not isinstance(memory, dict) or memory.get("query_owned_complete") is not True:
            return None
        if not isinstance(totals, dict) or totals.get("decode_complete") is not True:
            return None
        costs = (transport["response_body_bytes"], totals.get("data_bytes"), totals.get("decoded_chunks"))
        if any(not isinstance(cost, int) or isinstance(cost, bool) or cost < 0 for cost in costs):
            return None
        return costs

    full = values(full_scan)
    local = values(local_scan)
    return full is not None and local is not None and all(left > right for left, right in zip(full, local))


def validate_run(manifest: Any, root: Path) -> list[str]:
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        return ["run manifest must be a schema_version=1 object"]
    errors: list[str] = []
    run_status = manifest.get("status")
    if not isinstance(run_status, str) or run_status not in {"success", "fail", "not-run"}:
        errors.append("run status is not success, fail, or not-run")
    gates = manifest.get("gates")
    if not isinstance(gates, list) or not gates:
        errors.append("run manifest has no gates")
    else:
        seen: set[str] = set()
        for gate in gates:
            gate_id = gate.get("id") if isinstance(gate, dict) else None
            if not isinstance(gate_id, str) or gate_id in seen:
                errors.append("run manifest has a missing or duplicate gate id")
            else:
                seen.add(gate_id)
            errors.extend(f"{gate_id or 'gate'}: {error}" for error in gate_errors(gate, root))
        if run_status == "success" and any(not isinstance(gate, dict) or gate.get("status") != "pass"
                                            for gate in gates):
            errors.append("successful run contains a missing, failed, or not-run requested gate")
        requested_gates = manifest.get("requested_gates")
        if not isinstance(requested_gates, list) or any(not isinstance(gate_id, str) for gate_id in requested_gates):
            if run_status == "success":
                errors.append("successful run lacks its frozen requested-gate list")
        elif set(requested_gates) != seen:
            errors.append("recorded gates do not exactly match the requested gate set")
    samples = manifest.get("samples", [])
    if not isinstance(samples, list):
        errors.append("samples must be an array")
    else:
        seen_domains: set[str] = set()
        for sample in samples:
            domain = sample.get("domain_id") if isinstance(sample, dict) else None
            if not isinstance(domain, str) or domain in seen_domains:
                errors.append("sample evidence must have unique per-domain identities")
            else:
                seen_domains.add(domain)
            errors.extend(f"{domain or 'sample'}: {error}" for error in sample_errors(sample, root))
        required_domains = manifest.get("required_domains", [])
        if not isinstance(required_domains, list) or any(not isinstance(domain, str) for domain in required_domains):
            errors.append("required_domains must be an array of frozen domain IDs")
        elif not set(required_domains).issubset(seen_domains):
            missing = sorted(set(required_domains) - seen_domains)
            errors.append("run manifest omits required sample domains: " + ", ".join(missing))
    for audit_key, gate_id in (("h3_local_audit", "H3"), ("h7_local_audit", "H7")):
        if audit_key in manifest:
            errors.extend(f"{gate_id} local subcheck: {error}"
                          for error in local_subcheck_errors(manifest[audit_key], gate_id, root))
    return errors


def local_subcheck_errors(audit: Any, gate_id: str, root: Path) -> list[str]:
    if not isinstance(audit, dict):
        return ["local audit record is not an object"]
    manifest_artifact = audit.get("manifest")
    if not checked_artifact(manifest_artifact, root):
        return ["nested local manifest is missing or has a hash mismatch"]
    manifest_path = Path(manifest_artifact["path"])
    if not manifest_path.is_absolute():
        manifest_path = root / manifest_path
    try:
        local_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return ["nested local manifest is unreadable"]
    if not isinstance(local_manifest, dict) or local_manifest.get("schema_version") != 1 or \
            local_manifest.get("gate") != gate_id:
        return ["nested local manifest has the wrong schema or gate"]

    status = local_manifest.get("local_synthetic_status")
    if status not in {"pass", "fail", "not-run"}:
        return ["nested local manifest has an invalid local_synthetic_status"]
    errors: list[str] = []
    if local_manifest.get("full_gate_status") != "not-run":
        errors.append("local subcheck cannot promote the full gate")
    for section in ("inputs", "outputs"):
        records = local_manifest.get(section)
        if not isinstance(records, dict):
            errors.append(f"nested local manifest lacks an {section} record map")
            continue
        for name, artifact in records.items():
            if not isinstance(artifact, dict):
                errors.append(f"nested local {section} artifact {name} is malformed")
                continue
            exists = artifact.get("exists")
            if exists is True and not checked_artifact(artifact, root):
                errors.append(f"nested local {section} artifact {name} is missing or has a hash mismatch")
            if status == "pass" and exists is not True:
                errors.append(f"passing local subcheck lacks {section} artifact {name}")

    if status == "pass":
        top_result = audit.get("synthetic_report", audit.get("public_source_report"))
        if not checked_artifact(top_result, root):
            errors.append("passing local subcheck lacks its hash-verified result report")
    command_records: list[dict[str, Any]] = []

    def collect_commands(value: Any) -> None:
        if isinstance(value, dict):
            if "argv" in value or "exit_code" in value:
                command_records.append(value)
                return
            for child in value.values():
                collect_commands(child)
        elif isinstance(value, list):
            for child in value:
                collect_commands(child)

    collect_commands(local_manifest.get("commands", local_manifest.get("command")))
    if status == "pass" and (not command_records or any(item.get("exit_code") != 0 for item in command_records)):
        errors.append("passing local subcheck lacks successful exact command records")
    top_command = audit.get("command")
    if status == "pass" and (not isinstance(top_command, dict) or top_command.get("exit_code") != 0):
        errors.append("passing local subcheck lacks a successful runner command record")
    return errors


class GridEvidenceContractTest(unittest.TestCase):
    def test_nested_local_subcheck_hashes_are_audited_without_promoting_gate(self) -> None:
        with TemporaryDirectory(prefix="duckomo-local-subcheck-audit-") as directory:
            root = Path(directory)
            validator = root / "validator.py"
            validator.write_text("pass\n", encoding="utf-8")
            report = root / "report.json"
            report.write_text("{\"status\":\"synthetic_local_pass\"}\n", encoding="utf-8")

            def record(path: Path) -> dict[str, Any]:
                return {
                    "path": str(path),
                    "exists": True,
                    "executable": False,
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                }

            local_manifest_path = root / "h7-local-manifest.json"
            local_manifest = {
                "schema_version": 1,
                "gate": "H7",
                "local_synthetic_status": "pass",
                "full_gate_status": "not-run",
                "reason": "synthetic checks passed; full H7 remains incomplete",
                "inputs": {"validator": record(validator)},
                "outputs": {"synthetic_report": record(report)},
                "command": {"argv": ["validator.py", "--validate"], "exit_code": 0},
            }
            local_manifest_path.write_text(json.dumps(local_manifest), encoding="utf-8")
            manifest = {
                "schema_version": 1,
                "status": "not-run",
                "gates": [{"id": "H7", "status": "not-run", "reason": "producer-axis proof is absent"}],
                "h7_local_audit": {
                    "manifest": record(local_manifest_path),
                    "synthetic_report": record(report),
                    "command": {"argv": ["runner", "--cases", "H7"], "exit_code": 0},
                },
            }
            self.assertEqual(validate_run(manifest, root), [])

            report.write_text("{\"status\":\"changed\"}\n", encoding="utf-8")
            errors = validate_run(manifest, root)
            self.assertTrue(any("outputs artifact synthetic_report" in error for error in errors))

    def test_h8_cannot_pass_from_build_identity_without_both_full_matrix_runs(self) -> None:
        with TemporaryDirectory(prefix="duckomo-h8-evidence-test-") as directory:
            root = Path(directory)
            summary_path = root / "h8-summary.json"
            pairs = [
                {"pair_id": pair_id, "identity_status": "pass", "compatibility_status": "pass",
                 "run_evidence": {"complete_h0_h7_run": None}}
                for pair_id in ("baseline-1.5.4", "prerelease-2.0-dev")
            ]
            summary_path.write_text(json.dumps({
                "schema_version": 1,
                "gate": "H8",
                "status": "pass",
                "identity_status": "pass",
                "matrix_sha256": "a" * 64,
                "sample_manifest_sha256": "b" * 64,
                "frozen_input_set_sha256": "c" * 64,
                "pairs": pairs,
            }), encoding="utf-8")
            gate = {
                "id": "H8",
                "status": "pass",
                "reason": "matrix identity checked",
                "input_snapshot": {"frozen_at_utc": "2026-10-07T00:00:00Z"},
                "commands": [{"argv": ["audit-grid-version-matrix.py"], "exit_code": 0}],
                "artifacts": [{"path": str(summary_path),
                               "sha256": hashlib.sha256(summary_path.read_bytes()).hexdigest()}],
            }
            errors = gate_errors(gate, root)
            self.assertTrue(any("lacks an audited H0-H7 run" in error for error in errors))

    def test_not_run_reason_does_not_promote_missing_sample(self) -> None:
        gate = {"id": "H0", "status": "not-run", "reason": "N160 input is not acquired"}
        self.assertEqual(gate_errors(gate, Path(".")), [])
        self.assertTrue(gate_errors({"id": "H0", "status": "not-run"}, Path(".")))

    def test_success_cannot_omit_requested_gate_or_required_domain(self) -> None:
        manifest = {
            "schema_version": 1,
            "status": "success",
            "requested_gates": ["H0", "H1"],
            "gates": [{"id": "H0", "status": "pass"}],
            "required_domains": ["n160"],
            "samples": [],
        }
        errors = validate_run(manifest, Path("."))
        self.assertTrue(any("frozen requested-gate list" in error or "requested gate set" in error for error in errors))
        self.assertTrue(any("required sample domains" in error for error in errors))

    def test_pass_requires_frozen_inputs_commands_and_hashed_outputs(self) -> None:
        gate = {
            "id": "H1", "status": "pass", "input_snapshot": {"frozen_at_utc": "2026-10-06T00:00:00Z"},
            "commands": [{"argv": ["duckdb", "query.sql"], "exit_code": 0}],
            "artifacts": [], "scans": [{"schema_version": 4}],
        }
        errors = gate_errors(gate, Path("."))
        self.assertTrue(any("artifact hash" in error for error in errors))
        gate["commands"][0]["exit_code"] = 2
        self.assertTrue(any("non-zero" in error for error in gate_errors(gate, Path("."))))

    def test_unknown_cost_and_limit_scan_cannot_be_counted_as_benefit(self) -> None:
        gate = {
            "id": "H6", "status": "pass", "requires_cost": True,
            "input_snapshot": {"frozen_at_utc": "2026-10-06T00:00:00Z"},
            "commands": [{"argv": ["gate"], "exit_code": 0}],
            "artifacts": [{"path": "absent.csv", "sha256": "0" * 64}],
            "scans": [{"schema_version": 4, "outcome": {"status": "success", "scan_complete": False,
                                                               "terminal_published": True}}],
            "server_audit": {"complete": True, "client_attempts": [], "server_attempts": []},
        }
        errors = gate_errors(gate, Path("."))
        self.assertTrue(any("unknown or incomplete transport" in error for error in errors))
        self.assertTrue(any("early-stopped" in error for error in errors))
        self.assertTrue(any("reconcile" in error for error in errors))

    def test_official_cost_gates_still_require_complete_scans_and_owned_memory(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            artifact = root / "result.csv"
            artifact.write_text("verified result\n")
            scan = {"schema_version": 4,
                    "transport": {"complete": False, "response_body_bytes": None,
                                  "attempts": None, "responses": None},
                    "memory": {"query_owned_complete": True},
                    "outcome": {"status": "success", "scan_complete": True, "terminal_published": True}}
            gate = {"id": "H6", "status": "pass", "requires_cost": True,
                    "contract": "official-httpfs-20261008",
                    "input_snapshot": {"frozen_at_utc": "2026-10-08T00:00:00Z"},
                    "commands": [{"argv": ["gate"], "exit_code": 0}],
                    "artifacts": [{"path": artifact.name,
                                   "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest()}],
                    "server_audit": {"scope": "service sent bytes", "complete": True},
                    "scans": [scan]}
            self.assertEqual(gate_errors(gate, root), [])
            for outcome in ({"status": "cancelled", "scan_complete": False, "terminal_published": True},
                            {"status": "success", "scan_complete": False, "terminal_published": True},
                            {"status": "success", "scan_complete": True, "terminal_published": False}):
                with self.subTest(outcome=outcome):
                    scan["outcome"] = outcome
                    self.assertTrue(any("early-stopped" in error for error in gate_errors(gate, root)))
            scan["outcome"] = {"status": "success", "scan_complete": True, "terminal_published": True}
            for memory in (None, {}, {"query_owned_complete": False}):
                with self.subTest(memory=memory):
                    scan["memory"] = memory
                    self.assertTrue(any("query-owned memory" in error for error in gate_errors(gate, root)))

    def test_real_domain_evidence_is_not_inherited_or_promoted_by_metadata(self) -> None:
        sample = {
            "domain_id": "n320", "classification": "real_public_open_meteo_om_v3",
            "evidence_level": "coordinate-value-validated", "numeric_policy": "openmeteo_f32_v1",
            "coordinate_tolerance_degrees": 0.01, "inherits_evidence_from": "o1280",
        }
        errors = sample_errors(sample, Path("."))
        self.assertTrue(any("coordinate_reference" in error for error in errors))
        self.assertTrue(any("cannot be inherited" in error for error in errors))
        self.assertTrue(any("frozen tolerance" in error for error in errors))

    def test_registered_definitions_use_supported_evidence_levels(self) -> None:
        root = Path(__file__).resolve().parents[2]
        registry = json.loads((root / "test/data/grids/definitions.json").read_text())
        for definition in registry["definitions"]:
            with self.subTest(domain=definition["id"]):
                self.assertIn(definition["evidence"]["level"], EVIDENCE_LEVELS)

    def test_value_only_evidence_does_not_establish_coordinate_validation(self) -> None:
        with TemporaryDirectory(prefix="duckomo-value-evidence-test-") as directory:
            root = Path(directory)
            oracle = root / "values.csv"
            oracle.write_text("index,value\n0,1\n")
            sample = {
                "domain_id": "ecmwf_ifs", "classification": "real_public_open_meteo_om_v3",
                "evidence_level": "metadata-checked", "numeric_policy": "openmeteo_f32_v1",
                "coordinate_tolerance_degrees": 1e-4,
                "official_value_reference": {
                    "path": "values.csv", "sha256": hashlib.sha256(oracle.read_bytes()).hexdigest(),
                },
            }
            self.assertEqual(sample_errors(sample, root), [])
            sample["evidence_level"] = "coordinate-value-validated"
            self.assertTrue(any("coordinate_reference" in error for error in sample_errors(sample, root)))
            sample["evidence_level"] = "value-validated"
            self.assertEqual(sample_errors(sample, root), ["sample evidence level is unknown"])

    def test_server_audit_matches_every_attempt_and_body_byte(self) -> None:
        audit = {
            "complete": True,
            "client_attempts": [{"request_id": 4, "attempt": 1, "body_bytes": 12}],
            "server_attempts": [{"request_id": 4, "attempt": 1, "body_bytes": 12}],
        }
        self.assertTrue(valid_attempt_audit(audit))
        audit["server_attempts"][0]["body_bytes"] = 11
        self.assertFalse(valid_attempt_audit(audit))

    def test_incomplete_or_equal_costs_cannot_claim_strict_benefit(self) -> None:
        full = {
            "schema_version": 4,
            "outcome": {"status": "success", "scan_complete": True, "terminal_published": True},
            "transport": {"complete": True, "response_body_bytes": 100},
            "memory": {"query_owned_complete": True},
            "value_totals": {"decode_complete": True, "data_bytes": 80, "decoded_chunks": 10},
        }
        local = {
            "schema_version": 4,
            "outcome": {"status": "success", "scan_complete": True, "terminal_published": True},
            "transport": {"complete": True, "response_body_bytes": 50},
            "memory": {"query_owned_complete": True},
            "value_totals": {"decode_complete": True, "data_bytes": 40, "decoded_chunks": 4},
        }
        self.assertTrue(strict_cost_benefit(full, local))
        local["transport"]["complete"] = False
        self.assertFalse(strict_cost_benefit(full, local))
        local["transport"]["complete"] = True
        local["value_totals"]["decoded_chunks"] = 10
        self.assertFalse(strict_cost_benefit(full, local))

    def test_frozen_artifact_hash_is_checked_against_file(self) -> None:
        with TemporaryDirectory(prefix="duckomo-evidence-test-") as directory:
            root = Path(directory)
            artifact = root / "oracle.csv"
            artifact.write_bytes(b"fixed oracle\n")
            digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
            self.assertTrue(checked_artifact({"path": "oracle.csv", "sha256": digest}, root))
            self.assertFalse(checked_artifact({"path": "oracle.csv", "sha256": "0" * 64}, root))


def audit_cli(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Validate a generated DuckOMO grid evidence run manifest")
    parser.add_argument("--audit-manifest", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        manifest = json.loads(args.audit_manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"cannot read evidence manifest: {error}", file=sys.stderr)
        return 2
    errors = validate_run(manifest, args.root.resolve())
    if errors:
        print("grid evidence manifest failed:")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"grid evidence manifest passed: {args.audit_manifest}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--audit-manifest":
        raise SystemExit(audit_cli(sys.argv[1:]))
    unittest.main()
