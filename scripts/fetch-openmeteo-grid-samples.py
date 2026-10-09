#!/usr/bin/env python3
"""Fetch the pinned public Open-Meteo OM v3 sample objects by content hash."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import tempfile
import urllib.request
from datetime import datetime, timezone
from email.utils import parsedate_to_datetime
from pathlib import Path


DEFAULT_MANIFEST = Path("test/data/grids/sample-manifest.json")
DEFAULT_OUTPUT = Path("build/s3-samples/openmeteo-v3")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_time(value: str | None) -> str | None:
    if not value:
        return None
    parsed = parsedate_to_datetime(value).astimezone(timezone.utc)
    return parsed.isoformat(timespec="seconds").replace("+00:00", "Z")


def verify_headers(sample: dict, headers) -> None:
    source = sample["source_object"]
    expected_size = int(sample["size_bytes"])
    actual_size = headers.get("Content-Length")
    if actual_size is None or int(actual_size) != expected_size:
        raise RuntimeError(f"Content-Length mismatch for {sample['id']}: {actual_size!r}")
    if headers.get("ETag") != source["etag"]:
        raise RuntimeError(f"ETag changed for {sample['id']}: {headers.get('ETag')!r}")
    if normalize_time(headers.get("Last-Modified")) != source["last_modified"]:
        raise RuntimeError(f"Last-Modified changed for {sample['id']}")
    if headers.get("x-amz-version-id") != source["version_id"]:
        raise RuntimeError(f"S3 VersionId changed for {sample['id']}")


def fetch(sample: dict, output_dir: Path) -> None:
    source = sample["source_object"]
    output_path = output_dir / Path(sample["local_copy"]["path"]).name
    expected_size = int(sample["size_bytes"])
    expected_hash = sample["sha256"]

    request = urllib.request.Request(source["https_url"], method="HEAD")
    with urllib.request.urlopen(request, timeout=30) as response:
        verify_headers(sample, response.headers)

    if output_path.exists():
        if output_path.stat().st_size == expected_size and sha256_file(output_path) == expected_hash:
            print(f"verified existing {sample['id']}: {output_path}")
            return
        raise RuntimeError(f"refusing to replace an existing file with different content: {output_path}")

    output_dir.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{output_path.name}.", suffix=".partial", dir=output_dir)
    temporary_path = Path(temporary_name)
    digest = hashlib.sha256()
    size = 0
    try:
        with os.fdopen(descriptor, "wb") as stream:
            request = urllib.request.Request(source["https_url"])
            with urllib.request.urlopen(request, timeout=60) as response:
                verify_headers(sample, response.headers)
                while chunk := response.read(1024 * 1024):
                    stream.write(chunk)
                    digest.update(chunk)
                    size += len(chunk)
        if size != expected_size:
            raise RuntimeError(f"download size mismatch for {sample['id']}: {size} != {expected_size}")
        actual_hash = digest.hexdigest()
        if actual_hash != expected_hash:
            raise RuntimeError(f"SHA-256 mismatch for {sample['id']}: {actual_hash}")
        with temporary_path.open("rb") as stream:
            if stream.read(3) != b"OM\x03":
                raise RuntimeError(f"downloaded object is not OM v3: {sample['id']}")
        os.replace(temporary_path, output_path)
    except Exception:
        temporary_path.unlink(missing_ok=True)
        raise
    print(f"fetched {sample['id']}: {size} bytes sha256={expected_hash} -> {output_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--only", action="append", default=[], help="sample id; may be repeated")
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    samples = manifest["public_open_meteo_samples"]["objects"]
    if args.only:
        requested = set(args.only)
        samples = [sample for sample in samples if sample["id"] in requested]
        found = {sample["id"] for sample in samples}
        missing = requested - found
        if missing:
            parser.error("unknown sample id(s): " + ", ".join(sorted(missing)))

    for sample in samples:
        fetch(sample, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
