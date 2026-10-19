#!/usr/bin/env python3
"""Exercise N7 acceptance and reject incomplete/altered independent evidence."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from sampling_independent import audit_n7, digest
ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / 'specs/006-region-nearest-sampling/evidence'
REFERENCE = EVIDENCE / 'user-manual-v1.5.5/full-acceptance.sql'


class IndependentTests(unittest.TestCase):
    def test_accepted_human_package(self):
        self.assertEqual(audit_n7(EVIDENCE / 'n7-human-verified', REFERENCE)['status'], 'pass')

    def test_runner_requires_explicit_attestation(self):
        for arguments in ([], ['--attested-by', '   ']):
            result = subprocess.run(['bash', str(ROOT / 'scripts/run-n7-manual.sh'), *arguments],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Usage:', result.stderr)

    def test_package_and_audit_outside_repository(self):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary) / 'n7'
            shutil.copytree(EVIDENCE / 'n7-human-verified', package)
            self.assertEqual(audit_n7(package, REFERENCE)['status'], 'pass')
            (package / 'manifest.json').unlink()
            command = [sys.executable, str(ROOT / 'scripts/sampling_independent.py'),
                       str(package), '--attested-by', 'Regression test fixture']
            missing_attestation = subprocess.run(command[:-1] + ['   '], capture_output=True, text=True)
            self.assertNotEqual(missing_attestation.returncode, 0)
            self.assertFalse((package / 'manifest.json').exists())
            result = subprocess.run(command, cwd=temporary, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            manifest = json.loads((package / 'manifest.json').read_text())
            self.assertEqual(manifest['execution_attested_by'], 'Regression test fixture')
            self.assertEqual(audit_n7(package, REFERENCE)['status'], 'pass')
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(json.loads((package / 'manifest.json').read_text()), manifest)
            (package / 'manifest.json').unlink()
            (package / 'output.txt').write_text('N7_FUNCTIONAL_PASS')
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((package / 'manifest.json').exists())

    def test_reject_invalid_identity_missing_or_altered_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary)
            package = parent / 'n7'
            shutil.copytree(EVIDENCE / 'n7-human-verified', package)
            original = json.loads((package / 'manifest.json').read_text())
            for key, value in (('executor_type', 'implementation-worker'), ('exit_code', 1),
                               ('fresh_process_verified', False), ('execution_attested_by', '')):
                altered = dict(original);altered[key] = value
                (package / 'manifest.json').write_text(json.dumps(altered))
                with self.assertRaises(ValueError):audit_n7(package, REFERENCE)
            (package / 'manifest.json').write_text(json.dumps(original))
            output = package / 'output.txt';before = output.read_text();output.write_text('N7_FUNCTIONAL_PASS')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):audit_n7(package, REFERENCE)
            altered = json.loads(json.dumps(original));altered['files']['output.txt'] = digest(output)
            (package / 'manifest.json').write_text(json.dumps(altered))
            with self.assertRaisesRegex(ValueError, 'marker missing'):audit_n7(package, REFERENCE)
            output.write_text(before)
            identity = package / 'identity.txt';identity.write_text('executor_type=human\nv1.5.5\n')
            altered = json.loads(json.dumps(original));altered['files']['identity.txt'] = digest(identity)
            (package / 'manifest.json').write_text(json.dumps(altered))
            with self.assertRaisesRegex(ValueError, 'identities differ'):audit_n7(package, REFERENCE)
            (package / 'executed.sql').unlink()
            with self.assertRaises(ValueError):audit_n7(package, REFERENCE)


if __name__ == '__main__':unittest.main()
