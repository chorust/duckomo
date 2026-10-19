"""Validate retained, owner-attested independent N7 execution evidence."""
import argparse
import hashlib
import json
from pathlib import Path

EVIDENCE = Path(__file__).resolve().parents[1] / 'specs/006-region-nearest-sampling/evidence'
REQUIRED_FILES = {'identity.txt', 'executed.sql', 'output.txt', 'exit-code.txt', 'SHA256SUMS'}

MARKERS = ('VERSION_PASS', 'FRESH_PROCESS_PASS', 'RAW_RECTANGLE_PASS', 'POINTS_PASS',
           'CARDINALITY_STATES_PASS', 'SOURCE_AND_NULL_PASS', 'EXACT_SOURCE_PARITY_PASS',
           'DISTANCE_REJECT_PASS', 'DISTANCE_ACCEPT_PASS', 'N7_FUNCTIONAL_PASS')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit_n7(package: Path, reference: Path) -> dict:
    manifest = json.loads((package / 'manifest.json').read_text())
    if (manifest.get('schema_version') != 1 or manifest.get('status') != 'pass' or
            manifest.get('executor_type') != 'human' or not manifest.get('execution_attested_by') or
            manifest.get('fresh_process_verified') is not True or manifest.get('exit_code') != 0):
        raise ValueError('N7 requires attested independent human execution in a fresh process with exit 0')
    files = manifest.get('files', {})
    if set(files) != REQUIRED_FILES:
        raise ValueError('N7 complete identity/SQL/output/exit/checksum inventory required')
    for name, expected in files.items():
        path = package / name
        if not path.is_file() or digest(path) != expected:
            raise ValueError(f'N7 evidence hash mismatch: {name}')
    if (package / 'exit-code.txt').read_text().strip() != '0':
        raise ValueError('N7 execution failed')
    if digest(package / 'executed.sql') != digest(reference):
        raise ValueError('N7 executed SQL differs from reviewed acceptance script')
    output = (package / 'output.txt').read_text()
    if any(marker not in output for marker in MARKERS):
        raise ValueError('N7 required functional assertion marker missing')
    identity = (package / 'identity.txt').read_text()
    if 'executor_type=human' not in identity or 'v1.5.5' not in identity:
        raise ValueError('N7 executor/runtime identity missing')
    expected = json.loads((EVIDENCE / 'private-n0-n5-final/inputs.json').read_text())
    build = expected['build_identities']['v1.5.5']
    runtime = json.loads((reference.parents[4] / 'test/data/sampling/runtime-matrix.json').read_text())
    pinned = next(row for row in runtime['runtimes'] if row['version'] == 'v1.5.5')
    hashes = (pinned['duckdb_cli']['sha256'], pinned['spatial_package']['sha256'],
              build['artifacts']['extension/duckomo/duckomo.duckdb_extension'], digest(reference))
    if any(value not in identity for value in hashes):
        raise ValueError('N7 runtime/artifact identities differ from accepted execution snapshot')
    return {'status': 'pass', 'reason': 'independent human fresh-process execution, matching accepted '
            'runtime/artifact identities, retained SQL/output hashes and all functional assertions verified',
            'commands': [], 'executor_type': 'human', 'package_manifest_sha256': digest(package / 'manifest.json')}


def package_n7(package: Path, attested_by: str) -> None:
    if not attested_by.strip():
        raise ValueError('N7 requires the independent human executor to provide an attestation name')
    manifest = {
        'schema_version': 1, 'status': 'pass', 'executor_type': 'human',
        'execution_attested_by': attested_by.strip(), 'fresh_process_verified': True,
        'exit_code': int((package / 'exit-code.txt').read_text()),
        'files': {name: digest(package / name) for name in sorted(REQUIRED_FILES)},
    }
    path = package / 'manifest.json'
    # Never replace an existing attestation when packaging retained evidence.
    with path.open('x') as stream:
        json.dump(manifest, stream, indent=2)
        stream.write('\n')
    try:
        audit_n7(package, EVIDENCE / 'user-manual-v1.5.5/full-acceptance.sql')
    except (ValueError, OSError, KeyError):
        path.unlink()
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Package an independent human N7 execution and audit it.')
    parser.add_argument('package', type=Path)
    parser.add_argument('--attested-by', required=True,
                        help='Name of the human attesting they independently executed this run')
    args = parser.parse_args()
    package_n7(args.package, args.attested_by)
