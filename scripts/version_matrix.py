#!/usr/bin/env python3
"""Resolve isolated DuckOMO build-matrix pairs from frozen input manifests."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Any


COMMIT_SHA_RE = re.compile(r"^[0-9a-f]{40}$")
CONTENT_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
PAIR_RE = re.compile(r"^[A-Za-z0-9._-]+$")
DEFAULT_CMAKE_OPTIONS = {
    "CMAKE_BUILD_TYPE": "Release",
    "BUILD_UNITTESTS": "TRUE",
    "BUILD_SHELL": "TRUE",
    "NATIVE_ARCH": "FALSE",
    "ENABLE_UNITTEST_CPP_TESTS": "FALSE",
    "ENABLE_EXTENSION_AUTOLOADING": "FALSE",
    "ENABLE_EXTENSION_AUTOINSTALL": "FALSE",
}


class MatrixError(RuntimeError):
    pass


def digest_file(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def digest_tree(root: pathlib.Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        relative = path.relative_to(root)
        if any(part in {".git", "build", "__pycache__", "duckdb_unittest_tempdir"}
               for part in relative.parts):
            continue
        digest.update(relative.as_posix().encode())
        digest.update(b"\0")
        digest.update(path.read_bytes())
    return digest.hexdigest()


def extension_source_digest(root: pathlib.Path) -> str:
    digest = hashlib.sha256()
    roots = ["src", "test", "scripts", "third_party/om-file-format"]
    files = ["CMakeLists.txt", "extension_config.cmake", "Makefile", "vcpkg.json"]
    # The matrix's selected pins and other frozen inputs are hashed separately
    # by input_payload(). Its build result is written after a successful build,
    # so hashing the whole file here would make recording that result change
    # the build ID retroactively.
    mutable_results = {"test/data/grids/version-matrix.json"}
    paths: list[pathlib.Path] = []
    for relative in roots:
        candidate = root / relative
        if candidate.exists():
            paths.extend(p for p in candidate.rglob("*") if p.is_file())
    paths.extend(root / relative for relative in files if (root / relative).is_file())
    for path in sorted(set(paths)):
        relative_path = path.relative_to(root)
        if any(part in {".git", "__pycache__", "duckdb_unittest_tempdir"} for part in relative_path.parts):
            continue
        relative = relative_path.as_posix()
        if relative in mutable_results:
            continue
        digest.update(relative.encode())
        digest.update(b"\0")
        digest.update(path.read_bytes())
    return digest.hexdigest()


def load_document(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise MatrixError(f"cannot read valid matrix manifest {path}: {error}") from error
    if not isinstance(value, dict):
        raise MatrixError("matrix manifest root must be a JSON object")
    return value


def git(*args: str, cwd: pathlib.Path | None = None, capture: bool = True) -> str:
    result = subprocess.run(["git", *args], cwd=cwd, check=True, text=True,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else ""


def ensure_checkout(repository: str, commit: str, destination: pathlib.Path, submodules: bool = False) -> None:
    if destination.exists() and not (destination / ".git").exists():
        raise MatrixError(f"refusing to replace non-git source directory: {destination}")
    bootstrap_marker = destination / ".duckomo-matrix-no-checkout"
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", "--no-checkout", repository, str(destination)], check=True)
        bootstrap_marker.write_text("DuckOMO isolated matrix clone awaiting its pinned checkout\n")
    origin = git("remote", "get-url", "origin", cwd=destination)
    if origin.rstrip("/") != repository.rstrip("/"):
        raise MatrixError(f"source checkout {destination} has origin {origin}, expected {repository}")
    if not bootstrap_marker.exists() and git("status", "--porcelain", cwd=destination):
        raise MatrixError(f"source checkout has local changes; refusing to switch pinned input: {destination}")
    try:
        git("cat-file", "-e", f"{commit}^{{commit}}", cwd=destination)
    except subprocess.CalledProcessError:
        subprocess.run(["git", "fetch", "--depth=1", "origin", commit], cwd=destination, check=True)
    git("checkout", "--detach", commit, cwd=destination)
    if submodules:
        subprocess.run(["git", "submodule", "update", "--init", "--recursive", "--depth", "1"],
                       cwd=destination, check=True)
    bootstrap_marker.unlink(missing_ok=True)
    actual = git("rev-parse", "HEAD", cwd=destination)
    if actual != commit:
        raise MatrixError(f"checkout at {destination} is {actual}, expected pinned {commit}")


def find_pair(matrix, pair_id):
    if matrix.get('schema_version') != 2 or not isinstance(matrix.get('pairs'), list):
        raise MatrixError('expected official runtime matrix schema_version=2 and pairs[]')
    if not PAIR_RE.fullmatch(pair_id):
        raise MatrixError('invalid version id')
    matches = [p for p in matrix['pairs'] if p.get('pair_id') == pair_id]
    if len(matches) != 1:
        raise MatrixError('matrix must contain exactly one selected version')
    return matches[0]


def input_payload(matrix, pair, source_sha):
    commits = {}
    for component in ('duckdb', 'om_c', 'extension_ci_tools'):
        item = pair.get(component, {})
        if not COMMIT_SHA_RE.fullmatch(str(item.get('commit', ''))) or not item.get('repository'):
            raise MatrixError(f'missing full pinned {component} commit/repository')
        commits[component] = dict(item)
    options = pair.get('build_options', {}).get('cmake', DEFAULT_CMAKE_OPTIONS)
    if not isinstance(options, dict) or not options:
        raise MatrixError('invalid build_options.cmake')
    runtime = pair.get('official_runtime', {})
    if runtime.get('version') != pair['duckdb'].get('version'):
        raise MatrixError('official HTTPFS/CLI must match the selected engine version')
    if runtime.get('platform') != matrix.get('platform', {}).get('official_extension_platform'):
        raise MatrixError('official runtime platform mismatch')
    for kind in ('cli', 'httpfs'):
        if not CONTENT_SHA256_RE.fullmatch(str(runtime.get(kind+'_sha256', ''))) or not str(runtime.get(kind+'_url', '')).startswith('https://'):
            raise MatrixError(f'official {kind} URL and artifact SHA256 required')
    return dict(schema_version=2, pair_id=pair['pair_id'], commits=commits,
                platform=matrix['platform'], build_options=options,
                extension_source_sha256=source_sha)


def compute_build_id(payload):
    return hashlib.sha256(json.dumps(payload, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def resolve(args):
    root = pathlib.Path(args.root).resolve()
    matrix_path = pathlib.Path(args.matrix).resolve()
    matrix = load_document(matrix_path)
    pair = find_pair(matrix, args.pair)
    payload = input_payload(matrix, pair, extension_source_digest(root))
    build_id = compute_build_id(payload)
    output = pathlib.Path(args.output_root)
    if not output.is_absolute():
        output = root / output
    pair_root = output.resolve() / args.pair
    build_root = pair_root / 'builds' / build_id
    return dict(pair_id=args.pair, build_id=build_id, input_payload=payload,
                matrix_path=str(matrix_path), pair_root=str(pair_root), build_root=str(build_root),
                duckdb_source=str(pair_root/'source/duckdb'), release_dir=str(build_root/'release'),
                build_manifest=str(build_root/'build-manifest.json'),
                official_runtime=pair['official_runtime'], runtime_dir=str(pair_root/'official'))


def fetch_runtime(resolved):
    import gzip
    import zipfile
    runtime = resolved['official_runtime']
    dest = pathlib.Path(resolved['runtime_dir'])
    dest.mkdir(parents=True, exist_ok=True)
    records = {}
    for kind, filename in [('cli', 'duckdb'), ('httpfs', 'httpfs.duckdb_extension')]:
        artifact = dest / filename
        if not artifact.is_file() or digest_file(artifact) != runtime[kind+'_sha256']:
            download = dest / (kind+'.download.tmp')
            result = subprocess.run(['curl', '-fLsS', '--retry', '2', '--max-time', '120',
                                     runtime[kind+'_url'], '-o', str(download)], capture_output=True)
            if result.returncode:
                download.unlink(missing_ok=True)
                raise MatrixError(f'official {kind} artifact unavailable for {runtime["version"]}/{runtime["platform"]}; no source fallback')
            try:
                if kind == 'cli':
                    with zipfile.ZipFile(download) as archive:
                        artifact.write_bytes(archive.read('duckdb'))
                    artifact.chmod(0o755)
                else:
                    artifact.write_bytes(gzip.decompress(download.read_bytes()))
            finally:
                download.unlink(missing_ok=True)
            if digest_file(artifact) != runtime[kind+'_sha256']:
                artifact.unlink()
                raise MatrixError(f'official {kind} SHA256 mismatch')
        records[kind] = dict(path=str(artifact), sha256=digest_file(artifact), url=runtime[kind+'_url'])
    version = subprocess.check_output([records['cli']['path'], '-version'], text=True).strip()
    if not version.startswith(runtime['version']+' '):
        raise MatrixError('official CLI reported unexpected version')
    records['reported_version'] = version
    records['version'] = runtime['version']
    records['platform'] = runtime['platform']
    (dest/'runtime.json').write_text(json.dumps(records, indent=2)+'\n')
    return records


def build_pair(args):
    root = pathlib.Path(args.root).resolve()
    resolved = resolve(args)
    inputs = resolved['input_payload']['commits']
    source = pathlib.Path(resolved['duckdb_source'])
    # Local shared clone avoids downloading the pinned base again; origin still
    # records the official repository and missing versions are fetched by SHA.
    if not source.exists():
        source.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git','clone','--shared','--no-checkout',str(root/'duckdb'),str(source)],check=True)
        git('remote','set-url','origin',inputs['duckdb']['repository'],cwd=source)
        (source/'.duckomo-matrix-no-checkout').write_text('initial checkout\n')
    ensure_checkout(inputs['duckdb']['repository'], inputs['duckdb']['commit'], source, submodules=True)
    for name, relative in [('om_c','third_party/om-file-format'), ('extension_ci_tools','extension-ci-tools')]:
        if git('rev-parse','HEAD',cwd=root/relative) != inputs[name]['commit']:
            raise MatrixError(f'{name} checkout does not match pinned input')
    release = pathlib.Path(resolved['release_dir']).resolve()
    configure = ['cmake','-S',str(source),'-B',str(release),
                 '-DDUCKDB_EXTENSION_CONFIGS='+str(root/'extension_config.cmake'),
                 '-DDUCKOMO_BUILD_DEVELOPER_TOOLS=ON',
                 '-DOVERRIDE_GIT_DESCRIBE='+inputs['duckdb']['version'],
                 '-DUNITTEST_ROOT_DIRECTORY='+str(root), '-DBENCHMARK_ROOT_DIRECTORY='+str(root),
                 '-DDUCKOMO_BUILD_PAIR_ID='+args.pair, '-DDUCKOMO_BUILD_ID='+resolved['build_id'],
                 '-DDUCKOMO_DUCKDB_COMMIT='+inputs['duckdb']['commit'],
                 '-DDUCKOMO_OM_COMMIT='+inputs['om_c']['commit'],
                 '-DDUCKOMO_EXTENSION_CI_TOOLS_COMMIT='+inputs['extension_ci_tools']['commit'],
                 '-DDUCKOMO_MATRIX_PLATFORM='+resolved['input_payload']['platform']['target'],
                 '-DDUCKOMO_CXX_ABI='+resolved['input_payload']['platform']['cxx_abi']]
    configure += [f'-D{k}={v}' for k,v in sorted(resolved['input_payload']['build_options'].items())]
    subprocess.run(configure, cwd=root, check=True)
    targets = ['duckomo_loadable_extension'] if args.extension_only else [
        'shell', 'duckomo_loadable_extension', 'core_functions_loadable_extension',
        'json_loadable_extension', 'unittest', 'duckomo_developer_tools',
    ]
    build = ['cmake','--build',str(release),'--target',*targets,'--parallel',str(args.jobs)]
    subprocess.run(build,cwd=root,check=True)
    extension=release/'extension/duckomo/duckomo.duckdb_extension'
    hashes={'duckomo':digest_file(extension)}
    for name,path in [('duckdb',release/'duckdb'),('unittest',release/'test/unittest')]:
        if path.is_file():hashes[name]=digest_file(path)
    manifest=dict(schema_version=2,status='built (runtime gates pending)',pair_id=args.pair,
                  build_id=resolved['build_id'],release_dir=str(release),inputs=resolved['input_payload'],
                  official_runtime=resolved['official_runtime'],artifact_hashes=hashes,
                  commands=[dict(argv=configure,exit_code=0),dict(argv=build,exit_code=0)])
    pathlib.Path(resolved['build_manifest']).write_text(json.dumps(manifest,indent=2)+'\n')
    pair_root=pathlib.Path(resolved['pair_root']);alias=pair_root/'release'
    if alias.is_symlink():alias.unlink()
    elif alias.exists():raise MatrixError('refusing to replace existing release directory')
    alias.symlink_to(pathlib.Path('builds')/resolved['build_id']/'release')
    (pair_root/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return manifest


def main():
    parser=argparse.ArgumentParser(description='Pinned DuckOMO engine builds and actual official runtime packages')
    sub=parser.add_subparsers(dest='command',required=True)
    for name in ('resolve','fetch-runtime','build'):
        command=sub.add_parser(name)
        command.add_argument('--root',required=True)
        command.add_argument('--matrix',required=True)
        command.add_argument('--pair',required=True)
        command.add_argument('--output-root',default='build/official-matrix')
        if name=='build':
            command.add_argument('--jobs',type=int,default=int(os.environ.get('DUCKOMO_BUILD_JOBS','8')))
            command.add_argument('--extension-only',action='store_true')
    args=parser.parse_args()
    try:
        result=resolve(args) if args.command=='resolve' else fetch_runtime(resolve(args)) if args.command=='fetch-runtime' else build_pair(args)
        print(json.dumps(result,sort_keys=True))
        return 0
    except (MatrixError,OSError,subprocess.CalledProcessError,KeyError,TypeError,ValueError) as error:
        print(f'version matrix: {error}',file=sys.stderr)
        return 1
if __name__=='__main__':
    raise SystemExit(main())
