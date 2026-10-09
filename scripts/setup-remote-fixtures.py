#!/usr/bin/env python3
"""Start reproducible DuckOMO remote fixtures and an S3 response audit proxy."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import hmac
import http.client
import json
import os
import queue
import select
import signal
import shlex
import ssl
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import quote, unquote, urlsplit, urlunsplit


def atomic_json(path: Path, value: object) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def append_jsonl(path: Path, value: dict) -> None:
    with path.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(value, sort_keys=True) + "\n")


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_private_text(path: Path, text: str) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_TRUNC
    flags |= getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags, 0o600)
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            descriptor = -1
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
    finally:
        if descriptor >= 0:
            os.close(descriptor)


def load_grid_samples(manifest_path: Path, repository_root: Path) -> tuple[list[dict], str]:
    manifest_path = manifest_path.resolve()
    payload = manifest_path.read_bytes()
    manifest = json.loads(payload)
    if manifest.get("schema_version") != 1:
        raise RuntimeError("grid manifest must use schema_version 1")
    public_samples = manifest.get("public_open_meteo_samples")
    if not isinstance(public_samples, dict) or not isinstance(public_samples.get("objects"), list):
        raise RuntimeError("grid manifest has no public_open_meteo_samples.objects array")

    samples = []
    seen_ids = set()
    for entry in public_samples["objects"]:
        if not isinstance(entry, dict) or entry.get("classification") != "real_public_open_meteo_om_v3":
            raise RuntimeError("grid manifest contains an object without real OM v3 classification")
        sample_id = entry.get("id")
        local_copy = entry.get("local_copy", {}).get("path")
        expected_hash = entry.get("sha256")
        expected_size = entry.get("size_bytes")
        if (not isinstance(sample_id, str) or not sample_id or
                any(not (char.isascii() and (char.isalnum() or char in "_.-")) for char in sample_id) or
                not isinstance(local_copy, str)):
            raise RuntimeError("grid manifest object has no id or local_copy.path")
        if sample_id in seen_ids:
            raise RuntimeError(f"grid manifest contains duplicate sample id {sample_id}")
        seen_ids.add(sample_id)
        relative_path = Path(local_copy)
        if relative_path.is_absolute() or ".." in relative_path.parts:
            raise RuntimeError(f"grid manifest contains an unsafe local path for {sample_id}")
        local_path = (repository_root / relative_path).resolve()
        if not local_path.is_relative_to(repository_root.resolve()):
            raise RuntimeError(f"grid manifest local path escapes the repository for {sample_id}")
        if not local_path.is_file():
            raise RuntimeError(f"pinned grid sample is missing locally: {local_path}; fetch it before fixture setup")
        if not isinstance(expected_hash, str) or len(expected_hash) != 64 or not isinstance(expected_size, int):
            raise RuntimeError(f"grid manifest object identity is incomplete for {sample_id}")
        if local_path.stat().st_size != expected_size or hash_file(local_path) != expected_hash:
            raise RuntimeError(f"pinned grid sample size/hash mismatch for {sample_id}")
        with local_path.open("rb") as stream:
            if stream.read(3) != b"OM\x03":
                raise RuntimeError(f"pinned grid sample is not OM v3: {sample_id}")
        samples.append({"id": sample_id, "path": local_path, "sha256": expected_hash,
                        "size_bytes": expected_size, "key": f"duckomo/grids/{sample_id}.om"})
    if not samples:
        raise RuntimeError("grid manifest public_open_meteo_samples.objects is empty")
    return samples, hashlib.sha256(payload).hexdigest()


def http_server_main(args: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixtures", required=True)
    parser.add_argument("--real-file")
    parser.add_argument("--grid-object", action="append", default=[])
    parser.add_argument("--readiness", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--tls-cert")
    parser.add_argument("--tls-key")
    options = parser.parse_args(args)
    if bool(options.tls_cert) != bool(options.tls_key):
        parser.error("--tls-cert and --tls-key must be supplied together")
    fixture_root = Path(options.fixtures).resolve()
    real_file = Path(options.real_file).resolve() if options.real_file else None
    log_path = Path(options.log)
    paths = {p.name: p.resolve() for p in fixture_root.glob("*.om") if p.is_file()}
    if real_file:
        paths["real.om"] = real_file
    for grid_object in options.grid_object:
        sample_id, separator, sample_path = grid_object.partition("=")
        if not separator or not sample_id or not sample_path:
            parser.error("--grid-object must use ID=PATH")
        paths[sample_id + ".om"] = Path(sample_path).resolve()
    etags = {name: '"' + hash_file(path) + '"' for name, path in paths.items()}
    counter = 0
    object_states: dict[str, tuple[str, str]] = {}
    state_lock = threading.Lock()

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_: object) -> None:
            return

        def respond(self, method: str) -> None:
            nonlocal counter
            counter += 1
            request_id = f"http-{counter}"
            parsed = urlsplit(self.path)
            name = Path(unquote(parsed.path)).name
            fault_values = dict(part.split("=", 1) for part in parsed.query.split("&") if "=" in part)
            fault = fault_values.get("fault", "")
            state_key = fault_values.get("state_key", "")
            with state_lock:
                state, replacement = object_states.get(state_key, ("normal", ""))
            source_name = replacement if state == "replacement" else name
            file_path = paths.get(source_name)
            status = 200
            body_sent = 0
            etag = ""
            range_header = self.headers.get("Range", "")
            audit_id = fault_values.get("audit") or None
            append_jsonl(log_path, {
                "request_id": request_id,
                "event": "started",
                "object": name,
                "audit": audit_id,
                "method": method,
                "range": range_header or None,
                "body_bytes": 0,
            })
            try:
                if state == "denied" or fault == "403":
                    status = 403
                    self.send_response(status)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                elif fault == "404" or file_path is None:
                    status = 404
                    self.send_response(status)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                elif fault == "no-head" and method == "HEAD":
                    status = 405
                    self.send_response(status)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                elif fault == "timeout" and method == "GET" and range_header != "bytes=0-0":
                    time.sleep(float(fault_values.get("seconds", "15")))
                    status = 504
                    self.send_response(status)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                elif fault == "redirect" and method == "GET" and range_header != "bytes=0-0":
                    status = 302
                    self.send_response(status)
                    self.send_header("Location", "https://example.invalid/moved.om")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                else:
                    size = file_path.stat().st_size
                    etag = etags[source_name]
                    if fault == "weak-etag":
                        etag = "W/" + etag
                    if fault == "no-etag":
                        etag = ""
                    begin, end = 0, size - 1
                    if range_header.startswith("bytes="):
                        try:
                            begin_text, end_text = range_header[6:].split("-", 1)
                            begin, end = int(begin_text), int(end_text)
                        except ValueError:
                            status = 416
                    is_probe = range_header == "bytes=0-0"
                    ignore_range = fault == "ignore-range" and not is_probe and method == "GET"
                    if fault == "wrong-range" and not is_probe and begin < end:
                        begin += 1
                    if fault == "change-version" and method == "GET" and not is_probe:
                        etag = '"changed-version"'
                    replace_body = fault == "replace" and method == "GET" and not is_probe
                    if replace_body:
                        etag = '"equal-length-replacement"'
                    if begin < 0 or end < begin or end >= size:
                        status = 416
                        self.send_response(status)
                        self.send_header("Content-Length", "0")
                        self.end_headers()
                    else:
                        status = 200 if ignore_range or not range_header else 206
                        actual_begin, actual_end = (0, size - 1) if ignore_range or not range_header else (begin, end)
                        body_length = actual_end - actual_begin + 1
                        self.send_response(status)
                        self.send_header("Accept-Ranges", "bytes")
                        self.send_header("Content-Length", str(size if status == 200 else body_length))
                        if status == 206:
                            self.send_header("Content-Range", f"bytes {actual_begin}-{actual_end}/{size}")
                        if etag:
                            self.send_header("ETag", etag)
                        self.send_header(
                            "Content-Encoding",
                            "gzip" if fault == "non-identity" and not is_probe else "identity",
                        )
                        self.end_headers()
                        if method != "HEAD":
                            with file_path.open("rb") as stream:
                                stream.seek(actual_begin)
                                remaining = size if status == 200 else body_length
                                if fault == "short" and not is_probe and remaining > 1:
                                    remaining -= 1
                                while remaining:
                                    block = stream.read(min(256 * 1024, remaining))
                                    if not block:
                                        break
                                    if replace_body and body_sent == 0:
                                        block = bytes([block[0] ^ 1]) + block[1:]
                                    self.wfile.write(block)
                                    body_sent += len(block)
                                    remaining -= len(block)
                                self.wfile.flush()
                self.close_connection = True
            except (BrokenPipeError, ConnectionResetError, TimeoutError, OSError):
                self.close_connection = True
            finally:
                append_jsonl(log_path, {
                    "request_id": request_id,
                    "event": "complete",
                    "object": name,
                    "audit": audit_id,
                    "method": method,
                    "range": range_header or None,
                    "status": status,
                    "etag": etag or None,
                    "body_bytes": body_sent,
                })

        def do_HEAD(self) -> None:
            self.respond("HEAD")

        def do_GET(self) -> None:
            self.respond("GET")

        def do_POST(self) -> None:
            if urlsplit(self.path).path != "/__control":
                self.send_error(404)
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if length <= 0 or length > 4096:
                    raise ValueError("invalid control request size")
                payload = json.loads(self.rfile.read(length))
                state_key = payload.get("state_key")
                state = payload.get("state")
                object_name = payload.get("object")
                replacement = payload.get("replacement", "")
                if not isinstance(state_key, str) or not state_key or len(state_key) > 128:
                    raise ValueError("invalid state key")
                if not isinstance(object_name, str) or object_name not in paths:
                    raise ValueError("unknown fixture object")
                if state not in {"normal", "replacement", "denied"}:
                    raise ValueError("unknown object state")
                if state == "replacement":
                    if not isinstance(replacement, str) or replacement not in paths:
                        raise ValueError("unknown replacement fixture")
                    if paths[replacement].stat().st_size != paths[object_name].stat().st_size:
                        raise ValueError("replacement fixture must have the same object length")
                with state_lock:
                    object_states[state_key] = (state, replacement if state == "replacement" else "")
                self.send_response(204)
                self.send_header("Content-Length", "0")
                self.end_headers()
            except (ValueError, TypeError, json.JSONDecodeError):
                self.send_error(400, "invalid local fixture control request")

    http_server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    http_server.daemon_threads = True
    readiness = {"host": "127.0.0.1", "port": http_server.server_address[1],
                 "http_port": http_server.server_address[1], "https_port": None}
    if options.tls_cert:
        https_server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        https_server.daemon_threads = True
        tls_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls_context.load_cert_chain(options.tls_cert, options.tls_key)
        https_server.socket = tls_context.wrap_socket(https_server.socket, server_side=True)
        readiness["https_port"] = https_server.server_address[1]
        http_thread = threading.Thread(target=http_server.serve_forever, kwargs={"poll_interval": 0.2}, daemon=True)
        http_thread.start()
        atomic_json(Path(options.readiness), readiness)
        https_server.serve_forever(poll_interval=0.2)
    else:
        atomic_json(Path(options.readiness), readiness)
        http_server.serve_forever(poll_interval=0.2)
    return 0


def s3_proxy_main(args: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream", required=True)
    parser.add_argument("--readiness", required=True)
    parser.add_argument("--log", required=True)
    options = parser.parse_args(args)
    upstream = urlsplit(options.upstream if "://" in options.upstream else "http://" + options.upstream)
    use_tls = upstream.scheme == "https"
    upstream_host = upstream.hostname or "127.0.0.1"
    upstream_port = upstream.port or (443 if use_tls else 80)
    prefix = upstream.path.rstrip("/")
    log_path = Path(options.log)
    counter = 0
    audit_lock = threading.Lock()

    def new_connection():
        connection_type = http.client.HTTPSConnection if use_tls else http.client.HTTPConnection
        connection_args = (upstream_host, upstream_port, 20)
        return (connection_type(*connection_args, context=ssl.create_default_context()) if use_tls
                else connection_type(*connection_args))

    # Downstream clients may close after every range. Reuse upstream sockets across
    # those clients to avoid exhausting the Docker forwarder's ephemeral ports.
    connections = queue.LifoQueue(maxsize=16)
    for _ in range(connections.maxsize):
        connections.put(new_connection())

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_: object) -> None:
            return

        def forward(self, method: str) -> None:
            nonlocal counter
            with audit_lock:
                counter += 1
                request_id = f"s3-{counter}"
            body_length = int(self.headers.get("Content-Length", "0"))
            request_body = self.rfile.read(body_length) if body_length else None
            target = prefix + self.path
            connection = connections.get()
            headers = {key: value for key, value in self.headers.items()
                       if key.lower() not in {"connection", "transfer-encoding"}}
            response_status = 502
            response_bytes = 0
            error_details = {}
            headers_sent = False
            reusable = False
            self.close_connection = True
            try:
                if connection.sock is not None:
                    # A consumed HTTP response leaves no readable data. EOF (or
                    # unsolicited data) means an idle socket must be discarded
                    # before sending the next independently signed request.
                    try:
                        stale = bool(select.select([connection.sock], [], [], 0)[0])
                        if use_tls:
                            stale = stale or bool(connection.sock.pending())
                    except (OSError, ValueError):
                        stale = True
                    if stale:
                        connection.close()
                connection.request(method, target, body=request_body, headers=headers)
                response = connection.getresponse()
                response_status = response.status
                response_headers = [(key, value) for key, value in response.getheaders()
                                    if key.lower() not in {"connection", "transfer-encoding", "keep-alive"}]
                self.send_response(response.status, response.reason)
                for key, value in response_headers:
                    self.send_header(key, value)
                self.send_header("Connection", "close")
                self.end_headers()
                headers_sent = True
                if method != "HEAD":
                    while True:
                        block = response.read(256 * 1024)
                        if not block:
                            if response.length:
                                raise http.client.IncompleteRead(b"", response.length)
                            break
                        self.wfile.write(block)
                        response_bytes += len(block)
                    self.wfile.flush()
                else:
                    response.read()
                reusable = not response.will_close
            except (BrokenPipeError, ConnectionResetError, TimeoutError, OSError, http.client.HTTPException) as error:
                error_details = {"error_type": type(error).__name__, "error_errno": getattr(error, "errno", None)}
                # After forwarding headers, close a partial response without
                # injecting another HTTP status line into its body.
                if not headers_sent and not self.wfile.closed:
                    try:
                        response_status = 502
                        error_body = b"upstream S3 request failed\n"
                        self.send_response(502)
                        self.send_header("Content-Type", "text/plain")
                        self.send_header("Content-Length", str(len(error_body)))
                        self.send_header("Connection", "close")
                        self.end_headers()
                        if method != "HEAD":
                            self.wfile.write(error_body)
                            response_bytes += len(error_body)
                            self.wfile.flush()
                    except OSError:
                        pass
            finally:
                if not reusable:
                    connection.close()
                    connection = new_connection()
                connections.put(connection)
                with audit_lock:
                    append_jsonl(log_path, {
                        "request_id": request_id,
                        "method": method,
                        "object": urlsplit(self.path).path,
                        "range": self.headers.get("Range"),
                        "status": response_status,
                        "body_bytes": response_bytes,
                        **error_details,
                    })

        def do_HEAD(self) -> None:
            self.forward("HEAD")

        def do_GET(self) -> None:
            self.forward("GET")

        def do_PUT(self) -> None:
            self.forward("PUT")

        def do_POST(self) -> None:
            self.forward("POST")

        def do_DELETE(self) -> None:
            self.forward("DELETE")

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    atomic_json(Path(options.readiness), {"host": "127.0.0.1", "port": server.server_address[1]})
    try:
        server.serve_forever(poll_interval=0.2)
    finally:
        server.server_close()
        while not connections.empty():
            connections.get_nowait().close()
    return 0


def sign_s3_put(endpoint: str, bucket: str, key: str, body: bytes | Path, access_key: str,
                secret_key: str, region: str, session_token: str = "") -> None:
    parsed = urlsplit(endpoint if "://" in endpoint else "http://" + endpoint)
    if parsed.scheme not in {"http", "https"} or parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise RuntimeError("S3 endpoint must be HTTP(S) without user info, query, or fragment")
    host = parsed.netloc
    canonical_path = quote((parsed.path.rstrip("/") + "/" + bucket + "/" + key).replace("//", "/"), safe="/-_.~")
    body_size = body.stat().st_size if isinstance(body, Path) else len(body)
    payload_hash = hash_file(body) if isinstance(body, Path) else hashlib.sha256(body).hexdigest()
    now = dt.datetime.now(dt.timezone.utc)
    amz_date = now.strftime("%Y%m%dT%H%M%SZ")
    date_stamp = now.strftime("%Y%m%d")
    headers = {
        "content-type": "application/octet-stream",
        "content-length": str(body_size),
        "host": host,
        "x-amz-content-sha256": payload_hash,
        "x-amz-date": amz_date,
    }
    if session_token:
        headers["x-amz-security-token"] = session_token
    signed_headers = ";".join(sorted(headers))
    canonical_headers = "".join(f"{name}:{headers[name].strip()}\n" for name in sorted(headers))
    canonical_request = "\n".join(("PUT", canonical_path, "", canonical_headers, signed_headers, payload_hash))
    scope = f"{date_stamp}/{region}/s3/aws4_request"
    string_to_sign = "\n".join(("AWS4-HMAC-SHA256", amz_date, scope,
                                 hashlib.sha256(canonical_request.encode()).hexdigest()))

    def mac(key_bytes: bytes, value: str) -> bytes:
        return hmac.new(key_bytes, value.encode(), hashlib.sha256).digest()

    signing_key = mac(mac(mac(mac(("AWS4" + secret_key).encode(), date_stamp), region), "s3"), "aws4_request")
    signature = hmac.new(signing_key, string_to_sign.encode(), hashlib.sha256).hexdigest()
    authorization = ("AWS4-HMAC-SHA256 Credential=" + access_key + "/" + scope +
                     ", SignedHeaders=" + signed_headers + ", Signature=" + signature)
    connection_type = http.client.HTTPSConnection if parsed.scheme == "https" else http.client.HTTPConnection
    connection_args = (parsed.hostname or "127.0.0.1", parsed.port or (443 if parsed.scheme == "https" else 80), 30)
    if parsed.scheme == "https":
        connection = connection_type(*connection_args, context=ssl.create_default_context())
    else:
        connection = connection_type(*connection_args)
    try:
        connection.putrequest("PUT", canonical_path, skip_host=True)
        for name, value in headers.items():
            connection.putheader(name, value)
        connection.putheader("Authorization", authorization)
        connection.endheaders()
        if isinstance(body, Path):
            with body.open("rb") as stream:
                while block := stream.read(1024 * 1024):
                    connection.send(block)
        else:
            view = memoryview(body)
            for offset in range(0, len(view), 1024 * 1024):
                connection.send(view[offset:offset + 1024 * 1024])
        response = connection.getresponse()
        response.read()
        if response.status not in (200, 201, 204):
            raise RuntimeError(f"S3 fixture upload failed with HTTP {response.status}")
    except Exception as error:
        raise RuntimeError(f"S3 fixture upload failed for key {key}: {type(error).__name__}") from None
    finally:
        connection.close()


def wait_ready(process: subprocess.Popen, readiness: Path, timeout: float = 15.0) -> dict:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if readiness.exists():
            return json.loads(readiness.read_text(encoding="utf-8"))
        if process.poll() is not None:
            raise RuntimeError(f"fixture service exited during startup (exit {process.returncode})")
        time.sleep(0.05)
    process.terminate()
    raise RuntimeError("fixture service did not become ready")


def stop_services(output: Path) -> None:
    pid_file = output / "service-pids.json"
    if not pid_file.exists():
        print("No tracked remote fixture services are running.")
        return
    services = json.loads(pid_file.read_text(encoding="utf-8"))
    for entry in services.get("services", []):
        pid = int(entry["pid"])
        proc_cmdline = Path(f"/proc/{pid}/cmdline")
        if not proc_cmdline.exists():
            continue
        command = proc_cmdline.read_bytes().replace(b"\x00", b" ").decode(errors="replace")
        if str(Path(__file__).resolve()) not in command:
            print(f"Skipping PID {pid}: it is no longer this fixture runner.", file=sys.stderr)
            continue
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    for entry in services.get("services", []):
        try:
            os.kill(int(entry["pid"]), 0)
        except ProcessLookupError:
            continue
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                os.kill(int(entry["pid"]), 0)
            except ProcessLookupError:
                break
            time.sleep(0.05)
    pid_file.unlink(missing_ok=True)
    for readiness in output.glob("*.ready.json"):
        readiness.unlink(missing_ok=True)
    print("Stopped only tracked loopback fixture services; evidence and S3 objects were retained.")


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "__http_server":
        return http_server_main(sys.argv[2:])
    if len(sys.argv) > 1 and sys.argv[1] == "__s3_proxy":
        return s3_proxy_main(sys.argv[2:])

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, default=Path("test/data"))
    parser.add_argument("--real-file", type=Path)
    parser.add_argument("--real-manifest", type=Path)
    parser.add_argument("--grid-manifest", type=Path)
    parser.add_argument("--s3-endpoint")
    parser.add_argument("--s3-bucket")
    parser.add_argument("--authorized-s3-bucket", default=os.getenv("DUCKOMO_AUTHORIZED_S3_BUCKET"),
                        help="must match the explicitly authorized test bucket (or DUCKOMO_AUTHORIZED_S3_BUCKET)")
    parser.add_argument("--region", default=os.getenv("AWS_REGION", os.getenv("AWS_DEFAULT_REGION", "us-east-1")))
    parser.add_argument("--s3-service-version", default=os.getenv("DUCKOMO_S3_SERVICE_VERSION"))
    parser.add_argument("--s3-service-digest", default=os.getenv("DUCKOMO_S3_SERVICE_DIGEST"))
    parser.add_argument("--http-tls-cert", type=Path)
    parser.add_argument("--http-tls-key", type=Path)
    parser.add_argument("--http-tls-ca", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--httpfs", type=Path)
    parser.add_argument("--stop", action="store_true")
    options = parser.parse_args()
    output = options.output.resolve()
    if options.stop:
        stop_services(output)
        return 0
    if not options.real_file and not options.grid_manifest:
        parser.error("--real-file or --grid-manifest is required unless --stop is used")
    if not options.s3_endpoint or not options.s3_bucket:
        parser.error("--s3-endpoint and --s3-bucket are required unless --stop is used")
    if not options.authorized_s3_bucket:
        parser.error("--authorized-s3-bucket or DUCKOMO_AUTHORIZED_S3_BUCKET must identify the authorized test bucket")
    if options.s3_bucket != options.authorized_s3_bucket:
        parser.error("--s3-bucket must exactly match the explicitly authorized test bucket")
    if (len(options.s3_bucket) < 3 or len(options.s3_bucket) > 63 or not options.s3_bucket.isascii() or
            not options.s3_bucket[0].isalnum() or not options.s3_bucket[-1].isalnum() or
            any(not (char.islower() or char.isdigit() or char in ".-") for char in options.s3_bucket)):
        parser.error("--s3-bucket must be a valid lowercase S3 bucket name without a path")
    s3_endpoint_url = urlsplit(options.s3_endpoint if "://" in options.s3_endpoint else "http://" + options.s3_endpoint)
    if (s3_endpoint_url.scheme not in {"http", "https"} or not s3_endpoint_url.hostname or
            s3_endpoint_url.username or s3_endpoint_url.password or s3_endpoint_url.query or s3_endpoint_url.fragment):
        parser.error("--s3-endpoint must use HTTP(S) without user info, query, or fragment")
    fixtures = options.fixtures.resolve()
    real_file = options.real_file.resolve() if options.real_file else None
    real_manifest = None
    real_manifest_sha256 = None
    if real_file:
        real_manifest = (options.real_manifest or fixtures / "domain-manifest.json").resolve()
        if not real_file.is_file() or not real_manifest.is_file():
            parser.error("fixed real OM file and domain manifest must both exist")
        manifest = json.loads(real_manifest.read_text(encoding="utf-8"))
        fixed = manifest.get("sample", {})
        expected_hash = fixed.get("sha256")
        expected_size = fixed.get("bytes")
        if not expected_hash or not expected_size:
            parser.error("real manifest does not contain the fixed sample identity")
        if hash_file(real_file) != expected_hash or real_file.stat().st_size != expected_size:
            parser.error("real OM SHA-256 differs from the pinned domain manifest")
        real_manifest_sha256 = hash_file(real_manifest)
    elif options.real_manifest:
        parser.error("--real-manifest requires --real-file")
    if not fixtures.is_dir():
        parser.error(f"fixture directory does not exist: {fixtures}")

    grid_samples = []
    grid_manifest_sha256 = None
    if options.grid_manifest:
        try:
            grid_samples, grid_manifest_sha256 = load_grid_samples(
                options.grid_manifest, Path(__file__).resolve().parent.parent)
        except (OSError, ValueError, RuntimeError) as error:
            parser.error(str(error))
        if not options.http_tls_cert or not options.http_tls_key:
            parser.error("--grid-manifest requires --http-tls-cert and --http-tls-key for controlled HTTPS")
    if not options.s3_service_digest:
        parser.error("--s3-service-digest or DUCKOMO_S3_SERVICE_DIGEST must identify the fixed S3 service build")
    service_digest = options.s3_service_digest.removeprefix("sha256:")
    if len(service_digest) != 64 or any(char not in "0123456789abcdef" for char in service_digest):
        parser.error("--s3-service-digest must be a lowercase SHA-256 digest")
    if bool(options.http_tls_cert) != bool(options.http_tls_key):
        parser.error("--http-tls-cert and --http-tls-key must be supplied together")
    if options.http_tls_cert:
        options.http_tls_cert = options.http_tls_cert.resolve()
        options.http_tls_key = options.http_tls_key.resolve()
        if not options.http_tls_cert.is_file() or not options.http_tls_key.is_file():
            parser.error("HTTP TLS certificate and private key files must exist")
    if options.http_tls_ca:
        options.http_tls_ca = options.http_tls_ca.resolve()
        if not options.http_tls_ca.is_file():
            parser.error("HTTP TLS CA certificate file must exist")
    elif options.http_tls_cert:
        # Self-signed fixture certificates can be used directly as the trust
        # anchor. Supply --http-tls-ca when the trust root is a separate file.
        options.http_tls_ca = options.http_tls_cert

    access_key = os.getenv("AWS_ACCESS_KEY_ID") or os.getenv("AWS_ACCESS_KEY")
    secret_key = os.getenv("AWS_SECRET_ACCESS_KEY") or os.getenv("AWS_SECRET_KEY")
    session_token = os.getenv("AWS_SESSION_TOKEN", "")
    if not access_key or not secret_key:
        parser.error("AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY must be set in the environment")
    if not options.s3_service_version:
        parser.error("--s3-service-version or DUCKOMO_S3_SERVICE_VERSION must identify the fixed S3 service build")

    output.mkdir(parents=True, exist_ok=True)
    logs = output / "logs"
    logs.mkdir(exist_ok=True)
    for stale in (output / "service-pids.json", output / "http.ready.json", output / "s3-proxy.ready.json"):
        if stale.exists():
            parser.error(f"tracked service state already exists ({stale}); run --stop or use a new output directory")

    process_entries = []
    log_streams = []
    try:
        http_ready = output / "http.ready.json"
        http_log = logs / "http.jsonl"
        http_stream = http_log.open("w", encoding="utf-8")
        log_streams.append(http_stream)
        http_command = [sys.executable, str(Path(__file__).resolve()), "__http_server",
                        "--fixtures", str(fixtures), "--readiness", str(http_ready), "--log", str(http_log)]
        if real_file:
            http_command.extend(["--real-file", str(real_file)])
        for sample in grid_samples:
            http_command.extend(["--grid-object", f"{sample['id']}={sample['path']}"])
        if options.http_tls_cert:
            http_command.extend(["--tls-cert", str(options.http_tls_cert), "--tls-key", str(options.http_tls_key)])
        http_process = subprocess.Popen(http_command,
                                        stdout=http_stream, stderr=subprocess.STDOUT, start_new_session=True)
        process_entries.append({"name": "http", "pid": http_process.pid})
        http_info = wait_ready(http_process, http_ready)
        http_base = f"http://127.0.0.1:{http_info['port']}"
        https_base = f"https://127.0.0.1:{http_info['https_port']}" if http_info.get("https_port") else None

        s3_ready = output / "s3-proxy.ready.json"
        s3_log = logs / "s3.jsonl"
        s3_stream = s3_log.open("w", encoding="utf-8")
        log_streams.append(s3_stream)
        s3_process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), "__s3_proxy",
                                       "--upstream", options.s3_endpoint, "--readiness", str(s3_ready),
                                       "--log", str(s3_log)], stdout=s3_stream, stderr=subprocess.STDOUT,
                                      start_new_session=True)
        process_entries.append({"name": "s3-audit-proxy", "pid": s3_process.pid})
        s3_info = wait_ready(s3_process, s3_ready)
        s3_endpoint = f"127.0.0.1:{s3_info['port']}"

        fixture_paths = sorted(path for path in fixtures.glob("*.om") if path.is_file())
        if real_file and real_file not in fixture_paths:
            fixture_paths.append(real_file)
        upload_specs = []
        for path in fixture_paths:
            key_name = "real.om" if path == real_file else path.name
            upload_specs.append({"key": "duckomo/" + key_name, "path": path,
                                 "sha256": hash_file(path), "size_bytes": path.stat().st_size})
        for sample in grid_samples:
            upload_specs.append({"key": sample["key"], "path": sample["path"],
                                 "sha256": sample["sha256"], "size_bytes": sample["size_bytes"],
                                 "sample_id": sample["id"]})
        upload_keys = set()
        for upload in upload_specs:
            if upload["key"] in upload_keys:
                raise RuntimeError(f"duplicate S3 fixture object key: {upload['key']}")
            upload_keys.add(upload["key"])
            sign_s3_put(options.s3_endpoint, options.s3_bucket, upload["key"], upload["path"],
                        access_key, secret_key, options.region, session_token)

        def sql_quote(value: str) -> str:
            return "'" + value.replace("'", "''") + "'"

        sql_path = output / "s3-setup.sql"
        endpoint_scheme = urlsplit(options.s3_endpoint if "://" in options.s3_endpoint else "http://" + options.s3_endpoint).scheme
        # DuckDB talks HTTP to the loopback audit proxy. TLS, when used by the
        # fixture service, is established by the proxy on its upstream leg.
        use_ssl = "false"
        secret_sql = ("CREATE OR REPLACE SECRET duckomo_remote_test (TYPE s3, KEY_ID " + sql_quote(access_key) +
                      ", SECRET " + sql_quote(secret_key) + ", REGION " + sql_quote(options.region) +
                      ", ENDPOINT " + sql_quote(s3_endpoint) + ", URL_STYLE 'path', USE_SSL '" + use_ssl + "');\n")
        if session_token:
            secret_sql = secret_sql.rstrip()[:-2] + ", SESSION_TOKEN " + sql_quote(session_token) + ");\n"
        write_private_text(sql_path, secret_sql)

        if options.httpfs:
            httpfs = options.httpfs.resolve()
        else:
            candidates = [Path("build/official-matrix/v1.5.4/official/httpfs.duckdb_extension")]
            httpfs = next((candidate.resolve() for candidate in candidates if candidate.is_file()), Path())
        if not httpfs or not httpfs.is_file():
            raise RuntimeError("official httpfs artifact not found; pass --httpfs or fetch-runtime first")

        env = {
            "DUCKOMO_HTTP_BASE": http_base,
            "DUCKOMO_S3_BASE": f"s3://{options.s3_bucket}/duckomo",
            "DUCKOMO_S3_SETUP": str(sql_path),
            "DUCKOMO_SERVER_LOG": str(logs),
            "DUCKOMO_HTTPFS": str(httpfs),
        }
        if https_base:
            env["DUCKOMO_HTTPS_BASE"] = https_base
            env["DUCKOMO_HTTPS_CA_CERT"] = str(options.http_tls_ca)
        if real_file:
            env["DUCKOMO_REAL_FILE"] = str(real_file)
            env["DUCKOMO_REAL_MANIFEST"] = str(real_manifest)
        if options.grid_manifest:
            env["DUCKOMO_GRID_MANIFEST"] = str(options.grid_manifest.resolve())
        write_private_text(output / "run.env",
                           "".join(f"export {key}={shlex.quote(value)}\n" for key, value in env.items()))
        atomic_json(output / "run-manifest.json", {
            "schema_version": 1,
            "http_bind": "127.0.0.1",
            "http_base": http_base,
            "http_control": http_base + "/__control",
            "https_base": https_base,
            "http_tls_certificate_sha256": hash_file(options.http_tls_cert) if options.http_tls_cert else None,
            "http_tls_ca_sha256": hash_file(options.http_tls_ca) if options.http_tls_ca else None,
            "s3_audit_bind": "127.0.0.1",
            "s3_audit_endpoint": s3_endpoint,
            "s3_upstream_scheme": endpoint_scheme,
            "s3_upstream_endpoint": urlunsplit((s3_endpoint_url.scheme, s3_endpoint_url.netloc,
                                                 s3_endpoint_url.path.rstrip("/"), "", "")),
            "s3_bucket": options.s3_bucket,
            "authorized_s3_bucket_match": True,
            "s3_region": options.region,
            "s3_service_version": options.s3_service_version,
            "s3_service_digest": "sha256:" + service_digest,
            "s3_service_identity_source": "caller_supplied",
            "s3_service_identity_independently_verified": False,
            "s3_client_tls_to_proxy": False,
            "fixtures": {upload["key"]: {"sha256": upload["sha256"], "size_bytes": upload["size_bytes"]}
                         for upload in upload_specs},
            "grid_manifest_sha256": grid_manifest_sha256,
            "public_grid_samples": {sample["id"]: {"key": sample["key"], "sha256": sample["sha256"],
                                                      "size_bytes": sample["size_bytes"]}
                                    for sample in grid_samples},
            "http_grid_paths": {sample["id"]: f"/{sample['id']}.om" for sample in grid_samples},
            "real_manifest_sha256": real_manifest_sha256,
            "official_httpfs": str(httpfs),
            "log_directory": str(logs),
            "credentials_written_to_evidence": False,
        })
        atomic_json(output / "service-pids.json", {"services": process_entries})
        print(f"Remote fixtures are ready. Source: {output / 'run.env'}")
        print(f"Loopback HTTP: {http_base}; HTTPS: {https_base or 'not configured'}; "
              f"S3: s3://{options.s3_bucket}/duckomo")
        return 0
    except Exception as error:
        for entry in process_entries:
            try:
                os.kill(int(entry["pid"]), signal.SIGTERM)
            except ProcessLookupError:
                pass
        for stream in log_streams:
            stream.close()
        raise SystemExit(f"setup-remote-fixtures: {error}")


if __name__ == "__main__":
    raise SystemExit(main())
