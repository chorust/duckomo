#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUILD="$ROOT/build/release"
MATRIX=""; PAIR=""; OUT_ROOT="$ROOT/build/official-matrix"
LOCAL_ONLY=false
REGIONS=false
SPATIAL=""
REGION_GATES="P0,P1,P2"
while (($#)); do
    case "$1" in
        --matrix) MATRIX="$2"; shift 2;;
        --pair) PAIR="$2"; shift 2;;
        --output-root) OUT_ROOT="$2"; shift 2;;
        --regions) REGIONS=true; shift;;
        --spatial) SPATIAL="$2"; shift 2;;
        --region-gates) REGION_GATES="$2"; shift 2;;
        --local-only) LOCAL_ONLY=true; shift;;
        -h|--help) echo 'validate.sh [BUILD] [--local-only] [--regions [--spatial PATH] [--region-gates P0,...]] | --matrix MANIFEST --pair v1.5.x [--output-root DIR]'; exit 0;;
        --*) echo "unknown option: $1" >&2;exit 2;;
        *) BUILD="$1";shift;;
    esac
done
if [[ -n "$MATRIX" ]]; then
    [[ -n "$PAIR" ]] || { echo '--pair required' >&2;exit 2; }
    [[ "$MATRIX" = /* ]] || MATRIX="$ROOT/$MATRIX"
    [[ "$OUT_ROOT" = /* ]] || OUT_ROOT="$ROOT/$OUT_ROOT"
    BUILD="$OUT_ROOT/$PAIR/release"
    python3 "$ROOT/scripts/version_matrix.py" fetch-runtime --root "$ROOT" --matrix "$MATRIX" --pair "$PAIR" --output-root "$OUT_ROOT" > /dev/null
    CLI="$OUT_ROOT/$PAIR/official/duckdb"
    HTTPFS="$OUT_ROOT/$PAIR/official/httpfs.duckdb_extension"
else
    [[ "$BUILD" = /* ]] || BUILD="$PWD/$BUILD"
    CLI="${DUCKOMO_OFFICIAL_DUCKDB:-$BUILD/duckdb}"
    HTTPFS="${DUCKOMO_HTTPFS:-}"
fi
if [[ -n "${HTTPFS:-}" ]];then export DUCKOMO_HTTPFS="$HTTPFS";fi
cd "$ROOT"
[[ -x "$BUILD/test/unittest" ]] || { echo 'SQL runner missing: build shell/unittest and native targets first' >&2;exit 2; }
REQUIRED_EXECUTABLES="$BUILD/test/duckomo-validation-executables.txt"
[[ -s "$REQUIRED_EXECUTABLES" ]] || { echo 'Validation executable manifest missing: configure with DUCKOMO_BUILD_DEVELOPER_TOOLS=ON and build duckomo_developer_tools' >&2;exit 2; }
while IFS= read -r executable; do
    [[ -x "$BUILD/test/$executable" ]] || { echo "Required validation executable missing: $BUILD/test/$executable (build duckomo_developer_tools)" >&2;exit 2; }
done < "$REQUIRED_EXECUTABLES"
export DUCKOMO_CORE_FUNCTIONS_EXTENSION="$BUILD/extension/core_functions/core_functions.duckdb_extension"
export DUCKOMO_TEST_EXTENSION="$BUILD/extension/duckomo/duckomo.duckdb_extension"
if [[ "$REGIONS" == true ]]; then
    REGION_RUNTIME_MATRIX="${DUCKOMO_REGION_RUNTIME_MATRIX:-$ROOT/test/data/regions/runtime-matrix.json}"
    [[ -x "$CLI" ]] || { echo 'Region validation requires a matching DuckDB CLI' >&2;exit 2; }
    if [[ -z "$PAIR" ]]; then
        REGION_VERSION="${DUCKOMO_SPATIAL_VERSION:-$($CLI --version | awk '{print $1}')}"
    else
        REGION_VERSION="$PAIR"
    fi
    if [[ -z "$SPATIAL" ]]; then
        SPATIAL="${DUCKOMO_SPATIAL_EXTENSION:-$ROOT/build/regions-runtime/$REGION_VERSION/official/spatial.duckdb_extension}"
    fi
    [[ -f "$SPATIAL" ]] || { echo "Matching spatial extension missing: $SPATIAL" >&2;exit 2; }
    SPATIAL_SHA="$(python3 "$ROOT/scripts/region_runtime.py" "$REGION_RUNTIME_MATRIX" "$REGION_VERSION")" || { echo "No verified spatial runtime record for $REGION_VERSION" >&2;exit 2; }
    [[ -n "$SPATIAL_SHA" ]] || { echo "Spatial package hash is missing for $REGION_VERSION" >&2;exit 2; }
    ACTUAL_SPATIAL_SHA="$(sha256sum "$SPATIAL" | awk '{print $1}')"
    [[ "$ACTUAL_SPATIAL_SHA" == "$SPATIAL_SHA" ]] || { echo "Spatial package hash mismatch for $REGION_VERSION" >&2;exit 2; }
    export DUCKOMO_SPATIAL_EXTENSION="$(realpath "$SPATIAL")"
    export DUCKOMO_SPATIAL_SHA256="$SPATIAL_SHA"
fi
for file in test/sql/*.test; do
    if [[ "$file" == test/sql/regions.test ]]; then continue; fi
    "$BUILD/test/unittest" --test-dir "$ROOT" "$file"
done
for path in "$BUILD"/test/native/*; do
    [[ -f "$path" && -x "$path" ]] || continue
    if [[ "$(basename "$path")" == domain_reference_test ]];then
        if [[ -n "${DUCKOMO_DOMAIN_FILE:-}" && -d "${DUCKOMO_DOMAIN_REFERENCE:-}" ]];then
            "$path" "$DUCKOMO_DOMAIN_FILE" "$ROOT/test/data/domain-manifest.json" "$DUCKOMO_DOMAIN_REFERENCE"
        else echo 'domain_reference_test: not-run (DUCKOMO_DOMAIN_FILE/DUCKOMO_DOMAIN_REFERENCE required)';fi
        continue
    fi
    [[ "$(basename "$path")" != official_httpfs_test ]] || { [[ "$LOCAL_ONLY" == false ]] || continue; }
    case "$(basename "$path")" in
        region_input_test|region_geometry_test|region_scan_test) continue;;
    esac
    DUCKOMO_EXTENSION_PATH="$BUILD/extension/duckomo/duckomo.duckdb_extension" "$path"
done
for file in test/tools/*_test.py;do python3 "$file";done
if [[ "$REGIONS" == true ]]; then
    REGION_OUTPUT="${DUCKOMO_REGION_EVIDENCE_DIR:-}"
    if [[ -z "$REGION_OUTPUT" ]]; then
        mkdir -p "$ROOT/build/evidence/regions"
        REGION_OUTPUT="$(mktemp -d "$ROOT/build/evidence/regions/local-$REGION_VERSION-XXXXXXXX")"
    else
        [[ "$REGION_OUTPUT" = /* ]] || REGION_OUTPUT="$ROOT/$REGION_OUTPUT"
        mkdir -p "$REGION_OUTPUT"
    fi
    REGION_VALIDATOR_ARGS=( \
        --root "$ROOT" --manifest "$ROOT/test/data/regions/manifest.json" \
        --runtime-matrix "$REGION_RUNTIME_MATRIX" \
        --duckdb "$CLI" --extension "$BUILD/extension/duckomo/duckomo.duckdb_extension" \
        --spatial "$SPATIAL" --output "$REGION_OUTPUT" --gates "$REGION_GATES" \
    )
    if [[ ",$REGION_GATES," == *,P5,* ]]; then
        [[ -n "$HTTPFS" ]] && REGION_VALIDATOR_ARGS+=(--httpfs "$HTTPFS")
        [[ -n "${DUCKOMO_REMOTE_ENV:-}" ]] && REGION_VALIDATOR_ARGS+=(--remote-env "$DUCKOMO_REMOTE_ENV")
        [[ -n "${DUCKOMO_SERVER_LOG:-}" ]] && REGION_VALIDATOR_ARGS+=(--server-log "$DUCKOMO_SERVER_LOG")
    fi
    "$BUILD/test/tools/duckomo_region_validation" "${REGION_VALIDATOR_ARGS[@]}"
fi
[[ "$LOCAL_ONLY" == false ]] || exit 0
[[ -x "$CLI" && -f "$HTTPFS" ]] || { echo 'Official CLI/HTTPFS required for remote validation; use --matrix or DUCKOMO_OFFICIAL_DUCKDB/DUCKOMO_HTTPFS' >&2;exit 2; }
OUTPUT="${DUCKOMO_EVIDENCE_DIR:-$ROOT/build/official-validation-${PAIR:-release}}"
python3 "$ROOT/scripts/validate-official-httpfs.py" --root "$ROOT" --duckdb "$CLI" --httpfs "$HTTPFS" \
    --extension "$BUILD/extension/duckomo/duckomo.duckdb_extension" --output "$OUTPUT"
