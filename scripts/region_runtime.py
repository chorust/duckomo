#!/usr/bin/env python3
"""Verify a frozen region runtime record and print its spatial artifact hash."""

from __future__ import annotations

import json
from pathlib import Path
import re
import sys


def verified_spatial_sha(matrix: dict, version: str) -> str | None:
    runtimes = matrix.get("runtimes") if isinstance(matrix, dict) else None
    if not isinstance(runtimes, list):
        return None
    record = next((item for item in runtimes
                   if isinstance(item, dict) and item.get("version") == version), None)
    if not record or record.get("gate_status") != "ready_for_adapter_check" or \
            record.get("source_identity_match") is not True:
        return None
    source_sha = record.get("spatial_source_sha")
    probes = record.get("probes")
    artifact = record.get("spatial_artifact")
    if not isinstance(probes, dict) or not isinstance(artifact, dict):
        return None
    extension_version = probes.get("extension_version")
    if not isinstance(source_sha, str) or not re.fullmatch(r"[0-9a-f]{40}", source_sha) or \
            not isinstance(extension_version, str) or not extension_version.startswith(source_sha[:7]):
        return None
    artifact_sha = artifact.get("sha256")
    if not isinstance(artifact_sha, str) or not re.fullmatch(r"[0-9a-f]{64}", artifact_sha):
        return None
    return artifact_sha


def main() -> int:
    if len(sys.argv) != 3:
        return 2
    try:
        matrix = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"region runtime matrix is unavailable: {error}", file=sys.stderr)
        return 2
    spatial_sha = verified_spatial_sha(matrix, sys.argv[2])
    if spatial_sha is None:
        print(f"spatial runtime identity is not verified for {sys.argv[2]}", file=sys.stderr)
        return 2
    print(spatial_sha)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
