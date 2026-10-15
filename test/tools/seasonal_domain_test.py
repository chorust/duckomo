#!/usr/bin/env python3
"""Exercise every SEAS5/EC46 domain with full producer-shaped synthetic data."""
from __future__ import annotations

import importlib.util
import json
import math
import os
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / os.environ.get("DUCKOMO_BUILD", "build/release")
SPEC = importlib.util.spec_from_file_location("seasonal", ROOT / "scripts/generate-seasonal-domains.py")
assert SPEC and SPEC.loader
seasonal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(seasonal)


def sql(query: str, spatial: bool = False) -> list:
    prefix = f"LOAD '{BUILD}/extension/duckomo/duckomo.duckdb_extension'; SET threads=2; "
    if spatial:
        prefix += f"LOAD '{os.environ['DUCKOMO_SPATIAL_EXTENSION']}'; "
    result = subprocess.run([str(BUILD / 'duckdb'), '-unsigned', '-bail', '-json'],
                            input=prefix + query, cwd=ROOT, text=True, capture_output=True, timeout=120)
    assert result.returncode == 0, result.stderr
    decoder = json.JSONDecoder()
    data = result.stdout.strip()
    batches = []
    while data:
        batch, end = decoder.raw_decode(data)
        batches.append(batch)
        data = data[end:].strip()
    return batches


def reference(n: int, counts: list[int]) -> list[tuple[float, float]]:
    f32 = lambda value: struct.unpack('<f', struct.pack('<f', value))[0]
    dy = f32(180.0 / f32(2.0 * n + 0.5))
    result = []
    for y, count in enumerate(counts + counts[::-1]):
        lat = f32(f32(f32(n - y - 1) * dy) + f32(dy / 2.0))
        step = f32(360.0 / count)
        for x in range(count):
            lon = math.fmod(f32(x * step), 360.0)
            result.append((lat, lon - 360 if lon >= 180 else lon))
    return result


def main() -> int:
    subprocess.run(['python3', str(ROOT / 'scripts/generate-seasonal-domains.py'), '--check'], check=True)
    definitions = json.loads((ROOT / 'test/data/grids/definitions.json').read_text())
    n160 = next(x for x in definitions['definitions'] if x['id'] == 'n160')
    refs = {'o320': reference(320, [20 + 4 * y for y in range(320)]),
            'n160': reference(160, n160['parameters']['north_hemisphere_counts'])}
    assert len(refs['o320']) == 421120 and len(refs['n160']) == 138346
    with tempfile.TemporaryDirectory(prefix='duckomo-seasonal-') as directory:
        subprocess.run([str(BUILD / 'test/tools/duckomo_fixture_tool'), '--seasonal-output', directory], check=True)
        for domain in seasonal.O320_DOMAINS + seasonal.N160_DOMAINS:
            grid = 'o320' if domain in seasonal.O320_DOMAINS else 'n160'
            # Product cadence is not inferred by domain: deliberately unequal month labels.
            read = f"read_om('{directory}/{grid}-time.om',domain:='{domain}',include_source:=true, "
            read += "axes:={'time':{'axis':'time','values':[TIMESTAMP '2026-01-01',TIMESTAMP '2026-02-01',TIMESTAMP '2026-03-01']}})"
            expected = [(i, lat, lon) for i, (lat, lon) in enumerate(refs[grid])
                        if -5 <= lat <= 5 and -170 <= lon <= -120]
            batches = sql(f"SELECT count(*) n, count(DISTINCT valid_time) times FROM {read} "
                          "WHERE lat BETWEEN -5 AND 5 AND lon BETWEEN -170 AND -120;")
            assert batches[0] == [{'n': len(expected) * 3, 'times': 3}], (domain, batches)
            rows = sql(f"SELECT om_source.point_index::BIGINT p,value,lat,lon FROM {read} "
                       "WHERE lat BETWEEN -5 AND 5 AND lon BETWEEN -170 AND -120 "
                       "AND valid_time=TIMESTAMP '2026-02-01' ORDER BY p LIMIT 3;")[0]
            assert rows == [{'p':i,'value':i % 4096 + .25,'lat':lat,'lon':lon}
                            for i, lat, lon in expected[:3]], (domain, rows)
        for grid, domain in [('o320','ecmwf_seas5'), ('n160','ecmwf_seas5_12hourly')]:
            read = f"read_om('{directory}/{grid}-ensemble.om',domain:='{domain}',include_source:=true,"
            read += "axes:={'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},'time':{'axis':'time','start':TIMESTAMP '2026-01-01','step':INTERVAL '6 hours'}})"
            rows = sql(f"SET duckomo_revalidate_metadata=true; SELECT member,value,valid_time FROM {read} "
                       "WHERE om_source.point_index=0 ORDER BY member,valid_time;")[0]
            assert [x['value'] for x in rows] == [0,.25,.5,10,10.25,10.5]
            assert [x['member'] for x in rows] == [0,0,0,1,1,1]
        # A full time slice per point shares compressed chunks across points.
        # Physical reads must follow chunks, while logical requests remain observable.
        read = f"read_om('{directory}/o320-time.om',domain:='ecmwf_seas5_monthly')"
        batches = sql(f"SELECT sum(value) FROM {read} WHERE lat BETWEEN -5 AND 5 AND lon BETWEEN -170 AND -120;"
                      "SELECT metrics FROM duckomo_last_scan_metrics();")
        metrics = json.loads(batches[1][0]['metrics'])
        totals = metrics['reads']['value_totals']
        variable = metrics['reads']['variables']['/value']
        assert totals['data_requests'] < 400 and totals['index_requests'] < 100, totals
        assert variable['logical_data_requests'] > totals['data_requests'] * 10, variable
        assert metrics['memory']['query_owned_complete'] and metrics['memory']['query_owned_released_at_terminal']
        bad = subprocess.run([str(BUILD/'duckdb'),'-unsigned','-bail'],input=
                             f"LOAD '{BUILD}/extension/duckomo/duckomo.duckdb_extension'; SELECT count(*) FROM "
                             f"read_om('{directory}/o320-time.om',domain:='ecmwf_seas5_12hourly');",
                             cwd=ROOT,text=True,capture_output=True)
        assert bad.returncode != 0 and ('point' in bad.stderr.lower() or 'conflicts' in bad.stderr.lower()), bad.stderr
        if os.environ.get('DUCKOMO_SPATIAL_EXTENSION'):
            rows = sql(f"SELECT count(*) n FROM read_om_regions('{directory}/o320-time.om',"
                       "domain:='ecmwf_seas5_monthly', regions:=[{'id':'Nino34','wkb':ST_AsWKB(ST_GeomFromText("
                       "'POLYGON((-170 -5,-120 -5,-120 5,-170 5,-170 -5))'))}],region_crs:='grid_lonlat');",True)[0]
            expected = sum(-5 <= lat <= 5 and -170 <= lon <= -120 for lat,lon in refs['o320']) * 3
            assert rows == [{'n':expected}], rows
    print('ten SEAS5/EC46 domains, independent coordinates, time/member axes and wrong-grid rejection passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
