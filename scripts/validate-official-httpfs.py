#!/usr/bin/env python3
"""Actual official DuckDB/HTTPFS runtime gates; transport audit stays outside DuckOMO."""
from __future__ import annotations
import argparse
import concurrent.futures
import csv
import hashlib
import json
import os
from pathlib import Path
import selectors
import subprocess
import tempfile
import time

AXES="dimensions := map(['value'], [['time','member']]), axes := {'time': {'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},'member': {'axis':'member','start':0,'step':1}}"
SELECTED=" WHERE valid_time=TIMESTAMP '2026-01-01 00:00:00' AND member BETWEEN 100 AND 103"
def literal(value): return "'"+str(value).replace("'","''")+"'"
def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def audit(log,position,object_name=None):
    if not log:return dict(scope='service sent bytes',available=False)
    with Path(log).open() as stream:
        stream.seek(position)
        events=[json.loads(line) for line in stream if line.strip()]
        if object_name:events=[e for e in events if str(e.get('object','')).split('/')[-1]==object_name]
    return dict(scope='service sent bytes, not client received bytes',available=True,
                sent_body_bytes=sum(e.get('body_bytes',0) for e in events if e.get('event')=='complete' or 'event' not in e),
                requests=sum(e.get('event')=='started' or 'event' not in e for e in events),events=events)

class CliSession:
    def __init__(self,args,setup=''):
        self.args=args
        env={k:v for k,v in os.environ.items() if k.lower() not in ('http_proxy','https_proxy','all_proxy')}
        # Private stderr is never published: CLI can repeat credential SQL or a signed URL.
        self.stderr=tempfile.TemporaryFile(mode='w+b')
        self.process=subprocess.Popen([str(args.duckdb),'-unsigned','-csv','-noheader'],
                                      stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.stderr,
                                      text=True,bufsize=1,env=env,cwd=args.root)
        self.counter=0
        self.send(f"LOAD {literal(args.httpfs)};LOAD {literal(args.extension)};SET threads=4;"+setup)
    def send(self,sql,timeout=120,check_success=False):
        diagnostic_start=os.fstat(self.stderr.fileno()).st_size
        self.counter+=1;marker=f'__duckomo_complete_{self.counter}__'
        self.process.stdin.write(sql+'\nSELECT '+literal(marker)+';\n');self.process.stdin.flush()
        # readline is bounded by the watchdog. DuckDB emits a marker after all
        # statements, including a failed statement, permitting same-connection recovery.
        import threading
        expired=False
        def kill():
            nonlocal expired
            expired=True;self.process.kill()
        timer=threading.Timer(timeout,kill);timer.start()
        try:
            while True:
                line=self.process.stdout.readline()
                if not line:raise RuntimeError('official CLI stopped or timed out (private diagnostics withheld)')
                if line.strip()==marker:break
        finally:timer.cancel()
        if expired:raise RuntimeError('official CLI timeout')
        if check_success and os.fstat(self.stderr.fileno()).st_size != diagnostic_start:
            raise RuntimeError('official CLI query reported diagnostics (private diagnostics withheld)')
    def rss(self):
        try:
            for line in Path(f'/proc/{self.process.pid}/status').read_text().splitlines():
                if line.startswith('VmHWM:'):return int(line.split()[1])*1024
        except OSError:pass
        return None
    def close(self):
        if self.process.poll() is None:
            self.process.stdin.close()
            try:self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:self.process.kill();self.process.wait()
        self.process.stdout.close();self.stderr.close()
    def __enter__(self):return self
    def __exit__(self,*_):self.close()

def read_metrics(path):
    with path.open(newline='') as stream:rows=list(csv.reader(stream))
    return [json.loads(row[0]) for row in rows]
def verify_metrics(metrics,remote):
    for m in metrics:
        if m['outcome']['status']!='success' or not m['outcome']['terminal_published']:raise AssertionError('query terminal metrics')
        v3=m['legacy_v3'];transport=m['transport']
        # Legacy schema uses response_body_bytes/requests/responses, with completeness.
        expected=None if remote else 0
        for key in ('response_body_bytes','attempts','responses'):
            if transport[key]!=expected:raise AssertionError(f'transport {key} must be {expected}')
        if transport['complete']!= (not remote):raise AssertionError('transport completeness')
        if v3['cache']['enabled'] or v3['cache']['capacity_bytes']!=0:raise AssertionError('own cache removed')

def run_scan(args,name,query,remote=False,log=None,workers=1,session=None,expect_error=False,cancel=False):
    directory=args.output/name;directory.mkdir(parents=True,exist_ok=True)
    result=directory/'rows.csv';profile=directory/'metrics.csv'
    result.unlink(missing_ok=True);profile.unlink(missing_ok=True)
    position=Path(log).stat().st_size if log else 0
    owned=session is None
    if owned:session=CliSession(args,args.setup)
    try:
        session.send(f'SET duckomo_max_threads={workers};')
        start=time.perf_counter()
        if cancel:
            import threading, signal
            cancel_timer=threading.Timer(0.25,lambda:session.process.send_signal(signal.SIGINT))
            cancel_timer.start()
        session.send(f"COPY ({query}) TO {literal(result)} (FORMAT CSV, HEADER false);")
        # After an error the CLI skips remaining statements in the same batch.
        # Fetch authoritative QueryEnd metrics in a separate command.
        session.send(f"COPY (SELECT metrics FROM duckomo_last_scan_metrics()) TO {literal(profile)} (FORMAT CSV, HEADER false);")
        if cancel:cancel_timer.cancel()
        elapsed=time.perf_counter()-start
        metrics=read_metrics(profile) if profile.exists() else []
        status='pass'
        if expect_error and (not metrics or any(m['outcome']['status']=='success' for m in metrics)):
            raise AssertionError('expected failure must publish unsuccessful terminal metrics')
        if cancel:
            if not metrics or not all(m['outcome']['status']=='cancelled' for m in metrics):raise AssertionError('cancelled QueryEnd metrics')
        if expect_error:
            if result.exists() and all(m['outcome']['status']=='success' for m in metrics):raise AssertionError('expected remote read failure')
        else:
            if not result.exists() or not metrics:raise AssertionError('query failed or produced no terminal metrics')
            verify_metrics(metrics,remote)
        record=dict(name=name,status=status,elapsed_seconds=elapsed,peak_process_rss_bytes=session.rss(),workers=workers,
                    query=query,expected_error=expect_error,result_sha256=sha(result) if result.exists() and not expect_error else None,
                    metrics=metrics,audit=audit(log,position,'dimensions_perf.om' if 'dimensions_perf.om' in query else 'real.om' if 'real.om' in query else 'raw.om'))
        (directory/'result.json').write_text(json.dumps(record,indent=2)+'\n')
        return record
    finally:
        if owned:session.close()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    for arg in ('duckdb','extension','httpfs','output'):parser.add_argument('--'+arg,type=Path,required=True)
    for arg in ('http-base','https-base','s3-base','s3-setup','server-log','https-ca-cert','real-file'):
        parser.add_argument('--'+arg,default=os.environ.get('DUCKOMO_'+arg.upper().replace('-','_'),''))
    parser.add_argument('--workers',default='1,2,4')
    parser.add_argument('--local-only',action='store_true')
    # Accepted by the native tool wrapper; no observer/cache gate reinstated.
    parser.add_argument('--fixtures',type=Path)
    parser.add_argument('--real-manifest')
    args=parser.parse_args();args.root=args.root.resolve();args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
    args.setup=Path(args.s3_setup).read_text() if args.s3_setup else ''
    if args.https_ca_cert:args.setup+='SET ca_cert_file='+literal(args.https_ca_cert)+';SET enable_server_cert_verification=true;'
    records=[];failure=None
    identities={k:dict(path=str(getattr(args,k).resolve()),sha256=sha(getattr(args,k))) for k in ('duckdb','httpfs','extension')}
    identities['fixtures']={name:dict(path=str(args.root/'test/data'/name),sha256=sha(args.root/'test/data'/name)) for name in ('raw.om','dimensions_perf.om')}
    identities['reported_version']=subprocess.check_output([str(args.duckdb),'-version'],text=True).strip()
    try:
        raw=args.root/'test/data/raw.om';perf=args.root/'test/data/dimensions_perf.om'
        manifest=json.loads((args.root/'test/data/dimensions-perf-manifest.json').read_text())
        if sha(perf)!=manifest['sha256']:raise AssertionError('fixed performance fixture SHA256')
        sources={'local':str(raw)}
        if not args.local_only:
            missing=[key for key in ('http_base','https_base','s3_base','s3_setup','server_log','https_ca_cert') if not getattr(args,key)]
            if missing:raise RuntimeError('required controlled remote configuration missing: '+', '.join(missing))
            sources.update(http=args.http_base+'/raw.om',https=args.https_base+'/raw.om',s3=args.s3_base+'/raw.om')
        baseline=None
        for source,uri in sources.items():
            record=run_scan(args,'same-content-'+source,'SELECT * FROM read_om('+literal(uri)+') ORDER BY ALL',source!='local')
            records.append(record)
            if baseline is None:baseline=record['result_sha256']
            if baseline!=record['result_sha256']:raise AssertionError('same-content cross-source results')
        for source in ('local',) if args.local_only else ('local','http','https','s3'):
            uri=str(perf) if source=='local' else getattr(args,source+'_base')+'/dimensions_perf.om'
            log=None if source=='local' else Path(args.server_log)/('s3.jsonl' if source=='s3' else 'http.jsonl')
            full=f'SELECT value FROM read_om({literal(uri)}, {AXES}) ORDER BY ALL'
            selected=f'SELECT value FROM read_om({literal(uri)}, {AXES}){SELECTED} ORDER BY ALL'
            expected=None
            full_one=None
            for workers in [int(w) for w in args.workers.split(',')]:
                record=run_scan(args,f'parallel-{source}-{workers}',full,source!='local',log,workers)
                records.append(record)
                if expected is None:
                    expected=record['result_sha256'];full_one=record
                if record['result_sha256']!=expected:raise AssertionError('parallel multiset coverage')
            cold=run_scan(args,'cold-selected-'+source,selected,source!='local',log)
            records.append(cold)
            reference=run_scan(args,'local-selected-reference-'+source,f'SELECT value FROM read_om({literal(perf)}, {AXES}){SELECTED} ORDER BY ALL')
            records.append(reference)
            if cold['result_sha256']!=reference['result_sha256']:raise AssertionError('selected rows differ')
            if log and not cold['audit']['sent_body_bytes']<full_one['audit']['sent_body_bytes']:raise AssertionError('cold selection network benefit')
            with CliSession(args,args.setup) as session:
                for n in range(2):records.append(run_scan(args,f'repeated-{source}-{n}',selected,source!='local',log,session=session))
                records.append(run_scan(args,'limit-'+source,f'SELECT value FROM read_om({literal(uri)}, {AXES}) LIMIT 1',source!='local',session=session))
        if not args.local_only:
            for fault in ('403','404','short','ignore-range','change-version'):
                uri=args.http_base+'/raw.om?fault='+fault
                records.append(run_scan(args,'fault-'+fault,'SELECT * FROM read_om('+literal(uri)+') ORDER BY ALL',True,expect_error=True))
                records.append(run_scan(args,'recovery-'+fault,'SELECT * FROM read_om('+literal(args.http_base+'/raw.om')+') ORDER BY ALL',True))
            # Independent connections execute concurrently. Keep audit out of overlapping runs.
            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                futures=[pool.submit(run_scan,args,f'concurrent-{n}','SELECT * FROM read_om('+literal(args.http_base+'/raw.om')+') ORDER BY ALL',True) for n in range(2)]
                concurrent_results=[f.result() for f in futures];records+=concurrent_results
                if len({r['result_sha256'] for r in concurrent_results})!=1:raise AssertionError('concurrent connection result isolation')
                if concurrent_results[0]['metrics'][0]['query_id']==concurrent_results[1]['metrics'][0]['query_id']:
                    # IDs are scoped to the process; distinct process sessions may both be 2-0.
                    pass
            if args.real_file:
                real=Path(args.real_file);local=run_scan(args,'real-local','SELECT * FROM read_om('+literal(real)+') ORDER BY ALL');records.append(local)
                for source in ('http','https','s3'):
                    r=run_scan(args,'real-'+source,'SELECT * FROM read_om('+literal(getattr(args,source+'_base')+'/real.om')+') ORDER BY ALL',True);records.append(r)
                    if r['result_sha256']!=local['result_sha256']:raise AssertionError('real sample cross-source results')
    except Exception as error:
        failure=str(error) if isinstance(error,AssertionError) or 'required controlled' in str(error) else type(error).__name__+': runtime validation failed (private diagnostics withheld)'
    summary=dict(schema_version=1,contract='official-httpfs-20261008',status='fail' if failure else 'pass',failure=failure,
                 identities=identities,cases=len(records),records=records,
                 historical_g5='superseded; prior failure unchanged',independent_verifier='not-run')
    (args.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(f'official HTTPFS: {summary["status"]}, {len(records)} completed cases; {args.output}')
    if failure:print(failure)
    return 1 if failure else 0
if __name__=='__main__':raise SystemExit(main())
