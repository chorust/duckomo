#!/usr/bin/env python3
"""Check deterministic grid-registry generation and shared identity vectors."""

from __future__ import annotations

import importlib.util
import json
import struct
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "scripts/generate-grid-registry.py"


def check_n160_sql_rows_match_registry() -> None:
    definitions = json.loads((ROOT / "test/data/grids/definitions.json").read_text())
    n160 = next(item for item in definitions["definitions"] if item["id"] == "n160")
    rows = n160["parameters"]["rows"]
    sql = (ROOT / "test/sql/multi_grid.test").read_text()
    for macro_name, field in (("n160_row_latitudes", "latitude"), ("n160_row_counts", "point_count")):
        line = next((line for line in sql.splitlines() if line.startswith(f"CREATE MACRO {macro_name}")), None)
        if line is None:
            raise AssertionError(f"{macro_name} is missing from multi_grid.test")
        encoded = line.split("from_json('", 1)[1].split("', '[", 1)[0]
        actual = json.loads(encoded)
        expected = [row[field] for row in rows]
        if actual != expected:
            raise AssertionError(f"{macro_name} differs from the registered N160 row table")
    if len(rows) != 320 or sum(row["point_count"] for row in rows) != n160["parameters"]["point_count"]:
        raise AssertionError("registered N160 row count or point sum changed")
    for row in rows:
        f32_step = struct.unpack("f", struct.pack("f", 360.0 / row["point_count"]))[0]
        if row["longitude_step"] != f32_step:
            raise AssertionError("registered N160 longitude step no longer follows the f32 source rule")


def check_sample_query_generation(generator) -> None:
    sample_manifest = ROOT / "test/data/grids/sample-manifest.json"
    definitions = ROOT / "test/data/grids/definitions.json"
    queries = generator.generate_sample_queries(sample_manifest, definitions)
    for grid_id in ("gem_rdps_10km", "gem_regional", "aladin_central_europe_2km", "n160"):
        if f"CREATE TEMP VIEW explicit_{grid_id}" not in queries:
            raise AssertionError(f"explicit query view is missing for {grid_id}")
        if f"CREATE TEMP VIEW domain_{grid_id}" not in queries:
            raise AssertionError(f"domain query view is missing for {grid_id}")
        baseline = queries.index(f"CREATE TEMP TABLE baseline_{grid_id}")
        restricted = queries.index(f"restricted_difference_{grid_id}")
        if baseline > restricted:
            raise AssertionError(f"{grid_id} baseline must be materialized before restricted comparisons")
    if "NOT RUN n320:" not in queries or "NOT RUN n320_ecmwf_aifs_europe_ensemble:" not in queries:
        raise AssertionError("unavailable N320 inputs were not preserved as not-run query entries")
    if "values_ecmwf_hres_o1280" not in queries or "not relabelled as N160 or N320" not in queries:
        raise AssertionError("the public HRES O1280 sample must remain supplemental and unmapped")
    if "polygon_covers_point" not in queries or "lon BETWEEN 170 AND 180" not in queries:
        raise AssertionError("fixed seam and point/polygon query cases are missing")


def check_flattened_axis_alias(generator) -> None:
    data = json.loads((ROOT / "test/data/grids/definitions.json").read_text())
    hres = next(item for item in data["definitions"] if item["id"] == "ecmwf_ifs")
    profile = hres["expected_spatial_axis_profile"]
    if profile["axis_order"] != ["point"] or profile.get("flattened_axis_alias") != "lon":
        raise AssertionError("HRES must retain point and explicitly register its static lon alias")
    with tempfile.TemporaryDirectory(prefix="duckomo-grid-alias-") as temporary:
        path = Path(temporary) / "definitions.json"
        for layout, alias in (("separate", "lon"), ("flattened", "point"), ("flattened", ["lon"])):
            profile["layout"], profile["flattened_axis_alias"] = layout, alias
            path.write_text(json.dumps(data))
            try:
                generator.generate_header(path)
            except ValueError as error:
                if "flattened_axis_alias" not in str(error):
                    raise
            else:
                raise AssertionError("invalid registered flattened alias was accepted")


def load_generator():
    spec = importlib.util.spec_from_file_location("grid_registry_generator", GENERATOR)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load grid registry generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    generator = load_generator()
    check_n160_sql_rows_match_registry()
    check_sample_query_generation(generator)
    check_flattened_axis_alias(generator)
    vectors = ROOT / "test/data/grids/canonical-vectors.json"
    sample_manifest = ROOT / "test/data/grids/sample-manifest.json"
    generator.canonical_vectors(vectors)
    checked_header = ROOT / "src/include/duckomo/generated_grid_registry.hpp"
    subprocess.run(
        ["python3", str(GENERATOR), "--input", str(ROOT / "test/data/grids/definitions.json"),
         "--vectors", str(vectors), "--output", str(checked_header), "--check"],
        cwd=ROOT, check=True,
    )

    with tempfile.TemporaryDirectory(prefix="duckomo-grid-registry-test-") as temporary:
        first = Path(temporary) / "first.hpp"
        second = Path(temporary) / "second.hpp"
        for output in (first, second):
            subprocess.run(
                ["python3", str(GENERATOR), "--input", str(ROOT / "test/data/grids/definitions.json"),
                 "--vectors", str(vectors), "--output", str(output)],
                cwd=ROOT, check=True, stdout=subprocess.DEVNULL,
            )
        if first.read_bytes() != second.read_bytes() or first.read_bytes() != checked_header.read_bytes():
            raise AssertionError("grid registry generation is not byte-for-byte deterministic")

        first_queries = Path(temporary) / "first-sample-queries.sql"
        second_queries = Path(temporary) / "second-sample-queries.sql"
        for output in (first_queries, second_queries):
            subprocess.run(
                ["python3", str(GENERATOR), "--input", str(ROOT / "test/data/grids/definitions.json"),
                 "--sample-manifest", str(ROOT / "test/data/grids/sample-manifest.json"),
                 "--output", str(Path(temporary) / "unused.hpp"), "--sample-queries-output", str(output)],
                cwd=ROOT, check=True, stdout=subprocess.DEVNULL,
            )
        if first_queries.read_bytes() != second_queries.read_bytes():
            raise AssertionError("sample query generation is not byte-for-byte deterministic")
        generated_queries = ROOT / "test/data/grids/sample-queries.sql"
        subprocess.run(
            ["python3", str(GENERATOR), "--input", str(ROOT / "test/data/grids/definitions.json"),
             "--sample-manifest", str(sample_manifest), "--output", str(checked_header),
             "--sample-queries-output", str(generated_queries), "--check"],
            cwd=ROOT, check=True,
        )
        if generated_queries.read_bytes() != first_queries.read_bytes():
            raise AssertionError("checked-in sample queries differ from deterministic generation")

        malformed = json.loads(vectors.read_text())
        malformed["vectors"][0]["grid_id"] = "0" * 64
        broken_vectors = Path(temporary) / "broken-vectors.json"
        broken_vectors.write_text(json.dumps(malformed))
        try:
            generator.canonical_vectors(broken_vectors)
        except ValueError:
            pass
        else:
            raise AssertionError("canonical vector checker accepted a corrupted golden hash")

    print("grid registry checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
