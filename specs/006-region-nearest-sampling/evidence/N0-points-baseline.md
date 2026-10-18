# N0 frozen points baseline

**Status:** N0 passed in the registered validator run [`run-20261017-sampling-validation-final`](run-20261017-sampling-validation-final/): exact nearest spatial signatures, ordinary `read_om` without spatial, points dependency checks, and frozen ordered rows/stable metrics replay succeeded on v1.5.4, v1.5.5, and v1.5.6. The replay uses the current sampling-enabled extension; it verifies that points behavior and stable reader metrics remain unchanged. Nearest result acceptance belongs to N1/N2 and passes for their declared synthetic regular spherical grid and NCEP GFS025 scope; N5's local/transport subchecks are recorded separately, N6 remains incomplete, and the existing local N7 reproduction does not meet the independent-executor requirement.

**Recorded:** 2026-10-15

## Source and artifact identity

- Repository HEAD: `d2a134d04ba4fd95b9c26e9f6ec656926709c336` (`main`).
- Working tree has pre-existing user changes in documentation/specification and `.specify/extensions/roadmap`; none of `src/`, `test/`, `scripts/`, build files, `.gitignore`, or `.dockerignore` had changes at baseline capture.
- DuckDB: v1.5.4, source `08e34c447bae34eaee3723cac61f2878b6bdf787`; DuckDB CLI SHA-256 `7b02b214c67f7394af7383ed7e805bfd4e4500db07efc28c203645310bca773d`.
- Built duckomo extension SHA-256: `f5a294510d22720c466a33b9ad89397edb0bfb02d96a1470ffda2fd7a8cf97bd`.
- Matching spatial package: `build/regions-runtime/v1.5.4/official/spatial.duckdb_extension`, SHA-256 `af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a`; runtime matrix records DuckDB v1.5.4, spatial source `28db190f7184bcf61eb01d291e0cba79849bddb6`, extension version `28db190`.
- OM reader submodule: `d8855e418e2231ae8439f0c7e840fa3f93b371e3`; extension-ci-tools submodule: `b777c70d30942cca5bef62d6d4fa23a13362f398`.
- Baseline fixture `test/data/regions/regions.om` SHA-256 `264fe0905d0681d0fedd08d60a43bf7974cbd161319df77e919f08b4cb582e36`.
- Existing test harness and build-source identities: `.gitmodules` SHA-256 `eb141b40679083237af6d16637102cb0e4d51602cfc30c96c11f202b74f9c580`; `CMakeLists.txt` `7bd12bde3746681f556bb1aacb1bff4d44369debe1f524298857d46641119649`; `test/CMakeLists.txt` `f1978c58de991a778a410c98174d34a298f121dbfa2e71da9e8cbcfdb976f418`; `test/data/grids/version-matrix.json` `c72b11267bd1f8795f3c1ddb96d07a74bf3e292bbf3e560cf4174300ae80d060`.
- Frozen independent membership oracle `test/data/regions/regions-membership.reference.csv` SHA-256 `5d2777324982402abfe6aad9f9dc1b8df4ec93c153dcfe45afe5f761108c59b4`.
- Compile context: Linux AArch64, `/usr/bin/c++`, Release `-O3 -DNDEBUG`, configured global CXX flags empty. No sampling numeric kernel was built or measured.

## Commands and outcomes

All commands ran against the clean-at-capture implementation sources described above and the v1.5.4 runtime.

1. `DUCKOMO_TEST_EXTENSION="$PWD/build/release/extension/duckomo/duckomo.duckdb_extension" DUCKOMO_CORE_FUNCTIONS_EXTENSION="$PWD/build/release/extension/core_functions/core_functions.duckdb_extension" DUCKOMO_SPATIAL_EXTENSION="$PWD/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension" build/release/test/unittest --test-dir "$PWD" test/sql/regions.test` — exit 0; **89 assertions passed**. This includes the legacy schema (`value FLOAT`, `lat/lon DOUBLE`, semantic axes, `region_id`, optional trailing `om_source`), exact typed points membership and NULL/original-value checks, and bidirectional `EXCEPT ALL` differences of zero against both the full `read_om` + `ST_Covers` relation and the independent frozen Python membership oracle (438 rows).
2. `DUCKOMO_TEST_EXTENSION="$PWD/build/release/extension/duckomo/duckomo.duckdb_extension" DUCKOMO_CORE_FUNCTIONS_EXTENSION="$PWD/build/release/extension/core_functions/core_functions.duckdb_extension" DUCKOMO_SPATIAL_EXTENSION="$PWD/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension" build/release/test/unittest --test-dir "$PWD" test/sql/regions_spatial_dependency.test` — exit 0; **5 assertions passed**.
3. `build/release/test/native/region_input_test` — exit 0; `region_input_test: ok`.
4. `build/release/test/native/region_geometry_test "$PWD/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension" af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a` — exit 0; printed DuckDB v1.5.4, source `08e34c44`, spatial extension version `28db190`, the package hash above, and `region_geometry_test: ok`.
5. `DUCKOMO_CORE_FUNCTIONS_EXTENSION="$PWD/build/release/extension/core_functions/core_functions.duckdb_extension" build/release/test/native/region_scan_test "$PWD/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension" "$PWD/build/release/extension/core_functions/core_functions.duckdb_extension"` — exit 0; task windows, source ordinals, zero-value I/O, metrics, cancellation, short-read/decoder/object-change recovery and connection isolation passed.

## Captured full result

The reproducible capture is [N0-points.sql](N0-points.sql). The full ordered rows are in [N0-points-rows.csv](N0-points-rows.csv): 438 data rows, SHA-256 `b453c656c5013c53568c123b331ff3617fe0fcb51f0e6476cf15c4313c1596e6`. The metrics output is in [N0-points-metrics.csv](N0-points-metrics.csv), SHA-256 `7de4d3b7f379800e6ad4a64943f1f86aef8415522c36a680fb5bd019f8ba8fbd`. The capture SQL SHA-256 is `f658ac5c08464aab2c02f102fb9a66abb50a9a99073a67b71de3c5e4fcf49858`.

The observed DESCRIBE output, captured separately because DuckDB's CLI does not allow DESCRIBE as a COPY subquery, was:

```text
column_name | column_type
value       | FLOAT
lat         | DOUBLE
lon         | DOUBLE
valid_time  | TIMESTAMP
member      | BIGINT
region_id   | VARCHAR
om_source   | STRUCT(object_id VARCHAR, object_version VARCHAR, version_strength VARCHAR, content_verified BOOLEAN, grid_id VARCHAR, layout_id VARCHAR, logical_index UBIGINT, point_index UBIGINT, parent_point_index UBIGINT, axis_indices UBIGINT[])
```

This freezes the points behavior and artifacts used in the omitted-method/explicit-points compatibility check. The initial capture exercised points before `method` existed; it makes no nearest result, locator, representative geometry, sampling I/O, or performance claim. The latest registered replay uses the sampling-enabled implementation and confirms the frozen points rows/metrics still match. The existing regions runtime manifest SHA-256 at capture is `d6b3d9d9211c03d6b3c9efaa22036577eefe366fbf4788734bcec0d3912c27ff`; it remains unchanged.

## Final replay against the implementation build

The baseline SQL was rerun against the current v1.5.4 extension with the frozen paths redirected into a new `/tmp` output directory. The ordered rows are byte-identical: both the frozen and replayed CSV have SHA-256 `b453c656c5013c53568c123b331ff3617fe0fcb51f0e6476cf15c4313c1596e6`.

The validator recursively compares stable reader/output metrics while omitting the named volatile fields, including timing, RSS, active-worker and memory-accounting values, as well as artifact-specific build identity. This is equality of the canonical stable projection, not byte equality of the full metrics JSON. A unit check now proves nested `legacy_v3`/`legacy_v2` timing and worker fields are ignored while stable read totals still affect equality. The frozen N0 CSV and metrics hash were not rewritten.

### Worker-schedule investigation

The initial 150-byte/6-request versus 175-byte/7-request value-index observation was a one-run scheduling artifact: five subsequent N0 runs produced 175/7, with matching rows and all other read totals. To reproduce the capture conditions explicitly, five fresh processes replayed the SQL with `threads=9` and `duckomo_max_threads=9` set after extension loading. All five had byte-identical rows (SHA-256 `b453c656c5013c53568c123b331ff3617fe0fcb51f0e6476cf15c4313c1596e6`), identical reader metrics after the validator's canonicalization, 1,310 metadata bytes/30 requests, 175 index bytes/7 requests, 1,535 data bytes/114 requests, and 222 decoded chunks. Each reported eight active workers; in several runs the peak was below eight. The historical frozen metrics CSV was not changed.

A separate diagnostic matrix varied `threads` and `duckomo_max_threads` independently. It showed that setting either limit to 8 caps the region scan at eight workers and omits three worker-reader metadata ranges (131 bytes); using `threads=9` with extension limit 0 or 9 preserves 1,310/30 metadata reads. The validator now injects `SET threads=9` and `SET duckomo_max_threads=9` after loading extensions in its generated replay SQL. This condition reproduces the frozen output and canonical reader metrics in five fresh processes; it records the setting in the new evidence package rather than changing the historical capture SQL. The one 150/6 observation was not a product regression. Diagnostic outputs remain under `/tmp/duckomo-n0-*`.

The latest registered N0–N4 package is [`run-20261017-sampling-validation-final`](run-20261017-sampling-validation-final/); its manifest binds per-version CLI, DuckOMO extension, and spatial hashes. Every runtime executed points SQL regressions and replayed frozen ordered rows plus stable metrics. All three row hashes match `b453c656c5013c53568c123b331ff3617fe0fcb51f0e6476cf15c4313c1596e6`; canonical metrics comparisons match the frozen stable projection. The controlled replay sets `threads=9` and `duckomo_max_threads=9`; the frozen SQL/CSV/metrics were not changed. The runtime matrix binds the exact ST_PointOnSurface/ST_X/ST_Y/ST_Covers signatures and geometry probe to each spatial package hash. The historical `test/data/regions/runtime-matrix.json` remains unchanged. N0 is separate from the N1/N2 scoped acceptance pass recorded in the 2026-10-19 refresh.
