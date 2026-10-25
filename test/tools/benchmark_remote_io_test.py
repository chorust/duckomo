#!/usr/bin/env python3
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('benchmark_remote_io', ROOT / 'scripts/benchmark-remote-io.py')
bench = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bench)

class BenchmarkRemoteIoTest(unittest.TestCase):
    def test_runtime_mapping_accepts_runtime_specific_extension(self):
        runtimes = bench.parse_runtime_specs(['v1.5.6=/bin/true|/tmp/ext156', 'v2.0.0=/bin/false|/tmp/ext200'])
        self.assertEqual(runtimes['v1.5.6'], {'cli': '/bin/true', 'extension': '/tmp/ext156'})
        self.assertEqual(runtimes['v2.0.0'], {'cli': '/bin/false', 'extension': '/tmp/ext200'})

    def test_query_profile_supports_both_engine_formats(self):
        self.assertEqual(bench.query_elapsed_ms({'latency': 0.125}), 125)
        self.assertEqual(bench.query_elapsed_ms({'query': {'total_time': 0.25}}), 250)

    def test_cli_flags_follow_runtime_capabilities(self):
        self.assertNotIn('-no-agent', bench.cli_command('/bin/duckdb-1.5', [], 'v1.5.6'))
        self.assertIn('-no-agent', bench.cli_command('/bin/duckdb-2.0', [], 'v2.0.0'))

if __name__ == '__main__':
    unittest.main()
