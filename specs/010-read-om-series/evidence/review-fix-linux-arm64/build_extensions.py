#!/usr/bin/env python3
"""Rebuild changed DuckOMO translation units for existing linux_arm64 matrix pins."""
import hashlib
import json
import platform
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
OUT = Path(__file__).resolve().parent
BUILD_ROOT = ROOT / "build/series-review-fix"
PAIRS = ("v1.5.4", "v1.5.5", "v1.5.6", "v2.0.0")
OBJECTS = (
    "extension/duckomo/CMakeFiles/duckomo_loadable_extension.dir/src/scan/read_om.cpp.o",
    "extension/duckomo/CMakeFiles/duckomo_loadable_extension.dir/src/scan/read_om_series.cpp.o",
)


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(command, cwd):
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True)


def main():
    if platform.machine() != "aarch64":
        raise SystemExit(f"linux_arm64 evidence requires aarch64 host, got {platform.machine()}")
    previous = json.loads((ROOT / "specs/010-read-om-series/evidence/code-review/probe-results.json").read_text())
    by_pair = {item["pair"]: item for item in previous["pairs"]}
    revision = subprocess.check_output(["git", "rev-parse", "--short=7", "HEAD"], cwd=ROOT, text=True).strip()
    results = {
        "purpose": "Fresh review-fix extension links from changed DuckOMO translation units; each pin's existing static archive is reused",
        "platform": platform.machine(),
        "repository_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "extension_version": revision,
        "pairs": [],
    }
    for pair in PAIRS:
        prior_cli, prior_extension = by_pair[pair]["verified_artifacts"]
        original = Path(prior_extension["path"])
        expected_old_sha = prior_extension["sha256"]
        if sha(original) != expected_old_sha:
            raise SystemExit(f"review artifact changed before build: {original}")
        release_dir = original.parents[2]
        source_dir = ROOT / f"build/series-matrix/{pair}/source/duckdb"
        static_archive = release_dir / "src/libduckdb_static.a"
        static_sha = sha(static_archive)
        pair_manifest = json.loads((ROOT / f"build/series-matrix/{pair}/build-manifest.json").read_text())
        engine_commit = pair_manifest["inputs"]["commits"]["duckdb"]["commit"]
        workdir = release_dir / "extension/duckomo"
        make_command = [shutil.which("gmake") or "make", "-B", "-C", str(release_dir), "-f",
                        "extension/duckomo/CMakeFiles/duckomo_loadable_extension.dir/build.make", *OBJECTS]
        link_command = ["cmake", "-E", "cmake_link_script",
                        "CMakeFiles/duckomo_loadable_extension.dir/link.txt", "--verbose=1"]
        metadata_command = [
            "cmake", "-DABI_TYPE=CPP", f"-DEXTENSION={original}",
            f"-DPLATFORM_FILE={release_dir / 'duckdb_platform_out'}",
            f"-DVERSION_FIELD={pair}", f"-DEXTENSION_VERSION={revision}",
            f"-DNULL_FILE={source_dir / 'scripts/null.txt'}",
            "-P", str(source_dir / "scripts/append_metadata.cmake"),
        ]
        with tempfile.TemporaryDirectory(prefix=f"duckomo-{pair}-review-fix-") as temporary:
            backup = Path(temporary) / original.name
            shutil.copy2(original, backup)
            commands = []
            try:
                compiled = run(make_command, ROOT)
                commands.append({"stage": "compile_changed_translation_units", "command": make_command,
                                 "exit_code": compiled.returncode, "stdout": compiled.stdout, "stderr": compiled.stderr})
                if compiled.returncode == 0:
                    linked = run(link_command, workdir)
                    commands.append({"stage": "link_extension", "command": link_command,
                                     "exit_code": linked.returncode, "stdout": linked.stdout, "stderr": linked.stderr})
                else:
                    linked = None
                if linked is not None and linked.returncode == 0:
                    metadata = run(metadata_command, ROOT)
                    commands.append({"stage": "append_extension_metadata", "command": metadata_command,
                                     "exit_code": metadata.returncode, "stdout": metadata.stdout, "stderr": metadata.stderr})
                else:
                    metadata = None
                success = compiled.returncode == 0 and linked is not None and linked.returncode == 0 \
                    and metadata is not None and metadata.returncode == 0
                if success:
                    artifact = BUILD_ROOT / pair / "extension/duckomo/duckomo.duckdb_extension"
                    artifact.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(original, artifact)
            finally:
                shutil.copy2(backup, original)
        record = {
            "pair": pair,
            "engine_version": pair,
            "engine_pin": engine_commit,
            "cli": {"path": prior_cli["path"], "sha256": prior_cli["sha256"]},
            "previous_extension": {"path": str(original), "sha256": expected_old_sha},
            "reused_duckdb_static": {"path": str(static_archive), "sha256": static_sha},
            "commands": commands,
            "exit_code": 0 if success else next((item["exit_code"] for item in commands if item["exit_code"]), 1),
            "previous_extension_restored": sha(original) == expected_old_sha,
            "duckdb_static_unchanged": sha(static_archive) == static_sha,
        }
        if success:
            record["review_fix_extension"] = {"path": str(artifact), "sha256": sha(artifact)}
        results["pairs"].append(record)
        (OUT / "build-artifacts.json").write_text(json.dumps(results, indent=2) + "\n")
        (OUT / f"build-{pair}.log").write_text("\n".join(
            f"[{item['stage']}] exit={item['exit_code']}\n{item['stdout']}{item['stderr']}"
            for item in commands
        ))
        if not success or not record["previous_extension_restored"] or not record["duckdb_static_unchanged"]:
            raise SystemExit(f"build or preservation check failed for {pair}; see build-{pair}.log")
        print(pair, "built", record["review_fix_extension"]["sha256"], "previous artifacts preserved")


if __name__ == "__main__":
    main()
