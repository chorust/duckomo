"""Private-input N5 execution and N6 evidence auditing for sampling validation."""
from __future__ import annotations

import csv
import hashlib
import io
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import urllib.request

VERSIONS = ("v1.5.4", "v1.5.5", "v1.5.6")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            value.update(block)
    return value.hexdigest()


def literal(value: object) -> str:
    return "'" + str(value).replace("'", "''") + "'"


def private_env(path: Path) -> dict[str, str]:
    result = {}
    for line in path.read_text().splitlines():
        parts = shlex.split(line)
        if parts and parts[0] == 'export':
            parts = parts[1:]
        if len(parts) == 1 and '=' in parts[0]:
            key, value = parts[0].split('=', 1)
            result[key] = value
    return result


def verify_private_archive(path: Path, expected_hash: str, expected_size: int) -> dict:
    record = json.loads(path.read_text())
    archive = Path(record['private_archive'])
    if (record.get('public_redistribution') is not False or not record.get('retrieval_verified') or
            record.get('sha256') != expected_hash or record.get('retrieval_sha256') != expected_hash or
            record.get('size_bytes') != expected_size or archive.stat().st_size != expected_size or
            digest(archive) != expected_hash):
        raise ValueError('private archive identity/retrieval verification failed')
    if archive.stat().st_mode & 0o077:
        raise ValueError('private archive must not be group/world accessible')
    return {'sha256': expected_hash, 'size_bytes': expected_size, 'retrieval_verified': True,
            'manifest_sha256': digest(path), 'public_redistribution': False,
            'retention': record.get('retention')}


def semantic_rows(rows: list[dict]) -> list[dict]:
    return sorted(({key: value for key, value in row.items() if key != 'object_id'} for row in rows),
                  key=lambda row: json.dumps(row, sort_keys=True))


def run_n5(args, root: Path, builds: dict, runtimes: dict, output: Path) -> dict:
    required = ('real_file', 'real_regions', 'remote_env', 'private_archive')
    if any(getattr(args, key, None) is None for key in required):
        raise ValueError('N5 requires --real-file, --real-regions, --remote-env and --private-archive')
    inventory = json.loads((root / 'test/data/sampling/real-inputs.json').read_text())
    fixed = next(item for item in inventory['inputs'] if item['id'] == 'gfs025-2026-10-14-0000')
    geometry = next(item for item in inventory['inputs'] if item['id'] == 'dongcheng-geoboundaries-2017')
    authorization = fixed.get('private_use_authorization', {})
    if any(authorization.get(key) != 'allowed' for key in ('internal_validation', 'private_retention')):
        raise ValueError('owner authorization for private validation/retention is missing')
    if args.real_file.stat().st_size != fixed['size_bytes'] or digest(args.real_file) != fixed['sha256']:
        raise ValueError('N5 real-file identity mismatch')
    if digest(args.real_regions) != geometry['sha256']:
        raise ValueError('N5 real-regions identity mismatch')
    archive = verify_private_archive(args.private_archive, fixed['sha256'], fixed['size_bytes'])
    env = private_env(args.remote_env)
    for key in ('DUCKOMO_HTTP_BASE', 'DUCKOMO_HTTPS_BASE', 'DUCKOMO_HTTPS_CA_CERT', 'DUCKOMO_S3_BASE', 'DUCKOMO_S3_SETUP'):
        if key not in env:
            raise ValueError(f'N5 controlled transport configuration missing {key}')
    from urllib.parse import urlsplit
    for key in ('DUCKOMO_HTTP_BASE', 'DUCKOMO_HTTPS_BASE'):
        if urlsplit(env[key]).hostname != '127.0.0.1':
            raise ValueError('private N5 fixture service must bind to loopback')
    service = json.loads((args.remote_env.parent / 'run-manifest.json').read_text())
    if not service.get('authorized_s3_bucket_match') or service.get('s3_audit_bind') != '127.0.0.1':
        raise ValueError('N5 requires an authorized loopback S3 fixture')
    if not service.get('s3_service_identity_independently_verified'):
        raise ValueError('N5 requires independently verified S3 service image identity')
    # Credentials are read privately into a temporary process input, never into
    # retained SQL, argv, logs or evidence. Only sanitized diagnostics persist.
    secret_sql = Path(env['DUCKOMO_S3_SETUP']).read_text()
    expected_httpfs = json.loads((root / 'test/data/grids/version-matrix.json').read_text())
    pairs = {pair['pair_id']: pair for pair in expected_httpfs['pairs']}
    records, outputs, comparisons = [], [], {}
    real_geometry = json.loads(args.real_regions.read_text())['features'][0]['geometry']
    synthetic_geometry = [
        "{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((100.1 20.1,100.2 20.1,100.2 20.2,100.1 20.2,100.1 20.1))'))}",
        "{'id':'Empty','wkb':ST_AsWKB(ST_GeomFromText('POLYGON EMPTY'))}",
        "{'id':'Outside','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((110 30,111 30,111 31,110 31,110 30))'))}",
    ]
    # The fixture uploader serves every *.om file in its configured directory;
    # verify that regions.om is actually the pinned synthetic input.
    synthetic = root / 'test/data/regions/regions.om'
    fixture_identity = service.get('fixtures', {}).get('duckomo/regions.om', {})
    if fixture_identity.get('sha256') != digest(synthetic):
        raise ValueError('controlled fixture must include the pinned regions.om synthetic object')

    def execute(version, label, query, *, remote=False, expected_error=False):
        runtime = runtimes[version]
        build = builds[version]
        spatial = Path(runtime['spatial_package']['path'])
        httpfs = root / f'build/regions-runtime/{version}/official/httpfs.duckdb_extension'
        # Frozen runtime package pins cover the HTTPFS binary too.
        package = pairs[version]['official_runtime']
        if digest(httpfs) != package['httpfs_sha256']:
            raise ValueError(f'{version} HTTPFS identity mismatch')
        prefix = f"LOAD {literal(build / 'extension/duckomo/duckomo.duckdb_extension')};\nLOAD {literal(spatial)};\n"
        if remote:
            prefix += (f"LOAD {literal(httpfs)};\nSET ca_cert_file={literal(env['DUCKOMO_HTTPS_CA_CERT'])};\n" + secret_sql + '\n')
        sql_path = output / f'N5-{version}-{label}.sql'
        sql_path.write_text(prefix.replace(secret_sql, '-- Private S3 setup supplied via stdin.') + query)
        process_env = {key: value for key, value in os.environ.items()
                       if key.lower() not in {'http_proxy', 'https_proxy', 'all_proxy'}}
        process = subprocess.run([runtime['duckdb_cli']['path'], '-unsigned', '-bail', '-csv'], cwd=root,
                                 env=process_env, input=prefix + query, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        # Failed SQL can echo secret statements. Never persist raw stderr.
        if expected_error:
            content = 'query_failed=' + str(process.returncode != 0).lower() + '\n'
        else:
            if process.returncode:
                raise ValueError(f'{version} {label} failed; private diagnostic withheld, exit={process.returncode}')
            # CREATE SECRET emits its own result table before the final query.
            content = process.stdout
            if content.startswith('Success\ntrue\n'):
                content = content[len('Success\ntrue\n'):]
        path = output / f'N5-{version}-{label}.csv'
        path.write_text(content)
        outputs.append({'path': path.name, 'sha256': digest(path)})
        records.append({'version': version, 'label': label, 'exit_code': process.returncode,
                        'expected_error': expected_error, 'output_path': path.name,
                        'output_sha256': digest(path), 'sql_path': sql_path.name, 'sql_sha256': digest(sql_path),
                        'cli_sha256': digest(Path(runtime['duckdb_cli']['path'])),
                        'httpfs_sha256': digest(httpfs), 'spatial_sha256': digest(spatial),
                        'extension_sha256': digest(build / 'extension/duckomo/duckomo.duckdb_extension')})
        if expected_error:
            if process.returncode == 0:
                raise ValueError('denied HTTP query unexpectedly succeeded')
            return []
        return list(csv.DictReader(io.StringIO(content)))

    for version in VERSIONS:
        comparisons[version] = {}
        for kind in ('real', 'synthetic'):
            local = args.real_file if kind == 'real' else synthetic
            name = 'real.om' if kind == 'real' else 'regions.om'
            sources = {'local': str(local), 'http': env['DUCKOMO_HTTP_BASE'] + '/' + name,
                       'https': env['DUCKOMO_HTTPS_BASE'] + '/' + name,
                       's3': env['DUCKOMO_S3_BASE'] + '/' + name}
            if kind == 'real':
                options = ("domain:='ncep_gfs025', variable:='temperature_80m', regions:=[{'id':'Dongcheng',"
                           "'wkb':ST_AsWKB(ST_GeomFromGeoJSON(" + literal(json.dumps(real_geometry)) + "))}], "
                           "region_crs:='grid_lonlat'")
                value = 'temperature_80m'
            else:
                options = ("regions:=[" + ','.join(synthetic_geometry) + "], region_crs:='grid_lonlat', "
                           "dimensions:=['member','time','lat','lon'], "
                           "grid:={'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
                           "spatial_axes:=['lat','lon'], axes:={"
                           "'time':{'axis':'time','start':TIMESTAMP '2026-01-01','step':INTERVAL '1 hour'},"
                           "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}}")
                value = 'value'
            baseline = None
            for transport, source in sources.items():
                scan = f'read_om_regions({literal(source)}, {options}'
                count = execute(version, f'{kind}-{transport}-points', f"SELECT count(*) AS n FROM {scan},method:='points');",
                                remote=transport != 'local')
                if count != [{'n': '0'}]:
                    raise ValueError(f'{kind} points expected zero rows')
                fields = ("region_id,lat,lon,sampling.status AS geometry_status,sampling.source_lat,sampling.source_lon,"
                          "sampling.distance_m,sampling.source_point_index,sampling.axis_indices,"
                          f"sampling.variables[{literal(value)}].status AS value_status,{value},"
                          "om_source.logical_index,om_source.point_index,om_source.object_id,"
                          "om_source.grid_id,om_source.layout_id")
                rows = execute(version, f'{kind}-{transport}-nearest',
                               f"SELECT {fields} FROM {scan},method:='nearest',include_source:=true) ORDER BY ALL;",
                               remote=transport != 'local')
                if kind == 'real':
                    if (len(rows) != 1 or rows[0]['source_lon'] != '116.5' or rows[0]['source_lat'] != '40.0' or
                            rows[0]['source_point_index'] != '749986' or rows[0]['geometry_status'] != 'ok'):
                        raise ValueError('Dongcheng nearest identity differs from the frozen case')
                elif len(rows) != 18:
                    raise ValueError('synthetic nearest must return 18 rows')
                semantic = semantic_rows(rows)
                if baseline is None:
                    baseline = semantic
                if semantic != baseline:
                    raise ValueError(f'{version} {kind} {transport} differs from local')
            comparisons[version][kind] = {'transports_match': True, 'rows': len(baseline), 'semantic_rows': baseline}
        direct = execute(version, 'real-direct',
                         f"SELECT temperature_80m,om_source.logical_index,om_source.point_index FROM read_om("
                         f"{literal(args.real_file)},domain:='ncep_gfs025',variable:='temperature_80m',include_source:=true) "
                         "WHERE lon=116.5 AND lat=40.0;")
        nearest = comparisons[version]['real']['semantic_rows'][0]
        if (len(direct) != 1 or any(direct[0][key] != nearest[key]
                                  for key in ('temperature_80m', 'logical_index', 'point_index'))):
            raise ValueError('real nearest differs from direct original record')
        # Compare original FLOAT values in SQL, rather than rounded CSV text.
        exact = execute(version, 'real-exact-source',
                        f"SELECT count(*) AS differences FROM ((SELECT temperature_80m FROM read_om_regions("
                        f"{literal(args.real_file)},"
                        "regions:=[{'id':'Dongcheng','wkb':ST_AsWKB(ST_GeomFromGeoJSON(" + literal(json.dumps(real_geometry)) + "))}],"
                        "region_crs:='grid_lonlat',domain:='ncep_gfs025',variable:='temperature_80m',method:='nearest') "
                        "EXCEPT ALL SELECT temperature_80m FROM read_om(" + literal(args.real_file) +
                        ",domain:='ncep_gfs025',variable:='temperature_80m') WHERE lon=116.5 AND lat=40.0) "
                        "UNION ALL (SELECT temperature_80m FROM read_om(" + literal(args.real_file) +
                        ",domain:='ncep_gfs025',variable:='temperature_80m') WHERE lon=116.5 AND lat=40.0 "
                        "EXCEPT ALL SELECT temperature_80m FROM read_om_regions(" + literal(args.real_file) +
                        ",regions:=[{'id':'Dongcheng','wkb':ST_AsWKB(ST_GeomFromGeoJSON(" + literal(json.dumps(real_geometry)) + "))}],"
                        "region_crs:='grid_lonlat',domain:='ncep_gfs025',variable:='temperature_80m',method:='nearest'))) d;")
        if exact != [{'differences': '0'}]:
            raise ValueError('exact source-value comparison failed')
        key = 'n5-' + version
        control = env['DUCKOMO_HTTP_BASE'] + '/__control'
        def set_state(state):
            request = urllib.request.Request(control, data=json.dumps({'state_key': key, 'state': state,
                                             'object': 'real.om'}).encode(), headers={'Content-Type': 'application/json'})
            with urllib.request.urlopen(request, timeout=10) as response:
                if response.status != 204:
                    raise ValueError('HTTP fixture control failed')
        denied_uri = env['DUCKOMO_HTTP_BASE'] + '/real.om?state_key=' + key
        query = (f"SELECT sum(temperature_80m) FROM read_om({literal(denied_uri)},domain:='ncep_gfs025',"
                 "variable:='temperature_80m') WHERE lon=116.5 AND lat=40.0;")
        set_state('denied')
        try:
            execute(version, 'http-denied', query, remote=True, expected_error=True)
        finally:
            set_state('normal')
        recovered = execute(version, 'http-recovered', query, remote=True)
        if len(recovered) != 1:
            raise ValueError('HTTP recovery failed')
    for kind in ('real', 'synthetic'):
        baseline = comparisons[VERSIONS[0]][kind]['semantic_rows']
        if any(comparisons[version][kind]['semantic_rows'] != baseline for version in VERSIONS):
            raise ValueError('cross-version semantic rows differ')
    logs = []
    for name in ('http', 's3'):
        source = Path(service['log_directory']) / (name + '.jsonl')
        # Only the known sanitized server log schema is retained.
        rows = [json.loads(line) for line in source.read_text().splitlines()]
        if not rows:
            raise ValueError('controlled service logs are empty')
        path = output / f'N5-{name}-service.jsonl'
        allowed = {'method', 'path', 'status', 'range', 'body_bytes', 'bytes_sent', 'timestamp', 'event',
                   'request_id', 'content_length', 'response_bytes', 'range_start', 'range_end'}
        path.write_text(''.join(json.dumps({k: v for k, v in row.items() if k in allowed}, sort_keys=True) + '\n' for row in rows))
        logs.append({'path': path.name, 'sha256': digest(path), 'records': len(rows)})
    return {'status': 'pass', 'reason': 'private fixed-input three-version local/HTTP/HTTPS/signed-S3 '
            'real and synthetic semantics, exact source parity and HTTP denial/recovery passed',
            'commands': records, 'outputs': outputs, 'service_logs': logs, 'archive': archive,
            'real_input_sha256': fixed['sha256'], 'geometry_sha256': geometry['sha256'],
            'comparisons': comparisons, 'service_image_digest': service['s3_service_digest'],
            'fixture_manifest_sha256': digest(args.remote_env.parent / 'run-manifest.json'),
            'authorization_inventory_sha256': digest(root / 'test/data/sampling/real-inputs.json'),
            'transport_body_scope': 'server application logs only; HTTPFS network body remains unknown'}


def audit_n6(package: Path, expected_source: str, identities: dict, runtimes: dict | None = None) -> dict:
    manifest = json.loads((package / 'manifest.json').read_text())
    inputs_path = package / manifest['inputs_path']
    if digest(inputs_path) != manifest['inputs_sha256']:
        raise ValueError('N6 inputs hash mismatch')
    inputs = json.loads(inputs_path.read_text())
    if digest(package / manifest['commands_path']) != manifest['commands_sha256']:
        raise ValueError('N6 commands hash mismatch')
    captured = inputs.get('build_identities', {})
    if set(captured) != set(VERSIONS):
        raise ValueError('N6 requires three runtime build identities')
    for version in VERSIONS:
        if captured[version]['extension_source_sha256'] != expected_source or captured[version] != identities[version]:
            raise ValueError('N6 source/artifact identity drift')
    executable_hashes = {str((Path(identity['build']) / relative).resolve()): value
                         for identity in identities.values()
                         for relative, value in identity['artifacts'].items()}
    for runtime in (runtimes or {}).values():
        cli = runtime['duckdb_cli']
        executable_hashes[str(Path(cli['path']).resolve())] = cli['sha256']
    manifest_hashes = {version: identity['manifest_sha256'] for version, identity in identities.items()}
    for gate in ('N0', 'N1', 'N2', 'N3', 'N4', 'N5'):
        result = manifest['gates'].get(gate, {})
        if result.get('status') != 'pass' or not result.get('commands'):
            raise ValueError(f'N6 missing passing executed {gate}')
        for command in result['commands']:
            if gate != 'N5':
                if command.get('build_manifest_sha256') != manifest_hashes:
                    raise ValueError(f'N6 build manifest identity mismatch in {gate}')
                executable = command['argv'][0]
                path = Path(shutil.which(executable) or executable)
                if not path.is_absolute():
                    path = Path(command['cwd']) / path
                expected = executable_hashes.get(str(path.resolve()))
                if expected is None:
                    ctest = shutil.which('ctest')
                    if ctest is None or path.resolve() != Path(ctest).resolve():
                        raise ValueError(f'N6 executable is not a verified runtime artifact in {gate}')
                    expected = digest(path)
                if command.get('executable_sha256') != expected:
                    raise ValueError(f'N6 executable identity mismatch in {gate}')
            code = command['exit_code']
            if (code != 0) != bool(command.get('expected_error', False)):
                raise ValueError(f'N6 invalid command result in {gate}')
            relative = Path(command['output_path'])
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('N6 unsafe evidence path')
            if digest(package / command['output_path']) != command['output_sha256']:
                raise ValueError(f'N6 output hash mismatch in {gate}')
            if 'sql_path' in command and digest(package / command['sql_path']) != command['sql_sha256']:
                raise ValueError('N6 SQL hash mismatch')
    n0 = manifest['gates']['N0']
    comparisons = n0.get('runtime_comparisons', {})
    if set(comparisons) != set(VERSIONS):
        raise ValueError('N6 incomplete N0 replay evidence')
    for version, comparison in comparisons.items():
        sql = f'N0-points-{version}.sql'
        if not any(command.get('sql_path') == sql and command.get('sql_sha256')
                   for command in n0['commands']):
            raise ValueError('N6 missing N0 replay SQL evidence')
        for kind in ('rows', 'metrics'):
            relative = Path(comparison[f'{kind}_path'])
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('N6 unsafe evidence path')
            if digest(package / relative) != comparison[f'actual_{kind}_sha256']:
                raise ValueError(f'N6 N0 {kind} hash mismatch')
    n5 = manifest['gates']['N5']
    if set(n5['comparisons']) != set(VERSIONS) or not n5['archive'].get('retrieval_verified'):
        raise ValueError('N6 incomplete N5 runtime/archive evidence')
    commands = [json.loads(line) for line in (package / manifest['commands_path']).read_text().splitlines()]
    expected_commands = [command for gate in ('N0', 'N1', 'N2', 'N3', 'N4')
                         for command in manifest['gates'][gate]['commands']]
    if sorted(json.dumps(row, sort_keys=True) for row in commands) != sorted(
            json.dumps(row, sort_keys=True) for row in expected_commands):
        raise ValueError('N6 command stream differs from gate records')
    for version in VERSIONS:
        records = [row for row in n5['commands'] if row.get('version') == version]
        expected_labels = {f'{kind}-{transport}-{method}' for kind in ('real', 'synthetic')
                           for transport in ('local', 'http', 'https', 's3') for method in ('points', 'nearest')}
        expected_labels.update({'real-direct', 'real-exact-source', 'http-denied', 'http-recovered'})
        if {row.get('label') for row in records} != expected_labels or len(records) != len(expected_labels):
            raise ValueError('N6 incomplete N5 transport/query inventory')
        for row in records:
            artifact = captured[version]['artifacts']['extension/duckomo/duckomo.duckdb_extension']
            if row['extension_sha256'] != artifact:
                raise ValueError('N6 N5 extension identity mismatch')
    for item in n5['service_logs']:
        if digest(package / item['path']) != item['sha256']:
            raise ValueError('N6 service log mismatch')
    return {'status': 'pass', 'reason': 'N0–N5 executed evidence, current three-version source/artifact '
            'identities, SQL/output/log hashes and private archive retrieval audited',
            'commands': [], 'package_manifest_sha256': digest(package / 'manifest.json'),
            'runtime_versions': list(VERSIONS)}
