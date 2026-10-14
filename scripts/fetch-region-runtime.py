#!/usr/bin/env python3
"""Fetch and identify the pinned DuckDB and spatial runtime matrix for spec 005."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MATRIX = ROOT / "test/data/grids/version-matrix.json"
OUTPUT = ROOT / "test/data/regions/runtime-matrix.json"
RUNTIME_ROOT = ROOT / "build/regions-runtime"
PINS = {
    "v1.5.4": {
        "duckdb": "08e34c447bae34eaee3723cac61f2878b6bdf787",
        "spatial": "28db190f7184bcf61eb01d291e0cba79849bddb6",
        "spatial_config": "b68b309d371dba936c5bb362980e559b7756b16d",
    },
    "v1.5.5": {
        "duckdb": "d8cdaa33fda8df955cc76ef58a280f68f4cd43fa",
        "spatial": "eb1e57c9d92c0f3f76eb03eaa52c315090f328cc",
        "spatial_config": "eb1e57c9d92c0f3f76eb03eaa52c315090f328cc",
    },
    "v1.5.6": {
        "duckdb": "069cc9f9b5be802405797faecc284961b07c70ef",
        "spatial": "04270fe7bb4eb412765cce8afde0276a9d81d3ba",
        "spatial_config": "04270fe7bb4eb412765cce8afde0276a9d81d3ba",
    },
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fetch_spatial(version: str, directory: Path) -> dict:
    import gzip as gzip_module

    url = f"https://extensions.duckdb.org/{version}/linux_arm64/spatial.duckdb_extension.gz"
    directory.mkdir(parents=True, exist_ok=True)
    compressed = directory / "spatial.duckdb_extension.gz"
    extension = directory / "spatial.duckdb_extension"
    download = directory / "spatial.download.tmp"
    result = subprocess.run(
        ["curl", "-fLsS", "--retry", "2", "--max-time", "180", url, "-o", str(download)],
        text=True,
        capture_output=True,
    )
    if result.returncode:
        download.unlink(missing_ok=True)
        raise RuntimeError(f"official spatial package unavailable for {version}: {result.stderr.strip()}")
    try:
        payload = download.read_bytes()
        unpacked = gzip_module.decompress(payload)
        if not unpacked.startswith(b"\x7fELF"):
            raise RuntimeError(f"spatial package for {version} did not unpack to an ELF extension")
        compressed.write_bytes(payload)
        extension.write_bytes(unpacked)
    finally:
        download.unlink(missing_ok=True)
    return {
        "url": url,
        "compressed_path": str(compressed.resolve()),
        "compressed_sha256": sha256(compressed),
        "path": str(extension.resolve()),
        "sha256": sha256(extension),
    }


def probe_spatial(cli: Path, extension: Path) -> dict:
    escaped = str(extension.resolve()).replace("'", "''")
    sql = f"""
LOAD '{escaped}';
SELECT version() AS duckdb_version,
       (SELECT extension_version FROM duckdb_extensions() WHERE extension_name='spatial' AND loaded) AS extension_version,
       (SELECT install_path FROM duckdb_extensions() WHERE extension_name='spatial' AND loaded) AS install_path,
       (SELECT count(*) FROM duckdb_functions() WHERE lower(function_name)='st_geomfromwkb' AND parameter_types=['BLOB'] AND return_type='GEOMETRY') >= 1 AS geom_from_wkb,
       (SELECT count(*) FROM duckdb_functions() WHERE lower(function_name)='st_isvalid' AND parameter_types=['GEOMETRY'] AND return_type='BOOLEAN') >= 1 AS is_valid,
       (SELECT count(*) FROM duckdb_functions() WHERE lower(function_name)='st_point' AND parameter_types=['DOUBLE','DOUBLE'] AND return_type='GEOMETRY') >= 1 AS point,
       (SELECT count(*) FROM duckdb_functions() WHERE lower(function_name)='st_covers' AND parameter_types=['GEOMETRY','GEOMETRY'] AND return_type='BOOLEAN') >= 1 AS covers,
       ST_Covers(ST_GeomFromText('POLYGON((0 0,2 0,2 2,0 2,0 0))'), ST_Point(0,1)) AS boundary_covers;
"""
    result = subprocess.run(
        [str(cli), "-unsigned", "-json", "-c", sql], text=True, capture_output=True, check=False
    )
    if result.returncode:
        raise RuntimeError(f"spatial load/probe failed: {result.stderr.strip()}")
    try:
        rows = json.loads(result.stdout)
        row = rows[-1] if isinstance(rows, list) else rows
    except (json.JSONDecodeError, IndexError, TypeError) as error:
        raise RuntimeError("DuckDB CLI did not return JSON probe output") from error
    required = ["geom_from_wkb", "is_valid", "point", "covers", "boundary_covers"]
    if any(row.get(field) is not True for field in required):
        raise RuntimeError(f"spatial capability/signature probe failed: {row}")
    if not row.get("extension_version"):
        raise RuntimeError(f"DuckDB did not report spatial package identity: {row}")
    return row


def run(args: argparse.Namespace) -> dict:
    matrix_path = args.matrix.resolve()
    output_path = args.output.resolve()
    runtime_root = args.runtime_root.resolve()
    source_matrix = json.loads(matrix_path.read_text())
    pairs = {pair["pair_id"]: pair for pair in source_matrix["pairs"]}
    if set(pairs) != set(PINS):
        raise RuntimeError("grid version matrix must contain exactly the pinned v1.5.4-v1.5.6 pairs")

    spec = importlib.util.spec_from_file_location("duckomo_version_matrix", ROOT / "scripts/version_matrix.py")
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load scripts/version_matrix.py")
    version_matrix = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(version_matrix)

    runtimes = []
    errors = []
    for version, pins in PINS.items():
        pair = pairs[version]
        if pair["duckdb"]["commit"] != pins["duckdb"]:
            raise RuntimeError(f"DuckDB commit pin changed for {version}")
        record = {
            "version": version,
            "platform": "linux_arm64",
            "duckdb_source_sha": pins["duckdb"],
            "spatial_source_sha": pins["spatial"],
            "engine_spatial_config_sha": pins["spatial_config"],
            "gate_status": "blocked",
        }
        try:
            fetched = version_matrix.fetch_runtime(
                version_matrix.resolve(argparse.Namespace(
                    root=str(ROOT), matrix=str(matrix_path), pair=version,
                    output_root=str(runtime_root),
                ))
            )
            spatial = fetch_spatial(version, runtime_root / version / "official")
            probes = probe_spatial(Path(fetched["cli"]["path"]), Path(spatial["path"]))
            identity_match = probes["extension_version"].startswith(pins["spatial"][:7])
            record.update({
                "official_runtime": fetched,
                "spatial_artifact": spatial,
                "probes": probes,
                "source_identity_match": identity_match,
                "gate_status": "ready_for_adapter_check" if identity_match else "blocked_identity_mismatch",
            })
            if not identity_match:
                errors.append(f"{version}: spatial extension_version {probes['extension_version']} does not match pinned source {pins['spatial']}")
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            record["error"] = str(error)
            errors.append(f"{version}: {error}")
        runtimes.append(record)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    document = {
        "schema_version": 1,
        "platform": {"target": "Linux AArch64", "extension_platform": "linux_arm64"},
        "build_identity_policy": "Runtime package hashes and install paths are evidence and do not affect reproducible source build IDs.",
        "status": "ready_for_adapter_check" if not errors else "blocked",
        "runtimes": runtimes,
    }
    temporary = output_path.with_suffix(output_path.suffix + ".tmp")
    temporary.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")
    temporary.replace(output_path)
    if errors:
        raise RuntimeError(f"runtime evidence saved to {output_path}; review required: " + "; ".join(errors))
    return document


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix", type=Path, default=MATRIX)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--runtime-root", type=Path, default=RUNTIME_ROOT)
    args = parser.parse_args()
    try:
        result = run(args)
    except (OSError, KeyError, TypeError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"fetch-region-runtime: {error}", file=sys.stderr)
        return 1
    print(json.dumps({"output": str(args.output.resolve()), "runtimes": len(result["runtimes"])}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
