#!/usr/bin/env python3
"""Official runtime provenance, isolated build identities and failure semantics."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import sampling_build
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/version_matrix.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class MatrixTests(unittest.TestCase):
    def setUp(self):
        self.matrix=json.loads((ROOT/'test/data/grids/version-matrix.json').read_text())
        self.pair=self.matrix['pairs'][0]
    def test_three_pinned_releases(self):
        self.assertEqual([p['pair_id'] for p in self.matrix['pairs']],['v1.5.4','v1.5.5','v1.5.6'])
        for p in self.matrix['pairs']:
            value=m.input_payload(self.matrix,p,'a'*64)
            self.assertNotIn('httpfs',value['commits'])
            self.assertNotIn('vcpkg',value)
            self.assertNotIn('duckomo_patchset',value)
    def test_full_commit_and_matching_runtime_required(self):
        for field in ('commit','version'):
            pair=copy.deepcopy(self.pair);pair['duckdb'][field]='bad'
            with self.assertRaises(m.MatrixError):m.input_payload(self.matrix,pair,'a'*64)
    def test_runtime_hash_and_platform_required(self):
        for field in ('cli_sha256','httpfs_sha256','platform'):
            pair=copy.deepcopy(self.pair);pair['official_runtime'][field]='bad'
            with self.assertRaises(m.MatrixError):m.input_payload(self.matrix,pair,'a'*64)
    def test_runtime_and_result_not_part_of_source_build_identity(self):
        payload=m.input_payload(self.matrix,self.pair,'a'*64)
        pair=copy.deepcopy(self.pair);pair['build']={'status':'pass'}
        pair['official_runtime']['httpfs_sha256']='b'*64
        self.assertEqual(m.compute_build_id(payload),m.compute_build_id(m.input_payload(self.matrix,pair,'a'*64)))
        pair['duckdb']['commit']='1'*40
        self.assertNotEqual(m.compute_build_id(payload),m.compute_build_id(m.input_payload(self.matrix,pair,'a'*64)))
    def test_region_runtime_evidence_does_not_change_source_build_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'test/data/regions').mkdir(parents=True)
            (root/'test/data/regions/runtime-matrix.json').write_text('{"artifact_sha256":"'+'a'*64+'"}\n')
            before=m.extension_source_digest(root)
            (root/'test/data/regions/runtime-matrix.json').write_text('{"artifact_sha256":"'+'b'*64+'"}\n')
            self.assertEqual(before,m.extension_source_digest(root))
    def test_sanitizer_spatial_runtime_requires_verified_identity_and_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            runtime_dir=root/'test/data/regions';runtime_dir.mkdir(parents=True)
            scripts=root/'scripts';scripts.mkdir()
            (scripts/'region_runtime.py').symlink_to(ROOT/'scripts/region_runtime.py')
            artifact=root/'spatial.duckdb_extension';artifact.write_bytes(b'verified spatial package')
            sha=m.digest_file(artifact)
            record={'version':'v1.5.4','gate_status':'ready_for_adapter_check',
                    'source_identity_match':True,'spatial_source_sha':'a'*40,
                    'probes':{'extension_version':'aaaaaaa'},
                    'spatial_artifact':{'path':str(artifact),'sha256':sha}}
            matrix=runtime_dir/'runtime-matrix.json'
            matrix.write_text(json.dumps({'runtimes':[record]}))
            self.assertEqual(m.verified_spatial_runtime(root,'v1.5.4'),(artifact,sha))
            record['gate_status']='blocked_identity_mismatch'
            matrix.write_text(json.dumps({'runtimes':[record]}))
            with self.assertRaises(m.MatrixError):m.verified_spatial_runtime(root,'v1.5.4')
    def test_unavailable_official_package_does_not_build_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            resolved={'runtime_dir':directory,'official_runtime':self.pair['official_runtime']}
            with patch.object(m.subprocess,'run') as run:
                run.return_value.returncode=22
                with self.assertRaisesRegex(m.MatrixError,'unavailable.*no source fallback'):m.fetch_runtime(resolved)
                self.assertEqual(run.call_count,1)
    def test_corrupt_existing_artifact_must_be_refetched_and_verified(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory)/'duckdb').write_text('corrupt')
            with patch.object(m.subprocess,'run') as run:
                run.return_value.returncode=22
                with self.assertRaises(m.MatrixError):m.fetch_runtime({'runtime_dir':directory,'official_runtime':self.pair['official_runtime']})
                self.assertEqual(run.call_args.args[0][0],'curl')
    def test_missing_and_duplicate_versions_fail(self):
        with self.assertRaises(m.MatrixError):m.find_pair(self.matrix,'unknown')
        self.matrix['pairs'].append(self.pair)
        with self.assertRaises(m.MatrixError):m.find_pair(self.matrix,self.pair['pair_id'])
    def test_default_build_includes_native_checks_and_validation_tools(self):
        for extension_only, region_sanitizers in ((False, False), (True, False), (False, True), (True, True)):
            with self.subTest(extension_only=extension_only, region_sanitizers=region_sanitizers), tempfile.TemporaryDirectory() as directory:
                pair_root=Path(directory);source=pair_root/'source';source.mkdir()
                release=pair_root/'build/release';release.mkdir(parents=True)
                payload=m.input_payload(self.matrix,self.pair,'a'*64)
                resolved=dict(duckdb_source=str(source),release_dir=str(release),input_payload=payload,
                              build_id='a'*64,official_runtime=self.pair['official_runtime'],
                              build_manifest=str(pair_root/'build/build-manifest.json'),pair_root=str(pair_root))
                args=SimpleNamespace(root=str(ROOT),pair=self.pair['pair_id'],jobs=2,
                                     extension_only=extension_only,region_sanitizers=region_sanitizers)
                def pinned_git(*args, cwd):
                    component='om_c' if Path(cwd).name=='om-file-format' else 'extension_ci_tools'
                    return payload['commits'][component]['commit']
                spatial=ROOT/'build/regions-runtime/v1.5.4/official/spatial.duckdb_extension'
                with patch.object(m,'resolve',return_value=resolved), \
                     patch.object(m,'verified_spatial_runtime',return_value=(spatial,'a'*64)) as verify_spatial, \
                     patch.object(m,'ensure_checkout'), \
                     patch.object(m,'git',side_effect=pinned_git), patch.object(m,'digest_file',return_value='a'*64), \
                     patch.object(m.subprocess,'run') as run, \
                     patch.object(sampling_build,'record_build') as record_sampling:
                    manifest=m.build_pair(args)
                    self.assertEqual(record_sampling.call_count,0 if extension_only else 1)
                targets=run.call_args.args[0]
                self.assertEqual(len(manifest['commands']),2)
                configure=manifest['commands'][0]['argv']
                if region_sanitizers and not extension_only:
                    verify_spatial.assert_called_once_with(ROOT,self.pair['pair_id'])
                    self.assertIn('-DDUCKOMO_REGION_SANITIZER_SPATIAL_PATH='+str(spatial),configure)
                else:
                    verify_spatial.assert_not_called()
                    self.assertIn('-DDUCKOMO_REGION_SANITIZER_SPATIAL_PATH=',configure)
                    self.assertIn('-DDUCKOMO_REGION_SANITIZER_SPATIAL_SHA256=',configure)
                if extension_only:
                    self.assertNotIn('duckomo_developer_tools',targets)
                    self.assertNotIn('unittest',targets)
                else:
                    for target in ('unittest','duckomo_developer_tools','core_functions_loadable_extension',
                                   'duckomo_sanitizer_checks'):
                        self.assertIn(target,targets)
    def test_local_validation_rejects_missing_executables_before_running_sql(self):
        for missing in ('manifest','native/raw_reader_test','native/axis_selection_test',
                        'native/lifecycle_test','tools/duckomo_validation'):
            with self.subTest(missing=missing), tempfile.TemporaryDirectory() as directory:
                build=Path(directory);test=build/'test';test.mkdir()
                runner=test/'unittest';marker=build/'sql-was-run'
                runner.write_text(f'#!/bin/sh\ntouch "{marker}"\nexit 0\n');runner.chmod(0o755)
                required=['native/raw_reader_test','native/axis_selection_test',
                          'native/lifecycle_test','tools/duckomo_validation']
                if missing!='manifest':
                    (test/'duckomo-validation-executables.txt').write_text('\n'.join(required)+'\n')
                    for name in required:
                        if name==missing:continue
                        executable=test/name;executable.parent.mkdir(exist_ok=True)
                        executable.write_text('#!/bin/sh\nexit 0\n');executable.chmod(0o755)
                result=subprocess.run(['bash',str(ROOT/'scripts/validate.sh'),str(build),'--local-only'],
                                      text=True,capture_output=True)
                self.assertEqual(result.returncode,2,result.stdout+result.stderr)
                self.assertIn('manifest missing' if missing=='manifest' else 'executable missing',result.stderr)
                self.assertFalse(marker.exists(),'incomplete validation must fail before SQL checks')

    def test_local_validation_resolves_build_relative_to_callers_directory(self):
        with tempfile.TemporaryDirectory(prefix='duckomo-relative-build-') as directory:
            caller=Path(directory);test=caller/'release/test';test.mkdir(parents=True)
            runner=test/'unittest';runner.write_text('#!/bin/sh\nexit 0\n');runner.chmod(0o755)
            result=subprocess.run(['bash',str(ROOT/'scripts/validate.sh'),'release','--local-only'],
                                  cwd=caller,text=True,capture_output=True)
            self.assertEqual(result.returncode,2,result.stdout+result.stderr)
            self.assertIn('Validation executable manifest missing',result.stderr)
            self.assertNotIn('SQL runner missing',result.stderr)

if __name__=='__main__':unittest.main()
