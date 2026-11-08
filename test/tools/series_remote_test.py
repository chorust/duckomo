#!/usr/bin/env python3
"""Unit checks for the isolated read_om_series remote fixture adapter."""

import hashlib
import http.client
import importlib.util
import json
from pathlib import Path
import stat
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("series_remote", ROOT / "scripts/series_remote.py")
series_remote = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = series_remote
SPEC.loader.exec_module(series_remote)


class SeriesRemoteTest(unittest.TestCase):
    def write_fixtures(self, root: Path) -> tuple[Path, dict[str, bytes]]:
        payloads = {
            "data/synthetic/temperature/chunk_0000.om": b"OM\x03original-chunk-zero",
            "data/replacements/temperature/chunk_0000.om": b"OM\x03replacement-chunk-zero-longer",
            "data/gaps/first-missing/temperature/chunk_0001.om": b"OM\x03same-basename-first-gap",
        }
        objects = []
        for key, payload in payloads.items():
            path = root / key
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
            objects.append({"id": key.rsplit("/", 1)[-1] + str(len(objects)), "path": key,
                            "sha256": hashlib.sha256(payload).hexdigest(), "size_bytes": len(payload)})
        manifest = {
            "feature": "010-read-om-series",
            "objects": objects[:2],
            "gap_prefixes": [{"id": "first_missing", "prefix": "data/gaps/first-missing/temperature",
                              "objects": [objects[2]]}],
        }
        (root / "series-fixtures.json").write_text(json.dumps(manifest), encoding="utf-8")
        return root / "series-fixtures.json", payloads

    def start_server(self, catalog, transport="http", audit_path=None, bucket="duckomo-series"):
        temporary = tempfile.TemporaryDirectory()
        audit_path = audit_path or Path(temporary.name) / "requests.jsonl"
        audit_path.parent.mkdir(parents=True, exist_ok=True)
        audit_path.touch(exist_ok=True)
        states = {}
        lock = threading.RLock()
        token = "private-control-token-do-not-log"
        server = series_remote.create_fixture_server(catalog, transport, audit_path, states, lock, token, bucket)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.shutdown)
        self.addCleanup(server.server_close)
        self.addCleanup(thread.join, 2)
        self.addCleanup(temporary.cleanup)
        return f"127.0.0.1:{server.server_address[1]}", audit_path, token

    @staticmethod
    def request(endpoint: str, method: str, path: str, headers: dict | None = None, body: bytes | None = None):
        host, port_text = endpoint.rsplit(":", 1)
        connection = http.client.HTTPConnection(host, int(port_text), timeout=3)
        try:
            connection.request(method, path, body=body, headers=headers or {})
            response = connection.getresponse()
            payload = response.read()
            return response.status, dict(response.getheaders()), payload
        finally:
            connection.close()

    def control(self, endpoint: str, token: str, payload: dict) -> int:
        status, _, _ = self.request(endpoint, "POST", "/__control",
                                    {"Content-Type": "application/json", "X-DuckOMO-Control": token},
                                    json.dumps(payload).encode())
        return status

    @staticmethod
    def read_audit_after_flush(path: Path) -> str:
        deadline = time.monotonic() + 1
        previous = ""
        while time.monotonic() < deadline:
            current = path.read_text(encoding="utf-8")
            if current == previous and current:
                return current
            previous = current
            time.sleep(0.01)
        return previous

    def test_manifest_catalog_uses_full_paths_and_rejects_identity_drift(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixtures(root)
            catalog, manifest_sha = series_remote.load_fixture_catalog(root)
            self.assertEqual(len(catalog), 3)
            self.assertEqual(len(manifest_sha), 64)
            self.assertNotEqual(
                catalog["data/synthetic/temperature/chunk_0000.om"].payload,
                catalog["data/gaps/first-missing/temperature/chunk_0001.om"].payload,
            )
            (root / "data/synthetic/temperature/chunk_0000.om").write_bytes(b"OM\x03changed")
            with self.assertRaisesRegex(ValueError, "size/hash mismatch"):
                series_remote.load_fixture_catalog(root)

    def test_fixture_key_rejects_noncanonical_and_escaping_paths(self):
        for key in ("../chunk.om", "data//chunk.om", "data/./chunk.om", "/data/chunk.om",
                    "data\\chunk.om", "data/chunk.txt", "data/naïve.om"):
            with self.subTest(key=key), self.assertRaises(ValueError):
                series_remote.safe_fixture_key(key)

    def test_http_ranges_mutation_faults_and_audit_redaction(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixtures(root)
            catalog, _ = series_remote.load_fixture_catalog(root)
            endpoint, audit_path, token = self.start_server(catalog, audit_path=root / "requests.jsonl")
            target = "data/synthetic/temperature/chunk_0000.om"
            replacement = "data/replacements/temperature/chunk_0000.om"

            status, headers, body = self.request(endpoint, "GET", "/" + target,
                                                 {"Range": "bytes=1-4", "Authorization": "Bearer private-header"})
            self.assertEqual(status, 206)
            self.assertEqual(body, catalog[target].payload[1:5])
            self.assertEqual(headers["Content-Range"], f"bytes 1-4/{len(catalog[target].payload)}")
            status, headers, _ = self.request(endpoint, "HEAD", "/" + target)
            self.assertEqual(status, 200)
            old_etag = headers["ETag"]

            self.assertEqual(self.control(endpoint, token, {"transport": "http", "object": target,
                                                             "action": "replace", "replacement": replacement}), 204)
            status, _, _ = self.request(endpoint, "GET", "/" + target, {"If-Match": old_etag})
            self.assertEqual(status, 412)
            status, _, body = self.request(endpoint, "GET", "/" + target)
            self.assertEqual(status, 200)
            self.assertEqual(body, catalog[replacement].payload)

            self.assertEqual(self.control(endpoint, token, {"transport": "http", "object": target,
                                                             "action": "remove"}), 204)
            status, _, _ = self.request(endpoint, "HEAD", "/" + target)
            self.assertEqual(status, 404)
            self.assertEqual(self.control(endpoint, token, {"transport": "http", "object": target,
                                                             "action": "restore"}), 204)
            status, _, body = self.request(endpoint, "GET", "/" + target)
            self.assertEqual((status, body), (200, catalog[target].payload))

            self.assertEqual(self.control(endpoint, token, {"transport": "http", "object": target,
                                                             "action": "fault", "fault": "body_404_status_403"}), 204)
            status, _, body = self.request(endpoint, "GET", "/" + target)
            self.assertEqual((status, body), (403, b"404"))
            for fault, expected_status in (("http_403", 403), ("http_404", 404),
                                           ("http_412", 412), ("http_500", 500), ("http_503", 503),
                                           ("corrupt", 200)):
                self.assertEqual(self.control(endpoint, token, {"transport": "http", "object": target,
                                                                 "action": "fault", "fault": fault}), 204)
                status, _, body = self.request(endpoint, "GET", "/" + target)
                self.assertEqual(status, expected_status, fault)
                if fault == "corrupt":
                    self.assertEqual(body, b"not-an-om-object")
            self.assertEqual(self.control(endpoint, token, {"transport": "http", "object": target,
                                                             "action": "fault", "fault": "drop"}), 204)
            with self.assertRaises((http.client.HTTPException, OSError)):
                self.request(endpoint, "GET", "/" + target)
            self.assertEqual(self.control(endpoint, token, {"transport": "http", "object": target,
                                                             "action": "restore"}), 204)
            self.request(endpoint, "GET", "/" + target + "?private-query-value=secret")

            audit = self.read_audit_after_flush(audit_path)
            self.assertNotIn(token, audit)
            self.assertNotIn("Authorization", audit)
            self.assertNotIn("X-DuckOMO-Control", audit)
            self.assertNotIn("private-header", audit)
            self.assertNotIn("private-query-value", audit)
            self.assertNotIn("secret", audit)
            events = [json.loads(line) for line in audit.splitlines()]
            responses = [event for event in events if event.get("event") == "response"]
            self.assertTrue(responses)
            self.assertFalse(any(event.get("list_request") for event in responses))
            self.assertFalse(any(event.get("metadata_request") for event in responses))
            self.assertTrue(any(event.get("event") == "control" and event.get("state") == "replacement"
                                for event in events))
            self.assertTrue(any(event.get("status") == 403 and event.get("fault") == "body_404_status_403"
                                for event in events))
            self.assertTrue(any(event.get("query_present") is True for event in events))

    def test_http_diagnostic_query_faults_do_not_retain_query_values(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixtures(root)
            catalog, _ = series_remote.load_fixture_catalog(root)
            endpoint, audit_path, _ = self.start_server(catalog, audit_path=root / "requests.jsonl")
            target = "data/synthetic/temperature/chunk_0000.om"
            status, _, _ = self.request(endpoint, "GET", "/" + target + "?fault=403&token=sensitive-marker")
            self.assertEqual(status, 403)
            status, _, _ = self.request(endpoint, "HEAD", "/" + target + "?fault=403-data&token=sensitive-marker")
            self.assertEqual(status, 200)
            status, _, _ = self.request(endpoint, "GET", "/" + target + "?fault=403-data&token=sensitive-marker")
            self.assertEqual(status, 403)
            status, _, _ = self.request(endpoint, "GET", "/" + target + "?fault=404&token=sensitive-marker")
            self.assertEqual(status, 404)
            status, _, body = self.request(endpoint, "GET", "/" + target + "?token=sensitive-marker")
            self.assertEqual((status, body), (200, catalog[target].payload))
            audit = self.read_audit_after_flush(audit_path)
            self.assertNotIn("sensitive-marker", audit)
            events = [json.loads(line) for line in audit.splitlines()]
            self.assertTrue(any(event.get("fault") == "http_403" and event.get("query_present") for event in events))
            self.assertTrue(any(event.get("fault") == "http_404" and event.get("query_present") for event in events))

    def test_s3_path_style_ranges_structured_fault_and_listing_audit(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixtures(root)
            catalog, _ = series_remote.load_fixture_catalog(root)
            endpoint, audit_path, token = self.start_server(catalog, "s3", root / "requests.jsonl")
            target = "data/synthetic/temperature/chunk_0000.om"
            status, headers, body = self.request(endpoint, "GET", "/duckomo-series/" + target,
                                                 {"Range": "bytes=0-2", "Authorization": "AWS4-HMAC-SHA256 private"})
            self.assertEqual(status, 206)
            self.assertEqual(body, catalog[target].payload[:3])
            self.assertEqual(headers["ETag"], '"' + hashlib.md5(catalog[target].payload).hexdigest() + '"')

            self.assertEqual(self.control(endpoint, token, {"transport": "s3", "object": target,
                                                             "action": "fault", "fault": "http_503"}), 204)
            status, headers, body = self.request(endpoint, "GET", "/duckomo-series/" + target)
            self.assertEqual(status, 503)
            self.assertEqual(headers["Content-Type"], "application/xml")
            self.assertIn(b"ServiceUnavailable", body)
            self.assertEqual(self.control(endpoint, token, {"transport": "s3", "object": target,
                                                             "action": "fault", "fault": "corrupt"}), 204)
            status, headers, body = self.request(endpoint, "GET", "/duckomo-series/" + target)
            self.assertEqual(status, 200)
            self.assertEqual(body, b"not-an-om-object")

            self.request(endpoint, "GET", "/duckomo-series?list-type=2&prefix=private-query-value")
            events = [json.loads(line) for line in self.read_audit_after_flush(audit_path).splitlines()]
            self.assertTrue(any(event.get("transport") == "s3" and event.get("list_request") is True
                                for event in events))
            audit = self.read_audit_after_flush(audit_path)
            self.assertNotIn("private-query-value", audit)
            self.assertNotIn("AWS4-HMAC", audit)

    def test_origin_audit_classifies_metadata_and_keeps_chunk_reads_discovery_free(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixtures(root)
            catalog, _ = series_remote.load_fixture_catalog(root)
            endpoint, audit_path, _ = self.start_server(catalog, "s3", root / "requests.jsonl")
            target = "data/synthetic/temperature/chunk_0000.om"
            status, _, _ = self.request(
                endpoint, "GET", "/duckomo-series/data/ecmwf_ifs/static/meta.json")
            self.assertEqual(status, 404)
            status, _, body = self.request(endpoint, "GET", "/duckomo-series/" + target)
            self.assertEqual((status, body), (200, catalog[target].payload))

            events = [json.loads(line) for line in self.read_audit_after_flush(audit_path).splitlines()]
            responses = [event for event in events if event.get("event") == "response"]
            metadata = [event for event in responses if event.get("metadata_request")]
            chunks = [event for event in responses if event.get("object") == target]
            self.assertEqual(len(metadata), 1)
            self.assertEqual(metadata[0]["object"], "data/ecmwf_ifs/static/meta.json")
            self.assertEqual(len(chunks), 1)
            self.assertFalse(chunks[0]["metadata_request"])
            self.assertFalse(any(event.get("list_request") for event in responses))
            self.assertFalse(any(event.get("metadata_request") or event.get("list_request")
                                 for event in chunks))

    def test_startup_private_files_and_after_bind_control_lifecycle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "fixtures"
            root.mkdir()
            self.write_fixtures(root)
            output = Path(temporary) / "remote-run"
            info = series_remote.start_service(root, output)
            try:
                private_path = output / "private/config.json"
                private = json.loads(private_path.read_text(encoding="utf-8"))
                self.assertEqual(stat.S_IMODE(private_path.stat().st_mode), 0o600)
                self.assertEqual(stat.S_IMODE((output / "run.env").stat().st_mode), 0o600)
                self.assertEqual(stat.S_IMODE((output / "s3-setup.sql").stat().st_mode), 0o600)
                manifest_text = (output / "run-manifest.json").read_text(encoding="utf-8")
                self.assertNotIn(private["control_token"], manifest_text)
                self.assertNotIn(private["s3_secret_key"], manifest_text)
                self.assertEqual(info["object_count"], 3)

                target = "data/synthetic/temperature/chunk_0000.om"
                replacement = "data/replacements/temperature/chunk_0000.om"
                series_remote.control_service(output, target, "replace", replacement=replacement)
                endpoint = info["http_base"].removeprefix("http://")
                status, _, body = self.request(endpoint, "GET", "/" + target)
                self.assertEqual((status, body), (200, series_remote.load_fixture_catalog(root)[0][replacement].payload))
            finally:
                series_remote.stop_service(output)
            self.assertFalse((output / "service-pids.json").exists())
            self.assertTrue((output / "run-manifest.json").exists())

    def test_startup_serves_verified_https_with_controlled_faults(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixtures = root / "fixtures"
            fixtures.mkdir()
            self.write_fixtures(fixtures)
            cert = root / "fixture-cert.pem"
            key = root / "fixture-key.pem"
            generated = subprocess.run(
                ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", str(key),
                 "-out", str(cert), "-days", "1", "-subj", "/CN=127.0.0.1",
                 "-addext", "subjectAltName=IP:127.0.0.1"],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
            )
            if generated.returncode != 0:
                self.skipTest(f"openssl could not create a local TLS certificate: {generated.stdout}")
            output = root / "https-run"
            info = series_remote.start_service(fixtures, output, cert, key, cert)
            try:
                target = "data/synthetic/temperature/chunk_0000.om"
                endpoint = info["https_base"].removeprefix("https://")
                context = ssl.create_default_context(cafile=str(cert))
                host, port_text = endpoint.rsplit(":", 1)
                connection = http.client.HTTPSConnection(host, int(port_text), context=context, timeout=3)
                try:
                    connection.request("GET", "/" + target)
                    response = connection.getresponse()
                    body = response.read()
                    self.assertEqual(response.status, 200)
                    self.assertEqual(body, series_remote.load_fixture_catalog(fixtures)[0][target].payload)
                finally:
                    connection.close()
                series_remote.control_service(output, target, "fault", transport="https", fault="http_403")
                connection = http.client.HTTPSConnection(host, int(port_text), context=context, timeout=3)
                try:
                    connection.request("HEAD", "/" + target)
                    response = connection.getresponse()
                    response.read()
                    self.assertEqual(response.status, 403)
                finally:
                    connection.close()
            finally:
                series_remote.stop_service(output)


if __name__ == "__main__":
    unittest.main()
