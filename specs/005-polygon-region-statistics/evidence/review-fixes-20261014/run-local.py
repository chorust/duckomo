#!/usr/bin/env python3
"""Run the authorized three-version local completion sequentially with durable logs."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import time

ROOT = Path('/home/blizhan/repo/github/duckomo')
OUT = ROOT / 'build/evidence/regions/005-review-fixes-20261014'
VERSIONS = ('v1.5.4', 'v1.5.5', 'v1.5.6')
MATRIX = ROOT / 'test/data/grids/version-matrix.json'
STATUS = OUT / 'local-status.json'
state = {'status': 'running', 'stage': 'starting', 'versions': {}, 'commands': []}


def save():
    tmp = STATUS.with_suffix('.tmp')
    tmp.write_text(json.dumps(state, indent=2) + '\n')
    tmp.replace(STATUS)


def run(version, stage, argv, env=None, timeout=14400):
    state['stage'] = version + ' ' + stage
    save()
    log = OUT / version / (stage + '.log')
    log.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    with log.open('w') as stream:
        result = subprocess.run(argv, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    record = {'version': version, 'stage': stage, 'argv': argv, 'exit_code': result.returncode,
              'elapsed_seconds': round(time.monotonic() - started, 3),
              'log': str(log.relative_to(OUT)), 'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest()}
    state['commands'].append(record)
    save()
    if result.returncode:
        raise RuntimeError(f'{version} {stage} failed: exit {result.returncode}; see {log}')


try:
    for version in VERSIONS:
        run(version, 'build', ['bash', 'scripts/build-version.sh', version, '--region-sanitizers'],
            dict(os.environ, DUCKOMO_BUILD_JOBS='16', PYTHONDONTWRITEBYTECODE='1'))
        build_manifest = json.loads((ROOT / 'build/official-matrix' / version / 'build-manifest.json').read_text())
        state['versions'][version] = {'build_id': build_manifest['build_id'],
                                     'source_sha256': build_manifest['inputs']['extension_source_sha256'],
                                     'artifact_hashes': build_manifest['artifact_hashes'], 'local_status': 'running'}
        save()
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
                   DUCKOMO_DOMAIN_FILE=str(ROOT / 'build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om'),
                   DUCKOMO_DOMAIN_REFERENCE=str(ROOT / 'build/evidence/spatial/reference'),
                   DUCKOMO_REGION_EVIDENCE_DIR=str(OUT / version / 'P0-P4'))
        run(version, 'local-validation', ['bash', 'scripts/validate.sh', '--matrix', str(MATRIX), '--pair', version,
                                        '--output-root', str(ROOT / 'build/official-matrix'), '--local-only',
                                        '--regions', '--region-gates', 'P0,P1,P2,P3,P4'], env)
        gates = json.loads((OUT / version / 'P0-P4/manifest.json').read_text())
        if gates['status'] != 'pass' or {g['id'] for g in gates['gates'] if g['status'] == 'pass'} != {'P0','P1','P2','P3','P4'}:
            raise RuntimeError(f'{version} local gates incomplete')
        state['versions'][version]['local_status'] = 'pass'
        save()
    state['status'] = 'pass'
    state['stage'] = 'all local versions completed'
except Exception as error:
    state['status'] = 'fail'
    state['error'] = str(error)
finally:
    save()

raise SystemExit(0 if state['status'] == 'pass' else 1)
