#!/usr/bin/env python3
"""Fixed cold/repeat comparison against preserved companion builds, with service audit."""
import argparse
import importlib.util
import json
from pathlib import Path
import statistics
import time

spec=importlib.util.spec_from_file_location('official',Path(__file__).with_name('validate-official-httpfs.py'))
v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
def main():
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path.cwd())
    p.add_argument('--baseline',type=Path,required=True);p.add_argument('--official-root',type=Path,required=True)
    p.add_argument('--matrix-output',type=Path,default=Path('build/official-matrix'));p.add_argument('--output',type=Path,required=True)
    p.add_argument('--http-base',required=True);p.add_argument('--s3-base',required=True)
    p.add_argument('--s3-setup',type=Path,required=True);p.add_argument('--server-log',type=Path,required=True)
    a=p.parse_args();a.root=a.root.resolve();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
    (a.output/'summary.json').unlink(missing_ok=True)
    policy=json.loads((a.root/'specs/004-multi-grid-selection/evidence/official-httpfs-refactor/performance-policy.json').read_text())
    perf=a.root/policy['fixture']
    if v.sha(perf)!=policy['sha256']:raise ValueError('fixed fixture mismatch')
    results=[]
    for source in ('http','s3'):
        uri=getattr(a,source+'_base')+'/dimensions_perf.om';log=a.server_log/('http.jsonl' if source=='http' else 's3.jsonl')
        query=f'SELECT value FROM read_om({v.literal(uri)}, {v.AXES}){v.SELECTED} ORDER BY ALL'
        variants=[('old-disabled',a.baseline/'duckdb',a.baseline/'extension/httpfs/httpfs.duckdb_extension',a.baseline/'extension/duckomo/duckomo.duckdb_extension','SET duckomo_cache_enabled=false;'),
                  ('old-cold-hot',a.baseline/'duckdb',a.baseline/'extension/httpfs/httpfs.duckdb_extension',a.baseline/'extension/duckomo/duckomo.duckdb_extension','SET duckomo_cache_enabled=true;')]
        for version in ('v1.5.4','v1.5.5','v1.5.6'):
            official=a.official_root/version
            variants.append((version,official/'duckdb',official/'httpfs.duckdb_extension',a.matrix_output/version/'release/extension/duckomo/duckomo.duckdb_extension',''))
        expected=None
        for mode,cli,httpfs,extension,extra in variants:
            data=[]
            args=argparse.Namespace(root=a.root,duckdb=cli.resolve(),httpfs=httpfs.resolve(),extension=extension.resolve())
            setup=a.s3_setup.read_text()+'SET threads=1;SET duckomo_max_threads=1;'+extra
            for repetition in range(5):
                with v.CliSession(args,setup) as session:
                    startup_rss=session.rss()
                    for n in range(2):
                        name=f'{source}-{mode}-{repetition}-{n}';result=a.output/(name+'.csv')
                        result.unlink(missing_ok=True)
                        position=log.stat().st_size
                        start=time.perf_counter();session.send(f'COPY ({query}) TO {v.literal(result)} (FORMAT CSV, HEADER false);',check_success=True);elapsed=time.perf_counter()-start
                        if not result.is_file():raise RuntimeError('performance COPY produced no result file')
                        digest=v.sha(result)
                        if expected is None:expected=digest
                        if digest!=expected:raise AssertionError('performance queries must fully match')
                        data.append(dict(repetition=repetition,phase='cold' if n==0 else 'repeat',elapsed_seconds=elapsed,
                                         peak_process_rss_bytes=session.rss(),startup_rss_bytes=startup_rss,
                                         result_sha256=digest,audit=v.audit(log,position,"dimensions_perf.om")))
            entry=dict(source=source,variant=mode,workers=1,connection_lifetime='two complete selected queries per fresh connection',
                       identities={k:dict(path=str(path.resolve()),sha256=v.sha(path)) for k,path in [('duckdb',cli),('httpfs',httpfs),('duckomo',extension)]},
                       observations=data,medians={phase:dict(elapsed_seconds=statistics.median(d['elapsed_seconds'] for d in data if d['phase']==phase),
                                                            service_sent_body_bytes=statistics.median(d['audit']['sent_body_bytes'] for d in data if d['phase']==phase),
                                                            service_requests=statistics.median(d['audit']['requests'] for d in data if d['phase']==phase)) for phase in ('cold','repeat')})
            results.append(entry)
    for entry in results:
        if entry['variant'].startswith('v'):
            old=next(r for r in results if r['source']==entry['source'] and r['variant']=='old-disabled')
            ratio=entry['medians']['cold']['elapsed_seconds']/old['medians']['cold']['elapsed_seconds']
            entry['cold_elapsed_ratio_to_old_disabled']=ratio;entry['cold_threshold_pass']=ratio<=2
    summary=dict(schema_version=1,policy=policy,fixture_sha256=v.sha(perf),query=query,status='pass' if all(r.get('cold_threshold_pass',True) for r in results) else 'fail',
                 upstream_policy='DIRECT_IO, NO_CACHING; no forced download, threshold=0, no fallback; official metadata caching uses its default; no own range cache',results=results)
    (a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print('performance:',summary['status']);return 0 if summary['status']=='pass' else 1
if __name__=='__main__':raise SystemExit(main())
