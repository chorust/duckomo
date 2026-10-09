#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
MATRIX="$ROOT/test/data/grids/version-matrix.json"
PAIR="";OUTPUT="$ROOT/build/official-matrix"
if [[ "${1:-}" =~ ^v1\.5\.[456]$ ]];then PAIR="$1";shift;fi
while (($#));do
    case "$1" in
        --matrix) MATRIX="$2";shift 2;;
        --pair) PAIR="$2";shift 2;;
        --output-root) OUTPUT="$2";shift 2;;
        *) echo "unknown build option: $1" >&2;exit 2;;
    esac
done
[[ -n "$PAIR" ]] || { echo 'build-version.sh v1.5.4|v1.5.5|v1.5.6 | --matrix MANIFEST --pair VERSION [--output-root DIR]' >&2;exit 2; }
[[ "$MATRIX" = /* ]] || MATRIX="$ROOT/$MATRIX"
exec python3 "$ROOT/scripts/version_matrix.py" build --root "$ROOT" --matrix "$MATRIX" --pair "$PAIR" --output-root "$OUTPUT"
