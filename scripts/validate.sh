#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUILD="$ROOT/build/release"
MATRIX=""; PAIR=""; OUT_ROOT="$ROOT/build/official-matrix"
LOCAL_ONLY=false
REGIONS=false
SAMPLING=false
SPATIAL=""
REGION_GATES="P0,P1,P2"
SAMPLING_RUNTIME_VERIFIED=false
SAMPLING_SPATIAL_SHA=""
while (($#)); do
    case "$1" in
        --matrix) MATRIX="$2"; shift 2;;
        --pair) PAIR="$2"; shift 2;;
        --output-root) OUT_ROOT="$2"; shift 2;;
        --regions) REGIONS=true; shift;;
        --sampling) SAMPLING=true; shift;;
        --spatial) SPATIAL="$2"; shift 2;;
        --region-gates) REGION_GATES="$2"; shift 2;;
        --sampling-gates) SAMPLING_GATES="$2"; shift 2;;
        --local-only) LOCAL_ONLY=true; shift;;
        -h|--help) echo 'validate.sh [BUILD] [--local-only] [--regions|--sampling [--spatial PATH]] | --matrix MANIFEST --pair v1.5.x [--output-root DIR]'; exit 0;;
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
if [[ "$SAMPLING" != true ]]; then
    REQUIRED_EXECUTABLES="$BUILD/test/duckomo-validation-executables.txt"
    [[ -s "$REQUIRED_EXECUTABLES" ]] || { echo 'Validation executable manifest missing: configure with DUCKOMO_BUILD_DEVELOPER_TOOLS=ON and build duckomo_developer_tools' >&2;exit 2; }
    while IFS= read -r executable; do
        [[ -x "$BUILD/test/$executable" ]] || { echo "Required validation executable missing: $BUILD/test/$executable (build duckomo_developer_tools)" >&2;exit 2; }
    done < "$REQUIRED_EXECUTABLES"
fi
if [[ "$SAMPLING" == true ]]; then
    SAMPLING_EXECUTABLES="$BUILD/test/duckomo-sampling-validation-executables.txt"
    [[ -s "$SAMPLING_EXECUTABLES" ]] || { echo 'Sampling validation executable manifest missing: configure with DUCKOMO_BUILD_DEVELOPER_TOOLS=ON' >&2; exit 2; }
    while IFS= read -r executable; do
        [[ -x "$BUILD/test/$executable" ]] || { echo "Required sampling validation executable missing: $BUILD/test/$executable (build duckomo_sampling_validation_bundle)" >&2; exit 2; }
    done < "$SAMPLING_EXECUTABLES"
fi
# Python integration checks must use the same build as SQL/native checks.
# Otherwise a 2.0 spatial override can be loaded by their default 1.5.4 CLI.
export DUCKOMO_BUILD="$BUILD"
export DUCKOMO_CORE_FUNCTIONS_EXTENSION="$BUILD/extension/core_functions/core_functions.duckdb_extension"
export DUCKOMO_TEST_EXTENSION="$BUILD/extension/duckomo/duckomo.duckdb_extension"
if [[ "$REGIONS" == true || "$SAMPLING" == true ]]; then
    if [[ "$SAMPLING" == true ]]; then
        REGION_RUNTIME_MATRIX="${DUCKOMO_SAMPLING_RUNTIME_MATRIX:-$ROOT/test/data/sampling/runtime-matrix.json}"
    else
        REGION_RUNTIME_MATRIX="${DUCKOMO_REGION_RUNTIME_MATRIX:-$ROOT/test/data/regions/runtime-matrix.json}"
    fi
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
    if [[ "$SAMPLING" == true ]]; then
        [[ -x "$CLI" ]] || { echo 'Sampling validation requires a matching DuckDB CLI' >&2; exit 2; }
        SPATIAL_SHA="$(python3 - "$REGION_RUNTIME_MATRIX" "$REGION_VERSION" <<'PY'
import json, pathlib, re, sys
matrix = json.loads(pathlib.Path(sys.argv[1]).read_text())
record = next((x for x in matrix.get("runtimes", []) if x.get("version") == sys.argv[2]), None)
sha = (record or {}).get("spatial_package", {}).get("sha256")
source = (record or {}).get("spatial_source_sha")
version = (record or {}).get("extension_version")
if not record or record.get("status") != "probe_pass" or not re.fullmatch(r"[0-9a-f]{64}", str(sha)) or \
        not re.fullmatch(r"[0-9a-f]{40}", str(source)) or not str(version).startswith(source[:7]):
    raise SystemExit("sampling runtime matrix is not a passing verified record")
print(sha)
PY
        )" || { echo "No verified sampling runtime record for $REGION_VERSION" >&2; exit 2; }
        SAMPLING_RUNTIME_VERIFIED=true
        SAMPLING_SPATIAL_SHA="$SPATIAL_SHA"
    else
        SPATIAL_SHA="$(python3 "$ROOT/scripts/region_runtime.py" "$REGION_RUNTIME_MATRIX" "$REGION_VERSION")" || { echo "No verified spatial runtime record for $REGION_VERSION" >&2;exit 2; }
    fi
    [[ -n "$SPATIAL_SHA" ]] || { echo "Spatial package hash is missing for $REGION_VERSION" >&2;exit 2; }
    ACTUAL_SPATIAL_SHA="$(sha256sum "$SPATIAL" | awk '{print $1}')"
    [[ "$ACTUAL_SPATIAL_SHA" == "$SPATIAL_SHA" ]] || { echo "Spatial package hash mismatch for $REGION_VERSION" >&2;exit 2; }
    export DUCKOMO_SPATIAL_EXTENSION="$(realpath "$SPATIAL")"
    export DUCKOMO_SPATIAL_SHA256="$SPATIAL_SHA"
fi
if [[ "$SAMPLING" == true ]]; then
    "$BUILD/test/tools/duckomo_fixture_tool" --sampling-output "$ROOT/test/data/sampling"
    python3 "$ROOT/scripts/generate-sampling-reference.py" --output "$ROOT/test/data/sampling" --self-check
    python3 "$ROOT/scripts/check-sampling-fixtures.py" --root "$ROOT/test/data/sampling"
    python3 -B "$ROOT/test/tools/sampling_evidence_test.py"
    python3 -B "$ROOT/test/tools/sampling_audit_test.py"
    python3 -B "$ROOT/test/tools/sampling_independent_test.py"
    # Fixture generation can change source inputs; build and record identities
    # only after it completes, before any acceptance commands run.
    python3 "$ROOT/scripts/sampling_build.py" --build "$BUILD" --sanitizers
fi
for file in test/sql/*.test; do
    if [[ "$file" == test/sql/regions.test || "$file" == test/sql/region_sampling.test || "$file" == test/sql/region_sampling_spatial_dependency.test ]]; then continue; fi
    "$BUILD/test/unittest" --test-dir "$ROOT" "$file"
done
if [[ "$SAMPLING" == true && -f "$ROOT/test/sql/region_sampling.test" ]]; then
    "$BUILD/test/unittest" --test-dir "$ROOT" "$ROOT/test/sql/region_sampling.test"
fi
if [[ -f "$ROOT/test/sql/region_sampling_spatial_dependency.test" ]]; then
    "$BUILD/test/unittest" --test-dir "$ROOT" "$ROOT/test/sql/region_sampling_spatial_dependency.test"
fi
for path in "$BUILD"/test/native/*; do
    [[ -f "$path" && -x "$path" ]] || continue
    if [[ "$SAMPLING" == true && "$(basename "$path")" != nearest_sampling_test && "$(basename "$path")" != region_sampling_test ]]; then
        continue
    fi
    if [[ "$(basename "$path")" == nearest_sampling_test || "$(basename "$path")" == region_sampling_test ]]; then
        [[ "$SAMPLING" == true ]] || continue
        if [[ "$(basename "$path")" == region_sampling_test ]]; then
            "$path" "$DUCKOMO_SPATIAL_EXTENSION"
            continue
        fi
    fi
    if [[ "$(basename "$path")" == domain_reference_test ]];then
        if [[ -n "${DUCKOMO_DOMAIN_FILE:-}" && -d "${DUCKOMO_DOMAIN_REFERENCE:-}" ]];then
            "$path" "$DUCKOMO_DOMAIN_FILE" "$ROOT/test/data/domain-manifest.json" "$DUCKOMO_DOMAIN_REFERENCE"
        else echo 'domain_reference_test: not-run (DUCKOMO_DOMAIN_FILE/DUCKOMO_DOMAIN_REFERENCE required)';fi
        continue
    fi
    [[ "$(basename "$path")" != official_httpfs_test ]] || { [[ "$LOCAL_ONLY" == false ]] || continue; }
    case "$(basename "$path")" in
        region_input_test|region_geometry_test|region_scan_test) continue;;
        region_sampling_test) [[ "$SAMPLING" == true ]] || continue;;
    esac
    DUCKOMO_EXTENSION_PATH="$BUILD/extension/duckomo/duckomo.duckdb_extension" "$path"
done
for file in test/tools/*_test.py;do python3 "$file";done
if [[ "$SAMPLING" == true ]]; then
    [[ -x "$CLI" ]] || { echo 'Sampling validation requires a matching DuckDB CLI' >&2; exit 2; }
    [[ -n "$SPATIAL" && -f "$SPATIAL" ]] || { echo 'Sampling validation requires --spatial matching the selected DuckDB runtime' >&2; exit 2; }
    SAMPLING_VERSION="${PAIR:-$($CLI --version | awk '{print $1}')}"
    SAMPLING_OUTPUT="${DUCKOMO_SAMPLING_EVIDENCE_DIR:-}"
    if [[ -z "$SAMPLING_OUTPUT" ]]; then
        mkdir -p "$ROOT/build/evidence/sampling"
        SAMPLING_OUTPUT="$(mktemp -d "$ROOT/build/evidence/sampling/local-$SAMPLING_VERSION-XXXXXXXX")"
    else
        [[ "$SAMPLING_OUTPUT" = /* ]] || SAMPLING_OUTPUT="$ROOT/$SAMPLING_OUTPUT"
    fi
    if [[ "$SAMPLING_RUNTIME_VERIFIED" != true ]]; then
        echo 'Sampling runtime probe and package verification did not run' >&2
        exit 2
    fi
    ACTUAL_SPATIAL_SHA="$(sha256sum "$SPATIAL" | awk '{print $1}')"
    [[ "$ACTUAL_SPATIAL_SHA" == "$SAMPLING_SPATIAL_SHA" ]] || { echo "Sampling spatial package hash mismatch for $SAMPLING_VERSION" >&2; exit 2; }
    SPATIAL_SHA="$SAMPLING_SPATIAL_SHA"
    export DUCKOMO_SPATIAL_EXTENSION="$(realpath "$SPATIAL")" DUCKOMO_SPATIAL_SHA256="$SPATIAL_SHA"
    python3 "$ROOT/scripts/validate-sampling.py" --root "$ROOT" --build "$BUILD" --duckdb "$CLI" \
        --extension "$BUILD/extension/duckomo/duckomo.duckdb_extension" --spatial "$SPATIAL" \
        --manifest "$ROOT/test/data/sampling/manifest.json" --runtime-matrix "$REGION_RUNTIME_MATRIX" \
        --gates "${SAMPLING_GATES:-N0,N1,N2,N3,N4}" --output "$SAMPLING_OUTPUT"
fi
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
