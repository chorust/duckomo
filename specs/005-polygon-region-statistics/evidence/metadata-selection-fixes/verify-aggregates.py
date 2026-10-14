import argparse, hashlib, json, os, subprocess, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser();p.add_argument('--version',default='v1.5.5');p.add_argument('--remote',action='store_true');p.add_argument('--output',required=True);args=p.parse_args()
out=Path(args.output);out.mkdir(parents=True,exist_ok=True)
manifest=json.loads((ROOT/f'build/official-matrix/{args.version}/build-manifest.json').read_text());release=Path(manifest['release_dir']);runtime=ROOT/f'build/regions-runtime/{args.version}/official';ext=release/'extension/duckomo/duckomo.duckdb_extension'
sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
assert sha(ext)==manifest['artifact_hashes']['duckomo']
import sys
sys.path.insert(0, str(ROOT/'scripts'))
import version_matrix
assert manifest['inputs']['extension_source_sha256']==version_matrix.extension_source_digest(ROOT)
setup=f"""LOAD '{runtime}/httpfs.duckdb_extension';
LOAD '{ext}'; LOAD '{runtime}/spatial.duckdb_extension';
SET threads=4; SET duckomo_max_threads=2;
CREATE TEMP TABLE qinghai AS SELECT feature.properties.GID_2 region_id,
 ST_GeomFromGeoJSON(feature.geometry::JSON) geometry FROM
 (SELECT unnest(features) feature FROM read_json_auto('build/manual-region-queries/qinghai-gadm41.geojson'));
SET VARIABLE polygons=(SELECT list({{'id':region_id,'wkb':ST_AsWKB(geometry)}} ORDER BY region_id) FROM qinghai);
"""
local='build/manual-region-queries/gfs025-20261014T0000.om';remote='s3://openmeteo/data_spatial/ncep_gfs025/2026/10/14/0000Z/2026-10-14T0000.om'
env=dict(os.environ)
for name in ['ALL_PROXY','all_proxy']:env.pop(name,None)
for name in ['http_proxy','https_proxy','HTTP_PROXY','HTTPS_PROXY']:
 if name in env:env[name]=env[name].rstrip('/')
report={'build_id':manifest['build_id'],'version':args.version,'extension_sha256':sha(ext),'boundary_sha256':sha(ROOT/'build/manual-region-queries/qinghai-gadm41.geojson'),'cases':[]}
def run_case(name,source,selected,strict=False):
 option=", variable := 'temperature_80m'" if selected else ''
 sql=setup+f"SET duckomo_revalidate_metadata={'true' if strict else 'false'};\nSELECT region_id,count(*) AS grid_points,avg(temperature_80m) AS mean_celsius FROM read_om_regions('{source}',domain:='ncep_gfs025'{option},regions:=getvariable('polygons'),region_crs:='grid_lonlat') GROUP BY region_id ORDER BY region_id;\nSELECT metrics FROM duckomo_last_scan_metrics();\n"
 (out/(name+'.sql')).write_text(sql);start=time.monotonic();result=subprocess.run([str(runtime/'duckdb'),'-unsigned','-bail','-json'],input=sql,text=True,cwd=ROOT,env=env,capture_output=True,timeout=1800);elapsed=time.monotonic()-start
 (out/(name+'.stdout')).write_text(result.stdout);(out/(name+'.stderr')).write_text(result.stderr)
 assert result.returncode==0,(name,result.stderr)
 dec=json.JSONDecoder();text=result.stdout.strip();objects=[]
 while text:
  obj,position=dec.raw_decode(text);objects.append(obj);text=text[position:].strip()
 assert len(objects)==2,(name,objects)
 rows=objects[0];metrics=json.loads(objects[1][0]['metrics']);reads=metrics['reads'];totals=reads['value_totals'];metadata=reads['metadata']
 assert len(rows)==8 and sum(x['grid_points'] for x in rows)==1146
 assert totals['decoded_chunks']>0 and totals['decode_complete']
 report['cases'].append({'name':name,'elapsed_seconds':elapsed,'rows':rows,'metadata':metadata,'value_totals':totals,'metrics':metrics})
 (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n');print(name,round(elapsed,3),metadata,flush=True)
 return rows
full=run_case('local-full',local,False);selected=run_case('local-selected',local,True);strict=run_case('local-selected-strict',local,True,True)
assert full==selected==strict
assert report['cases'][1]['metadata']['bind_requests'] < report['cases'][0]['metadata']['bind_requests']
if args.remote:
 remote_selected=run_case('s3-selected',remote,True);remote_full=run_case('s3-full',remote,False)
 assert remote_full==remote_selected==full
print('all equivalent aggregation checks passed',flush=True)
