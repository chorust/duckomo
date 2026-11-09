#!/usr/bin/env python3
"""CI wiring checks for series SQL fixtures. No DuckDB build required."""

from __future__ import annotations

import importlib.util
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[2]
MAKEFILE = ROOT / "Makefile"
ENSURE = ROOT / "scripts" / "ensure-series-fixtures.sh"


def _run(command: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    merged = os.environ.copy()
    if env:
        merged.update(env)
    return subprocess.run(command, text=True, capture_output=True, env=merged, check=False)


def _load_fixture_helpers() -> tuple[ModuleType, ModuleType]:
    spec = importlib.util.spec_from_file_location(
        "series_fixtures_test_helpers", ROOT / "test" / "tools" / "series_fixtures_test.py"
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load series fixture test helpers")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    generator = module.load_generator()
    return generator, module


class SeriesFixtureCiTest(unittest.TestCase):
    def test_make_test_targets_generate_fixtures_first(self) -> None:
        text = MAKEFILE.read_text(encoding="utf-8")
        self.assertIn("test_release test_release_internal: series-fixtures-release", text)
        self.assertIn("test_debug test_debug_internal: series-fixtures-debug", text)
        self.assertIn("test_reldebug test_reldebug_internal: series-fixtures-reldebug", text)
        self.assertIn("scripts/ensure-series-fixtures.sh", text)
        validate = (ROOT / "scripts" / "validate.sh").read_text(encoding="utf-8")
        sql_loop = validate.index("for file in test/sql/*.test")
        generator = validate.index("scripts/ensure-series-fixtures.sh")
        self.assertLess(generator, sql_loop)

    def test_skip_tests_does_not_need_a_build(self) -> None:
        result = _run([str(ENSURE), "/no/such/build"], {"SKIP_TESTS": "1"})
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_configuration_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="duckomo-series-fixtures-") as directory:
            root = Path(directory)
            result = _run(
                [str(ENSURE), str(root / "build"), str(root / "series-fixtures")],
                {"SKIP_TESTS": "0"},
            )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("not configured", result.stderr)

    def test_complete_fixtures_skip_cmake(self) -> None:
        generator, fixtures = _load_fixture_helpers()
        with tempfile.TemporaryDirectory(prefix="duckomo-series-fixtures-") as directory:
            root = Path(directory)
            output = root / "series-fixtures"
            tool = root / "fake_fixture_tool.py"
            fixtures.create_fake_writer(tool)
            generator.generate(tool, output)
            build = root / "not-a-build"
            bin_dir = root / "bin"
            bin_dir.mkdir()
            marker = root / "cmake-invoked"
            cmake = bin_dir / "cmake"
            cmake.write_text(
                f"#!/bin/sh\nprintf '%s\\n' invoked > '{marker}'\nexit 99\n",
                encoding="utf-8",
            )
            cmake.chmod(0o755)
            env = {"SKIP_TESTS": "0", "PATH": f"{bin_dir}{os.pathsep}{os.environ.get('PATH', '')}"}
            result = _run([str(ENSURE), str(build), str(output)], env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("already present", result.stdout)
            self.assertFalse(marker.exists(), "complete fixtures must not invoke cmake")
            self.assertNotIn("CMake", result.stderr)

            sample = next(output.rglob("*.om"))
            sample.unlink()
            build.mkdir()
            (build / "CMakeCache.txt").write_text("dummy\n", encoding="utf-8")
            result = _run([str(ENSURE), str(build), str(output)], env)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(marker.read_text(encoding="utf-8").strip(), "invoked")
            self.assertNotIn("already present", result.stdout)

    def test_fixture_step_is_ordered_before_sql_for_both_ci_makefiles(self) -> None:
        cases = {
            "v1.5": """
test_release: test_release_internal
test_release_internal:
	@echo RUN_SQL release
""",
            "v2.0": """
define RUN_TEST
	@echo RUN_SQL $(1)
endef
test_release:
	$(call RUN_TEST,release)
""",
        }
        hook = """
series-fixtures-release:
	@echo GENERATE release
test_release test_release_internal: series-fixtures-release
"""
        for name, upstream in cases.items():
            with self.subTest(name=name):
                with tempfile.TemporaryDirectory(prefix="duckomo-make-") as directory:
                    path = Path(directory) / "Makefile"
                    path.write_text(upstream + hook, encoding="utf-8")
                    result = _run(["make", "-C", directory, "-n", "test_release"])
                self.assertEqual(result.returncode, 0, result.stderr)
                output = result.stdout
                self.assertIn("GENERATE release", output)
                self.assertIn("RUN_SQL release", output)
                self.assertLess(output.index("GENERATE release"), output.index("RUN_SQL release"))


if __name__ == "__main__":
    unittest.main()
