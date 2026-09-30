#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"

usage() {
	cat <<'USAGE'
Usage: scripts/validate.sh [release-build-directory]

Run the DuckOMO SQLLogicTests, spatial/raw/projection native checks, fixture
validation, projection metrics harness, and evidence schema checks. The real
domain release gate runs when DUCKOMO_DOMAIN_FILE points to the pinned sample.
The default release build is build/release. Set DUCKOMO_EVIDENCE_DIR to change
the evidence output directory. Requires jq, sha256sum, diff, and release tools.
USAGE
}

die() {
	printf 'validate.sh: error: %s\n' "$*" >&2
	exit 1
}

case "${1:-}" in
	-h|--help)
		usage
		exit 0
		;;
esac
[[ $# -le 1 ]] || { usage >&2; die "expected at most one build directory"; }

BUILD_INPUT="${1:-$REPO_ROOT/build/release}"
if [[ "$BUILD_INPUT" != /* ]]; then
	BUILD_INPUT="$PWD/$BUILD_INPUT"
fi
[[ -d "$BUILD_INPUT" ]] || die "release build directory does not exist: $BUILD_INPUT (run make release first)"
BUILD_DIR="$(cd -- "$BUILD_INPUT" && pwd -P)"

# The projection native test loads the release extension from this fixed path.
EXPECTED_BUILD_DIR="$(cd -- "$REPO_ROOT/build/release" 2>/dev/null && pwd -P)" || die "build/release is missing"
[[ "$BUILD_DIR" == "$EXPECTED_BUILD_DIR" ]] || die "the native projection check expects $EXPECTED_BUILD_DIR because its extension loader uses that path"

EVIDENCE_INPUT="${DUCKOMO_EVIDENCE_DIR:-$REPO_ROOT/build/evidence}"
if [[ "$EVIDENCE_INPUT" != /* ]]; then
	EVIDENCE_INPUT="$REPO_ROOT/$EVIDENCE_INPUT"
fi
EVIDENCE_DIR="$EVIDENCE_INPUT"
FIXTURE_DIR="$REPO_ROOT/test/data"
MANIFEST="$FIXTURE_DIR/manifest.json"
DOMAIN_MANIFEST="$FIXTURE_DIR/domain-manifest.json"

for tool in jq sha256sum diff mktemp; do
	command -v "$tool" >/dev/null 2>&1 || die "required command not found: $tool"
done

REQUIRED_EXECUTABLES=(
	"$BUILD_DIR/duckdb"
	"$BUILD_DIR/test/unittest"
	"$BUILD_DIR/test/tools/duckomo_fixture_tool"
	"$BUILD_DIR/test/tools/duckomo_validation"
	"$BUILD_DIR/test/tools/duckomo_spatial_validation"
	"$BUILD_DIR/test/native/batch_test"
	"$BUILD_DIR/test/native/raw_reader_test"
	"$BUILD_DIR/test/native/lifecycle_test"
	"$BUILD_DIR/test/native/schema_test"
	"$BUILD_DIR/test/native/projection_evidence_test"
	"$BUILD_DIR/test/native/regular_grid_test"
	"$BUILD_DIR/test/native/spatial_layout_test"
	"$BUILD_DIR/test/native/spatial_metrics_test"
	"$BUILD_DIR/test/native/spatial_callback_test"
	"$BUILD_DIR/test/native/spatial_selection_test"
	"$BUILD_DIR/test/native/spatial_io_test"
	"$BUILD_DIR/test/native/spatial_lifecycle_test"
	"$BUILD_DIR/test/native/domain_reference_test"
	"$BUILD_DIR/extension/duckomo/duckomo.duckdb_extension"
)
for executable in "${REQUIRED_EXECUTABLES[@]}"; do
	[[ -x "$executable" ]] || die "required release artifact is missing or not executable: $executable"
done
[[ -f "$MANIFEST" ]] || die "fixture manifest is missing: $MANIFEST"

run() {
	local description="$1"
	shift
	printf '\n== %s ==\n+' "$description"
	printf ' %q' "$@"
	printf '\n'
	"$@"
}

run_sqllogictest() {
	local description="$1"
	shift
	local output exit_code
	printf '\n== %s ==\n+' "$description"
	printf ' %q' "$@"
	printf '\n'
	if output="$("$@" 2>&1)"; then
		printf '%s\n' "$output"
	else
		exit_code=$?
		printf '%s\n' "$output" >&2
		die "$description failed with exit code $exit_code"
	fi
	[[ "$output" == *"All tests passed ("* && "$output" != *"All tests passed (0 assertions"* ]] || die "$description completed without running assertions"
}

cd -- "$REPO_ROOT"

check_fixture_hashes() {
	local entries relative expected actual count=0 expected_count
	jq -e '
		.schema_version == 1 and
		([.fixtures[].fixture_id] | index("raw") != null) and
		([.fixtures[].fixture_id] | index("multi") != null) and
		([.fixtures[].fixture_id] | index("nested") != null) and
		([.fixtures[].fixture_id] | index("special") != null) and
		([.fixtures[].fixture_id] | index("raw_large") != null) and
		([.fixtures[].fixture_id] | index("projection") != null) and
		([.fixtures[].fixture_id] | index("pfor_attributes") != null) and
		([.fixtures[].fixture_id] | index("spatial_flat") != null) and
		([.fixtures[].fixture_id] | index("spatial_axes") != null) and
		([.fixtures[].fixture_id] | index("spatial_single") != null) and
		([.fixtures[].fixture_id] | index("spatial_conflict") != null) and
		([.projection_scenarios.scenarios[].scenario_id] | sort) ==
			["count", "full_scan", "output_plus_filter", "single_variable"] and
		([.negative_assets | length] >= 7)
	' "$MANIFEST" >/dev/null || die "fixture manifest is invalid or lacks required fixture/scenario entries"

	entries="$(jq -er '
		[(.fixtures[] | [.path, .sha256]),
		 (.fixtures[] | select(.reference_csv? != null) | [.reference_csv, .reference_csv_sha256]),
		 (.fixtures[].variables[]? | [.reference_csv, .reference_csv_sha256]),
		 (.negative_assets[] | [.path, .sha256])]
		| .[] | @tsv
	' "$MANIFEST")" || die "cannot enumerate fixture checksums from $MANIFEST"
	expected_count="$(jq -er '
		[(.fixtures[] | [.path, .sha256]),
		 (.fixtures[] | select(.reference_csv? != null) | [.reference_csv, .reference_csv_sha256]),
		 (.fixtures[].variables[]? | [.reference_csv, .reference_csv_sha256]),
		 (.negative_assets[] | [.path, .sha256])]
		| length
	' "$MANIFEST")" || die "cannot count fixture checksum entries from $MANIFEST"
	while IFS=$'\t' read -r relative expected; do
		[[ -n "$relative" && -n "$expected" ]] || die "manifest contains an empty checksum entry"
		[[ "$relative" != /* && "/$relative/" != *"/../"* ]] || die "manifest path is not a safe relative path: $relative"
		[[ "$expected" =~ ^[0-9a-f]{64}$ ]] || die "manifest has an invalid SHA-256 for $relative"
		[[ -f "$FIXTURE_DIR/$relative" ]] || die "fixture asset is missing: $relative"
		actual="$(sha256sum -- "$FIXTURE_DIR/$relative")"
		actual="${actual%% *}"
		[[ "$actual" == "$expected" ]] || die "SHA-256 mismatch for $relative: expected $expected, got $actual"
		count=$((count + 1))
	done <<< "$entries"
	[[ "$count" -eq "$expected_count" ]] || die "expected $expected_count fixture/reference/negative checksums, verified $count"
	printf 'Verified %s fixture, reference, and negative asset SHA-256 values.\n' "$count"
}

check_domain_manifest() {
	[[ -s "$DOMAIN_MANIFEST" ]] || die "real-domain identity manifest is missing: $DOMAIN_MANIFEST"
	jq -e '
		type == "object" and .schema_version == 1 and .domain == "ncep_gfswave025" and
		.sample.bytes == 5812040 and
		.sample.sha256 == "0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd" and
		.upstream.commit == "34b9cea169395be9b4686f2b5b23eca26dfef7a2" and
		.layout.shape == [721, 1440] and .layout.axes == ["lat", "lon"] and
		.layout.grid.nx == 1440 and .layout.grid.ny == 721 and
		.layout.grid.order == "separate" and
		.layout.expected_bbox == [-90, -180, 90, 179.75] and
		.oracle.rows_per_variable == 1038240 and
		.oracle.coordinate_absolute_tolerance_degrees == 1e-9 and
		.oracle.value_tolerance == 0 and
		([.oracle.variables[].path] | length == 15) and
		([.oracle.variables[].path] | unique | length == 15) and
		all(.oracle.variables[]; (.sha256 | test("^[0-9a-f]{64}$")))
	' "$DOMAIN_MANIFEST" >/dev/null || die "real-domain identity manifest is invalid or differs from the pinned sample contract"
	printf 'Validated the separate ncep_gfswave025 source identity manifest.\n'
}

check_evidence() {
	local summary="$EVIDENCE_DIR/summary.json"
	local fixture_sha scenario scenario_file
	local scenarios=(full_scan single_variable output_plus_filter count)

	[[ -s "$summary" ]] || die "validation harness did not produce $summary"
	jq -e '
		type == "object" and
		.schema_version == 1 and .status == "success" and .fixture_id == "projection" and
		(.fixture_sha256 | type == "string" and test("^[0-9a-f]{64}$")) and
		(.dependency_commits | type == "object" and
			.duckdb == "08e34c447bae34eaee3723cac61f2878b6bdf787" and
			.["om-file-format"] == "d8855e418e2231ae8439f0c7e840fa3f93b371e3" and
			.["extension-ci-tools"] == "b777c70d30942cca5bef62d6d4fa23a13362f398") and
		.scenario_count == 4 and
		(.required_scenarios | sort) == ["count", "full_scan", "output_plus_filter", "single_variable"] and
		(.scenarios | type == "object" and (keys | sort) == ["count", "full_scan", "output_plus_filter", "single_variable"]) and
		(.full_scan_variable_data_bytes | type == "number" and . > 0) and
		(.single_variable_data_bytes | type == "number" and . > 0) and
		.single_variable_data_bytes_reduced == true and
		(.single_variable_data_bytes < .full_scan_variable_data_bytes) and
		(.environment | type == "object" and .build == "release" and .threads == "1") and
		(.cache_policy | type == "object" and (.application_cache | type == "string" and length > 0) and (.os_page_cache | type == "string" and length > 0))
	' "$summary" >/dev/null || die "summary.json is invalid or lacks required success, dependency, scenario, or comparison evidence"
	fixture_sha="$(jq -r '.fixture_sha256' "$summary")"

	for scenario in "${scenarios[@]}"; do
		scenario_file="$EVIDENCE_DIR/$scenario.json"
		[[ -s "$scenario_file" ]] || die "required scenario evidence is missing: $scenario_file"
		jq -e --arg scenario "$scenario" --arg fixture_sha "$fixture_sha" '
			type == "object" and
			.schema_version == 2 and .query_id == ("projection_" + $scenario) and .scenario == $scenario and
			(.sql | type == "string" and length > 0) and
			.fixture_id == "projection" and .fixture_sha256 == $fixture_sha and .status == "success" and
			(.dependency_commits | type == "object" and
				.duckdb == "08e34c447bae34eaee3723cac61f2878b6bdf787" and
				.["om-file-format"] == "d8855e418e2231ae8439f0c7e840fa3f93b371e3" and
				.["extension-ci-tools"] == "b777c70d30942cca5bef62d6d4fa23a13362f398") and
			(.result_rows | type == "number" and . >= 0) and
			.comparison_passed == true and (.comparison | type == "string" and length > 0) and
			(.metadata_bytes | type == "number" and . >= 0) and
			(.metadata_requests | type == "number" and . >= 0) and
			(.variables | type == "object" and all(.[];
				(.index_bytes | type == "number" and . >= 0) and
				(.index_requests | type == "number" and . >= 0) and
				(.data_bytes | type == "number" and . >= 0) and
				(.data_requests | type == "number" and . >= 0) and
				(.decoded_chunks | type == "number" and . >= 0) and .decode_count_complete == true)) and
			.decode_count_complete == true and
			(.bytes_fetched == (.metadata_bytes + ([.variables[].index_bytes] | add // 0) + ([.variables[].data_bytes] | add // 0))) and
			(.read_requests == (.metadata_requests + ([.variables[].index_requests] | add // 0) + ([.variables[].data_requests] | add // 0))) and
			(.elapsed_ms | type == "number" and . >= 0) and
			(.peak_rss_bytes | type == "number" and . > 0) and
			(.environment | type == "object" and .build == "release" and .threads == "1" and (.system | type == "string" and length > 0) and (.machine | type == "string" and length > 0)) and
			(.cache_policy | type == "object" and (.application_cache | type == "string" and length > 0) and (.os_page_cache | type == "string" and length > 0)) and
			.error_category == "" and .child_exit_code == 0 and
			(.command | type == "array" and length > 0)
		' "$scenario_file" >/dev/null || die "scenario evidence has missing/invalid required fields or inconsistent metrics: $scenario_file"
		jq -e --arg scenario "$scenario" '.scenarios[$scenario] == ($scenario + ".json")' "$summary" >/dev/null || die "summary.json does not reference $scenario.json"
		case "$scenario" in
			full_scan)
				jq -e '
					.result_rows == 10541 and
					.variables["/humidity"].data_bytes > 0 and .variables["/humidity"].decoded_chunks > 0 and
					.variables["/pressure"].data_bytes > 0 and .variables["/pressure"].decoded_chunks > 0 and
					.variables["/temperature"].data_bytes > 0 and .variables["/temperature"].decoded_chunks > 0
				' "$scenario_file" >/dev/null || die "full_scan evidence does not show all 10,541 expected rows and all three decoded variables"
				;;
			single_variable)
				jq -e '
					.result_rows == 10541 and
					.variables["/temperature"].data_bytes > 0 and .variables["/temperature"].decoded_chunks > 0 and
					(.variables["/humidity"].data_bytes // 0) == 0 and (.variables["/humidity"].decoded_chunks // 0) == 0 and
					(.variables["/pressure"].data_bytes // 0) == 0 and (.variables["/pressure"].decoded_chunks // 0) == 0
				' "$scenario_file" >/dev/null || die "single_variable evidence violates row or projection-read requirements"
				;;
			output_plus_filter)
				jq -e '
					.result_rows == 108 and
					.variables["/humidity"].data_bytes > 0 and .variables["/humidity"].decoded_chunks > 0 and
					.variables["/temperature"].data_bytes > 0 and .variables["/temperature"].decoded_chunks > 0 and
					(.variables["/pressure"].data_bytes // 0) == 0 and (.variables["/pressure"].decoded_chunks // 0) == 0
				' "$scenario_file" >/dev/null || die "output_plus_filter evidence violates row or filter-dependency requirements"
				;;
			count)
				jq -e '(.result_rows == 1) and all(.variables[]; .index_bytes == 0 and .index_requests == 0 and .data_bytes == 0 and .data_requests == 0 and .decoded_chunks == 0)' "$scenario_file" >/dev/null || die "count evidence shows value/index reads, decoded chunks, or an incorrect result-row count"
				;;
		esac
	done

	jq -e --slurpfile full "$EVIDENCE_DIR/full_scan.json" --slurpfile single "$EVIDENCE_DIR/single_variable.json" '
		.full_scan_variable_data_bytes == ([$full[0].variables[].data_bytes] | add // 0) and
		.single_variable_data_bytes == ([$single[0].variables[].data_bytes] | add // 0)
	' "$summary" >/dev/null || die "summary data-byte totals do not match their scenario evidence"
	printf 'Validated summary and all four scenario evidence files.\n'
}

for sql_test in raw read_om projection spatial spatial_pushdown spatial_composition; do
	run_sqllogictest "SQLLogicTest: $sql_test.test" "$BUILD_DIR/test/unittest" "test/sql/$sql_test.test"
done

for native_test in batch_test raw_reader_test lifecycle_test schema_test projection_evidence_test regular_grid_test spatial_layout_test \
	spatial_metrics_test spatial_callback_test spatial_selection_test spatial_io_test spatial_lifecycle_test; do
	run "Native check: $native_test" "$BUILD_DIR/test/native/$native_test"
done

check_fixture_hashes
check_domain_manifest

TEMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/duckomo-validate.XXXXXX")" || die "cannot create fixture regeneration directory"
trap 'rm -rf -- "$TEMP_DIR"' EXIT
run "Regenerate fixtures using the pinned official writer" "$BUILD_DIR/test/tools/duckomo_fixture_tool" --output "$TEMP_DIR/fixtures"
# domain-manifest.json is intentionally pinned separately from regenerable
# synthetic fixtures; real-domain references are produced by the dedicated
# domain harness and are never regenerated from the synthetic fixture tool.
run "Compare regenerated synthetic fixtures and manifest" diff -qr --exclude=domain-manifest.json -- "$FIXTURE_DIR" "$TEMP_DIR/fixtures"

mkdir -p -- "$EVIDENCE_DIR"
run "Release projection metrics harness" \
	"$BUILD_DIR/test/tools/duckomo_validation" \
	--root "$REPO_ROOT" \
	--fixtures "$FIXTURE_DIR" \
	--output "$EVIDENCE_DIR" \
	--duckdb "$BUILD_DIR/duckdb" \
	--extension "$BUILD_DIR/extension/duckomo/duckomo.duckdb_extension"
check_evidence

if [[ -n "${DUCKOMO_DOMAIN_FILE:-}" ]]; then
	[[ -f "$DUCKOMO_DOMAIN_FILE" ]] || die "DUCKOMO_DOMAIN_FILE is not a regular file: $DUCKOMO_DOMAIN_FILE"
	run "Complete spatial/domain validation harness" \
		"$BUILD_DIR/test/tools/duckomo_spatial_validation" \
		--root "$REPO_ROOT" \
		--fixtures "$FIXTURE_DIR" \
		--output "$EVIDENCE_DIR/spatial" \
		--duckdb "$BUILD_DIR/duckdb" \
		--extension "$BUILD_DIR/extension/duckomo/duckomo.duckdb_extension" \
		--domain-file "$DUCKOMO_DOMAIN_FILE"
else
	printf '\nReal-domain release gate not run. Set DUCKOMO_DOMAIN_FILE to the pinned sample path to run it.\n'
fi

printf '\nDuckOMO validation passed. Evidence: %s\n' "$EVIDENCE_DIR"
