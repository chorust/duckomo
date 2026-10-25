#!/usr/bin/env python3
"""Repeatable DuckOMO remote I/O benchmark runner.

Example:
  scripts/benchmark-remote-io.py --runtime v2=/path/to/duckdb --extension /path/to/duckomo.duckdb_extension \
      --run-env build/grid-controlled-remote/fixtures/run.env --object o1280=http://127.0.0.1:8000/o1280.om \
      --queries full,selected --modes merge-off,merge-on --repeats 3 --output evidence/008/merge-final.json

Query modes other than full/selected require their registered SQL in a JSON query file
passed with --query-file; each entry maps query name to SQL containing {object}.
"""
from __future__ import annotations
import argparse, csv, hashlib, json, os, re, statistics, subprocess, time
from pathlib import Path


def parse_env(path: Path) -> dict[str, str]:
    values = {}
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith('#') or not line.startswith('export '):
            continue
        key, _, value = line[7:].partition('=')
        value = value.strip().strip('"\'')
        values[key] = value
    return values


def mapping(items, label):
    result = {}
    for item in items:
        if '=' not in item: raise ValueError(f'{label} must use NAME=VALUE')
        key, value = item.split('=', 1); result[key] = value
    return result


def parse_runtime_specs(items):
    result = {}
    for item in items:
        if '=' not in item: raise ValueError('runtime must use NAME=CLI|EXTENSION')
        name, spec = item.split('=', 1)
        cli, separator, extension = spec.partition('|')
        result[name] = {'cli': cli, 'extension': extension if separator else None}
    return result


def cli_command(cli, args, version):
    return [cli, '-unsigned'] + (['-no-agent'] if version.startswith('v2.') else []) + args


def query_elapsed_ms(profile):
    # DuckDB 1.5 exposes latency at the root; 2.0 groups query fields.
    return (profile['latency'] if 'latency' in profile else profile['query']['total_time']) * 1000


def safe_metrics(path: Path):
    rows = list(csv.reader(path.open(newline='')))
    for row in reversed(rows):
        for cell in row:
            try: value = json.loads(cell)
            except (ValueError, TypeError): continue
            if isinstance(value, dict) and 'remote_io' in value: return value
    raise RuntimeError('metrics query did not return remote_io JSON')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime', action='append', default=[], metavar='NAME=CLI|EXTENSION')
    p.add_argument('--extension', help='default extension path when --runtime omits one')
    p.add_argument('--httpfs', action='append', default=[], metavar='NAME=HTTPFS')
    p.add_argument('--run-env', type=Path)
    p.add_argument('--object', action='append', default=[], metavar='ID=URI')
    p.add_argument('--modes', default='merge-off,merge-on,async-off,async-on')
    p.add_argument('--queries', default='full,selected')
    p.add_argument('--query-file', type=Path)
    p.add_argument('--repeats', type=int, default=3)
    p.add_argument('--workers', default='1,4')
    p.add_argument('--merge-gap', type=int, default=4096)
    p.add_argument('--max-request', type=int, default=65536)
    p.add_argument('--memory-limit', default='')
    p.add_argument('--prefetch-batches', type=int, choices=range(1, 5), default=1)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--resume', action='store_true')
    a = p.parse_args()
    if a.repeats < 1: p.error('--repeats must be at least 1')
    env = os.environ.copy(); env.update(parse_env(a.run_env) if a.run_env else {})
    runtimes = parse_runtime_specs(a.runtime)
    if not runtimes: runtimes = {'default': {'cli': 'duckdb', 'extension': a.extension}}
    for spec in runtimes.values():
        if not spec['extension']: spec['extension'] = a.extension
        if not spec['extension']: p.error('each runtime needs an extension path or --extension')
    objects = mapping(a.object, 'object')
    if not objects:
        uri = env.get('DUCKOMO_O1280_URI') or env.get('DUCKOMO_HTTP_BASE')
        if not uri: p.error('provide --object or DUCKOMO_O1280_URI/DUCKOMO_HTTP_BASE in --run-env')
        objects = {'o1280': uri.rstrip('/') + ('/ecmwf_ifs_static_hsurf_reduced_gaussian_o1280.om' if uri.endswith('127.0.0.1') or uri == env.get('DUCKOMO_HTTP_BASE') else '')}
    custom = json.loads(a.query_file.read_text()) if a.query_file else {}
    queries_requested = [x.strip() for x in a.queries.split(',') if x.strip()]
    missing = [x for x in queries_requested if x != 'full' and x not in custom]
    if missing:
        p.error('provide explicit selection/coordinate/polygon/nearest SQL with --query-file: ' + ', '.join(missing))
    modes = [x.strip() for x in a.modes.split(',') if x.strip()]
    queries = [x.strip() for x in a.queries.split(',') if x.strip()]
    workers = [int(x) for x in a.workers.split(',')]
    if any(x not in {'merge-off','merge-on','async-off','async-on'} for x in modes): p.error('unknown mode')
    out = a.output; out.parent.mkdir(parents=True, exist_ok=True)
    prior = json.loads(out.read_text()) if a.resume and out.exists() else {'schema_version': 1, 'runs': []}
    done = {(x['runtime'], x['object_id'], x['query'], x['mode'], x['workers'], x['repeat'], x.get('memory_limit','')) for x in prior['runs']}
    for runtime, runtime_spec in runtimes.items():
      cli = runtime_spec['cli']; extension = runtime_spec['extension']
      artifact_sha256 = hashlib.sha256(Path(extension).read_bytes()).hexdigest()
      if a.resume and any(x['runtime'] == runtime and x.get('extension_sha256') != artifact_sha256 for x in prior['runs']):
          p.error('cannot resume evidence from a different or unidentified extension build')
      for object_id, uri in objects.items():
       for query in queries:
        sql_template = custom.get(query, "SELECT value FROM read_om('{object}')" + (" WHERE value > 6000" if query == 'selected' else '') + " ORDER BY value")
        for mode in modes:
         for nthreads in workers:
          for repeat in range(a.repeats):
           key=(runtime,object_id,query,mode,nthreads,repeat,a.memory_limit)
           query_sha256 = hashlib.sha256(custom.get(query, "SELECT value FROM read_om('{object}') ORDER BY value").encode()).hexdigest()
           if key in done:
               previous = next(x for x in prior['runs'] if (x['runtime'], x['object_id'], x['query'], x['mode'], x['workers'], x['repeat'], x.get('memory_limit','')) == key)
               if previous.get('query_sha256') != query_sha256 or previous.get('executor_threads') != max(4, nthreads) or previous.get('prefetch_batches', 1) != a.prefetch_batches:
                   p.error('cannot resume evidence with different query SQL or executor thread settings')
               continue
           merge = mode in {'merge-on', 'async-off', 'async-on'}
           async_on = mode == 'async-on'
           stem = hashlib.sha256(repr(key).encode()).hexdigest()[:16]
           tmp = out.parent / f'.remote-io-{stem}.csv'
           setup = []
           httpfs_map = mapping(a.httpfs, 'httpfs')
           httpfs = httpfs_map.get(runtime, env.get('DUCKOMO_HTTPFS',''))
           if httpfs: setup.append(f"LOAD '{httpfs.replace(chr(39), chr(39)*2)}';")
           if uri.startswith('s3://') and env.get('DUCKOMO_S3_SETUP'):
               setup.append(Path(env['DUCKOMO_S3_SETUP']).read_text())
           setup += [f"LOAD '{extension.replace(chr(39), chr(39)*2)}';", f"SET threads={max(4, nthreads)};", f"SET duckomo_max_threads={nthreads};", f"SET duckomo_remote_merge={'true' if merge else 'false'};", f"SET duckomo_remote_merge_gap={a.merge_gap};", f"SET duckomo_remote_max_request={a.max_request};", f"SET duckomo_async_io={'true' if async_on else 'false'};"]
           setup.append(f"SET duckomo_async_prefetch_batches={a.prefetch_batches};")
           if a.memory_limit: setup.append(f"SET memory_limit='{a.memory_limit}';")
           sql = sql_template.replace('{object}', uri.replace("'", "''"))
           metrics_file = out.parent / f'.remote-io-{stem}-metrics.csv'
           profile_file = out.parent / f'.remote-io-{stem}-profile.json'
           script = '\n'.join(setup + ["PRAGMA enable_profiling='json';", f"PRAGMA profiling_output='{str(profile_file).replace(chr(39), chr(39)*2)}';", f"COPY ({sql}) TO '{str(tmp).replace(chr(39), chr(39)*2)}' (FORMAT CSV, HEADER false);", "PRAGMA disable_profiling;", f"COPY (SELECT metrics FROM duckomo_last_scan_metrics()) TO '{str(metrics_file).replace(chr(39), chr(39)*2)}' (FORMAT CSV, HEADER false);"])
           version = 'v2.0.0' if '2.0' in runtime else ('v1.5.6' if '1.5' in runtime else runtime)
           rss_file = out.parent / f'.remote-io-{stem}-rss.txt'
           command = ['/usr/bin/time', '-f', '%M', '-o', str(rss_file)] + cli_command(cli, ['-c', script], version)
           start=time.monotonic(); proc=subprocess.run(command,text=True,capture_output=True,env=env); elapsed=(time.monotonic()-start)*1000
           peak_rss_bytes = int(rss_file.read_text().strip()) * 1024 if proc.returncode == 0 else None
           rss_file.unlink(missing_ok=True)
           if proc.returncode: raise RuntimeError(f'{runtime}/{object_id}/{query}/{mode} failed ({proc.returncode}): {proc.stderr[-2000:]}')
           if not tmp.exists() or not metrics_file.exists(): raise RuntimeError('benchmark result or metrics CSV was not created')
           process_elapsed_ms = elapsed
           profile = json.loads(profile_file.read_text()); profile_file.unlink()
           elapsed = query_elapsed_ms(profile)
           result_hash=hashlib.sha256(tmp.read_bytes()).hexdigest(); tmp.unlink()
           remote_metrics=safe_metrics(metrics_file); metrics_file.unlink()
           rec={'runtime':runtime,'object_id':object_id,'query':query,'mode':mode,'workers':nthreads,'repeat':repeat,'memory_limit':a.memory_limit,'executor_threads':max(4,nthreads),'prefetch_batches':a.prefetch_batches,'query_sha256':query_sha256,'elapsed_ms':elapsed,'process_elapsed_ms':process_elapsed_ms,'peak_rss_bytes':peak_rss_bytes,'extension_sha256':artifact_sha256,'result_sha256':result_hash,'remote_io':remote_metrics.get('remote_io',{})}
           prior['runs'].append(rec); out.write_text(json.dumps(prior,indent=2)+'\n')
    groups = {}
    for run in prior['runs']:
        key = (run['runtime'], run['object_id'], run['query'], run['mode'], run['workers'], run.get('memory_limit', ''))
        groups.setdefault(key, []).append(run)
    prior['summaries'] = []
    for key, runs in sorted(groups.items()):
        prior['summaries'].append({
            'runtime': key[0], 'object_id': key[1], 'query': key[2], 'mode': key[3], 'workers': key[4],
            'memory_limit': key[5], 'repeats': len(runs),
            'median_elapsed_ms': statistics.median(x['elapsed_ms'] for x in runs),
            'result_sha256': runs[0]['result_sha256'] if len({x['result_sha256'] for x in runs}) == 1 else None,
            'result_hashes_match': len({x['result_sha256'] for x in runs}) == 1,
            'remote_io': runs[len(runs)//2]['remote_io']
        })
    out.write_text(json.dumps(prior, indent=2) + '\n')
    print(out)

if __name__ == '__main__': main()
