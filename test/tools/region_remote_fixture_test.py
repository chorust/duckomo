#!/usr/bin/env python3
"""Validate the five-target manifest mapping used by the controlled remote fixture."""

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("setup_remote_fixtures", ROOT / "scripts/setup-remote-fixtures.py")
REMOTE_FIXTURES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REMOTE_FIXTURES)


class RegionRemoteFixtureTest(unittest.TestCase):
    def test_repo_targets_are_complete_before_any_remote_provisioning(self):
        samples, digest = REMOTE_FIXTURES.load_region_samples(
            ROOT / "test/data/regions/real-targets.json", ROOT)
        self.assertEqual(len(samples), 5)
        self.assertEqual(len(digest), 64)

    def test_loads_exact_frozen_target_set_and_preserves_object_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            ids = ("ncep_gfswave025", "gem_rdps_10km", "gem_regional",
                   "aladin_central_europe_2km", "ecmwf_ifs")
            targets = []
            for sample_id in ids:
                relative = Path("objects") / f"{sample_id}.om"
                path = root / relative
                path.parent.mkdir(exist_ok=True)
                payload = b"OM\x03" + sample_id.encode()
                path.write_bytes(payload)
                targets.append({"grid_id": sample_id, "object": {
                    "path": str(relative), "sha256": hashlib.sha256(payload).hexdigest(),
                    "size_bytes": len(payload)}})
            manifest = root / "targets.json"
            manifest.write_text(json.dumps({"schema_version": 1, "targets": targets}))

            samples, digest = REMOTE_FIXTURES.load_region_samples(manifest, root)

            self.assertEqual({item["id"] for item in samples}, set(ids))
            self.assertEqual({item["key"] for item in samples},
                             {f"duckomo/grids/{sample_id}.om" for sample_id in ids})
            self.assertEqual(digest, hashlib.sha256(manifest.read_bytes()).hexdigest())

    def test_rejects_missing_target_and_identity_mismatch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            ids = ("ncep_gfswave025", "gem_rdps_10km", "gem_regional",
                   "aladin_central_europe_2km", "ecmwf_ifs")
            targets = []
            for sample_id in ids:
                payload = b"OM\x03" + sample_id.encode()
                target = root / f"{sample_id}.om"
                target.write_bytes(payload)
                targets.append({"grid_id": sample_id, "object": {
                    "path": target.name, "sha256": hashlib.sha256(payload).hexdigest(),
                    "size_bytes": len(payload)}})
            manifest = root / "targets.json"
            manifest.write_text(json.dumps({"schema_version": 1, "targets": targets[:-1]}))
            with self.assertRaisesRegex(RuntimeError, "exactly the five"):
                REMOTE_FIXTURES.load_region_samples(manifest, root)
            targets[-1]["object"]["sha256"] = "0" * 64
            manifest.write_text(json.dumps({"schema_version": 1, "targets": targets}))
            with self.assertRaisesRegex(RuntimeError, "size/hash mismatch"):
                REMOTE_FIXTURES.load_region_samples(manifest, root)


if __name__ == "__main__":
    unittest.main()
