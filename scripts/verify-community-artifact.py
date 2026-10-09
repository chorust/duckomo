#!/usr/bin/env python3
"""Check a distribution binary in the matching official version/platform runtime."""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import subprocess
import threading


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def literal(value):
    return "'" + str(value).replace("'", "''") + "'"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--version', required=True, choices=['v1.5.4', 'v1.5.5', 'v1.5.6'])
    parser.add_argument('--platform', default='linux_arm64',
                        choices=['linux_amd64', 'linux_arm64', 'osx_amd64', 'osx_arm64'])
    parser.add_argument('--runtime-manifest', type=Path,
                        help='multi-platform release runtime manifest; omitted uses historical ARM64 pins')
    for name in ('duckdb', 'httpfs', 'extension', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.root = args.root.resolve()
    for name in ('duckdb', 'httpfs', 'extension', 'output'):
        setattr(args, name, getattr(args, name).resolve())
    report = dict(version=args.version, platform=args.platform, status='fail',
                  checks=[], signed_community_install='not-run (local/CI verification uses -unsigned)')
    server = None
    try:
        if args.runtime_manifest:
            from release_tools import load_runtimes, find_runtime
            runtime = find_runtime(load_runtimes(args.runtime_manifest), args.version, args.platform)
        else:
            if args.platform != 'linux_arm64':
                raise ValueError('non-ARM64 validation requires --runtime-manifest')
            matrix = json.loads((args.root / 'test/data/grids/version-matrix.json').read_text())
            runtime = next(p['official_runtime'] for p in matrix['pairs'] if p['pair_id'] == args.version)
        assert sha(args.duckdb) == runtime['cli_sha256'], 'Official CLI hash mismatch'
        assert sha(args.httpfs) == runtime['httpfs_sha256'], 'Official HTTPFS hash mismatch'
        reported = subprocess.check_output([str(args.duckdb), '-version'], text=True).strip()
        assert reported.startswith(args.version + ' '), 'Engine version mismatch'
        platform = subprocess.check_output(
            [str(args.duckdb), '-batch', '-noheader', '-csv', ':memory:', '-c', 'PRAGMA platform;'],
            text=True, timeout=30).strip()
        assert platform == args.platform, 'Engine platform mismatch'
        report['runtime_version'] = reported
        report['artifact_hashes'] = {name: sha(getattr(args, name)) for name in ('duckdb', 'httpfs', 'extension')}
        fixtures = json.loads((args.root / 'test/data/manifest.json').read_text())['fixtures']
        raw = args.root / 'test/data/raw.om'
        fixture = args.root / 'test/data/dimensions_perf.om'
        for path in (raw, fixture):
            expected = next(f['sha256'] for f in fixtures if f['path'] == path.name)
            assert sha(path) == expected, 'Fixture hash mismatch: ' + path.name
        payload = fixture.read_bytes()
        events = []

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *unused):
                pass

            def do_HEAD(self):
                self.respond(False)

            def do_GET(self):
                self.respond(True)

            def respond(self, body):
                if self.path != '/dimensions_perf.om':
                    self.send_error(404)
                    return
                start, end = 0, len(payload) - 1
                requested = self.headers.get('Range')
                if requested:
                    match = re.fullmatch(r'bytes=(\d+)-(\d*)', requested)
                    if not match:
                        self.send_error(416)
                        return
                    start = int(match[1])
                    end = min(int(match[2]) if match[2] else end, end)
                    if start > end:
                        self.send_error(416)
                        return
                self.send_response(206 if requested else 200)
                self.send_header('Content-Length', str(end - start + 1))
                self.send_header('Accept-Ranges', 'bytes')
                self.send_header('ETag', '"community-fixture-v1"')
                if requested:
                    self.send_header('Content-Range', f'bytes {start}-{end}/{len(payload)}')
                self.end_headers()
                if body:
                    self.wfile.write(payload[start:end + 1])
                    events.append(dict(range=requested, sent_bytes=end - start + 1))

        server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        env = {k: v for k, v in os.environ.items() if k.lower() not in ('http_proxy', 'https_proxy', 'all_proxy')}
        setup = f'LOAD {literal(args.httpfs)}; LOAD {literal(args.extension)}; SET threads=1;'

        def query(sql):
            result = subprocess.run([str(args.duckdb), '-unsigned', '-batch', '-bail', '-csv', '-noheader', ':memory:'],
                                    input=setup + sql, text=True, capture_output=True, env=env, timeout=120)
            if result.returncode:
                raise RuntimeError('Official runtime query failed: ' + result.stderr[-2000:])
            return result.stdout.strip()

        assert query(f'SELECT count(*), sum(value)::BIGINT FROM read_om({literal(raw)});') == '6,15'
        report['checks'].append('local raw fixture: 6 rows, sum 15')
        axes = "dimensions := map(['value'], [['time','member']]), axes := {'time': {'axis':'time','start':TIMESTAMP '2026-01-01','step':INTERVAL '1 hour'},'member': {'axis':'member','start':0,'step':1}}"

        def selected(uri):
            return query(f"SELECT value FROM read_om({literal(uri)}, {axes}) WHERE valid_time=TIMESTAMP '2026-01-01' AND member BETWEEN 100 AND 103 ORDER BY value;")

        local = selected(fixture)
        remote = selected(f'http://127.0.0.1:{server.server_port}/dimensions_perf.om')
        assert len(local.splitlines()) == 4 and remote == local, 'HTTP/local selected-result mismatch'
        sent = sum(e['sent_bytes'] for e in events)
        assert events and all(e['range'] for e in events) and 0 < sent < len(payload), 'Selective HTTP range reads missing'
        report['checks'].append('official HTTPFS: selected HTTP/local values match (4 rows)')
        report['http_audit'] = dict(scope='service sent bytes', sent_bytes=sent, object_bytes=len(payload), requests=len(events))
        report['status'] = 'pass'
    except Exception as error:
        report['error'] = str(error)
    finally:
        if server:
            server.shutdown()
            server.server_close()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f"{args.version} DuckOMO candidate: {report['status']} ({args.output})")
    return 0 if report['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
