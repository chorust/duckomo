#!/usr/bin/env python3
"""Reject incomplete or altered N6 packages without rerunning remote services."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from sampling_remote import audit_n6, digest, VERSIONS, verify_private_archive


class SamplingAuditTests(unittest.TestCase):
    def test_missing_or_mutated_evidence_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifact = 'extension/duckomo/duckomo.duckdb_extension'
            identities = {v: {'extension_source_sha256': 'source', 'build': str(root / v), 'manifest_sha256': v,
                              'artifacts': {artifact: 'extension', 'test/unittest': 'runner'}}
                          for v in VERSIONS}
            (root / 'inputs.json').write_text(json.dumps({'build_identities': identities}))
            (root / 'result.txt').write_text('passed\n')
            command = {'argv': [str(root / VERSIONS[0] / 'test/unittest')],
                       'executable_sha256': 'runner',
                       'build_manifest_sha256': {v: v for v in VERSIONS}, 'exit_code': 0, 'output_path': 'result.txt', 'output_sha256': digest(root / 'result.txt')}
            gates = {f'N{i}': {'status': 'pass', 'commands': [command]} for i in range(5)}
            gates['N0']['commands'] = []
            gates['N0']['runtime_comparisons'] = {}
            for version in VERSIONS:
                sql = root / f'N0-points-{version}.sql'
                sql.write_text('COPY replay;')
                gates['N0']['commands'].append({**command, 'sql_path': sql.name, 'sql_sha256': digest(sql)})
                comparison = {}
                for kind in ('rows', 'metrics'):
                    path = root / f'N0-points-{kind}-{version}.csv'
                    path.write_text('replay results')
                    comparison[f'{kind}_path'] = path.name
                    comparison[f'actual_{kind}_sha256'] = digest(path)
                gates['N0']['runtime_comparisons'][version] = comparison
            stream = [row for gate in gates.values() for row in gate['commands']]
            (root / 'commands.jsonl').write_text(''.join(json.dumps(c) + '\n' for c in stream))
            n5 = {'status': 'pass', 'commands': [], 'archive': {'retrieval_verified': True},
                  'comparisons': {v: {} for v in VERSIONS}, 'service_logs': []}
            for version in VERSIONS:
                labels = {f'{kind}-{transport}-{method}' for kind in ('real', 'synthetic')
                          for transport in ('local', 'http', 'https', 's3') for method in ('points', 'nearest')}
                labels.update({'real-direct', 'real-exact-source', 'http-denied', 'http-recovered'})
                for label in sorted(labels):
                    n5['commands'].append({**command, 'version': version, 'label': label,
                                           'extension_sha256': 'extension',
                                           'exit_code': 1 if label == 'http-denied' else 0,
                                           'expected_error': label == 'http-denied'})
            gates['N5'] = n5
            manifest = {'inputs_path': 'inputs.json', 'inputs_sha256': digest(root / 'inputs.json'),
                        'commands_path': 'commands.jsonl', 'commands_sha256': digest(root / 'commands.jsonl'),
                        'gates': gates}
            def check(value):
                (root / 'manifest.json').write_text(json.dumps(value))
                return audit_n6(root, 'source', identities)
            self.assertEqual(check(manifest)['status'], 'pass')
            for field in ('executable_sha256', 'build_manifest_sha256'):
                altered = copy.deepcopy(manifest)
                altered['gates']['N1']['commands'][0][field] = 'stale'
                stale_stream = [row for gate in ('N0', 'N1', 'N2', 'N3', 'N4')
                                for row in altered['gates'][gate]['commands']]
                (root / 'commands.jsonl').write_text(''.join(json.dumps(c) + '\n' for c in stale_stream))
                altered['commands_sha256'] = digest(root / 'commands.jsonl')
                with self.assertRaisesRegex(ValueError, 'identity mismatch'):
                    check(altered)
            (root / 'commands.jsonl').write_text(''.join(json.dumps(c) + '\n' for c in stream))
            for version in VERSIONS:
                for suffix in (f'N0-points-{version}.sql', f'N0-points-rows-{version}.csv',
                               f'N0-points-metrics-{version}.csv'):
                    path = root / suffix
                    original = path.read_bytes()
                    path.write_text('altered')
                    with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                        check(manifest)
                    path.unlink()
                    with self.assertRaises(OSError):
                        check(manifest)
                    path.write_bytes(original)
            altered = copy.deepcopy(manifest)
            del altered['gates']['N0']['commands'][0]['sql_path']
            with self.assertRaisesRegex(ValueError, 'SQL evidence'):
                check(altered)
            for gate in ('N0', 'N5'):
                altered = copy.deepcopy(manifest);altered['gates'][gate]['status'] = 'blocked'
                with self.assertRaisesRegex(ValueError, 'missing passing'):
                    check(altered)
            altered = copy.deepcopy(manifest);altered['gates']['N5']['commands'].pop()
            with self.assertRaisesRegex(ValueError, 'incomplete N5'):
                check(altered)
            altered = copy.deepcopy(manifest);altered['gates']['N5']['commands'][0]['extension_sha256'] = 'old'
            with self.assertRaisesRegex(ValueError, 'extension identity'):
                check(altered)
            altered = copy.deepcopy(manifest);altered['gates']['N5']['archive']['retrieval_verified'] = False
            with self.assertRaisesRegex(ValueError, 'archive'):
                check(altered)
            check(manifest)
            (root / 'result.txt').write_text('altered')
            with self.assertRaisesRegex(ValueError, 'output hash'):
                check(manifest)
            (root / 'inputs.json').write_text('{}')
            with self.assertRaisesRegex(ValueError, 'inputs hash'):
                check(manifest)

    def test_archive_rejects_public_permissions_and_bad_hash(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary);data = root / 'private.om';data.write_bytes(b'fixture');data.chmod(0o600)
            record = {'private_archive': str(data), 'public_redistribution': False, 'retrieval_verified': True,
                      'sha256': digest(data), 'retrieval_sha256': digest(data), 'size_bytes': 7}
            path = root / 'archive.json';path.write_text(json.dumps(record))
            self.assertTrue(verify_private_archive(path, digest(data), 7)['retrieval_verified'])
            data.chmod(0o644)
            with self.assertRaisesRegex(ValueError, 'accessible'):
                verify_private_archive(path, digest(data), 7)
            data.chmod(0o600)
            with self.assertRaisesRegex(ValueError, 'verification'):
                verify_private_archive(path, '0' * 64, 7)


if __name__ == '__main__':
    unittest.main()
