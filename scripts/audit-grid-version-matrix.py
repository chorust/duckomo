#!/usr/bin/env python3
"""Audit official package identities without promoting partial grid gates to H8."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def audit_version(root,pair,evidence_root):
    version=pair['pair_id'];runtime=pair['official_runtime']
    summary=evidence_root/version/'summary.json'
    record=dict(pair_id=version,identity_status='not-run',compatibility_status='not-run',runtime_validation=None,full_grid_gates='not-run')
    if not summary.is_file():return record
    data=json.loads(summary.read_text());identities=data['identities']
    if data.get('contract')!='official-httpfs-20261008':raise ValueError('runtime evidence uses an obsolete contract')
    if not identities['reported_version'].startswith(version+' '):raise ValueError('official engine version mismatch')
    for name,key in [('duckdb','cli'),('httpfs','httpfs')]:
        identity=identities[name]
        if identity['sha256']!=runtime[key+'_sha256'] or sha(identity['path'])!=identity['sha256']:raise ValueError('official artifact provenance mismatch')
    if sha(identities['extension']['path'])!=identities['extension']['sha256']:raise ValueError('DuckOMO artifact mismatch')
    if data['status']!='pass' or data['cases']<48:raise ValueError('official remote gate incomplete or failed')
    record.update(identity_status='pass',compatibility_status='pass',runtime_validation={'path':str(summary),'sha256':sha(summary)})
    # Broad 004 H0-H7 acceptance is distinct from runtime compatibility.
    grid=evidence_root/version/'grid/run.json'
    if grid.is_file():
        run=json.loads(grid.read_text());gates={g['id']:g['status'] for g in run.get('gates',[])}
        if run.get('contract')=='official-httpfs-20261008' and all(gates.get(f'H{i}')=='pass' for i in range(8)):
            record['full_grid_gates']='pass'
    return record

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--root',type=Path,default=Path.cwd());p.add_argument('--matrix',type=Path,required=True)
    p.add_argument('--evidence-root',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    for key in ('sample-manifest','current-pair-id','current-build-id','duckdb','extension','httpfs'):p.add_argument('--'+key)
    args=p.parse_args();status='not-run';identity='not-run';records=[];reason=''
    try:
        matrix=json.loads(args.matrix.read_text())
        if matrix['schema_version']!=2 or {v['pair_id'] for v in matrix['pairs']}!={'v1.5.4','v1.5.5','v1.5.6'}:raise ValueError('expected three official 1.5 release entries')
        records=[audit_version(args.root,v,args.evidence_root) for v in matrix['pairs']]
        identity='pass' if all(r['identity_status']=='pass' for r in records) else 'not-run'
        status='pass' if identity=='pass' and all(r['full_grid_gates']=='pass' for r in records) else 'not-run'
        reason='all official runtime checks and full H0-H7 gates passed' if status=='pass' else 'official runtime compatibility is recorded separately; complete same-input H0-H7 evidence remains required for H8'
    except (OSError,ValueError,KeyError,TypeError) as error:status='fail';identity='fail';reason=str(error)
    result=dict(schema_version=2,contract='official-httpfs-20261008',gate='H8',status=status,identity_status=identity,
                reason=reason,matrix_sha256=sha(args.matrix),pairs=records)
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n')
    print('H8 official matrix:',status)
    return 0 if status=='pass' else 1 if status=='fail' else 2
if __name__=='__main__':raise SystemExit(main())
