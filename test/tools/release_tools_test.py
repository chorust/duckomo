#!/usr/bin/env python3
"""Fail-closed release packaging and pinned multi-platform runtime contracts."""
import copy
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import release_tools as release


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.document = json.loads((ROOT / 'test/data/release-runtimes.json').read_text())
        self.version, self.platform = 'v1.5.6', 'linux_arm64'
        self.tag, self.commit = 'v0.1.0', 'a' * 40

    def test_exact_twelve_pinned_pairs(self):
        document = release.load_runtimes(ROOT / 'test/data/release-runtimes.json')
        self.assertEqual(len(release.runtime_matrix(document)['include']), 12)
        old = json.loads((ROOT / 'test/data/grids/version-matrix.json').read_text())
        for pair in old['pairs']:
            self.assertEqual(release.find_runtime(document, pair['pair_id'], 'linux_arm64'),
                             pair['official_runtime'])

    def test_missing_duplicate_bad_hash_and_wrong_urls_rejected(self):
        documents = []
        missing = copy.deepcopy(self.document); missing['runtimes'].pop(); documents.append(missing)
        duplicate = copy.deepcopy(self.document); duplicate['runtimes'].append(duplicate['runtimes'][0]); documents.append(duplicate)
        for field, value in [('cli_sha256', 'bad'), ('httpfs_url', 'http://untrusted.invalid/file'),
                             ('cli_url', 'https://github.com/duckdb/duckdb/releases/download/v9/duckdb.zip')]:
            document = copy.deepcopy(self.document); document['runtimes'][0][field] = value; documents.append(document)
        for document in documents:
            with self.subTest(document=document), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'matrix.json'; path.write_text(json.dumps(document))
                with self.assertRaises(ValueError): release.load_runtimes(path)

    def test_safe_release_tags_only(self):
        for tag in ['v0.1.0', 'v2.0.0-rc.1', 'v10.20.30']:
            release.validate_tag(tag)
        for tag in ['main', 'v1', 'v1.2.3/../../bad', 'v1.2.3;echo bad', 'v1.2.3\n', 'v01.2.3']:
            with self.subTest(tag=tag), self.assertRaises(ValueError): release.validate_tag(tag)

    def fixture(self, directory, version=None, platform=None):
        version, platform = version or self.version, platform or self.platform
        extension = directory / f'{version}-{platform}.duckdb_extension'
        extension.write_bytes(f'binary {version}/{platform}'.encode())
        runtime = release.find_runtime(self.document, version, platform)
        report = dict(status='pass', version=version, platform=platform,
                      artifact_hashes=dict(extension=release.sha(extension), duckdb=runtime['cli_sha256'],
                                           httpfs=runtime['httpfs_sha256']))
        path = directory / f'{version}-{platform}.json'; path.write_text(json.dumps(report))
        return extension, path

    def pack(self, directory, version=None, platform=None):
        version, platform = version or self.version, platform or self.platform
        extension, report = self.fixture(directory, version, platform)
        return release.package(self.document, version, platform, self.tag, self.commit,
                               extension, report, ROOT / 'LICENSE', directory / 'packages')

    def test_package_has_standard_binary_bound_provenance_and_gz_asset(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = self.pack(Path(directory))
            self.assertEqual(archive.name, 'duckomo-v0.1.0-duckdb-v1.5.6-linux_arm64.zip')
            with zipfile.ZipFile(archive) as z:
                self.assertEqual(set(z.namelist()), {'duckomo.duckdb_extension', 'manifest.json', 'verification.json', 'LICENSE'})
                manifest = json.loads(z.read('manifest.json'))
                self.assertFalse(manifest['signed'])
                self.assertEqual(manifest['source_commit'], self.commit)
                self.assertEqual(manifest['extension_sha256'], hashlib.sha256(z.read('duckomo.duckdb_extension')).hexdigest())
            gz_asset = archive.with_name('duckomo-v0.1.0-duckdb-v1.5.6-linux_arm64.duckdb_extension.gz')
            with zipfile.ZipFile(archive) as z:
                self.assertEqual(gzip.decompress(gz_asset.read_bytes()), z.read('duckomo.duckdb_extension'))
            self.assertEqual(gz_asset.name, release.gz_asset_name(self.tag, self.version, self.platform))

    def test_failed_wrong_pair_stale_binary_or_unpinned_runtime_cannot_package(self):
        for mutation in ['status', 'version', 'platform', 'extension', 'duckdb', 'httpfs']:
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                root = Path(directory); extension, path = self.fixture(root)
                report = json.loads(path.read_text())
                if mutation in ['status', 'version', 'platform']: report[mutation] = 'wrong'
                else: report['artifact_hashes'][mutation] = 'b' * 64
                path.write_text(json.dumps(report))
                with self.assertRaises(ValueError):
                    release.package(self.document, self.version, self.platform, self.tag, self.commit,
                                    extension, path, ROOT / 'LICENSE', root / 'packages')
                self.assertFalse((root / 'packages').exists())

    def test_verifier_records_platform_mismatch_as_failure(self):
        spec = importlib.util.spec_from_file_location('release_verifier', ROOT / 'scripts/verify-community-artifact.py')
        verifier = importlib.util.module_from_spec(spec); spec.loader.exec_module(verifier)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('duckdb', 'httpfs', 'extension'): (root / name).write_bytes(name.encode())
            document = copy.deepcopy(self.document)
            runtime = release.find_runtime(document, self.version, self.platform)
            runtime['cli_sha256'], runtime['httpfs_sha256'] = release.sha(root / 'duckdb'), release.sha(root / 'httpfs')
            args = SimpleNamespace(root=ROOT, version=self.version, platform=self.platform,
                                   runtime_manifest=root / 'matrix.json', duckdb=root / 'duckdb',
                                   httpfs=root / 'httpfs', extension=root / 'extension', output=root / 'report.json')
            with patch.object(verifier.argparse.ArgumentParser, 'parse_args', return_value=args), \
                    patch.object(release, 'load_runtimes', return_value=document), \
                    patch.object(verifier.subprocess, 'check_output', side_effect=['v1.5.6 test', 'osx_arm64']):
                self.assertEqual(verifier.main(), 1)
            report = json.loads(args.output.read_text())
            self.assertEqual(report['status'], 'fail')
            self.assertIn('platform mismatch', report['error'])

    def test_identical_inputs_create_identical_packages_and_refuse_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); extension, report = self.fixture(root)
            args = (self.document, self.version, self.platform, self.tag, self.commit,
                    extension, report, ROOT / 'LICENSE')
            first = release.package(*args, root / 'a')
            second = release.package(*args, root / 'b')
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(first.with_suffix('.duckdb_extension.gz').read_bytes(),
                             second.with_suffix('.duckdb_extension.gz').read_bytes())
            with self.assertRaises(FileExistsError): release.package(*args, root / 'a')

    def test_invalid_source_commit_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); extension, report = self.fixture(root)
            with self.assertRaises(ValueError):
                release.package(self.document, self.version, self.platform, self.tag, 'main',
                                extension, report, ROOT / 'LICENSE', root / 'packages')

    def test_aggregate_requires_all_twelve_and_checks_every_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for row in self.document['runtimes']: self.pack(root, row['version'], row['platform'])
            output = root / 'dist'
            release.assemble(self.document, self.tag, self.commit, root / 'packages', output)
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(len(manifest['packages']), 12)
            for record in manifest['packages']:
                self.assertEqual(record['gz_file'], release.gz_asset_name(self.tag, record['duckdb_version'], record['platform']))
                self.assertEqual(release.sha(output / record['gz_file']), record['gz_sha256'])
            self.assertEqual(len(list(output.glob('*.duckdb_extension.gz'))), 12)
            # 12 ZIP + 12 gz assets + manifest.json
            self.assertEqual(len((output / 'SHA256SUMS').read_text().splitlines()), 25)
            for line in (output / 'SHA256SUMS').read_text().splitlines():
                digest, name = line.split('  '); self.assertEqual(release.sha(output / name), digest)
            with self.assertRaises(ValueError):
                release.assemble(self.document, self.tag, self.commit, root / 'packages', output)
            next((root / 'packages').glob('*.zip')).unlink()
            with self.assertRaises(ValueError):
                release.assemble(self.document, self.tag, self.commit, root / 'packages', root / 'missing')

    def test_aggregate_rejects_missing_or_tampered_gz_asset(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for row in self.document['runtimes']:
                self.pack(root, row['version'], row['platform'])
            gz_assets = sorted(root.rglob('*.duckdb_extension.gz'))
            self.assertEqual(len(gz_assets), 12)
            gz_assets[0].unlink()
            with self.assertRaises(ValueError):
                release.assemble(self.document, self.tag, self.commit, root / 'packages', root / 'dist')
            gz_assets[0].write_bytes(gzip.compress(b'changed after verification', mtime=0))
            with self.assertRaises(ValueError):
                release.assemble(self.document, self.tag, self.commit, root / 'packages', root / 'dist')

    def test_aggregate_rejects_tampered_archive_even_with_passing_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archives = [self.pack(root, row['version'], row['platform']) for row in self.document['runtimes']]
            archive = archives[0]
            with zipfile.ZipFile(archive) as z: contents = {name: z.read(name) for name in z.namelist()}
            contents['duckomo.duckdb_extension'] = b'changed after verification'
            with zipfile.ZipFile(archive, 'w') as z:
                for name, data in contents.items(): z.writestr(name, data)
            with self.assertRaises(ValueError):
                release.assemble(self.document, self.tag, self.commit, root / 'packages', root / 'dist')


if __name__ == '__main__':
    unittest.main()
