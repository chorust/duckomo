#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="$repo_root/build/httpfs-stage-v2"

while (($#)); do
	case "$1" in
	--output)
		if [[ "$2" == /* ]]; then
			output_dir="$2"
		else
			output_dir="$repo_root/$2"
		fi
		shift 2
		;;
	*)
		echo "unknown argument: $1" >&2
		exit 2
		;;
	esac
done

source_dir="$repo_root/third_party/duckdb-httpfs"
manifest="$repo_root/third_party/httpfs-patches/manifest.json"
patch_dir="$repo_root/third_party/httpfs-patches"
stage_dir="$output_dir/source"
marker="$output_dir/stage.json"

python3 - "$repo_root" "$manifest" "$source_dir" <<'PY'
import hashlib
import json
import pathlib
import subprocess
import sys

root = pathlib.Path(sys.argv[1])
manifest_path = pathlib.Path(sys.argv[2])
source = pathlib.Path(sys.argv[3])
manifest = json.loads(manifest_path.read_text())
pin = manifest["httpfs"]["upstream_commit"]
actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
if actual != pin:
    raise SystemExit(f"httpfs checkout is {actual}, expected pinned commit {pin}")
duckdb = root / "duckdb"
duckdb_actual = subprocess.check_output(["git", "-C", str(duckdb), "rev-parse", "HEAD"], text=True).strip()
if duckdb_actual != manifest["duckdb_commit"]:
    raise SystemExit(f"DuckDB checkout is {duckdb_actual}, expected pinned commit {manifest['duckdb_commit']}")
for patch in sorted(manifest["httpfs"]["patches"], key=lambda item: item["order"]):
    path = root / patch["file"]
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != patch["sha256"]:
        raise SystemExit(f"patch hash mismatch: {path}")
header = manifest["httpfs"]["shared_header"]
header_path = root / header["file"]
if hashlib.sha256(header_path.read_bytes()).hexdigest() != header["sha256"]:
    raise SystemExit(f"shared header hash mismatch: {header_path}")
PY

mkdir -p "$output_dir"
manifest_sha="$(python3 - "$manifest" <<'PY'
import hashlib, pathlib, sys
print(hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest())
PY
)"

if [[ -e "$stage_dir" ]]; then
	if [[ -f "$marker" ]] && python3 - "$marker" "$manifest_sha" "$stage_dir" <<'PY'
import hashlib, json, pathlib, sys
data = json.load(open(sys.argv[1]))
root = pathlib.Path(sys.argv[3])
digest = hashlib.sha256()
for path in sorted(p for p in root.rglob("*") if p.is_file() and ".git" not in p.parts):
    digest.update(path.relative_to(root).as_posix().encode())
    digest.update(path.read_bytes())
valid = data.get("manifest_sha256") == sys.argv[2] and data.get("source_tree_sha256") == digest.hexdigest()
valid = valid and "ValidateOmRangeResponse" in (root / "src/httpfs.cpp").read_text()
valid = valid and "httpfs_om_range_capabilities" in (root / "src/httpfs_extension.cpp").read_text()
raise SystemExit(0 if valid else 1)
PY
	then
		echo "DUCKOMO_HTTPFS_SOURCE_DIR=$stage_dir"
		exit 0
	fi
	echo "refusing to replace an existing httpfs stage with different provenance: $stage_dir" >&2
	exit 1
fi

temporary_dir="$(mktemp -d "$output_dir/.httpfs-stage.XXXXXX")"
trap 'rm -rf "$temporary_dir"' EXIT
mkdir -p "$temporary_dir/source/src/include"
pin="$(python3 - "$manifest" <<'PY'
import json, sys
print(json.load(open(sys.argv[1]))["httpfs"]["upstream_commit"])
PY
)"
git -C "$source_dir" archive "$pin" | tar -x -C "$temporary_dir/source"
cp "$patch_dir/httpfs_om_range.hpp" "$temporary_dir/source/src/include/httpfs_om_range.hpp"
git -C "$temporary_dir/source" init -q

python3 - "$manifest" "$patch_dir" "$temporary_dir/source" <<'PY'
import json
import pathlib
import subprocess
import sys

manifest = json.loads(pathlib.Path(sys.argv[1]).read_text())
patch_root = pathlib.Path(sys.argv[2])
source = pathlib.Path(sys.argv[3])
patches = sorted(manifest["httpfs"]["patches"], key=lambda item: item["order"])
for patch in patches:
    patch_path = patch_root / pathlib.Path(patch["file"]).name
    subprocess.run(["git", "-C", str(source), "apply", "--check", str(patch_path)], check=True)
    subprocess.run(["git", "-C", str(source), "apply", str(patch_path)], check=True)
PY

rm -rf "$temporary_dir/source/.git"
if ! rg -q 'ValidateOmRangeResponse' "$temporary_dir/source/src/httpfs.cpp" ||
	! rg -q 'httpfs_om_range_capabilities' "$temporary_dir/source/src/httpfs_extension.cpp"; then
	echo "staged httpfs does not contain every manifest patch" >&2
	exit 1
fi
mv "$temporary_dir/source" "$stage_dir"
python3 - "$marker" "$manifest_sha" "$stage_dir" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[3])
digest = hashlib.sha256()
for path in sorted(p for p in root.rglob("*") if p.is_file()):
    digest.update(path.relative_to(root).as_posix().encode())
    digest.update(path.read_bytes())
data = {"manifest_sha256": sys.argv[2], "source_tree_sha256": digest.hexdigest()}
pathlib.Path(sys.argv[1]).write_text(json.dumps(data, indent=2) + "\n")
PY
echo "DUCKOMO_HTTPFS_SOURCE_DIR=$stage_dir"
