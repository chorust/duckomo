#!/usr/bin/env bash
# Build duckomo_fixture_tool when the configured tree does not already have it,
# then write the gitignored series SQL inputs at build/series-fixtures/.
# SQLLogic tests hard-code that path under the repository root.
set -euo pipefail

# extension-ci-tools sets this for steps that must not run SQL (for example the
# Linux job outside the build image). Do not configure or compile anything there.
if [[ "${SKIP_TESTS:-0}" == "1" ]]; then
    exit 0
fi

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUILD="${1:-$ROOT/build/release}"
if [[ "$BUILD" != /* ]]; then
    BUILD="$ROOT/$BUILD"
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
if [[ -z "$python" ]]; then
    echo "series fixtures: Python >= 3.10 is required to run scripts/generate-series-fixtures.py" >&2
    exit 1
fi

"$python" "$ROOT/scripts/generate-series-fixtures.py" \
    --fixture-tool "$tool" \
    --output-root "$ROOT/build/series-fixtures"
