#!/usr/bin/env python3
"""Keep supplemental Open-Meteo HRES/O1280 samples out of N-grid references."""

from __future__ import annotations

import importlib.util
import csv
import gzip
import json
import os
import unittest
from argparse import Namespace
from pathlib import Path
from tempfile import TemporaryDirectory


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("grid_reference", ROOT / "scripts/generate-grid-reference.py")
assert SPEC and SPEC.loader
grid_reference = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(grid_reference)
COORDINATE_SPEC = importlib.util.spec_from_file_location(
    "grid_coordinate_reference", ROOT / "scripts/generate-grid-coordinate-reference.py"
)
assert COORDINATE_SPEC and COORDINATE_SPEC.loader
grid_coordinate_reference = importlib.util.module_from_spec(COORDINATE_SPEC)
COORDINATE_SPEC.loader.exec_module(grid_coordinate_reference)
COMPARE_SPEC = importlib.util.spec_from_file_location(
    "grid_reference_compare", ROOT / "scripts/compare-grid-sample-references.py"
)
assert COMPARE_SPEC and COMPARE_SPEC.loader
grid_reference_compare = importlib.util.module_from_spec(COMPARE_SPEC)
COMPARE_SPEC.loader.exec_module(grid_reference_compare)
SPATIAL_SPEC = importlib.util.spec_from_file_location(
    "grid_spatial_reference", ROOT / "test/tools/grid_spatial_reference.py"
)
assert SPATIAL_SPEC and SPATIAL_SPEC.loader
grid_spatial_reference = importlib.util.module_from_spec(SPATIAL_SPEC)
SPATIAL_SPEC.loader.exec_module(grid_spatial_reference)


class GridCoordinateReferenceTest(unittest.TestCase):
    def test_source_native_position_uses_float32_operation_order(self) -> None:
        source_value = grid_coordinate_reference.source_grid_coordinate(-53.859, 0.090298, 581)
        self.assertEqual(source_value, -1.3958663940429688)
        self.assertNotEqual(source_value, -53.859 + 581 * 0.090298)

    def test_pinned_producer_float32_projection_landmarks(self) -> None:
        definitions = json.loads((ROOT / "test/data/grids/definitions.json").read_text())
        by_id = {row["id"]: row["parameters"] for row in definitions["definitions"]}
        cases = (
            ("gem_rdps_10km", grid_coordinate_reference.pinned_rotated_coordinate, 597, 892,
             (-26.357208251953125, 89.9515380859375)),
            ("aladin_central_europe_2km", grid_coordinate_reference.pinned_lambert_coordinate, 0, 0,
             (1.3339996337890625, 38.599002838134766)),
            ("gem_regional", grid_coordinate_reference.pinned_stereographic_coordinate, 0, 0,
             (-142.892578125, 18.145030975341797)),
        )
        for definition_id, transform, x, y, expected in cases:
            with self.subTest(definition=definition_id):
                actual = transform(by_id[definition_id], x, y)
                self.assertLessEqual(abs(actual[0] - expected[0]), 1e-4)
                self.assertLessEqual(abs(actual[1] - expected[1]), 1e-4)


class GridReferenceInputTest(unittest.TestCase):
    def test_only_required_real_v3_definitions_enter_reference_coverage(self) -> None:
        manifest = json.loads((ROOT / "test/data/grids/sample-manifest.json").read_text())
        objects = manifest["public_open_meteo_samples"]["objects"]
        accepted = grid_reference.accepted_sample_targets(objects)
        self.assertEqual(set(accepted.values()), {"rotated_v3", "stereographic_v3", "lambert_v3"})
        self.assertNotIn("ecmwf_ifs_static_hsurf_reduced_gaussian_o1280", accepted)
        self.assertNotIn("ecmwf_ifs_hres_temperature_2m_chunk_817_o1280_supplemental", accepted)
        traceable_n160 = {
            "id": "authorized_n160",
            "classification": "real_authorized_archive_om_v3",
            "format_version": 3,
            "grid_definition_id": "n160",
        }
        archive_samples = grid_reference.iter_sample_objects({"authorized_samples": [traceable_n160]})
        self.assertEqual(grid_reference.accepted_sample_targets(archive_samples),
                         {"authorized_n160": "gaussian_n160_v3"})

    def test_o1280_cannot_be_filed_as_a_required_n_grid(self) -> None:
        with self.assertRaises(grid_reference.ReferenceError):
            grid_reference.validate_records(
                "coordinate",
                [{"target": "gaussian_n160_v3", "sample_id": "ecmwf_ifs_static_hsurf_reduced_gaussian_o1280"}],
                ROOT,
                ROOT,
                {"ecmwf_ifs_static_hsurf_reduced_gaussian_o1280": "gaussian_o1280_supplemental"},
            )

    def test_import_preserves_existing_supplemental_value_evidence(self) -> None:
        with TemporaryDirectory(prefix="duckomo-grid-reference-test-") as directory:
            root = Path(directory)
            sample_manifest = root / "samples.json"
            sample_manifest.write_text(json.dumps({"public_open_meteo_samples": {"objects": []}}))
            records = root / "records.json"
            records.write_text(json.dumps({
                "schema_version": 1,
                "coordinate": {"references": []},
                "region": {"references": []},
                "value": {"references": []},
            }))
            output = root / "out"
            output.mkdir()
            existing = output / "value-reference-manifest.json"
            existing.write_text(json.dumps({
                "schema_version": 1,
                "scope": "O1280 supplemental only",
                "samples": [{"sample_id": "o1280", "acceptance_effect": "supplemental only"}],
                "required_target_gates": {"gaussian_n160": "not-run"},
            }))

            no_force = Namespace(root=root, sample_manifest=sample_manifest, records=records,
                                 output_dir=output, force=False)
            with self.assertRaises(grid_reference.ReferenceError):
                grid_reference.generate(no_force)
            self.assertFalse((output / "coordinate-reference.json").exists())

            force = Namespace(root=root, sample_manifest=sample_manifest, records=records,
                              output_dir=output, force=True)
            grid_reference.generate(force)
            imported = json.loads(existing.read_text())
            self.assertEqual(imported["supplemental_references"][0]["sample_id"], "o1280")
            self.assertEqual(imported["required_target_gates"]["gaussian_n160_v3"], "not-run")
            grid_reference.generate(force)
            rerun = json.loads(existing.read_text())
            self.assertEqual(rerun["supplemental_references"][0]["sample_id"], "o1280")


class GridReferenceComparisonTest(unittest.TestCase):
    def test_coordinate_comparison_rejects_out_of_range_coordinates(self) -> None:
        with TemporaryDirectory(prefix="duckomo-coordinate-range-") as directory:
            actual = Path(directory) / "actual.csv"
            reference = Path(directory) / "reference.csv"
            for longitude, latitude in ((721, 0), (-721, 0), (181, 0), (0, 91), (0, -91)):
                for invalid_reference in (False, True):
                    with self.subTest(longitude=longitude, latitude=latitude, reference=invalid_reference):
                        actual_coordinate = (0, 0) if invalid_reference else (longitude, latitude)
                        expected_coordinate = (longitude, latitude) if invalid_reference else (0, 0)
                        actual.write_text("spatial_index,longitude,latitude\n0,%s,%s\n" % actual_coordinate)
                        reference.write_text("spatial_index,row,column,longitude,latitude\n0,0,0,%s,%s\n" %
                                             expected_coordinate)
                        with self.assertRaisesRegex(grid_reference_compare.ReferenceCompareError, "out of range"):
                            grid_reference_compare.compare_coordinates(actual, reference, 1, 1, 1e-6)

    def test_coordinate_comparison_wraps_antimeridian_with_nonnegative_error(self) -> None:
        with TemporaryDirectory(prefix="duckomo-coordinate-wrap-") as directory:
            actual = Path(directory) / "actual.csv"
            reference = Path(directory) / "reference.csv"
            for actual_lon, expected_lon, error in ((179.999, -179.999, 0.002),
                                                    (-179.999, 179.999, 0.002), (180, -180, 0)):
                with self.subTest(actual=actual_lon, expected=expected_lon):
                    actual.write_text(f"spatial_index,longitude,latitude\n0,{actual_lon},0\n")
                    reference.write_text(f"spatial_index,row,column,longitude,latitude\n0,0,0,{expected_lon},0\n")
                    result = grid_reference_compare.compare_coordinates(actual, reference, 1, 1, 1e-6)
                    self.assertAlmostEqual(result["maximum_error_degrees"], error)
                    self.assertEqual(result["status"], "fail" if error else "pass")

    def test_coordinate_comparison_checks_every_original_point(self) -> None:
        with TemporaryDirectory(prefix="duckomo-coordinate-compare-") as directory:
            root = Path(directory)
            actual = root / "actual.csv"
            reference = root / "reference.csv"
            actual.write_text("spatial_index,longitude,latitude\n0,10,20\n1,11,21\n")
            reference.write_text(
                "spatial_index,row,column,longitude,latitude\n0,0,0,10.00001,20\n1,0,1,11,21\n"
            )
            result = grid_reference_compare.compare_coordinates(actual, reference, 2, 2, 0.0001)
            self.assertEqual(result["rows_compared"], 2)
            self.assertEqual(result["status"], "pass")

    def test_coordinate_comparison_rejects_position_and_tolerance_mismatches(self) -> None:
        with TemporaryDirectory(prefix="duckomo-coordinate-compare-") as directory:
            root = Path(directory)
            actual = root / "actual.csv"
            reference = root / "reference.csv"
            actual.write_text("spatial_index,longitude,latitude\n1,10,20\n")
            reference.write_text("spatial_index,row,column,longitude,latitude\n0,0,0,10,20\n")
            with self.assertRaises(grid_reference_compare.ReferenceCompareError):
                grid_reference_compare.compare_coordinates(actual, reference, 1, 1, 0.0001)
            actual.write_text("spatial_index,longitude,latitude\n0,10,20.01\n")
            result = grid_reference_compare.compare_coordinates(actual, reference, 1, 1, 0.0001)
            self.assertEqual(result["status"], "fail")
            self.assertEqual(result["positions_over_tolerance"], 1)
            self.assertEqual(result["first_exceeded_spatial_index"], 0)


class SyntheticSourceValidationTest(unittest.TestCase):
    def test_synthetic_sql_preserves_reference_columns_with_short_geographic_outputs(self) -> None:
        extension_path = os.environ.get("DUCKOMO_TEST_EXTENSION")
        if not extension_path:
            self.skipTest("DUCKOMO_TEST_EXTENSION is required for synthetic SQL validation")
        extension = Path(extension_path).resolve()
        self.assertTrue(extension.is_file(), "configured DuckOMO extension must exist")
        cli_path = os.environ.get("DUCKOMO_OFFICIAL_DUCKDB")
        duckdb = Path(cli_path).resolve() if cli_path else extension.parents[2] / "duckdb"
        if not cli_path and not duckdb.is_file():
            self.skipTest("matching DuckDB CLI is not built; set DUCKOMO_OFFICIAL_DUCKDB")
        with TemporaryDirectory(prefix="duckomo-synthetic-source-") as directory:
            root = Path(directory)
            # Keep this regression independent of optional public-sample archives.
            for fixture in ("test/data/raw.om", "test/data/grids/spatial-relations-gaussian.om"):
                path = root / fixture
                path.parent.mkdir(parents=True, exist_ok=True)
                path.symlink_to(ROOT / fixture)
            output = root / "output"
            report = grid_spatial_reference.run_validation(duckdb, extension, root, output)
            self.assertEqual(report["status"], "synthetic_local_pass")
            self.assertEqual({case["grid_type"] for case in report["cases"]}, {
                "rotated_latlon", "lambert_conformal_conic", "stereographic", "reduced_gaussian",
            })
            for case in report["cases"]:
                with self.subTest(case=case["id"]):
                    self.assertTrue(case["full_relation_multiset_matches"])
                    self.assertTrue(case["source_value_reads_zero"])
                    self.assertTrue(case["grid_info_value_reads_zero"])
            self.assertEqual(json.loads((output / "h7-synthetic-report.json").read_text()), report)


class PublicSourceValidationIntegrationTest(unittest.TestCase):
    def test_public_prefix_checks_four_spatial_positions_at_time_zero(self) -> None:
        extension_path = os.environ.get("DUCKOMO_TEST_EXTENSION")
        if not extension_path:
            self.skipTest("DUCKOMO_TEST_EXTENSION is required for public source validation")
        extension = Path(extension_path).resolve()
        cli_path = os.environ.get("DUCKOMO_OFFICIAL_DUCKDB")
        duckdb = Path(cli_path).resolve() if cli_path else extension.parents[2] / "duckdb"
        if not cli_path and not duckdb.is_file():
            self.skipTest("matching DuckDB CLI is not built; set DUCKOMO_OFFICIAL_DUCKDB")
        with TemporaryDirectory(prefix="duckomo-public-source-validation-") as directory:
            output = Path(directory)
            _, records, reason = grid_spatial_reference.prepare_public_sample_queries(ROOT, output)
            if not records:
                self.skipTest(reason)
            report = grid_spatial_reference.run_public_source_validation(duckdb, extension, ROOT, output)
            self.assertEqual(report["status"], "public_source_local_pass")
            self.assertEqual(len(report["samples"]), 3)
            for sample in report["samples"]:
                with self.subTest(sample=sample["id"]):
                    self.assertEqual(sample["positions_checked"], 4)
                    self.assertTrue(sample["pinned_coordinate_reference_matches"])
                    self.assertTrue(sample["source_identity_sql_matches"])
                    self.assertTrue(sample["source_identity_sql_value_reads_zero"])


class PublicSourceCoordinateReferenceTest(unittest.TestCase):
    def test_public_source_uri_comes_from_manifest_without_conversion(self) -> None:
        item = {"id": "public_om", "source_object": {
            "https_url": "https://openmeteo.s3.us-west-2.amazonaws.com/data/example.om",
            "s3_uri": "s3://openmeteo/data/example.om",
        }}
        self.assertIsNone(grid_spatial_reference.public_sample_source_uri(item, "local"))
        self.assertEqual(grid_spatial_reference.public_sample_source_uri(item, "https"),
                         item["source_object"]["https_url"])
        self.assertEqual(grid_spatial_reference.public_sample_source_uri(item, "s3"),
                         item["source_object"]["s3_uri"])
        self.assertEqual(grid_spatial_reference.public_sample_s3_region(item), "us-west-2")
        with self.assertRaises(ValueError):
            grid_spatial_reference.public_sample_source_uri({"id": "missing"}, "https")

    def test_remote_uri_replaces_only_the_generated_read_om_source_path(self) -> None:
        sql = "CREATE TEMP VIEW explicit AS SELECT * FROM read_om('build/sample.om', grid := {});"
        actual = grid_spatial_reference.replace_public_source_uri(
            sql, "https://openmeteo.s3.us-west-2.amazonaws.com/data/sample.om"
        )
        self.assertIn("read_om('https://openmeteo.s3.us-west-2.amazonaws.com/data/sample.om'", actual)
        self.assertNotIn("build/sample.om", actual)
        with self.assertRaises(ValueError):
            grid_spatial_reference.replace_public_source_uri("SELECT 1;", "https://example.test/a.om")

    def test_cross_uri_position_comparison_allows_distinct_object_ids(self) -> None:
        header = ("object_id,object_version,version_strength,content_verified,grid_id,layout_id,"
                  "logical_index,point_index,parent_point_index,axis_indices,latitude,longitude\n")
        rows = (
            '"local-id",NULL,unverifiable,false,grid-a,layout-a,0,0,0,"[0,0]",20,10\n'
            '"local-id",NULL,unverifiable,false,grid-a,layout-a,1,1,1,"[0,1]",21,11\n'
        )
        remote_rows = rows.replace('"local-id",NULL,unverifiable',
                                   '"https://object-id","etag:opaque",weak')
        with TemporaryDirectory(prefix="duckomo-cross-uri-source-") as directory:
            root = Path(directory)
            local, remote = root / "local.csv.gz", root / "remote.csv.gz"
            local.write_bytes(gzip.compress((header + rows).encode()))
            remote.write_bytes(gzip.compress((header + remote_rows).encode()))
            result = grid_spatial_reference.compare_full_source_positions(local, remote)
            self.assertEqual(result["positions_compared"], 2)
            self.assertTrue(result["positions_match"])
            self.assertFalse(result["object_id_equal"])
            self.assertFalse(result["object_id_equality_required"])

            remote.write_bytes(gzip.compress((header + remote_rows.replace(",21,11", ",21,12")).encode()))
            with self.assertRaises(ValueError):
                grid_spatial_reference.compare_full_source_positions(local, remote)

    def test_public_sample_source_query_keeps_prefix_bounded(self) -> None:
        with TemporaryDirectory(prefix="duckomo-public-source-prefix-") as directory:
            root = Path(directory)
            data_dir = root / "test/data/grids"
            data_dir.mkdir(parents=True)
            sample_dir = root / "samples"
            sample_dir.mkdir()
            coordinate_dir = root / "coordinates"
            coordinate_dir.mkdir()

            targets = {
                "gem_rdps_10km": "rotated_v3",
                "gem_regional": "stereographic_v3",
                "aladin_central_europe_2km": "lambert_v3",
            }
            objects = []
            references = []
            views = []
            coordinates = (
                "spatial_index,row,column,longitude,latitude\n"
                "0,0,0,10,20\n1,0,1,11,21\n2,0,2,12,22\n"
                "3,1,0,13,23\n4,1,1,14,24\n5,1,2,15,25\n"
            )
            for index, (grid_id, target) in enumerate(targets.items()):
                sample_id = "sample_" + grid_id
                sample_path = sample_dir / (grid_id + ".om")
                sample_path.write_bytes(f"sample-{index}".encode())
                coordinate_path = coordinate_dir / (grid_id + ".csv")
                coordinate_path.write_text(coordinates, encoding="utf-8")
                objects.append({
                    "id": sample_id,
                    "classification": "real_public_open_meteo_om_v3",
                    "grid_definition_id": grid_id,
                    "local_copy": {"path": str(sample_path.relative_to(root))},
                    "sha256": grid_spatial_reference.sha256_file(sample_path),
                    "array": {"shape": [2, 3, 4]},
                })
                references.append({
                    "target": target,
                    "sample_id": sample_id,
                    "method": "pinned_producer_coordinates",
                    "numeric_policy": "openmeteo_f32_v1",
                    "tolerance_degrees": 1e-4,
                    "row_count": 6,
                    "artifact": {
                        "path": str(coordinate_path.relative_to(root)),
                        "sha256": grid_spatial_reference.sha256_file(coordinate_path),
                    },
                })
                read = "read_om('" + str(sample_path) + "', grid := {'version': 1}, include_source := true)"
                views.extend([
                    f"CREATE TEMP VIEW explicit_{grid_id} AS SELECT * FROM {read};",
                    f"CREATE TEMP VIEW domain_{grid_id} AS SELECT * FROM {read};",
                ])

            (data_dir / "sample-manifest.json").write_text(
                json.dumps({"public_open_meteo_samples": {"objects": objects}}), encoding="utf-8"
            )
            (data_dir / "coordinate-reference.json").write_text(
                json.dumps({"references": references}), encoding="utf-8"
            )
            (data_dir / "sample-queries.sql").write_text("\n".join(views), encoding="utf-8")
            output = root / "output"
            statements, records, reason = grid_spatial_reference.prepare_public_sample_queries(root, output)

            source_queries = [statement for statement in statements if statement.startswith("COPY (SELECT om_source.")]
            self.assertEqual(len(records), 3)
            self.assertIn("first four", reason)
            self.assertEqual(len(source_queries), 6)
            prefix = "WHERE om_source.axis_indices[3] = 0 LIMIT 4"
            self.assertTrue(all(prefix + ") TO" in statement for statement in source_queries))
            self.assertTrue(all("ORDER BY om_source.logical_index" not in statement for statement in source_queries))
            identity_queries = [statement for statement in statements
                                if statement.startswith("COPY (WITH explicit_prefix")]
            self.assertEqual(len(identity_queries), 3)
            self.assertTrue(all(statement.count(prefix) == 2 for statement in identity_queries))

    def test_public_source_prefix_rejects_wrong_time_and_corrupt_positions(self) -> None:
        with TemporaryDirectory(prefix="duckomo-public-source-invalid-position-") as directory:
            output = Path(directory)
            record = {"id": "sample", "shape": [2, 4, 5], "explicit_source_csv": "source.csv"}
            for field, invalid in (("axis_indices", "[0, 1, 1]"), ("logical_index", 6),
                                   ("point_index", 0), ("parent_point_index", 0)):
                with self.subTest(field=field):
                    rows = [{"object_id": "object-a", "object_version": "NULL",
                             "version_strength": "unverifiable", "content_verified": "false",
                             "grid_id": "grid-a", "layout_id": "layout-a",
                             "logical_index": index * 5, "point_index": index,
                             "parent_point_index": index, "axis_indices": json.dumps([0, index, 0]),
                             "longitude": 10 + index, "latitude": 20}
                            for index in range(4)]
                    rows[1][field] = invalid
                    with (output / "source.csv").open("w", encoding="utf-8", newline="") as stream:
                        writer = csv.DictWriter(stream, fieldnames=grid_spatial_reference.PUBLIC_SOURCE_COLUMNS)
                        writer.writeheader()
                        writer.writerows(rows)
                    with self.assertRaisesRegex(ValueError, "identity does not reconstruct"):
                        grid_spatial_reference.validate_public_sample_source(record, output)

    def test_public_spatial_source_selection_uses_full_scan_baseline_and_sql_identity(self) -> None:
        with TemporaryDirectory(prefix="duckomo-public-source-selection-") as directory:
            root = Path(directory)
            data_dir = root / "test/data/grids"
            data_dir.mkdir(parents=True)
            sample_dir = root / "samples"
            sample_dir.mkdir()
            coordinate_dir = root / "coordinates"
            coordinate_dir.mkdir()

            targets = {
                "gem_rdps_10km": "rotated_v3",
                "gem_regional": "stereographic_v3",
                "aladin_central_europe_2km": "lambert_v3",
            }
            objects = []
            references = []
            views = []
            coordinates = (
                "spatial_index,row,column,longitude,latitude\n"
                "0,0,0,10,20\n1,0,1,11,21\n2,0,2,12,22\n"
                "3,1,0,13,23\n4,1,1,14,24\n5,1,2,15,25\n"
            )
            first_valid_times = {}
            for index, (grid_id, target) in enumerate(targets.items()):
                sample_id = "sample_" + grid_id
                sample_path = sample_dir / (grid_id + ".om")
                sample_path.write_bytes(f"sample-{index}".encode())
                coordinate_path = coordinate_dir / (grid_id + ".csv")
                coordinate_path.write_text(coordinates, encoding="utf-8")
                objects.append({
                    "id": sample_id,
                    "classification": "real_public_open_meteo_om_v3",
                    "grid_definition_id": grid_id,
                    "local_copy": {"path": str(sample_path.relative_to(root))},
                    "sha256": grid_spatial_reference.sha256_file(sample_path),
                    "array": {"shape": [2, 3, 4]},
                })
                references.append({
                    "target": target,
                    "sample_id": sample_id,
                    "method": "pinned_producer_coordinates",
                    "numeric_policy": "openmeteo_f32_v1",
                    "tolerance_degrees": 1e-4,
                    "row_count": 6,
                    "artifact": {
                        "path": str(coordinate_path.relative_to(root)),
                        "sha256": grid_spatial_reference.sha256_file(coordinate_path),
                    },
                })
                first_valid_times[grid_id] = "2026-01-01 00:00:00"
                read = "read_om('" + str(sample_path) + "', grid := {'version': 1}, include_source := true)"
                views.extend([
                    f"CREATE TEMP VIEW explicit_{grid_id} AS SELECT * FROM {read};",
                    f"CREATE TEMP VIEW domain_{grid_id} AS SELECT * FROM {read};",
                ])

            (data_dir / "sample-manifest.json").write_text(
                json.dumps({"public_open_meteo_samples": {"objects": objects}}), encoding="utf-8"
            )
            (data_dir / "coordinate-reference.json").write_text(
                json.dumps({"references": references}), encoding="utf-8"
            )
            (data_dir / "sample-queries.sql").write_text("\n".join(views), encoding="utf-8")
            output = root / "output"
            statements, records, reason = grid_spatial_reference.prepare_public_sample_queries(
                root, output, full_positions=True, first_valid_times=first_valid_times,
                spatial_selection=True,
            )

            self.assertEqual(len(records), 3)
            self.assertIn("full spatial positions", reason)
            self.assertIn("materialized full-scan source baseline", reason)
            first = records[0]
            self.assertEqual(first["spatial_selection_bounds"]["anchor_spatial_index"], 3)
            self.assertEqual(first["spatial_selection_bounds"]["latitude_min"], 22.8)
            self.assertEqual(first["spatial_selection_bounds"]["latitude_max"], 23.2)
            self.assertEqual(first["spatial_selection_bounds"]["longitude_min"], 12.8)
            self.assertEqual(first["spatial_selection_bounds"]["longitude_max"], 13.2)

            baseline = [statement for statement in statements
                        if "gem_rdps_10km.explicit-full-source.csv.gz" in statement]
            selected = [statement for statement in statements
                        if "gem_rdps_10km.explicit-spatial-source.csv" in statement]
            identity = [statement for statement in statements
                        if "gem_rdps_10km.spatial-source-identity.csv" in statement]
            self.assertEqual(len(baseline), 1)
            self.assertEqual(len(selected), 1)
            self.assertEqual(len(identity), 1)
            self.assertNotIn("lat BETWEEN", baseline[0])
            self.assertIn("valid_time = TIMESTAMP '2026-01-01 00:00:00'", baseline[0])
            self.assertIn("lat BETWEEN 22.8 AND 23.2", selected[0])
            self.assertIn("lon BETWEEN 12.8 AND 13.2", selected[0])
            self.assertIn("om_source.object_id", selected[0])
            self.assertIn("FULL OUTER JOIN", identity[0])
            self.assertIn("om_source.logical_index", identity[0])
            self.assertNotIn("ORDER BY", selected[0])

    def test_resolves_only_matching_pinned_producer_reference(self) -> None:
        with TemporaryDirectory(prefix="duckomo-public-coordinate-reference-") as directory:
            root = Path(directory)
            artifact = root / "coordinates.csv"
            artifact.write_text(
                "spatial_index,row,column,longitude,latitude\n"
                "0,0,0,10,20\n1,0,1,11,21\n2,0,2,12,22\n"
                "3,1,0,13,23\n4,1,1,14,24\n5,1,2,15,25\n"
            )
            sample = {
                "id": "rotated_sample",
                "grid_definition_id": "gem_rdps_10km",
                "array": {"shape": [2, 3, 4]},
            }
            manifest = {"references": [{
                "target": "rotated_v3",
                "sample_id": "rotated_sample",
                "method": "pinned_producer_coordinates",
                "numeric_policy": "openmeteo_f32_v1",
                "tolerance_degrees": 1e-4,
                "row_count": 6,
                "artifact": {
                    "path": str(artifact),
                    "sha256": grid_spatial_reference.sha256_file(artifact),
                },
            }]}
            resolved = grid_spatial_reference.resolve_public_coordinate_reference(root, sample, manifest)
            self.assertEqual(resolved["row_count"], 6)
            self.assertEqual(resolved["method"], "pinned_producer_coordinates")
            manifest["references"][0]["sample_id"] = "different_sample"
            with self.assertRaises(ValueError):
                grid_spatial_reference.resolve_public_coordinate_reference(root, sample, manifest)

    def test_compares_public_source_positions_to_reference_at_fixed_tolerance(self) -> None:
        with TemporaryDirectory(prefix="duckomo-public-source-coordinates-") as directory:
            reference = Path(directory) / "coordinates.csv"
            reference.write_text(
                "spatial_index,row,column,longitude,latitude\n"
                "0,0,0,10,20\n1,0,1,11,21\n2,0,2,12,22\n"
                "3,1,0,13,23\n4,1,1,14,24\n5,1,2,15,25\n"
            )
            record = {
                "id": "rotated_sample",
                "shape": [2, 3, 4],
                "coordinate_reference_csv": str(reference),
                "coordinate_tolerance_degrees": 1e-4,
            }
            positions = [
                {"point_index": index, "longitude": 10.0 + index + 5e-5, "latitude": 20.0 + index}
                for index in range(4)
            ]
            self.assertAlmostEqual(
                grid_spatial_reference.compare_public_source_coordinates(record, positions), 5e-5
            )
            positions[3]["latitude"] += 2e-4
            with self.assertRaises(ValueError):
                grid_spatial_reference.compare_public_source_coordinates(record, positions)

    def test_value_comparison_matches_permuted_scan_rows_by_logical_index(self) -> None:
        with TemporaryDirectory(prefix="duckomo-value-compare-") as directory:
            root = Path(directory)
            actual = root / "actual.csv"
            reference = root / "reference.csv"
            actual.write_text("logical_index,value\n2,-999\n0,1.25\n1,nan\n")
            reference.write_text("index,value\n0,1.25000000\n1,nan\n2,-999.0\n")
            result = grid_reference_compare.compare_values(actual, reference, 3, 1)
            self.assertEqual(result["positions_compared"], 3)
            self.assertEqual(result["null_count"], 1)
            self.assertEqual(result["scan_row_order"], "arbitrary; unique, in-range logical indices required")
            actual.write_text("logical_index,value\n0,1.5\n1,nan\n2,-999\n")
            with self.assertRaises(grid_reference_compare.ReferenceCompareError):
                grid_reference_compare.compare_values(actual, reference, 3, 1)
            actual.write_text("logical_index,value\n0,1.25\n0,nan\n2,-999\n")
            with self.assertRaises(grid_reference_compare.ReferenceCompareError):
                grid_reference_compare.compare_values(actual, reference, 3, 1)


if __name__ == "__main__":
    unittest.main()
