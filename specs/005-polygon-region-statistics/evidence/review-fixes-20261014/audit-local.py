from pathlib import Path
import hashlib
import json
import importlib.util
import re
import shutil

ROOT = Path('/home/blizhan/repo/github/duckomo')
OUT = Path(__file__).resolve().parent
DEST = ROOT / 'specs/005-polygon-region-statistics/evidence/review-fixes-20261014'

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

state = json.loads((OUT/'local-status.json').read_text())
assert state['status'] == 'pass', state
for command in state['commands']:
    assert command['exit_code'] == 0 and sha(OUT/command['log']) == command['log_sha256']
summary = {'schema_version': 1, 'status': 'pass', 'date': '2026-10-14',
           'scope': 'review fixes; three-version local regression, P0-P4 and 16 ASan/UBSan checks per version',
           'p5_p6_p7_rerun': False, 'bulk_root': str(OUT.relative_to(ROOT)),
           'local_status_sha256': sha(OUT/'local-status.json'), 'versions': {}}
for version, record in state['versions'].items():
    gates_dir = OUT/version/'P0-P4'
    manifest = json.loads((gates_dir/'manifest.json').read_text())
    assert manifest['status'] == 'pass'
    assert set(manifest['requested_gates']) == {'P0','P1','P2','P3','P4'}
    for path_key, hash_key in [('manifest_path','manifest_sha256'),('duckdb_path','duckdb_sha256'),
                               ('duckomo_path','duckomo_sha256'),('spatial_path','spatial_sha256')]:
        assert sha(Path(manifest[path_key])) == manifest[hash_key], (version,path_key)
    for gate in manifest['gates']:
        assert gate['status'] == 'pass', (version, gate)
        for command in gate['commands']:
            assert command['exit_code'] == 0
            assert sha(gates_dir/command['output_path']) == command['output_sha256']
        if gate.get('evidence_report'):
            assert sha(gates_dir/gate['evidence_report']) == gate['evidence_report_sha256']
    build = (OUT/version/'build.log').read_text(errors='replace')
    assert '100% tests passed, 0 tests failed out of 16' in build
    local = (OUT/version/'local-validation.log').read_text(errors='replace')
    assert 'domain_reference_test: not-run' not in local
    assert 'match all 15 official references (1,038,240 positions each)' in local
    sql_assertions = sum(int(n) for n in re.findall(r'All tests passed \((\d+) assertions', local))
    region_sql = (gates_dir/'P0-4.stdout.txt').read_text()
    region_assertions = sum(int(n) for n in re.findall(r'All tests passed \((\d+) assertions', region_sql))
    assert region_assertions > 0
    sql_files = len(re.findall(r'^Filters: test/sql/',local,re.M)) + 1
    python_cases = sum(int(n) for n in re.findall(r'Ran (\d+) tests?', local))
    python_suites = len(re.findall(r'Ran \d+ tests?', local))
    p3 = json.loads((gates_dir/'P3-resources.json').read_text())
    p4 = json.loads((gates_dir/'P4-lifecycle.json').read_text())
    assert p3['status'] == p4['status'] == 'pass'
    summary['versions'][version] = dict(record, sanitizer_tests=16, sql_files=sql_files, sql_assertions=sql_assertions+region_assertions,
        python_cases=python_cases, python_suites=python_suites, domain_reference='pass',
        gate_manifest_sha256=sha(gates_dir/'manifest.json'), p3_sha256=sha(gates_dir/'P3-resources.json'),
        p4_sha256=sha(gates_dir/'P4-lifecycle.json'), p3_ratios=p3['ratios'], p4_resources=p4['resources'])
    dest = DEST/version
    dest.mkdir(parents=True,exist_ok=False)
    for name in ('manifest.json','P3-resources.json','P4-lifecycle.json'):
        shutil.copyfile(gates_dir/name,dest/name)
assert len({r['source_sha256'] for r in summary['versions'].values()}) == 1
spec = importlib.util.spec_from_file_location('matrix',ROOT/'scripts/version_matrix.py')
matrix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matrix)
assert matrix.extension_source_digest(ROOT) == next(iter(summary['versions'].values()))['source_sha256']
DEST.mkdir(parents=True,exist_ok=True)
shutil.copyfile(OUT/'local-status.json',DEST/'local-status.json')
(DEST/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
