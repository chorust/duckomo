#!/usr/bin/env python3
"""Build the actual OM C target without compiling DuckDB.

Optional cross check: CC=/path/to/x86_64-linux-gnu-gcc CFLAGS=... python3 ...
CMake uses the compiler's target macros, not the build host architecture.
"""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class OmBuildTest(unittest.TestCase):
    def test_om_target_builds_with_target_specific_simd(self):
        with tempfile.TemporaryDirectory(prefix='duckomo-om-build-') as directory:
            root = Path(directory)
            # Stub only DuckDB's extension registration; the OM target and all
            # its compiler settings come from the real root CMakeLists.txt.
            (root / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.16)
project(om_build_check LANGUAGES C CXX)
function(build_static_extension name)
  add_library(${name}_extension STATIC EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/src/om_extension.cpp")
endfunction()
function(build_loadable_extension name)
  add_library(${name}_loadable_extension STATIC EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/src/om_extension.cpp")
endfunction()
''' +
                f'add_subdirectory("{ROOT.as_posix()}" duckomo)\n')
            build = root / 'build'
            self.run_command(['cmake', '-S', str(root), '-B', str(build),
                              '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DCMAKE_BUILD_TYPE=Release',
                              '-DDUCKOMO_BUILD_DEVELOPER_TOOLS=OFF'])
            commands = json.loads((build / 'compile_commands.json').read_text())
            om = [row for row in commands if 'duckomo_om.dir' in row['command']]
            self.assertEqual(len(om), len(list((ROOT / 'third_party/om-file-format/c/src').glob('*.c'))))
            # Compiler detection is part of the actual configured target.
            cache = (build / 'CMakeCache.txt').read_text()
            x86 = 'DUCKOMO_OM_TARGET_X86:INTERNAL=1' in cache
            for row in om:
                self.assertEqual('-mssse3' in row['command'], x86)
                self.assertNotIn('-march=native', row['command'])
                self.assertNotIn('-mavx2', row['command'])
            self.run_command(['cmake', '--build', str(build), '--target', 'duckomo_om', '--parallel', '2'])

    def run_command(self, command):
        result = subprocess.run(command, text=True, capture_output=True, timeout=600)
        self.assertEqual(result.returncode, 0, result.stdout[-3000:] + result.stderr[-6000:])


if __name__ == '__main__':
    unittest.main()
