#!/usr/bin/env bash
# Build duckomo_fixture_tool when the configured tree does not already have it,
# then write the gitignored series SQL inputs at build/series-fixtures/.
# SQLLogic tests hard-code that path under the repository root.
#
# Linux release CI generates the files inside the build image, then runs
# `make test_release` again on the host. That second run still depends on
# series-fixtures-release. The image's CMakeCache path does not match the host,
# and ninja-build is not installed there, so a complete fixture tree must exit
# before CMake. An optional second argument overrides the output root for tests.
set -euo pipefail

# An exported SKIP_TESTS=1 must not configure or compile. The Linux host step
# only assigns that name inside Make, so it does not reach this process. The
# presence check below covers that second `make test_release`.
if [[ "${SKIP_TESTS:-0}" == "1" ]]; then
    exit 0
fi

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUILD="${1:-$ROOT/build/release}"
if [[ "$BUILD" != /* ]]; then
    BUILD="$ROOT/$BUILD"
fi
OUTPUT_ROOT="${2:-$ROOT/build/series-fixtures}"
if [[ "$OUTPUT_ROOT" != /* ]]; then
    OUTPUT_ROOT="$ROOT/$OUTPUT_ROOT"
fi

python_is_new_enough() {
    "$1" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)'
}

python=""
for candidate in python3 python3.13 python3.12 python3.11 python3.10; do
    if command -v "$candidate" >/dev/null 2>&1 && python_is_new_enough "$candidate"; then
        python="$candidate"
        break
    fi
done
if [[ -z "$python" ]]; then
    for candidate in \
        /opt/python/cp313-cp313/bin/python3 \
        /opt/python/cp312-cp312/bin/python3 \
        /opt/python/cp311-cp311/bin/python3 \
        /opt/python/cp310-cp310/bin/python3
    do
        if [[ -x "$candidate" ]] && python_is_new_enough "$candidate"; then
            python="$candidate"
            break
        fi
    done
fi

# Presence is decided before CMakeCache is read. A tree written by the image
# must not be rebuilt on the host.
if [[ -n "$python" ]] && "$python" "$ROOT/scripts/generate-series-fixtures.py" \
        --check-existing --output-root "$OUTPUT_ROOT"; then
    echo "series fixtures already present: $OUTPUT_ROOT"
    exit 0
fi

if [[ ! -f "$BUILD/CMakeCache.txt" ]]; then
    echo "series fixtures: $BUILD is not configured. Run the matching release/debug/reldebug build first." >&2
    exit 1
fi

# Release CI leaves DUCKOMO_BUILD_DEVELOPER_TOOLS off. The series fixture tool is
# its own EXCLUDE_FROM_ALL target, so this does not build the rest of the suite.
cmake --build "$BUILD" --target duckomo_fixture_tool --parallel

tool=""
if [[ -x "$BUILD/test/tools/duckomo_fixture_tool" ]]; then
    tool="$BUILD/test/tools/duckomo_fixture_tool"
else
    shopt -s nullglob
    for candidate in "$BUILD"/test/tools/*/duckomo_fixture_tool "$BUILD"/test/tools/duckomo_fixture_tool.exe; do
        if [[ -x "$candidate" ]]; then
            tool="$candidate"
            break
        fi
    done
    shopt -u nullglob
fi
if [[ -z "$tool" ]]; then
    echo "series fixtures: duckomo_fixture_tool was not produced under $BUILD/test/tools" >&2
    exit 1
fi

if [[ -z "$python" ]]; then
    echo "series fixtures: Python >= 3.10 is required to run scripts/generate-series-fixtures.py" >&2
    exit 1
fi

"$python" "$ROOT/scripts/generate-series-fixtures.py" \
    --fixture-tool "$tool" \
    --output-root "$OUTPUT_ROOT"
