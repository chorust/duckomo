#!/usr/bin/env python3
"""Serve isolated read_om_series fixtures over controlled HTTP, HTTPS and S3."""

from __future__ import annotations

import argparse
import atexit
import hashlib
import hmac
import http.client
import importlib.util
import json
import os
import secrets
import shlex
import signal
import ssl
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
REMOTE_FIXTURE_SCRIPT = Path(__file__).resolve().with_name("setup-remote-fixtures.py")
_REMOTE_SPEC = importlib.util.spec_from_file_location("series_remote_fixture_primitives", REMOTE_FIXTURE_SCRIPT)
if _REMOTE_SPEC is None or _REMOTE_SPEC.loader is None:
    raise RuntimeError("could not load existing remote fixture primitives")
_REMOTE = importlib.util.module_from_spec(_REMOTE_SPEC)
_REMOTE_SPEC.loader.exec_module(_REMOTE)

FAULTS = {
    "http_403": (403, "AccessDenied"),
    "http_404": (404, "NoSuchKey"),
    "http_412": (412, "PreconditionFailed"),
    "http_500": (500, "InternalError"),
    "http_503": (503, "ServiceUnavailable"),
    "corrupt": (200, "CorruptObject"),
    "body_404_status_403": (403, "body_404_status_403"),
    "drop": (None, "TransportClosed"),
}
CONTROL_ACTIONS = {"restore": "normal", "remove": "missing", "replace": "replacement", "fault": "fault"}

_CHILDREN: dict[int, subprocess.Popen] = {}


def _detach_children_at_exit() -> None:
    for process in _CHILDREN.values():
        if process.poll() is None:
            # The CLI intentionally leaves this loopback service under PID-file control.
            process.returncode = 0


atexit.register(_detach_children_at_exit)


@dataclass(frozen=True)
class FixtureObject:
    key: str
    identifier: str
    path: Path
    payload: bytes
    sha256: str
    size_bytes: int


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def safe_fixture_key(value: object) -> str:
    if not isinstance(value, str) or not value or "\\" in value or "\x00" in value:
        raise ValueError("fixture object path must be a non-empty relative POSIX path")
    parts = value.split("/")
    if (value.startswith("/") or any(part in {"", ".", ".."} for part in parts) or
            any(not all(char.isascii() and (char.isalnum() or char in "_.-") for char in part)
                for part in parts) or not value.endswith(".om")):
        raise ValueError(f"unsafe series fixture object path: {value!r}")
    return "/".join(parts)


def load_fixture_catalog(fixtures_root: Path, manifest_path: Path | None = None) -> tuple[dict[str, FixtureObject], str]:
    """Load only manifest-listed objects and verify their frozen size and hash."""
    fixtures_root = fixtures_root.resolve(strict=True)
    manifest_path = (manifest_path or fixtures_root / "series-fixtures.json").resolve(strict=True)
    if not manifest_path.is_relative_to(fixtures_root) or not manifest_path.is_file():
        raise ValueError("series fixture manifest must be inside the fixture root")
    raw_manifest = manifest_path.read_bytes()
    manifest = json.loads(raw_manifest)
    if manifest.get("feature") != "010-read-om-series":
        raise ValueError("fixture manifest is not for 010-read-om-series")

    entries: dict[str, dict] = {}
    for entry in manifest.get("objects", []):
        if not isinstance(entry, dict):
            raise ValueError("fixture manifest contains an invalid object entry")
        key = safe_fixture_key(entry.get("path"))
        if key in entries:
            raise ValueError(f"duplicate series fixture object path: {key}")
        entries[key] = entry
    for prefix in manifest.get("gap_prefixes", []):
        if not isinstance(prefix, dict) or not isinstance(prefix.get("objects"), list):
            raise ValueError("fixture manifest contains an invalid gap prefix")
        for entry in prefix["objects"]:
            if not isinstance(entry, dict):
                raise ValueError("fixture manifest contains an invalid gap object")
            key = safe_fixture_key(entry.get("path"))
            if key in entries:
                prior = entries[key]
                if prior.get("sha256") != entry.get("sha256") or prior.get("size_bytes") != entry.get("size_bytes"):
                    raise ValueError(f"fixture manifest has conflicting identities for {key}")
                continue
            entries[key] = entry

    catalog: dict[str, FixtureObject] = {}
    for key, entry in entries.items():
        expected_hash = entry.get("sha256")
        expected_size = entry.get("size_bytes")
        identifier = entry.get("id", key)
        if (not isinstance(expected_hash, str) or len(expected_hash) != 64 or
                any(char not in "0123456789abcdef" for char in expected_hash) or
                not isinstance(expected_size, int) or isinstance(expected_size, bool) or expected_size <= 0 or
                not isinstance(identifier, str) or not identifier):
            raise ValueError(f"fixture manifest identity is incomplete for {key}")
        candidate = fixtures_root / key
        if candidate.is_symlink():
            raise ValueError(f"series fixture path is a symbolic link: {key}")
        path = candidate.resolve(strict=True)
        if not path.is_relative_to(fixtures_root) or not path.is_file():
            raise ValueError(f"series fixture path escapes the fixture root: {key}")
        if path.stat().st_size != expected_size or sha256_file(path) != expected_hash:
            raise ValueError(f"series fixture size/hash mismatch for {key}")
        payload = path.read_bytes()
        if not payload.startswith(b"OM\x03"):
            raise ValueError(f"series fixture is not OM v3: {key}")
        catalog[key] = FixtureObject(key, identifier, path, payload, expected_hash, expected_size)
    if not catalog:
        raise ValueError("series fixture manifest contains no objects")
    return catalog, hashlib.sha256(raw_manifest).hexdigest()


def _atomic_json(path: Path, value: object) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def _write_private_json(path: Path, value: object) -> None:
    _REMOTE.write_private_text(path, json.dumps(value, sort_keys=True) + "\n")


def _append_audit(path: Path, lock: threading.Lock, value: dict) -> None:
    with lock:
        with path.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(value, sort_keys=True) + "\n")
            stream.flush()


def _safe_metadata_request_key(value: str) -> str | None:
    parts = value.split("/")
    if (not value or value.startswith("/") or parts[-1].lower() != "meta.json" or
            any(part in {"", ".", ".."} for part in parts) or
            any(not all(char.isascii() and (char.isalnum() or char in "_.-") for char in part)
                for part in parts)):
        return None
    return "/".join(parts)


def _safe_request_key(path: str, transport: str, bucket: str) -> tuple[str | None, bool, bool]:
    """Return (object key, list request, query present), without retaining query values."""
    parsed = urlsplit(path)
    query = parse_qs(parsed.query, keep_blank_values=True)
    if transport == "s3":
        parts = [unquote(part) for part in parsed.path.split("/") if part]
        if not parts or parts[0] != bucket:
            return None, False, bool(parsed.query)
        if len(parts) == 1:
            is_list = bool(query) or parsed.path.rstrip("/").endswith(bucket)
            return None, is_list, bool(parsed.query)
        raw_key = "/".join(parts[1:])
        try:
            key = safe_fixture_key(raw_key)
        except ValueError:
            key = _safe_metadata_request_key(raw_key)
            if key is None:
                return None, False, bool(parsed.query)
        is_list = any(name in query for name in ("list-type", "prefix", "delimiter", "marker"))
        return key, is_list, bool(parsed.query)
    raw_key = unquote(parsed.path.lstrip("/"))
    try:
        key = safe_fixture_key(raw_key)
    except ValueError:
        key = _safe_metadata_request_key(raw_key)
        if key is None:
            return None, False, bool(parsed.query)
    return key, False, bool(parsed.query)


def _parse_range(value: str | None, size: int) -> tuple[int, int] | None:
    if not value:
        return (0, size - 1)
    if not value.startswith("bytes=") or "," in value:
        return None
    spec = value[6:]
    if "-" not in spec:
        return None
    first, last = spec.split("-", 1)
    try:
        if not first:
            suffix = int(last)
            if suffix <= 0:
                return None
            start = max(0, size - suffix)
            end = size - 1
        else:
            start = int(first)
            end = size - 1 if not last else min(int(last), size - 1)
    except ValueError:
        return None
    if start < 0 or end < start or start >= size:
        return None
    return start, end


def create_fixture_server(
    catalog: dict[str, FixtureObject], transport: str, audit_path: Path, state: dict,
    state_lock: threading.RLock, control_token: str, s3_bucket: str = "duckomo-series",
) -> ThreadingHTTPServer:
    """Create one loopback HTTP-compatible origin sharing state and audit records."""
    if transport not in {"http", "https", "s3"}:
        raise ValueError(f"unsupported controlled transport: {transport}")
    counter = {"value": 0}
    counter_lock = threading.Lock()
    audit_lock = state.setdefault("_audit_lock", threading.Lock())

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_: object) -> None:
            return

        def send_empty(self, status: int, size: int = 0, extra: dict[str, str] | None = None) -> None:
            self.send_response(status)
            self.send_header("Content-Length", str(size))
            self.send_header("Connection", "close")
            for name, value in (extra or {}).items():
                self.send_header(name, value)
            self.end_headers()

        def send_fault(self, status: int, code: str, key: str | None) -> int:
            if code == "CorruptObject":
                body = b"not-an-om-object"
                self.send_response(status)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Connection", "close")
                self.end_headers()
                if self.command != "HEAD":
                    self.wfile.write(body)
                return len(body) if self.command != "HEAD" else 0
            if transport == "s3":
                xml_code = code
                if status == 404:
                    xml_code = "NoSuchKey"
                elif xml_code == "body_404_status_403":
                    xml_code = "AccessDenied"
                body = (f"<Error><Code>{xml_code}</Code><Message>{xml_code}</Message>"
                        f"<Key>{key or ''}</Key><RequestId>series-fixture</RequestId></Error>").encode()
                self.send_response(status)
                self.send_header("Content-Type", "application/xml")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Connection", "close")
                self.end_headers()
                if self.command != "HEAD":
                    self.wfile.write(body)
                return len(body) if self.command != "HEAD" else 0
            body = b"404" if code == "body_404_status_403" else b""
            self.send_response(status)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            if self.command != "HEAD" and body:
                self.wfile.write(body)
            return len(body) if self.command != "HEAD" else 0

        def record(self, *, key: str | None, list_request: bool, query_present: bool,
                   range_value: str | None, status: int | None, etag: str | None,
                   body_bytes: int, object_state: str, fault: str | None = None) -> None:
            with counter_lock:
                counter["value"] += 1
                request_id = f"series-{transport}-{counter['value']}"
            _append_audit(audit_path, audit_lock, {
                "request_id": request_id,
                "event": "response",
                "transport": transport,
                "method": self.command,
                "object": key if key is not None else ("<bucket-list>" if list_request else "<unknown>"),
                "list_request": list_request,
                "metadata_request": bool(key and key.rsplit("/", 1)[-1].lower() == "meta.json"),
                "query_present": query_present,
                "range": range_value or None,
                "status": status,
                "etag": etag,
                "body_bytes": body_bytes,
                "state": object_state,
                "fault": fault,
            })

        def serve_object(self) -> None:
            parsed = urlsplit(self.path)
            if parsed.path == "/__control":
                self.send_empty(405)
                return
            key, list_request, query_present = _safe_request_key(self.path, transport, s3_bucket)
            range_value = self.headers.get("Range")
            if key is None and transport == "s3" and parsed.path.rstrip("/") == "/" + s3_bucket:
                if self.command == "HEAD":
                    self.send_empty(200)
                    self.record(key=None, list_request=False, query_present=query_present, range_value=range_value,
                                status=200, etag=None, body_bytes=0, object_state="bucket")
                    return
                if self.command == "GET" and list_request:
                    body = b"<ListBucketResult/>"
                    self.send_response(200)
                    self.send_header("Content-Type", "application/xml")
                    self.send_header("Content-Length", str(len(body)))
                    self.send_header("Connection", "close")
                    self.end_headers()
                    self.wfile.write(body)
                    self.record(key=None, list_request=True, query_present=query_present, range_value=range_value,
                                status=200, etag=None, body_bytes=len(body), object_state="bucket")
                    return
            if self.command not in {"GET", "HEAD"}:
                self.send_empty(405, extra={"Allow": "GET, HEAD"})
                self.record(key=key, list_request=list_request, query_present=query_present, range_value=range_value,
                            status=405, etag=None, body_bytes=0, object_state="method_rejected")
                return
            query_fault: str | None = None
            query_delay = False
            if transport != "s3" and query_present:
                query = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
                fault_values = query.get("fault", [])
                if fault_values:
                    query_fault = {"403": "http_403", "403-data": "http_403", "404": "http_404"}.get(fault_values[0])
                    query_fault_on_get = fault_values[0] == "403-data"
                    query_delay = fault_values[0] == "timeout"
                    if query_fault is None and not query_delay:
                        self.send_empty(400)
                        self.record(key=key, list_request=False, query_present=True, range_value=range_value,
                                    status=400, etag=None, body_bytes=0, object_state="query_rejected")
                        return
                    if query_fault_on_get and self.command != "GET":
                        query_fault = None

            current_state = state.get((transport, key), {"state": "normal"}) if key else {"state": "missing"}
            state_name = current_state.get("state", "normal")
            fault = query_fault or (current_state.get("fault") if state_name == "fault" else None)
            source_key = current_state.get("replacement") if state_name == "replacement" else key
            fixture = catalog.get(source_key) if source_key else None
            if query_delay:
                time.sleep(2.0)
                try:
                    self.connection.shutdown(2)
                except OSError:
                    pass
                self.connection.close()
                self.record(key=key, list_request=False, query_present=True, range_value=range_value,
                            status=None, etag=None, body_bytes=0, object_state=state_name, fault="http_timeout")
                return
            if fault:
                status, code = FAULTS.get(fault, (500, "InternalError"))
                if status is None:
                    try:
                        self.connection.shutdown(2)
                    except OSError:
                        pass
                    self.connection.close()
                    self.record(key=key, list_request=list_request, query_present=query_present,
                                range_value=range_value, status=None, etag=None, body_bytes=0,
                                object_state=state_name, fault=fault)
                    return
                body_bytes = self.send_fault(status, code, key)
                self.record(key=key, list_request=list_request, query_present=query_present,
                            range_value=range_value, status=status, etag=None, body_bytes=body_bytes,
                            object_state=state_name, fault=fault)
                return
            if state_name == "missing" or fixture is None:
                body_bytes = self.send_fault(404, "NoSuchKey", key)
                self.record(key=key, list_request=list_request, query_present=query_present,
                            range_value=range_value, status=404, etag=None, body_bytes=body_bytes,
                            object_state=state_name)
                return

            payload = fixture.payload
            digest = hashlib.md5(payload).hexdigest() if transport == "s3" else hashlib.sha256(payload).hexdigest()
            etag = '"' + digest + '"'
            if_match = self.headers.get("If-Match")
            if if_match and if_match not in {etag, "*"}:
                body_bytes = self.send_fault(412, "PreconditionFailed", key)
                self.record(key=key, list_request=list_request, query_present=query_present,
                            range_value=range_value, status=412, etag=etag, body_bytes=body_bytes,
                            object_state=state_name)
                return
            byte_range = _parse_range(range_value, len(payload))
            if byte_range is None:
                self.send_empty(416, extra={"Content-Range": f"bytes */{len(payload)}"})
                self.record(key=key, list_request=list_request, query_present=query_present, range_value=range_value,
                            status=416, etag=etag, body_bytes=0, object_state=state_name)
                return
            start, end = byte_range
            ranged = bool(range_value)
            status = 206 if ranged else 200
            body = payload[start:end + 1] if self.command == "GET" else b""
            self.send_response(status)
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("ETag", etag)
            self.send_header("Content-Length", str((end - start + 1) if ranged else len(payload)))
            if ranged:
                self.send_header("Content-Range", f"bytes {start}-{end}/{len(payload)}")
            if transport == "s3":
                self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Connection", "close")
            self.end_headers()
            if body:
                self.wfile.write(body)
                self.wfile.flush()
            self.record(key=key, list_request=list_request, query_present=query_present, range_value=range_value,
                        status=status, etag=etag, body_bytes=len(body), object_state=state_name)

        def do_GET(self) -> None:
            self.serve_object()

        def do_HEAD(self) -> None:
            self.serve_object()

        def do_POST(self) -> None:
            if urlsplit(self.path).path != "/__control":
                self.send_empty(404)
                return
            if not hmac.compare_digest(self.headers.get("X-DuckOMO-Control", ""), control_token):
                self.send_empty(403)
                _append_audit(audit_path, audit_lock, {
                    "event": "control_rejected", "transport": transport, "status": 403,
                })
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if length <= 0 or length > 4096:
                    raise ValueError("invalid control request size")
                payload = json.loads(self.rfile.read(length))
                target_transport = payload.get("transport")
                key = safe_fixture_key(payload.get("object"))
                action = payload.get("action")
                if target_transport not in {"http", "https", "s3"} or action not in CONTROL_ACTIONS:
                    raise ValueError("invalid transport or action")
                updated = {"state": CONTROL_ACTIONS[action]}
                if action == "replace":
                    replacement = safe_fixture_key(payload.get("replacement"))
                    if replacement not in catalog:
                        raise ValueError("replacement fixture is not in the manifest")
                    updated["replacement"] = replacement
                if action == "fault":
                    fault = payload.get("fault")
                    if fault not in FAULTS:
                        raise ValueError("unsupported structured fault")
                    updated["fault"] = fault
                with state_lock:
                    state[(target_transport, key)] = updated
                self.send_empty(204)
                _append_audit(audit_path, audit_lock, {
                    "event": "control", "transport": target_transport, "object": key,
                    "state": updated["state"], "fault": updated.get("fault"),
                    "replacement": updated.get("replacement"), "status": 204,
                })
            except (ValueError, TypeError, json.JSONDecodeError):
                self.send_empty(400)
                _append_audit(audit_path, audit_lock, {
                    "event": "control_rejected", "transport": transport, "status": 400,
                })

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    return server


def _wait_readiness(process: subprocess.Popen, path: Path, timeout: float = 15.0) -> dict:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.is_file():
            return json.loads(path.read_text(encoding="utf-8"))
        if process.poll() is not None:
            raise RuntimeError(f"series fixture service exited during startup (exit {process.returncode})")
        time.sleep(0.05)
    process.terminate()
    raise RuntimeError("series fixture service did not become ready")


def _serve_main(args: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixtures", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--private-config", required=True, type=Path)
    parser.add_argument("--tls-cert", type=Path)
    parser.add_argument("--tls-key", type=Path)
    options = parser.parse_args(args)
    if bool(options.tls_cert) != bool(options.tls_key):
        parser.error("--tls-cert and --tls-key must be supplied together")
    catalog, manifest_sha = load_fixture_catalog(options.fixtures, options.manifest)
    private = json.loads(options.private_config.read_text(encoding="utf-8"))
    token = private["control_token"]
    bucket = private["s3_bucket"]
    output = options.output.resolve()
    logs = output / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    audit = logs / "requests.jsonl"
    audit.touch(exist_ok=True)
    states: dict = {}
    state_lock = threading.RLock()
    servers: list[tuple[str, ThreadingHTTPServer]] = [
        ("http", create_fixture_server(catalog, "http", audit, states, state_lock, token, bucket)),
        ("s3", create_fixture_server(catalog, "s3", audit, states, state_lock, token, bucket)),
    ]
    if options.tls_cert:
        https_server = create_fixture_server(catalog, "https", audit, states, state_lock, token, bucket)
        tls_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls_context.load_cert_chain(options.tls_cert, options.tls_key)
        https_server.socket = tls_context.wrap_socket(https_server.socket, server_side=True)
        servers.append(("https", https_server))
    threads = []
    for _, server in servers:
        thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.2}, daemon=True)
        thread.start()
        threads.append(thread)
    info = {"schema_version": 1, "fixture_manifest_sha256": manifest_sha,
            "object_count": len(catalog), "s3_bucket": bucket}
    for name, server in servers:
        if name == "s3":
            info["s3_endpoint"] = f"127.0.0.1:{server.server_address[1]}"
        else:
            info[name + "_base"] = f"http{'s' if name == 'https' else ''}://127.0.0.1:{server.server_address[1]}"
    _atomic_json(output / "service.ready.json", info)
    stopping = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: stopping.set())
    signal.signal(signal.SIGINT, lambda *_: stopping.set())
    stopping.wait()
    for _, server in servers:
        server.shutdown()
        server.server_close()
    for thread in threads:
        thread.join(timeout=2)
    return 0


def _validate_tls(cert: Path | None, key: Path | None, ca: Path | None) -> tuple[Path | None, Path | None, Path | None]:
    if bool(cert) != bool(key):
        raise ValueError("--tls-cert and --tls-key must be supplied together")
    if cert:
        cert = cert.resolve(strict=True)
        key = key.resolve(strict=True)
        if not cert.is_file() or not key.is_file():
            raise ValueError("TLS certificate and key must be regular files")
        ca = (ca or cert).resolve(strict=True)
        if not ca.is_file():
            raise ValueError("TLS CA certificate must be a regular file")
    elif ca:
        raise ValueError("--tls-ca requires --tls-cert and --tls-key")
    return cert, key, ca


def start_service(fixtures: Path, output: Path, tls_cert: Path | None = None,
                  tls_key: Path | None = None, tls_ca: Path | None = None,
                  httpfs: Path | None = None) -> dict:
    fixtures = fixtures.resolve(strict=True)
    output = output.resolve()
    catalog, manifest_sha = load_fixture_catalog(fixtures)
    tls_cert, tls_key, tls_ca = _validate_tls(tls_cert, tls_key, tls_ca)
    if httpfs:
        httpfs = httpfs.resolve(strict=True)
        if not httpfs.is_file():
            raise ValueError("--httpfs must identify an existing official HTTPFS extension")
    output.mkdir(parents=True, exist_ok=True)
    if (output / "service-pids.json").exists() or (output / "private").exists():
        raise RuntimeError("series fixture output already has private service state; stop it or choose a fresh directory")
    logs = output / "logs"
    private_dir = output / "private"
    logs.mkdir(parents=True, exist_ok=True)
    private_dir.mkdir(mode=0o700, exist_ok=True)
    os.chmod(private_dir, 0o700)
    private_path = private_dir / "config.json"
    private = {
        "schema_version": 1,
        "control_token": secrets.token_urlsafe(32),
        "s3_bucket": "duckomo-series",
        "s3_key_id": "duckomo-series-local-test",
        "s3_secret_key": secrets.token_hex(32),
    }
    _write_private_json(private_path, private)
    readiness = output / "service.ready.json"
    command = [sys.executable, str(Path(__file__).resolve()), "__serve",
               "--fixtures", str(fixtures), "--manifest", str(fixtures / "series-fixtures.json"),
               "--output", str(output), "--private-config", str(private_path)]
    if tls_cert and tls_key:
        command.extend(["--tls-cert", str(tls_cert), "--tls-key", str(tls_key)])
    log_stream = (logs / "service.log").open("w", encoding="utf-8")
    process = subprocess.Popen(command, stdout=log_stream, stderr=subprocess.STDOUT,
                               start_new_session=True, close_fds=True)
    _CHILDREN[process.pid] = process
    try:
        info = _wait_readiness(process, readiness)
    except Exception:
        try:
            process.terminate()
            process.wait(timeout=5)
        except (ProcessLookupError, subprocess.TimeoutExpired):
            pass
        _CHILDREN.pop(process.pid, None)
        log_stream.close()
        raise
    http_base = info["http_base"]
    s3_setup = output / "s3-setup.sql"
    s3_sql = ("CREATE OR REPLACE SECRET duckomo_series_remote (TYPE s3, KEY_ID " +
              _sql_quote(private["s3_key_id"]) + ", SECRET " + _sql_quote(private["s3_secret_key"]) +
              ", REGION 'us-east-1', ENDPOINT " + _sql_quote(info["s3_endpoint"]) +
              ", URL_STYLE 'path', USE_SSL 'false');\n")
    _REMOTE.write_private_text(s3_setup, s3_sql)
    env = {
        "DUCKOMO_HTTP_BASE": http_base,
        "DUCKOMO_S3_BASE": f"s3://{private['s3_bucket']}",
        "DUCKOMO_S3_SETUP": str(s3_setup),
        "DUCKOMO_SERIES_CONTROL_URL": http_base + "/__control",
        "DUCKOMO_SERIES_CONTROL_CONFIG": str(private_path),
        "DUCKOMO_SERIES_AUDIT": str(logs / "requests.jsonl"),
    }
    if info.get("https_base"):
        env["DUCKOMO_HTTPS_BASE"] = info["https_base"]
        env["DUCKOMO_HTTPS_CA_CERT"] = str(tls_ca)
    if httpfs:
        env["DUCKOMO_HTTPFS"] = str(httpfs)
    _REMOTE.write_private_text(output / "run.env",
                               "".join(f"export {name}={shlex.quote(value)}\n" for name, value in env.items()))
    _atomic_json(output / "run-manifest.json", {
        "schema_version": 1,
        "feature": "010-read-om-series",
        "status": "controlled_fixture_service_ready",
        "fixture_manifest_sha256": manifest_sha,
        "fixture_object_count": len(catalog),
        "fixture_objects": {key: {"id": fixture.identifier, "sha256": fixture.sha256,
                                   "size_bytes": fixture.size_bytes}
                            for key, fixture in sorted(catalog.items())},
        "http_base": http_base,
        "https_base": info.get("https_base"),
        "https_ca_sha256": sha256_file(tls_ca) if tls_ca else None,
        "s3_endpoint": info["s3_endpoint"],
        "s3_bucket": private["s3_bucket"],
        "s3_base": env["DUCKOMO_S3_BASE"],
        "s3_style": "path",
        "official_httpfs": env.get("DUCKOMO_HTTPFS"),
        "request_audit": str(logs / "requests.jsonl"),
        "credentials_in_run_manifest": False,
    })
    _atomic_json(output / "service-pids.json", {"services": [{"name": "series-remote", "pid": process.pid}]})
    log_stream.close()
    return info


def _sql_quote(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def stop_service(output: Path) -> None:
    output = output.resolve()
    pid_file = output / "service-pids.json"
    if not pid_file.is_file():
        return
    services = json.loads(pid_file.read_text(encoding="utf-8"))
    for entry in services.get("services", []):
        pid = int(entry["pid"])
        cmdline_path = Path(f"/proc/{pid}/cmdline")
        if not cmdline_path.exists():
            continue
        command = cmdline_path.read_bytes().replace(b"\x00", b" ").decode(errors="replace")
        if str(Path(__file__).resolve()) not in command:
            raise RuntimeError(f"refusing to stop PID {pid}: it is no longer this series fixture service")
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            _CHILDREN.pop(pid, None)
            continue
        child = _CHILDREN.pop(pid, None)
        if child:
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                raise RuntimeError(f"series fixture PID {pid} did not stop")
            continue
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                break
            time.sleep(0.05)
    pid_file.unlink(missing_ok=True)
    (output / "service.ready.json").unlink(missing_ok=True)


def control_service(output: Path, object_key: str, action: str,
                    transport: str = "http", replacement: str | None = None,
                    fault: str | None = None) -> None:
    output = output.resolve()
    key = safe_fixture_key(object_key)
    if transport not in {"http", "https", "s3"}:
        raise ValueError("transport must be http, https or s3")
    if action not in CONTROL_ACTIONS:
        raise ValueError("action must be restore, remove, replace or fault")
    if action == "replace":
        replacement = safe_fixture_key(replacement)
    if action == "fault" and fault not in FAULTS:
        raise ValueError("fault must be one of the supported structured faults")
    manifest = json.loads((output / "run-manifest.json").read_text(encoding="utf-8"))
    private = json.loads((output / "private/config.json").read_text(encoding="utf-8"))
    control_url = manifest["http_base"] + "/__control"
    parsed = urlsplit(control_url)
    payload = {"transport": transport, "object": key, "action": action}
    if replacement:
        payload["replacement"] = replacement
    if fault:
        payload["fault"] = fault
    connection = http.client.HTTPConnection(parsed.hostname, parsed.port, timeout=5)
    try:
        body = json.dumps(payload).encode()
        connection.request("POST", parsed.path, body=body,
                           headers={"Content-Type": "application/json",
                                    "X-DuckOMO-Control": private["control_token"]})
        response = connection.getresponse()
        response.read()
        if response.status != 204:
            raise RuntimeError(f"series fixture control failed with HTTP {response.status}")
    finally:
        connection.close()


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "__serve":
        return _serve_main(sys.argv[2:])
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    start = subparsers.add_parser("start", help="start loopback HTTP/S and path-style S3 fixture services")
    start.add_argument("--fixtures", type=Path, default=Path("build/series-fixtures"))
    start.add_argument("--output", type=Path, required=True)
    start.add_argument("--tls-cert", type=Path)
    start.add_argument("--tls-key", type=Path)
    start.add_argument("--tls-ca", type=Path)
    start.add_argument("--httpfs", type=Path)
    stop = subparsers.add_parser("stop", help="stop only this output directory's tracked loopback service")
    stop.add_argument("--output", type=Path, required=True)
    control = subparsers.add_parser("control", help="mutate one fixture after bind or inject a structured fault")
    control.add_argument("--output", type=Path, required=True)
    control.add_argument("--transport", choices=("http", "https", "s3"), default="http")
    control.add_argument("--object", required=True, help="manifest-relative path, e.g. data/synthetic/temperature/chunk_0000.om")
    control.add_argument("--action", choices=tuple(CONTROL_ACTIONS), required=True)
    control.add_argument("--replacement", help="manifest-relative replacement path")
    control.add_argument("--fault", choices=tuple(FAULTS), help="structured remote fault for --action fault")
    options = parser.parse_args()
    try:
        if options.command == "start":
            info = start_service(options.fixtures, options.output, options.tls_cert,
                                 options.tls_key, options.tls_ca, options.httpfs)
            print(f"Series fixtures ready: HTTP {info['http_base']}; "
                  f"HTTPS {info.get('https_base') or 'not configured'}; S3 {info['s3_endpoint']}")
        elif options.command == "stop":
            stop_service(options.output)
            print("Stopped tracked series fixture service; logs and run identity were retained.")
        else:
            control_service(options.output, options.object, options.action,
                            options.transport, options.replacement, options.fault)
            print(f"Applied {options.action} to {options.transport} fixture {safe_fixture_key(options.object)}")
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"series_remote: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
