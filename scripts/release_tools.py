#!/usr/bin/env python3
"""Pinned release runtimes and fail-closed packaging (no GitHub write operations)."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import zipfile

from version_matrix import fetch_runtime

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / 'test/data/release-runtimes.json'
PLATFORMS = {'linux_amd64', 'linux_arm64', 'osx_amd64', 'osx_arm64'}
VERSION_RE = r'v(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def validate_tag(tag):
    if not re.fullmatch(VERSION_RE + r'(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?', tag):
        raise ValueError('release tag must be vMAJOR.MINOR.PATCH with an optional prerelease suffix')


def load_runtimes(path):
    document = json.loads(Path(path).read_text())
    versions, platforms = document['versions'], document['platforms']
    if document['schema_version'] != 1 or not versions or len(set(versions)) != len(versions):
        raise ValueError('invalid runtime manifest versions/schema')
    if any(not re.fullmatch(VERSION_RE, version) for version in versions):
        raise ValueError('stable DuckDB versions required')
    if len(platforms) != len(PLATFORMS) or {p['platform'] for p in platforms} != PLATFORMS:
        raise ValueError('release platforms must be Linux/macOS x86_64/ARM64')
    if any(not p['runner'] for p in platforms):
        raise ValueError('native runtime runner required')
    expected = {(v, p) for v in versions for p in PLATFORMS}
    actual = [(r['version'], r['platform']) for r in document['runtimes']]
    if len(actual) != len(expected) or set(actual) != expected:
        raise ValueError('missing, duplicate or unexpected runtime pair')
    for runtime in document['runtimes']:
        version, platform = runtime['version'], runtime['platform']
        cli_url = f'https://github.com/duckdb/duckdb/releases/download/{version}/duckdb_cli-{platform.replace("_", "-")}.zip'
        httpfs_url = f'https://extensions.duckdb.org/{version}/{platform}/httpfs.duckdb_extension.gz'
        if runtime['cli_url'] != cli_url or runtime['httpfs_url'] != httpfs_url:
            raise ValueError('runtime URL must identify the matching official version/platform')
        for kind in ('cli', 'httpfs'):
            if not re.fullmatch(r'[0-9a-f]{64}', runtime[kind + '_sha256']):
                raise ValueError('official artifact SHA256 required')
    return document


def find_runtime(document, version, platform):
    matches = [r for r in document['runtimes'] if (r['version'], r['platform']) == (version, platform)]
    if len(matches) != 1:
        raise ValueError(f'expected one official runtime for {version}/{platform}')
    return matches[0]


def runtime_matrix(document):
    return {'include': [dict(version=v, **p) for v in document['versions'] for p in document['platforms']]}


def archive_name(tag, version, platform):
    validate_tag(tag)
    return f'duckomo-{tag}-duckdb-{version}-{platform}.zip'


def gz_asset_name(tag, version, platform):
    validate_tag(tag)
    return f'duckomo-{tag}-duckdb-{version}-{platform}.duckdb_extension.gz'


def check_report(document, report, version, platform, extension_sha256):
    runtime = find_runtime(document, version, platform)
    if report.get('status') != 'pass' or report.get('version') != version or report.get('platform') != platform:
        raise ValueError('passing verification for the exact version/platform required')
    expected = dict(extension=extension_sha256, duckdb=runtime['cli_sha256'], httpfs=runtime['httpfs_sha256'])
    if report.get('artifact_hashes') != expected:
        raise ValueError('verified binary/runtime hashes do not match package inputs')


def check_identity(tag, commit):
    validate_tag(tag)
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('full source commit SHA required')


def package(document, version, platform, tag, commit, extension, verification, license_path, output):
    check_identity(tag, commit)
    binary = Path(extension).read_bytes()
    if not binary:
        raise ValueError('empty extension binary')
    digest = hashlib.sha256(binary).hexdigest()
    report = json.loads(Path(verification).read_text())
    check_report(document, report, version, platform, digest)
    manifest = dict(schema_version=1, tag=tag, source_commit=commit, duckdb_version=version,
                    platform=platform, signed=False, extension_sha256=digest,
                    validation='official runtime local/HTTP range smoke; not full grid acceptance')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    archive = output / archive_name(tag, version, platform)
    gz_asset = output / gz_asset_name(tag, version, platform)
    # Exclusive creation prevents stale reruns silently replacing an existing package.
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as z:
        for name, content in [('duckomo.duckdb_extension', binary), ('manifest.json', json.dumps(manifest, indent=2) + '\n'),
                              ('verification.json', json.dumps(report, indent=2) + '\n'), ('LICENSE', Path(license_path).read_bytes())]:
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            z.writestr(info, content)
    # A deterministic gzip of the verified binary lets DuckDB INSTALL straight
    # from the release URL (DuckDB accepts .duckdb_extension.gz over HTTP).
    gz_asset.write_bytes(gzip.compress(binary, compresslevel=9, mtime=0))
    return archive


def assemble(document, tag, commit, packages, output):
    check_identity(tag, commit)
    expected = {archive_name(tag, r['version'], r['platform']): r for r in document['runtimes']}
    expected_gz = {name: gz_asset_name(tag, r['version'], r['platform']) for name, r in expected.items()}
    archives = list(Path(packages).rglob('*.zip'))
    gz_assets = list(Path(packages).rglob('*.duckdb_extension.gz'))
    if len(archives) != len(expected) or {a.name for a in archives} != set(expected):
        raise ValueError('exactly one package for every runtime pair required')
    if len(gz_assets) != len(expected_gz) or {a.name for a in gz_assets} != set(expected_gz.values()):
        raise ValueError('exactly one .duckdb_extension.gz asset for every runtime pair required')
    gz_by_name = {a.name: a for a in gz_assets}
    records = []
    for archive in sorted(archives):
        runtime = expected[archive.name]
        with zipfile.ZipFile(archive) as z:
            required = {'duckomo.duckdb_extension', 'manifest.json', 'verification.json', 'LICENSE'}
            if len(z.namelist()) != len(required) or set(z.namelist()) != required:
                raise ValueError('unexpected package contents')
            binary_digest = hashlib.sha256(z.read('duckomo.duckdb_extension')).hexdigest()
            manifest = json.loads(z.read('manifest.json'))
            required_identity = dict(schema_version=1, tag=tag, source_commit=commit,
                                     duckdb_version=runtime['version'], platform=runtime['platform'],
                                     signed=False, extension_sha256=binary_digest)
            if any(manifest.get(k) != v for k, v in required_identity.items()):
                raise ValueError('package provenance does not match the release')
            check_report(document, json.loads(z.read('verification.json')), runtime['version'],
                         runtime['platform'], binary_digest)
        gz_asset = gz_by_name[expected_gz[archive.name]]
        if hashlib.sha256(gzip.decompress(gz_asset.read_bytes())).hexdigest() != binary_digest:
            raise ValueError('gz install asset does not match the verified extension binary')
        records.append(dict(file=archive.name, sha256=sha(archive), gz_file=gz_asset.name,
                            gz_sha256=sha(gz_asset), **required_identity))
    output = Path(output)
    if output.exists() and any(output.iterdir()):
        raise ValueError('release output must be empty to exclude stale assets')
    output.mkdir(parents=True, exist_ok=True)
    for archive in archives:
        shutil.copyfile(archive, output / archive.name)
    for gz_asset in gz_assets:
        shutil.copyfile(gz_asset, output / gz_asset.name)
    (output / 'manifest.json').write_text(json.dumps(dict(schema_version=1, tag=tag, source_commit=commit,
                                                       signed=False, packages=records), indent=2) + '\n')
    files = sorted(output.iterdir())
    (output / 'SHA256SUMS').write_text(''.join(f'{sha(p)}  {p.name}\n' for p in files))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=DEFAULT_MANIFEST)
    commands = parser.add_subparsers(dest='command', required=True)
    matrix = commands.add_parser('matrix')
    matrix.add_argument('--tag', required=True)
    fetch = commands.add_parser('fetch-runtime')
    pack = commands.add_parser('package')
    collect = commands.add_parser('assemble')
    for command in (fetch, pack):
        command.add_argument('--version', required=True)
        command.add_argument('--platform', required=True)
        command.add_argument('--output', type=Path, required=True)
    for command in (pack, collect):
        command.add_argument('--tag', required=True)
        command.add_argument('--commit', required=True)
    for name in ('extension', 'verification'):
        pack.add_argument('--' + name, type=Path, required=True)
    pack.add_argument('--license', type=Path, default=ROOT / 'LICENSE')
    collect.add_argument('--packages', type=Path, required=True)
    collect.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    document = load_runtimes(args.manifest)
    if args.command == 'matrix':
        validate_tag(args.tag)
        print('versions=' + json.dumps(document['versions'], separators=(',', ':')))
        print('runtime=' + json.dumps(runtime_matrix(document), separators=(',', ':')))
    elif args.command == 'fetch-runtime':
        fetch_runtime(dict(runtime_dir=str(args.output), official_runtime=find_runtime(document, args.version, args.platform)))
    elif args.command == 'package':
        print(package(document, args.version, args.platform, args.tag, args.commit,
                      args.extension, args.verification, args.license, args.output))
    else:
        assemble(document, args.tag, args.commit, args.packages, args.output)


if __name__ == '__main__':
    main()
