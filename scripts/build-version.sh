#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"

if [[ $# -ne 1 || ! "$1" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
	printf 'Usage: %s vMAJOR.MINOR.PATCH\n' "$0" >&2
	exit 2
fi

version="$1"
version_dir="$ROOT/build/versions/$version"
source_dir="$version_dir/duckdb"
build_dir="$version_dir/release"

if [[ ! -f "$source_dir/CMakeLists.txt" ]]; then
	if ! git -C "$ROOT/duckdb" rev-parse -q --verify "refs/tags/$version^{commit}" >/dev/null; then
		git -C "$ROOT/duckdb" fetch --depth=1 origin "refs/tags/$version:refs/tags/$version"
	fi
	mkdir -p "$version_dir"
	git -C "$ROOT/duckdb" worktree add --detach "$source_dir" "$version"
fi

actual_version="$(git -C "$source_dir" describe --tags --exact-match 2>/dev/null || true)"
[[ "$actual_version" == "$version" ]] || {
	printf 'DuckDB source at %s does not match %s\n' "$source_dir" "$version" >&2
	exit 1
}

cmake -S "$source_dir" -B "$build_dir" \
	-DCMAKE_BUILD_TYPE=Release \
	-DBUILD_UNITTESTS=TRUE \
	-DBUILD_SHELL=TRUE \
	-DNATIVE_ARCH=FALSE \
	-DDUCKDB_EXTENSION_CONFIGS="$ROOT/extension_config.cmake" \
	-DOVERRIDE_GIT_DESCRIBE="$version" \
	-DUNITTEST_ROOT_DIRECTORY="$ROOT" \
	-DBENCHMARK_ROOT_DIRECTORY="$ROOT" \
	-DENABLE_UNITTEST_CPP_TESTS=FALSE \
	-DENABLE_EXTENSION_AUTOLOADING=FALSE \
	-DENABLE_EXTENSION_AUTOINSTALL=FALSE

build_log="$version_dir/build.log"
printf 'Building DuckDB %s (log: %s)\n' "$version" "$build_log"
if ! cmake --build "$build_dir" --config Release --target shell duckomo_loadable_extension unittest \
	--parallel "${DUCKOMO_BUILD_JOBS:-4}" >"$build_log" 2>&1; then
	tail -n 80 "$build_log" >&2
	exit 1
fi

cd "$ROOT"
for test_file in test/sql/*.test; do
	"$build_dir/test/unittest" "$test_file"
done

printf '\nBuilt and tested DuckDB %s extension: %s\n' "$version" "$build_dir/extension/duckomo/duckomo.duckdb_extension"
