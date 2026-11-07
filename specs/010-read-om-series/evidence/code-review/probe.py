#!/usr/bin/env python3
"""Read-only Feature 010 review probes against retained final artifacts."""
import hashlib
import json
import platform
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
OUT = Path(__file__).resolve().parent
EVIDENCE = ROOT / 'specs/010-read-om-series/evidence/run-20260803-linux-arm64'

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def quote(value):
    return "'" + str(value).replace("'", "''") + "'"

missing = ROOT / 'build/series-fixtures/review-prefix-that-does-not-exist'
assert not missing.exists()
present = ROOT / 'build/series-fixtures/data/synthetic/temperature'
grid = "grid := {'nx':3,'ny':2,'lat0':10.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'lon_fastest'}, spatial_axes := ['point']"
window = "start := TIMESTAMP '1970-01-01', \"end\" := TIMESTAMP '1970-01-01 01:00:00'"
slotless = "start := TIMESTAMP '1970-01-01 03:00:00.000001', \"end\" := TIMESTAMP '1970-01-01 03:59:59.999999'"
dims = "dimensions := ['lead','point','time']"

def query(prefix, spatial=grid, temporal=window, dimensions=dims, extra=''):
    return f"SELECT count(*) FROM read_om_series({quote(prefix)}, chunk_time_length := 4, time_step := INTERVAL '1 hour', {dimensions}, {spatial}, {temporal}{extra});"

cases = [
    ('strict_missing_slotless', query(missing, temporal=slotless, extra=', strict := true'), 'error: strict validates candidate presence even without selected slots'),
    ('strict_missing_with_slots_control', query(missing, extra=', strict := true'), 'error: initial missing child'),
    ('unknown_domain_all_missing', query(missing, spatial="domain := 'review_nonexistent_domain'"), 'error: invalid spatial declaration'),
    ('invalid_grid_all_missing', query(missing, spatial="grid := 42, spatial_axes := ['point']"), 'error: grid must be a STRUCT'),
    ('invalid_grid_present', query(present, spatial="grid := 42, spatial_axes := ['point']"), 'error before any object access'),
    ('case_collision_all_missing', query(missing, dimensions="dimensions := map(['/Foo','/foo'], [['lead','point','time'],['lead','point','time']])"), 'error: case-insensitive column collision'),
]
results = {'platform': platform.machine(), 'purpose': 'Independent review probes; not acceptance gate upgrades', 'pairs': []}
for pair in ['v1.5.4', 'v1.5.5', 'v1.5.6', 'v2.0.0']:
    manifest = json.loads((EVIDENCE / f't040/local-final-{pair}/manifest.json').read_text())
    prerequisites = {item['name']: item for item in manifest['prerequisites']}
    cli, extension = (prerequisites[name] for name in ['duckdb', 'extension'])
    identities = []
    for artifact in [cli, extension]:
        actual = sha(artifact['path'])
        assert actual == artifact['sha256'], artifact['path']
        identities.append({'path': artifact['path'], 'sha256': actual})
    record = {'pair': pair, 'verified_artifacts': identities, 'cases': []}
    for name, sql, expected in cases:
        full_sql = f"LOAD {quote(extension['path'])};\n{sql}\n"
        sql_path = OUT / f'{pair}-{name}.sql'
        sql_path.write_text(full_sql)
        command = [cli['path'], '-no-init', '-unsigned', '-bail', '-json', ':memory:']
        trace_path = OUT / f'{pair}-{name}.strace'
        if name == 'invalid_grid_present':
            command = ['strace', '-f', '-e', 'trace=openat', '-o', str(trace_path), *command]
        completed = subprocess.run(command, input=full_sql, text=True, capture_output=True, cwd=ROOT, timeout=30)
        item = {'case': name, 'expected': expected, 'sql_file': sql_path.name, 'command': command,
                'exit_code': completed.returncode, 'stdout': completed.stdout, 'stderr': completed.stderr}
        if trace_path.exists():
            item['fixture_open_events'] = [line for line in trace_path.read_text().splitlines() if 'series-fixtures/' in line]
        record['cases'].append(item)
    results['pairs'].append(record)
(OUT / 'probe-results.json').write_text(json.dumps(results, indent=2) + '\n')
for pair in results['pairs']:
    print(pair['pair'])
    for case in pair['cases']:
        print(' ', case['case'], 'exit=', case['exit_code'], 'stdout=', case['stdout'].strip(), 'fixture_opens=', len(case.get('fixture_open_events', [])))
