#!/usr/bin/env python3
"""Check local real SEAS5/EC46 samples against official OM decode and producer coordinates.

Fetch the three public objects documented in docs/seasonal-domains.md first.
No unit conversion is applied. Optional --remote compares complete SST polygon
aggregates through S3, without claiming transport/request-count performance.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import math
import os
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SAMPLES = {
    'HSURF.om': ('ecmwf_seas5', 'data/ecmwf_seas5/static/HSURF.om'),
    'sst-monthly.om': ('ecmwf_seas5_monthly', 'data_run/ecmwf_seas5_monthly/2026/10/01/0000Z/sea_surface_temperature_mean.om'),
    'ec46-weekly.om': ('ecmwf_ec46_weekly', 'data_run/ecmwf_ec46_weekly/2026/07/12/0000Z/cloud_cover_anomaly.om'),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT/'build/release')
    parser.add_argument('--samples', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--spatial', type=Path, required=True)
    parser.add_argument('--httpfs', type=Path)
    parser.add_argument('--remote', action='store_true')
    args = parser.parse_args()
    build, samples, output = args.build.resolve(), args.samples.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    extension = build/'extension/duckomo/duckomo.duckdb_extension'
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    report = {'extension_sha256':sha(extension), 'samples':[], 'coordinate_reference':'independent pinned producer Float32 port; not native GRIB', 'remote': 'not-run'}
    prefix = f"LOAD '{extension}'; LOAD '{args.spatial.resolve()}'; SET threads=2; "
    env = dict(os.environ)
    for name in ['ALL_PROXY','all_proxy']:
        env.pop(name,None)
    for name in ['http_proxy','https_proxy','HTTP_PROXY','HTTPS_PROXY']:
        if name in env: env[name]=env[name].rstrip('/')
    if args.httpfs: prefix += f"LOAD '{args.httpfs.resolve()}'; "

    def query(sql):
        result = subprocess.run([str(build/'duckdb'),'-unsigned','-bail','-json'],input=prefix+sql,cwd=ROOT,
                                env=env,text=True,capture_output=True,timeout=900)
        assert result.returncode==0,result.stderr
        return json.loads(result.stdout) if result.stdout.strip() else []

    coords=[]
    f32=lambda v:struct.unpack('<f',struct.pack('<f',v))[0]
    dy=f32(180/f32(640.5))
    for y in range(640):
        count=20+4*min(y,639-y)
        lat=f32(f32(f32(319-y)*dy)+f32(dy/2))
        dx=f32(360/count)
        for x in range(count):
            lon=math.fmod(f32(x*dx),360)
            coords.append((lat,lon-360 if lon>=180 else lon))
    assert len(coords)==421120
    point_samples=[0,19,20,210559,210560,421119]
    for filename,(domain,key) in SAMPLES.items():
        path=samples/filename
        assert path.is_file(),path
        info=query(f"SELECT grid_id,layout FROM om_grid_info('{path}',domain:='{domain}');")[0]
        layout=info['layout'];points=layout['shape'][1];assert points==421120 and layout['point_axis']==1
        base=f"read_om('{path}',domain:='{domain}',include_source:=true)"
        if filename=='HSURF.om':
            target=output/'static.csv'
            query(f"COPY (SELECT om_source.point_index::BIGINT p,value,lat,lon FROM {base} ORDER BY p) TO '{target}' (HEADER,DELIMITER ',');")
            oracle=output/'static-oracle.csv'
            subprocess.run([str(build/'test/tools/duckomo_fixture_tool'),'--oracle',str(path),'--csv',str(oracle)],cwd=ROOT,check=True,capture_output=True)
            with target.open() as actual,oracle.open() as expected:
                count=0
                for row,reference in zip(csv.DictReader(actual),csv.DictReader(expected),strict=True):
                    p=int(row['p']);assert int(reference['index'])==p
                    if row['value'] and reference['value']:assert f32(float(row['value']))==f32(float(reference['value']))
                    else:assert not row['value'] and reference['value'].lower() in ('','nan')
                    assert (float(row['lat']),float(row['lon']))==coords[p]
                    count+=1
                assert count==421120
            checked=count
        else:
            times=layout['shape'][-1]
            checked=0
            for p in point_samples:
                oracle=output/f'{filename}-{p}.csv'
                subprocess.run([str(build/'test/tools/duckomo_fixture_tool'),'--oracle-slice',str(path),
                                '--offset',f'0,{p},0','--shape',f'1,1,{times}','--csv',str(oracle)],cwd=ROOT,check=True,capture_output=True)
                rows=query(f"SELECT value,lat,lon,valid_time FROM {base} WHERE om_source.point_index={p} ORDER BY om_source.logical_index;")
                with oracle.open() as source:expected=list(csv.DictReader(source))
                assert len(rows)==len(expected)==times
                for row,ref in zip(rows,expected):
                    value=float(ref['value']) if ref['value'] else float('nan')
                    assert (row['value'] is None and math.isnan(value)) or (row['value'] is not None and f32(row['value'])==f32(value))
                    assert (row['lat'],row['lon'])==coords[p]
                    checked+=1
        report['samples'].append({'domain':domain,'key':key,'sha256':sha(path),'layout':layout,'checked_values':checked})

    polygons="[{'id':'nino34','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((-170 -5,-120 -5,-120 5,-170 5,-170 -5))'))},{'id':'nino12','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((-90 -10,-80 -10,-80 0,-90 0,-90 -10))'))}]"
    def aggregates(path):
        return query(f"SELECT region_id,valid_time,count(*) n,avg(value) mean_raw FROM read_om_regions('{path}',domain:='ecmwf_seas5_monthly',regions:={polygons},region_crs:='grid_lonlat') GROUP BY region_id,valid_time ORDER BY region_id,valid_time;")
    rows=aggregates(samples/'sst-monthly.om')
    expected_counts={'nino34':sum(-5<=lat<=5 and -170<=lon<=-120 for lat,lon in coords),
                     'nino12':sum(-10<=lat<=0 and -90<=lon<=-80 for lat,lon in coords)}
    assert len(rows)==14
    assert {r['valid_time'] for r in rows}=={f'{year}-{month:02d}-01 00:00:00' for year,month in [(2026,10),(2026,11),(2026,12),(2027,1),(2027,2),(2027,3),(2027,4)]}
    assert all(r['n']==expected_counts[r['region_id']] for r in rows)
    report.update(status='local-pass',nino_counts=expected_counts,aggregates=rows)
    (output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    if args.remote:
        assert args.httpfs,'--remote requires --httpfs'
        remote=aggregates('s3://openmeteo/'+SAMPLES['sst-monthly.om'][1])
        assert all(a['region_id']==b['region_id'] and a['valid_time']==b['valid_time'] and a['n']==b['n'] and abs(a['mean_raw']-b['mean_raw'])<1e-10 for a,b in zip(rows,remote,strict=True))
        report['remote']='S3 and local complete Nino aggregates match'
    report.update(status='pass',nino_counts=expected_counts,aggregates=rows)
    (output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    print('real seasonal samples passed:',json.dumps(expected_counts),report['remote'])
    return 0


if __name__=='__main__':
    raise SystemExit(main())
