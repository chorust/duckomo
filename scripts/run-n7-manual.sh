#!/usr/bin/env bash
# Run by the independent human validator; no downloads or builds.
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$ROOT"
if [[ "$#" -ne 2 || "$1" != --attested-by || -z "${2//[[:space:]]/}" ]]; then
    printf 'Usage: bash scripts/run-n7-manual.sh --attested-by "Your name"\nBy supplying your name, you attest that you are the independent human executor.\n' >&2
    exit 1
fi
ATTESTED_BY="$2"
CLI="$ROOT/build/regions-runtime/v1.5.5/official/duckdb"
SQL="$ROOT/specs/006-region-nearest-sampling/evidence/user-manual-v1.5.5/full-acceptance.sql"
EXT="$ROOT/build/official-matrix/v1.5.5/builds/8c86d97a161b1f2f5b88c7714fe5f36fd896d12c8b53acdc9b679a4fd88de8c9/release/extension/duckomo/duckomo.duckdb_extension"
SPATIAL="$ROOT/build/regions-runtime/v1.5.5/official/spatial.duckdb_extension"
mkdir -p "$ROOT/build/evidence/sampling"
OUTPUT="$(mktemp -d "$ROOT/build/evidence/sampling/n7-human-XXXXXXXX")"
{
    printf 'executor_type=human\nexecution_mode=owner_invoked_local_script\n'
    date -u '+started_at=%Y-%m-%dT%H:%M:%SZ'
    "$CLI" --version
    sha256sum "$CLI" "$EXT" "$SPATIAL" "$SQL" "$ROOT/test/data/regions/regions.om"
} > "$OUTPUT/identity.txt"
cp "$SQL" "$OUTPUT/executed.sql"
printf 'Evidence directory: %s\n' "$OUTPUT"
set +e
"$CLI" -unsigned -bail -f "$OUTPUT/executed.sql" 2>&1 | tee "$OUTPUT/output.txt"
RESULT=${PIPESTATUS[0]}
set -e
printf '%s\n' "$RESULT" > "$OUTPUT/exit-code.txt"
sha256sum "$OUTPUT/identity.txt" "$OUTPUT/executed.sql" "$OUTPUT/output.txt" "$OUTPUT/exit-code.txt" > "$OUTPUT/SHA256SUMS"
if [[ "$RESULT" -ne 0 ]]; then
    printf 'N7 failed; inspect %s/output.txt\n' "$OUTPUT" >&2
    exit "$RESULT"
fi
if ! rg -q N7_FUNCTIONAL_PASS "$OUTPUT/output.txt"; then
    printf 'N7 failed: final success marker missing\n' >&2
    exit 1
fi
python3 "$ROOT/scripts/sampling_independent.py" "$OUTPUT" --attested-by "$ATTESTED_BY"
printf '\nN7 manual checks passed. Retained evidence: %s\n' "$OUTPUT"
