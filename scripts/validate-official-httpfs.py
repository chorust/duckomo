#!/usr/bin/env python3
"""Actual official DuckDB/HTTPFS runtime gates; transport audit stays outside DuckOMO."""
from __future__ import annotations
import argparse
import concurrent.futures
import csv
import hashlib
import importlib.util
import ipaddress
import json
import os
from pathlib import Path
import selectors
import shlex
import shutil
import socket
import subprocess
import tempfile
import time
from urllib.parse import urlsplit

RUNTIME_SPEC=importlib.util.spec_from_file_location('region_runtime',Path(__file__).with_name('region_runtime.py'))
region_runtime=importlib.util.module_from_spec(RUNTIME_SPEC)
RUNTIME_SPEC.loader.exec_module(region_runtime)

AXES="dimensions := map(['value'], [['time','member']]), axes := {'time': {'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},'member': {'axis':'member','start':0,'step':1}}"
SELECTED=" WHERE valid_time=TIMESTAMP '2026-01-01 00:00:00' AND member BETWEEN 100 AND 103"
P5_SCAN_TIMEOUT_SECONDS=7200
def literal(value): return "'"+str(value).replace("'","''")+"'"
def sha(path):
    digest=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):
            digest.update(chunk)
    return digest.hexdigest()

def contains_credential_material(path):
    markers=(b'AWS4-HMAC-SHA256',b'Authorization:')
    overlap=max(map(len,markers))-1
    tail=b''
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):
            payload=tail+chunk
            if any(marker in payload for marker in markers):return True
            tail=payload[-overlap:]
    return False

def audit(log,position,object_name=None):
    if not log:return dict(scope='service sent bytes',available=False)
    started=set();completed=set();responses=0;sent_body_bytes=0;failed_requests=0
    with Path(log).open(encoding='utf-8') as stream:
        stream.seek(position)
        for line in stream:
            if not line.strip():continue
            event=json.loads(line)
            if object_name and str(event.get('object','')).split('/')[-1]!=object_name:continue
            request_id=event.get('request_id')
            if event.get('event')=='started':started.add(request_id)
            if event.get('event')=='complete':completed.add(request_id)
            if event.get('event')=='complete' or 'event' not in event:
                responses+=1
                sent_body_bytes+=event.get('body_bytes',0)
                failed_requests+=event.get('status') not in (200,206) or bool(event.get('error_type'))
    return dict(scope='service sent bytes, not client received bytes',available=True,
                sent_body_bytes=sent_body_bytes,
                requests=len(started) if started else responses,responses=responses,
                incomplete_requests=len(started-completed),
                failed_requests=failed_requests)

class CliSession:
    def __init__(self,args,setup='',cold_client=False):
        self.args=args
        env={k:v for k,v in os.environ.items() if k.lower() not in ('http_proxy','https_proxy','all_proxy')}
        # Private stderr is never published: CLI can repeat credential SQL or a signed URL.
        self.stderr=tempfile.TemporaryFile(mode='w+b')
        self.process=subprocess.Popen([str(args.duckdb),'-unsigned','-csv','-noheader'],
                                      stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.stderr,
                                      text=True,bufsize=1,env=env,cwd=args.root)
        self.counter=0
        spatial=f"LOAD {literal(args.spatial)};" if getattr(args,'spatial',None) else ''
        self.send(f"LOAD {literal(args.httpfs)};LOAD {literal(args.extension)};{spatial}SET threads=4;"+setup)
        self.client_caches_disabled=False
        if cold_client:
            self.send('SET enable_external_file_cache=false;SET enable_http_metadata_cache=false;'
                      'SET enable_object_cache=false;',check_success=True)
            self.client_caches_disabled=True
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

def run_scan(args,name,query,remote=False,log=None,workers=1,session=None,expect_error=False,cancel=False,
             audit_object=None,cold_client=False,timeout=120):
    directory=args.output/name;directory.mkdir(parents=True,exist_ok=True)
    result=directory/'rows.csv';profile=directory/'metrics.csv'
    result.unlink(missing_ok=True);profile.unlink(missing_ok=True)
    position=Path(log).stat().st_size if log else 0
    owned=session is None
    if owned:session=CliSession(args,args.setup,cold_client=cold_client)
    try:
        session.send(f'SET duckomo_max_threads={workers};')
        start=time.perf_counter()
        if cancel:
            import threading, signal
            cancel_timer=threading.Timer(0.25,lambda:session.process.send_signal(signal.SIGINT))
            cancel_timer.start()
        session.send(f"COPY ({query}) TO {literal(result)} (FORMAT CSV, HEADER false);",timeout=timeout)
        # After an error the CLI skips remaining statements in the same batch.
        # Fetch authoritative QueryEnd metrics in a separate command.
        session.send(f"COPY (SELECT metrics FROM duckomo_last_scan_metrics()) TO {literal(profile)} (FORMAT CSV, HEADER false);",
                     timeout=timeout)
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
        result_rows=0
        if result.exists():
            with result.open(newline='') as stream:result_rows=sum(1 for _ in csv.reader(stream))
        record=dict(name=name,status=status,elapsed_seconds=elapsed,peak_process_rss_bytes=session.rss(),workers=workers,
                    query=query,expected_error=expect_error,result_sha256=sha(result) if result.exists() and not expect_error else None,
                    result_path=str(result),result_rows=result_rows,metrics=metrics,audit=audit(log,position,audit_object or
                            ('dimensions_perf.om' if 'dimensions_perf.om' in query else 'real.om' if 'real.om' in query else 'raw.om')),
                    cold_state={'fresh_cli_process':owned,'client_caches_disabled':session.client_caches_disabled})
        (directory/'result.json').write_text(json.dumps(record,indent=2)+'\n')
        return record
    finally:
        if owned:session.close()

class MissingP5Input(Exception):pass

def read_generated_env(path):
    allowed={"DUCKOMO_HTTP_BASE","DUCKOMO_HTTPS_BASE","DUCKOMO_S3_BASE","DUCKOMO_S3_SETUP",
             "DUCKOMO_HTTPS_CA_CERT","DUCKOMO_HTTPFS","DUCKOMO_REGION_MANIFEST","DUCKOMO_SERVER_LOG",
             "DUCKOMO_GRID_MANIFEST","DUCKOMO_REAL_FILE","DUCKOMO_REAL_MANIFEST"}
    values={}
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if not line.strip():continue
        if not line.startswith('export '):raise ValueError('remote environment file is not generated run.env format')
        words=shlex.split(line[7:],comments=False,posix=True)
        if len(words)!=1 or '=' not in words[0]:raise ValueError('remote environment file has a malformed assignment')
        key,value=words[0].split('=',1)
        if key not in allowed or key in values:raise ValueError('remote environment contains an unknown or duplicate field')
        values[key]=value
    return values

def require_loopback(url,scheme):
    parsed=urlsplit(url)
    if parsed.scheme!=scheme or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment or parsed.path not in ('','/'):
        raise MissingP5Input(f'controlled {scheme} fixture URL is missing or malformed')
    try:loopback=ipaddress.ip_address(parsed.hostname).is_loopback
    except ValueError:loopback=parsed.hostname.lower()=='localhost'
    if not loopback:raise MissingP5Input('controlled fixture endpoint must be loopback-only')
    return parsed

def require_live_fixture(run_manifest_path,run_manifest):
    pid_path=run_manifest_path.parent/'service-pids.json'
    if not pid_path.is_file():raise MissingP5Input('controlled fixture process manifest is missing')
    entries=json.loads(pid_path.read_text(encoding='utf-8')).get('services',[])
    script=str((Path(__file__).resolve().parents[1]/'scripts/setup-remote-fixtures.py').resolve())
    for name,role in (('http','__http_server'),('s3-audit-proxy','__s3_proxy')):
        entry=next((item for item in entries if item.get('name')==name),None)
        if not entry:raise MissingP5Input(f'controlled {name} process is not registered')
        try:cmdline=Path(f"/proc/{int(entry['pid'])}/cmdline").read_bytes().replace(b'\0',b' ').decode(errors='replace')
        except (OSError,ValueError,KeyError):raise MissingP5Input(f'controlled {name} process is not live')
        if script not in cmdline or role not in cmdline:raise MissingP5Input(f'controlled {name} process identity is stale')
    endpoint=run_manifest.get('s3_audit_endpoint','')
    try:
        parsed=urlsplit('http://'+endpoint)
        if not parsed.hostname or not ipaddress.ip_address(parsed.hostname).is_loopback:
            raise MissingP5Input('signed-S3 audit endpoint is not loopback-only')
        with socket.create_connection((parsed.hostname,parsed.port),timeout=1):pass
    except (OSError,ValueError):raise MissingP5Input('signed-S3 audit proxy is not reachable')

def p5_remote_config(args):
    if not args.remote_env or not args.server_log or not args.region_manifest or not args.spatial:
        raise MissingP5Input('P5 requires --remote-env, --server-log, --region-manifest, and --spatial')
    root=args.root.resolve()
    region_manifest=Path(args.region_manifest).resolve()
    if not region_manifest.is_file():raise MissingP5Input('frozen five-target region manifest is missing')
    feature_manifest=json.loads((root/'test/data/regions/manifest.json').read_text(encoding='utf-8'))
    frozen=feature_manifest['real_targets']
    expected_manifest=(root/'test/data/regions'/frozen['path']).resolve()
    if region_manifest!=expected_manifest or sha(region_manifest)!=frozen['sha256']:
        raise ValueError('P5 region manifest differs from the frozen feature manifest')
    targets=json.loads(region_manifest.read_text(encoding='utf-8'))['targets']
    if len(targets)!=5:raise ValueError('P5 requires exactly five frozen real targets')
    runtime_matrix_path=root/'test/data/regions/runtime-matrix.json'
    if not runtime_matrix_path.is_file():raise MissingP5Input('frozen runtime matrix is missing')
    version=subprocess.check_output([str(Path(args.duckdb).resolve()),'--version'],text=True).split()[0]
    runtime_matrix=json.loads(runtime_matrix_path.read_text(encoding='utf-8'))
    runtime=next((item for item in runtime_matrix.get('runtimes',[]) if item.get('version')==version),None)
    spatial_sha=region_runtime.verified_spatial_sha(runtime_matrix,version)
    if not runtime or spatial_sha is None:raise MissingP5Input('matching official runtime identity is unavailable or unverified')
    identities=((args.duckdb,runtime.get('official_runtime',{}).get('cli',{}).get('sha256')),
                (args.httpfs,runtime.get('official_runtime',{}).get('httpfs',{}).get('sha256')),
                (args.spatial,spatial_sha))
    if any(not expected for _,expected in identities):raise MissingP5Input('frozen CLI, HTTPFS, or spatial identity is incomplete')
    if any(sha(path)!=expected for path,expected in identities):raise MissingP5Input('CLI, HTTPFS, or spatial hash differs from the frozen runtime matrix')
    windows_path=root/'test/data/regions/remote-windows.json'
    if not windows_path.is_file():raise MissingP5Input('frozen bounded-window validation input is missing')
    windows=json.loads(windows_path.read_text(encoding='utf-8'))
    if windows.get('scope')!='bounded_window' or windows.get('real_targets_sha256')!=sha(region_manifest):
        raise ValueError('bounded-window validation inputs differ from the frozen targets')
    by_id={item['grid_id']:item for item in windows['targets']}
    if set(by_id)!={item['grid_id'] for item in targets} or len(windows['targets'])!=5:
        raise ValueError('bounded-window validation must cover exactly the five frozen targets')
    targets=[dict(target,remote_window=by_id[target['grid_id']]) for target in targets]
    args.remote_windows_path=windows_path
    remote_env_path=Path(args.remote_env).resolve()
    if not remote_env_path.is_file():raise MissingP5Input('generated remote environment file is missing')
    if remote_env_path.stat().st_mode & 0o077 or remote_env_path.stat().st_uid!=os.getuid():
        raise MissingP5Input('generated remote environment file is not private to the current user')
    env=read_generated_env(remote_env_path)
    run_manifest_path=remote_env_path.parent/'run-manifest.json'
    if not run_manifest_path.is_file():raise MissingP5Input('remote fixture run-manifest.json is missing')
    run_manifest=json.loads(run_manifest_path.read_text(encoding='utf-8'))
    if run_manifest.get('authorized_s3_bucket_match') is not True or run_manifest.get('credentials_written_to_evidence') is not False:
        raise MissingP5Input('remote fixture does not prove authorized-bucket matching and credential exclusion')
    if run_manifest.get('region_manifest_sha256')!=sha(region_manifest):
        raise MissingP5Input('remote fixture was not provisioned from the frozen five-target manifest')
    require_live_fixture(run_manifest_path,run_manifest)
    service_digest=str(run_manifest.get('s3_service_digest','')).removeprefix('sha256:')
    if (not run_manifest.get('s3_service_version') or len(service_digest)!=64 or
            any(char not in '0123456789abcdef' for char in service_digest)):
        raise MissingP5Input('fixed S3 service version and digest are unavailable')
    if not env.get('DUCKOMO_REGION_MANIFEST') or sha(Path(env['DUCKOMO_REGION_MANIFEST']))!=sha(region_manifest):
        raise MissingP5Input('remote environment does not identify the frozen region manifest')
    http=require_loopback(env.get('DUCKOMO_HTTP_BASE',''),'http')
    https=require_loopback(env.get('DUCKOMO_HTTPS_BASE',''),'https')
    if env['DUCKOMO_HTTP_BASE'].rstrip('/')!=run_manifest.get('http_base') or env['DUCKOMO_HTTPS_BASE'].rstrip('/')!=run_manifest.get('https_base'):
        raise ValueError('remote environment endpoints differ from the controlled fixture manifest')
    if not env.get('DUCKOMO_HTTPS_CA_CERT') or not Path(env['DUCKOMO_HTTPS_CA_CERT']).is_file():
        raise MissingP5Input('controlled HTTPS CA certificate is unavailable')
    s3=urlsplit(env.get('DUCKOMO_S3_BASE',''))
    if s3.scheme!='s3' or not s3.netloc or s3.username or s3.password or s3.query or s3.fragment or s3.path!='/duckomo':
        raise MissingP5Input('authorized signed-S3 fixture base is unavailable')
    if s3.netloc!=run_manifest.get('s3_bucket'):
        raise ValueError('signed-S3 fixture bucket differs from the authorized run manifest')
    setup=Path(env.get('DUCKOMO_S3_SETUP','')).resolve()
    expected_setup=run_manifest_path.parent/'s3-setup.sql'
    if (setup!=expected_setup or not setup.is_file() or setup.stat().st_mode & 0o077 or
            setup.stat().st_uid!=os.getuid()):
        raise MissingP5Input('private signed-S3 setup file is missing or has unsafe permissions')
    if Path(args.httpfs).resolve()!=Path(env.get('DUCKOMO_HTTPFS','')).resolve() or Path(args.httpfs).resolve()!=Path(run_manifest.get('official_httpfs','')).resolve():
        raise ValueError('remote fixture official HTTPFS identity differs from the requested artifact')
    if Path(args.server_log).resolve()!=Path(env.get('DUCKOMO_SERVER_LOG','')).resolve() or Path(args.server_log).resolve()!=Path(run_manifest.get('log_directory','')).resolve():
        raise ValueError('service log directory differs from the controlled fixture manifest')
    logs={name:Path(args.server_log)/f'{name}.jsonl' for name in ('http','s3')}
    if any(not path.is_file() for path in logs.values()):raise MissingP5Input('controlled service JSONL logs are missing')
    samples=run_manifest.get('region_samples',{})
    for target in targets:
        grid_id=target['grid_id'];obj=target['object']
        path=(root/obj['path']).resolve()
        if not path.is_file():raise MissingP5Input(f'frozen local object is missing for {grid_id}')
        if path.stat().st_size!=obj['size_bytes'] or sha(path)!=obj['sha256']:
            raise ValueError(f'frozen local object identity mismatch for {grid_id}')
        region=target['region_wkb'];wkb_path=root/'test/data/regions'/region['path']
        if not wkb_path.is_file():raise MissingP5Input(f'frozen region WKB is missing for {grid_id}')
        wkb=wkb_path.read_bytes()
        if len(wkb)!=region['bytes'] or hashlib.sha256(wkb).hexdigest()!=region['sha256'] or wkb.hex()!=region['wkb_hex'].lower():
            raise ValueError(f'frozen region WKB identity mismatch for {grid_id}')
        if region['wkb_hex'].lower() not in target['queries']['region_local_sql'].lower():
            raise ValueError(f'frozen region SQL geometry differs for {grid_id}')
        expected={'key':f'duckomo/grids/{grid_id}.om','sha256':obj['sha256'],'size_bytes':obj['size_bytes']}
        if (samples.get(grid_id)!=expected or run_manifest.get('fixtures',{}).get(expected['key'])!=
                {'sha256':obj['sha256'],'size_bytes':obj['size_bytes']} or
                run_manifest.get('http_grid_paths',{}).get(grid_id)!=f'/{grid_id}.om'):
            raise ValueError(f'controlled remote fixture identity mismatch for {grid_id}')
    args.http_base=env['DUCKOMO_HTTP_BASE'].rstrip('/')
    args.https_base=env['DUCKOMO_HTTPS_BASE'].rstrip('/')
    args.s3_base=env['DUCKOMO_S3_BASE'].rstrip('/')
    args.s3_setup=str(setup)
    args.https_ca_cert=env['DUCKOMO_HTTPS_CA_CERT']
    args.spatial=Path(args.spatial).resolve()
    if not args.spatial.is_file():raise MissingP5Input('matching spatial extension is missing')
    args.setup=setup.read_text(encoding='utf-8')
    args.setup+=f"SET ca_cert_file={literal(args.https_ca_cert)};SET enable_server_cert_verification=true;"
    return targets,logs,run_manifest,region_manifest,runtime_matrix_path,runtime

def retarget(sql,old_path,new_uri):
    if sql.count(old_path)!=1:raise ValueError('frozen SQL must contain the target object path exactly once')
    return sql.replace(old_path,new_uri,1)

def p5_queries(target,uri):
    obj=target['object'];window=target['remote_window']
    return (retarget(window['baseline_local_sql'],obj['path'],uri),
            retarget(window['region_local_sql'],obj['path'],uri))

def p5_metrics(record,operation,region=False):
    rows=record.get('metrics',[])
    if len(rows)!=1:raise AssertionError('P5 query did not publish exactly one scan metric')
    metric=rows[0]
    if metric.get('operation')!=operation or metric.get('outcome',{}).get('status')!='success' or not metric.get('outcome',{}).get('scan_complete') or not metric.get('outcome',{}).get('terminal_published'):
        raise AssertionError('P5 query lacks a successful complete terminal scan')
    values=metric.get('reads',{}).get('value_totals',{})
    if values.get('decode_complete') is not True:
        raise AssertionError('P5 value decode count is incomplete')
    result={'data_bytes':values.get('data_bytes'),'decoded_blocks':values.get('decoded_chunks'),
            'decode_complete':values.get('decode_complete'),'scan_complete':True}
    if not isinstance(result['data_bytes'],int) or not isinstance(result['decoded_blocks'],int):
        raise AssertionError('P5 value byte/block metrics are unknown')
    if region:
        exact=metric.get('regions',{})
        if exact.get('match_count_complete') is not True or not isinstance(exact.get('matched_pairs_exact'),int):
            raise AssertionError('P5 region exact count is incomplete')
        result['matched_pairs_exact']=exact['matched_pairs_exact']
    return result

def p5_savings_failures(full,region,source):
    failures=[]
    if full['metrics']['data_bytes']<=region['metrics']['data_bytes']:
        failures.append(f'{source} value data bytes did not strictly decrease')
    if full['metrics']['decoded_blocks']<=region['metrics']['decoded_blocks']:
        failures.append(f'{source} decoded blocks did not strictly decrease')
    if full['audit']['sent_body_bytes']<=region['audit']['sent_body_bytes']:
        failures.append(f'{source} service sent bytes did not strictly decrease')
    return failures

def p5_unrun_targets(path):
    ids=('ncep_gfswave025','gem_rdps_10km','gem_regional','aladin_central_europe_2km','ecmwf_ifs')
    try:
        targets=json.loads(Path(path).read_text(encoding='utf-8')).get('targets',[])
        if len(targets)==5:ids=tuple(target['grid_id'] for target in targets)
    except (OSError,ValueError,KeyError,TypeError):pass
    sources=('local','http','https','s3')
    return [{'grid_id':grid_id,'status':'not-run',
             'sources':{source:{'status':'not-run'} for source in sources}} for grid_id in ids]

def run_region_p5(args):
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    if any(output.iterdir()):raise ValueError('P5 output directory must be empty')
    summary={'schema_version':1,'gate':'P5','status':'not-run','reason':'controlled P5 inputs unavailable',
             'cold_policy':{'fresh_cli_process_per_scan':True,'duckdb_external_file_cache':False,
                            'duckdb_http_metadata_cache':False,'duckdb_object_cache':False,
                            'duckomo_cache':'removed',
                            'server_page_cache':'not used to infer bytes; service response bytes are audited'},
             'comparison_scope':'bounded_window',
             'full_domain_scanned':False,
             'baseline_kind':'materialized read_om window then ST_Covers',
             'full_domain_savings_claimed':False,
             'targets':[],'service_logs':{}}
    logs={}
    try:
        targets,logs,run_manifest,region_manifest,runtime_matrix_path,runtime=p5_remote_config(args)
        summary['inputs']={'region_manifest_sha256':sha(region_manifest),'duckdb_sha256':sha(args.duckdb),
                           'duckomo_sha256':sha(args.extension),'httpfs_sha256':sha(args.httpfs),
                           'spatial_sha256':sha(args.spatial),
                           'runtime_matrix_sha256':sha(runtime_matrix_path),
                           'runtime_version':runtime['version'],
                           'remote_windows_sha256':sha(args.remote_windows_path),
                           's3_service_version':run_manifest['s3_service_version'],
                           's3_service_digest':run_manifest['s3_service_digest'],
                           's3_service_identity_independently_verified':run_manifest.get('s3_service_identity_independently_verified',False)}
        summary['status']='pass';summary['reason']='';failures=[]
        for target in targets:
            source_start=len(failures)
            grid_id=target['grid_id'];obj=target['object'];queries=target['queries']
            source_uris={'local':obj['path'],'http':args.http_base+'/'+grid_id+'.om',
                         'https':args.https_base+'/'+grid_id+'.om',
                         's3':args.s3_base+'/grids/'+grid_id+'.om'}
            target_record={'grid_id':grid_id,'object_sha256':obj['sha256'],'sources':{},
                           'region_wkb_sha256':target['region_wkb']['sha256'],
                           'window_bounds_lonlat':target['remote_window']['window_bounds_lonlat'],
                           'expected_baseline_rows':target['expected_region_rows'],
                           'expected_region_rows':target['expected_region_rows']}
            summary['targets'].append(target_record)
            for source,uri in source_uris.items():
                full_query,region_query=p5_queries(target,uri)
                remote=source!='local'
                log=logs['s3' if source=='s3' else 'http'] if remote else None
                full=run_scan(args,f'p5-{grid_id}-{source}-full',full_query,remote,log,
                              audit_object=grid_id+'.om',cold_client=True,timeout=P5_SCAN_TIMEOUT_SECONDS)
                region=run_scan(args,f'p5-{grid_id}-{source}-region',region_query,remote,log,
                                audit_object=grid_id+'.om',cold_client=True,timeout=P5_SCAN_TIMEOUT_SECONDS)
                full['scan_metrics']=full['metrics'];region['scan_metrics']=region['metrics']
                full['metrics']=p5_metrics(full,'read_om')
                region['metrics']=p5_metrics(region,'read_om_regions',True)
                full['metrics']['row_count']=full['result_rows']
                if full['result_sha256']!=region['result_sha256']:
                    failures.append(f'{grid_id}/{source} bounded baseline and region rows differ')
                full['local_result_sha256']=full['result_sha256'] if source=='local' else None
                region['local_result_sha256']=region['result_sha256'] if source=='local' else None
                if source=='local':
                    target_record['local_full_result_sha256']=full['result_sha256']
                    target_record['local_region_result_sha256']=region['result_sha256']
                target_record['sources'][source]={'full':full,'region':region}
                if full['result_sha256'] is None or region['result_sha256'] is None or not full['metrics']['scan_complete'] or not region['metrics']['scan_complete']:
                    failures.append(f'{grid_id}/{source} scan evidence is incomplete')
                if any(not scan['cold_state']['fresh_cli_process'] or not scan['cold_state']['client_caches_disabled']
                       for scan in (full,region)):
                    failures.append(f'{grid_id}/{source} scan did not use a fresh process with client caches disabled')
                if source=='local':
                    full['local_result_sha256']=full['result_sha256'];region['local_result_sha256']=region['result_sha256']
                else:
                    full['local_result_sha256']=target_record.get('local_full_result_sha256')
                    region['local_result_sha256']=target_record.get('local_region_result_sha256')
                    if full['result_sha256']!=full['local_result_sha256']:
                        failures.append(f'{grid_id}/{source} full aggregate differs from local')
                    if region['result_sha256']!=region['local_result_sha256']:
                        failures.append(f'{grid_id}/{source} region result differs from local')
                if full['metrics']['row_count']!=target['expected_region_rows']:
                    failures.append(f'{grid_id}/{source} bounded baseline row count differs from frozen manifest')
                if region['metrics']['matched_pairs_exact']!=target['expected_region_rows']:
                    failures.append(f'{grid_id}/{source} exact region count differs from frozen manifest')
                if region['result_rows']!=target['expected_region_rows']:
                    failures.append(f'{grid_id}/{source} region output row count differs from frozen manifest')
                if source in ('http','s3'):
                    target_record['sources'][source]['bounded_cost_reductions']={
                        'value_data_bytes':full['metrics']['data_bytes']-region['metrics']['data_bytes'],
                        'decoded_blocks':full['metrics']['decoded_blocks']-region['metrics']['decoded_blocks'],
                        'service_sent_bytes':full['audit']['sent_body_bytes']-region['audit']['sent_body_bytes']}
                    failures.extend(f'{grid_id}/{failure}' for failure in p5_savings_failures(full,region,source))
                if remote:
                    for scan_name,scan in (('full',full),('region',region)):
                        if scan['audit']['incomplete_requests'] or scan['audit']['failed_requests'] or not scan['audit']['requests']:
                            failures.append(f'{grid_id}/{source} {scan_name} service audit has missing or failed requests')
                target_record['sources'][source]['status']='pass' if len(failures)==source_start else 'fail'
                source_start=len(failures)
            target_record['status']='pass' if all(record.get('status')=='pass' for record in target_record['sources'].values()) else 'fail'
        if failures:
            summary['status']='fail';summary['reason']='one or more P5 target/source comparisons failed'
            summary['failures']=failures
    except MissingP5Input as error:
        summary['status']='not-run';summary['reason']=str(error)
        if not summary['targets']:summary['targets']=p5_unrun_targets(args.region_manifest)
    except Exception as error:
        summary['status']='fail';summary['reason']=str(error) if isinstance(error,(AssertionError,ValueError)) else type(error).__name__+': P5 execution failed (private diagnostics withheld)'
    finally:
        for name,path in logs.items():
            try:
                if contains_credential_material(path):
                    summary['status']='fail';summary['reason']='service log contains credential material; raw log was not archived'
                    continue
                target=output/f'{name}-service.jsonl'
                if path.resolve()!=target.resolve():shutil.copyfile(path,target)
                summary['service_logs'][name]={'path':target.name,'bytes':target.stat().st_size,'sha256':sha(target)}
            except OSError:pass
    summary_path=output/'summary.json'
    summary_path.write_text(json.dumps(summary,sort_keys=True,indent=2)+'\n',encoding='utf-8')
    print(f"region P5: {summary['status']}; {len(summary['targets'])} targets; {output}")
    return 0 if summary['status']=='pass' else 2 if summary['status']=='not-run' else 1

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    for arg in ('duckdb','extension','httpfs','output'):parser.add_argument('--'+arg,type=Path,required=True)
    for arg in ('http-base','https-base','s3-base','s3-setup','server-log','https-ca-cert','real-file'):
        parser.add_argument('--'+arg,default=os.environ.get('DUCKOMO_'+arg.upper().replace('-','_'),''))
    parser.add_argument('--region-manifest',type=Path)
    parser.add_argument('--remote-env',type=Path)
    parser.add_argument('--spatial',type=Path)
    parser.add_argument('--workers',default='1,2,4')
    parser.add_argument('--local-only',action='store_true')
    # Accepted by the native tool wrapper; no observer/cache gate reinstated.
    parser.add_argument('--fixtures',type=Path)
    parser.add_argument('--real-manifest')
    args=parser.parse_args();args.root=args.root.resolve();args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
    if args.region_manifest:
        return run_region_p5(args)
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
