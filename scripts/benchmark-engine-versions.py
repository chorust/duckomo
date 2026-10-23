#!/usr/bin/env python3
"""Compare matched CLI/HTTPFS/DuckOMO triples on a fixed local OM fixture.

Reports client-observed COPY latency (including CSV output), not S3 throughput.
Each repetition gets a fresh process; repeat is in the same connection. OS page
cache is uncontrolled. Variants rotate to reduce ordering bias.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import statistics
import subprocess
import time

spec = importlib.util.spec_from_file_location('official', Path(__file__).with_name('validate-official-httpfs.py'))
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtimes', type=Path, required=True,
                   help='JSON array of {name, duckdb, httpfs, extension} paths')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--repetitions', type=int, default=7)
    a = p.parse_args()
    if a.repetitions < 3:
        p.error('at least three repetitions required')
    root = Path(__file__).resolve().parents[1]
    a.output.mkdir(parents=True, exist_ok=True)
    a.output = a.output.resolve()
    (a.output / 'summary.json').unlink(missing_ok=True)
    runtimes = json.loads(a.runtimes.read_text())
    if len(runtimes) < 2 or len({r['name'] for r in runtimes}) != len(runtimes):
        p.error('need at least two uniquely named runtimes')
    fixture = root / 'test/data/dimensions_perf.om'
    source = f'read_om({v.literal(fixture)}, {v.AXES})'
    queries = {
        'selected': f'SELECT value FROM {source}{v.SELECTED} ORDER BY ALL',
        'full': f'SELECT value FROM {source} ORDER BY ALL',
    }
    identities = []
    for r in runtimes:
        for key in ('duckdb', 'httpfs', 'extension'):
            r[key] = Path(r[key]).resolve()
        identities.append(dict(name=r['name'], version=subprocess.check_output(
            [str(r['duckdb']), '-version'], text=True).strip(), artifacts={
                k: dict(path=str(r[k]), sha256=v.sha(r[k])) for k in ('duckdb', 'httpfs', 'extension')}))
    observations, expected = [], {}
    for repetition in range(a.repetitions):
        order = runtimes[repetition % len(runtimes):] + runtimes[:repetition % len(runtimes)]
        for r in order:
            for workers in (1, 4):
                for name, query in queries.items():
                    args = argparse.Namespace(root=root, **{k: r[k] for k in ('duckdb', 'httpfs', 'extension')})
                    with v.CliSession(args, f'SET threads={workers}; SET duckomo_max_threads={workers};') as session:
                        for phase in ('fresh-process', 'repeat'):
                            result = a.output / 'rows.csv'
                            result.unlink(missing_ok=True)
                            start = time.perf_counter()
                            session.send(f'COPY ({query}) TO {v.literal(result)} (FORMAT CSV, HEADER false);', check_success=True)
                            elapsed = time.perf_counter() - start
                            digest = v.sha(result)
                            if expected.setdefault(name, digest) != digest:
                                raise AssertionError(f'result mismatch: {r["name"]} {name}')
                            observations.append(dict(runtime=r['name'], query=name, workers=workers,
                                repetition=repetition, phase=phase, seconds=elapsed,
                                result_sha256=digest, process_peak_rss_bytes=session.rss()))
    medians = []
    for r in runtimes:
        for name in queries:
            for workers in (1, 4):
                for phase in ('fresh-process', 'repeat'):
                    values = [o['seconds'] for o in observations if
                              (o['runtime'], o['query'], o['workers'], o['phase']) == (r['name'], name, workers, phase)]
                    medians.append(dict(runtime=r['name'], query=name, workers=workers, phase=phase,
                                        seconds=statistics.median(values)))
    summary = dict(scope='local fixture smoke benchmark; COPY includes CSV serialization; OS cache uncontrolled',
                   fixture=dict(path=str(fixture), bytes=fixture.stat().st_size, sha256=v.sha(fixture)),
                   runtimes=identities, queries=queries, observations=observations, medians=medians,
                   correctness='all complete result hashes match')
    (a.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(medians, indent=2))


if __name__ == '__main__':
    main()
