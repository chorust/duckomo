#!/usr/bin/env python3
"""Run real cloud S3 benchmark on Open-Meteo public sample (FR-018 / T044)."""
import hashlib
import json
import os
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

RUNTIMES = {
    "v1.5.6": {
        "cli": str(ROOT / "build/regions-runtime/v1.5.6/official/duckdb"),
        "httpfs": str(ROOT / "build/official-matrix/v1.5.6/official/httpfs.duckdb_extension"),
        "extension": str(ROOT / "build/official-matrix/v1.5.6/builds/0737841cfe351dc6c41e6ffd252f2c4f17738aa08678223fcfeeb705e57ca67c/release/extension/duckomo/duckomo.duckdb_extension"),
        "modes": ["merge-off", "merge-on"],
    },
    "v2.0.0": {
        "cli": str(ROOT / "build/official-matrix/v2.0.0/official/duckdb"),
        "httpfs": str(ROOT / "build/official-matrix/v2.0.0/official/httpfs.duckdb_extension"),
        "extension": str(ROOT / "build/official-matrix/v2.0.0/builds/68583bd5755d710e1d45b5a514f085ca08456fbf45343a7badb0a223bd89feaf/release/extension/duckomo/duckomo.duckdb_extension"),
        "modes": ["merge-off", "async-off", "async-on"],
    },
}

CLOUD_OBJECT = "https://openmeteo.s3.us-west-2.amazonaws.com/data/cmc_gem_rdps_10km/cape/chunk_4337.om"
QUERY = f"SELECT value FROM read_om('{CLOUD_OBJECT}') WHERE value != 0 LIMIT 10"

def run_single(runtime_name, cfg, mode, repeat):
    cli = cfg["cli"]
    httpfs = cfg["httpfs"]
    extension = cfg["extension"]

    merge = mode in {"merge-on", "async-off", "async-on"}
    async_on = mode == "async-on"

    setup = [
        f"LOAD '{httpfs}';",
        f"LOAD '{extension}';",
        "SET threads=1;",
        f"SET duckomo_remote_merge={'true' if merge else 'false'};",
        f"SET duckomo_remote_merge_gap=4096;",
        f"SET duckomo_remote_max_request=65536;",
    ]
    if "v2.0" in runtime_name:
        setup.append(f"SET duckomo_async_io={'true' if async_on else 'false'};")

    tmp_csv = ROOT / "build" / "008" / f".cloud-test-{runtime_name}-{mode}-{repeat}.csv"
    tmp_metrics = ROOT / "build" / "008" / f".cloud-test-{runtime_name}-{mode}-{repeat}-metrics.csv"
    tmp_csv.parent.mkdir(parents=True, exist_ok=True)

    sql_script = "\n".join(setup + [
        f"COPY ({QUERY}) TO '{tmp_csv}' (FORMAT CSV, HEADER false);",
        f"COPY (SELECT metrics FROM duckomo_last_scan_metrics()) TO '{tmp_metrics}' (FORMAT CSV, HEADER false);",
    ])

    cmd = [cli, "-unsigned"]
    if "v2.0" in runtime_name:
        cmd.append("-no-agent")
    cmd.extend(["-c", sql_script])

    start = time.monotonic()
    proc = subprocess.run(cmd, text=True, capture_output=True)
    elapsed_ms = (time.monotonic() - start) * 1000
    if proc.returncode != 0:
        raise RuntimeError(f"Run failed {runtime_name}/{mode}/{repeat}: {proc.stderr}")

    result_bytes = tmp_csv.read_bytes()
    result_sha256 = hashlib.sha256(result_bytes).hexdigest()

    metrics_raw = tmp_metrics.read_text().strip().strip('"').replace('""', '"')
    metrics_json = json.loads(metrics_raw)
    remote_io = metrics_json.get("remote_io", {})

    try:
        tmp_csv.unlink()
        tmp_metrics.unlink()
    except OSError:
        pass

    return {
        "runtime": runtime_name,
        "mode": mode,
        "repeat": repeat,
        "kind": "first_read" if repeat == 0 else "repeat_read",
        "elapsed_ms": elapsed_ms,
        "result_sha256": result_sha256,
        "remote_io": remote_io,
    }

def main():
    runs = []
    for runtime_name, cfg in RUNTIMES.items():
        for mode in cfg["modes"]:
            for repeat in range(2): # 0 = cold/first read, 1 = warm/repeat read
                print(f"Running {runtime_name} {mode} repeat={repeat}...")
                rec = run_single(runtime_name, cfg, mode, repeat)
                runs.append(rec)
                print(f"  elapsed: {rec['elapsed_ms']:.1f}ms, requests: {rec['remote_io'].get('requests')}, bytes: {rec['remote_io'].get('physical_bytes')}")

    out_data = {
        "schema_version": 1,
        "target": "real_public_cloud_endpoint",
        "client_location": "Linux aarch64 local environment (Asia/Shanghai)",
        "cloud_endpoint": {
            "service": "AWS S3 Open Data (openmeteo)",
            "region": "us-west-2",
            "url": CLOUD_OBJECT,
            "object_id": "rotated_cmc_gem_rdps_10km_cape_chunk_4337",
            "source_manifest": "test/data/grids/sample-manifest.json",
        },
        "query": QUERY,
        "runs": runs,
    }

    out_file = ROOT / "evidence" / "008" / "cloud.json"
    out_file.parent.mkdir(parents=True, exist_ok=True)
    out_file.write_text(json.dumps(out_data, indent=2) + "\n")
    print(f"Wrote cloud evidence to {out_file}")

if __name__ == "__main__":
    main()
