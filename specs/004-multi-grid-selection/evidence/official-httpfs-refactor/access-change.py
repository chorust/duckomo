#!/usr/bin/env python3
"""Fixed same-URI access transition using the existing controlled fixture service."""
import argparse,importlib.util,json,urllib.request,uuid
from pathlib import Path
p=argparse.ArgumentParser()
for name in ('duckdb','httpfs','extension','output'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--http-base',required=True);p.add_argument('--root',type=Path,default=Path.cwd())
a=p.parse_args();a.root=a.root.resolve();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
a.setup=''
spec=importlib.util.spec_from_file_location('official',a.root/'scripts/validate-official-httpfs.py');v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
state_key='official-access-'+uuid.uuid4().hex
opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def state(value):
 request=urllib.request.Request(a.http_base+'/__control',data=json.dumps({'state_key':state_key,'state':value,'object':'raw.om'}).encode(),headers={'Content-Type':'application/json'},method='POST')
 with opener.open(request,timeout=5) as response:assert response.status==204
uri=a.http_base+'/raw.om?state_key='+state_key;query='SELECT * FROM read_om('+v.literal(uri)+') ORDER BY ALL'
try:
 state('normal')
 with v.CliSession(a) as session:
  one=v.run_scan(a,'allowed',query,True,session=session)
  state('denied');denied=v.run_scan(a,'denied',query,True,session=session,expect_error=True)
  state('normal');two=v.run_scan(a,'restored',query,True,session=session)
  assert one['result_sha256']==two['result_sha256']
 (a.output/'summary.json').write_text(json.dumps({'status':'pass','scope':'same URI, same CLI connection; controlled service access transitions; no general cache revocation promise','records':[one,denied,two]},indent=2)+'\n')
 print('same-URI access changes: pass')
finally:state('normal')
