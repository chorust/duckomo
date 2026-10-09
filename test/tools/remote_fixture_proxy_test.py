#!/usr/bin/env python3
"""Exercise the fixture proxy through separate downstream HTTP connections."""
from contextlib import closing
from concurrent.futures import ThreadPoolExecutor
import http.client
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[2]


class Upstream(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self):
        self.accepted = 0
        self.budget = None
        self.requests = []
        super().__init__(("127.0.0.1", 0), UpstreamHandler)

    def get_request(self):
        connection, address = super().get_request()
        self.accepted += 1
        if self.budget is not None and self.accepted > self.budget:
            connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            connection.close()
            raise OSError("test upstream connection budget exhausted")
        return connection, address


class UpstreamHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        self.server.requests.append(dict(self.headers))
        if self.path == "/slow":
            time.sleep(.03)
        if self.path == "/reset":
            self.close_connection = True
            return
        payload = b"abc" if self.path == "/partial" else b"range payload"
        self.send_response(206)
        self.send_header("Content-Length", "8" if self.path == "/partial" else str(len(payload)))
        self.send_header("Content-Range", "bytes 0-7/8" if self.path == "/partial" else "bytes 0-12/13")
        if self.path in {"/partial", "/close"}:
            self.send_header("Connection", "close")
            self.close_connection = True
        if self.path == "/silent-close":
            self.close_connection = True
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(payload)
            self.wfile.flush()


class ProxyTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.upstream = Upstream()
        self.thread = threading.Thread(target=self.upstream.serve_forever, daemon=True)
        self.thread.start()
        self.log = self.directory / "audit.jsonl"
        ready = self.directory / "ready.json"
        self.process = subprocess.Popen(
            [sys.executable, str(ROOT / "scripts/setup-remote-fixtures.py"), "__s3_proxy",
             "--upstream", f"http://127.0.0.1:{self.upstream.server_port}",
             "--readiness", str(ready), "--log", str(self.log)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(200):
            if ready.exists():
                self.port = json.loads(ready.read_text())["port"]
                break
            if self.process.poll() is not None:
                self.fail("proxy exited before readiness")
            time.sleep(.01)
        else:
            self.fail("proxy did not become ready")
        self.headers = {"Host": "signed.example:9000", "Authorization": "test-signature",
                        "Range": "bytes=0-12"}

    def tearDown(self):
        self.process.terminate()
        self.process.wait(timeout=5)
        self.upstream.shutdown()
        self.upstream.server_close()
        self.thread.join(timeout=5)
        self.temp.cleanup()

    def request(self, method="GET", path="/object"):
        with closing(http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)) as connection:
            connection.request(method, path, headers=self.headers)
            response = connection.getresponse()
            return response.status, response.read()

    def audit(self, count):
        for _ in range(200):
            rows = [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []
            if len(rows) >= count:
                return rows
            time.sleep(.01)
        self.fail("missing proxy audit event")

    def test_new_downstream_connections_share_upstream_connection(self):
        # Model the finite TCP port budget in Docker's forwarding layer.
        self.upstream.budget = 4
        for index in range(40):
            method = "HEAD" if index % 2 else "GET"
            status, body = self.request(method)
            self.assertEqual(status, 206)
            self.assertEqual(body, b"" if method == "HEAD" else b"range payload")
        self.assertLessEqual(self.upstream.accepted, 4)
        rows = self.audit(40)
        self.assertEqual(sum(row["body_bytes"] for row in rows), 20 * len(b"range payload"))
        for headers in self.upstream.requests:
            self.assertEqual(headers["Host"], self.headers["Host"])
            self.assertEqual(headers["Authorization"], self.headers["Authorization"])
        self.assertNotIn("test-signature", self.log.read_text())

    def test_upstream_reset_counts_generated_error_body_and_recovers(self):
        status, body = self.request(path="/reset")
        self.assertEqual(status, 502)
        rows = self.audit(1)
        self.assertEqual(rows[0]["body_bytes"], len(body))
        self.assertTrue(rows[0]["error_type"])
        self.assertEqual(self.request(), (206, b"range payload"))

    def test_concurrent_downstream_clients_keep_the_upstream_pool_bounded(self):
        self.upstream.budget = 16
        with ThreadPoolExecutor(max_workers=32) as clients:
            results = list(clients.map(lambda _: self.request(path="/slow"), range(64)))
        self.assertEqual(results, [(206, b"range payload")] * 64)
        self.assertLessEqual(self.upstream.accepted, 16)
        self.assertEqual(sum(row["body_bytes"] for row in self.audit(64)), 64 * len(b"range payload"))

    def test_head_reset_has_no_response_body(self):
        self.assertEqual(self.request("HEAD", "/reset"), (502, b""))
        self.assertEqual(self.audit(1)[0]["body_bytes"], 0)

    def test_partial_response_is_not_followed_by_a_second_http_response(self):
        with closing(http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)) as connection:
            connection.request("GET", "/partial", headers=self.headers)
            response = connection.getresponse()
            self.assertEqual(response.status, 206)
            with self.assertRaises(http.client.IncompleteRead) as raised:
                response.read()
            self.assertEqual(raised.exception.partial, b"abc")
        rows = self.audit(1)
        self.assertEqual(rows[0]["status"], 206)
        self.assertEqual(rows[0]["body_bytes"], 3)
        self.assertEqual(rows[0]["error_type"], "IncompleteRead")
        self.assertEqual(self.request(), (206, b"range payload"))

    def test_upstream_connection_close_is_respected(self):
        self.assertEqual(self.request(path="/close"), (206, b"range payload"))
        self.assertEqual(self.request(), (206, b"range payload"))
        self.assertEqual(self.upstream.accepted, 2)

    def test_idle_closed_socket_is_discarded_before_the_next_request(self):
        self.assertEqual(self.request(path="/silent-close"), (206, b"range payload"))
        time.sleep(.02)
        self.assertEqual(self.request(), (206, b"range payload"))
        self.assertEqual(self.upstream.accepted, 2)
        self.assertTrue(all("error_type" not in row for row in self.audit(2)))


if __name__ == "__main__":
    unittest.main()
