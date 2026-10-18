# US1 nearest sampling core evidence

**Status: locally implemented synthetic US1 core; N1/N2 pass for their declared profile scope.** The synthetic-domain nearest scan is implemented and exercised end to end on the pinned Linux AArch64 DuckDB v1.5.4, v1.5.5, and v1.5.6 runtimes. The native scan test checks official-reader CSV parity and reconstructed source indices on each version. This report does not complete US1's full acceptance contract or N1–N7; N5 subchecks are reported separately, N6 audit remains open, and the recorded local N7 reproduction does not meet the independent-executor requirement.

## Runtime identity

- Host: `aarch64` Linux.
- DuckDB: `v1.5.4 (Variegata) 08e34c44`.
- DuckDB CLI: `build/release/duckdb`; ELF AArch64 BuildID `81a54c97c102d4e09212af256d7b971a555f7e64`.
- DuckDB extension: `build/release/extension/duckomo/duckomo.duckdb_extension`; ELF AArch64 BuildID `acdd9b0a73f06baa40bb6126bdc63e24ba44e223`; SHA-256 `9ac434256d3783fcb41f6e6a7f6feaf7449f79e3d2cdc791c876015cf01dfcff`.
- Spatial extension: `build/regions-runtime/v1.5.4/official/spatial.duckdb_extension`; SHA-256 `af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a`.
- The extension build rules apply `-ffp-contract=off` to `src/grid/nearest_sampling.cpp` in both static and loadable targets. The current feature sources were rebuilt on all three pinned Linux AArch64 DuckDB versions.

## Checks

Commands and outcomes:

```text
cmake --build build/release --target duckdb duckomo_loadable_extension nearest_sampling_test region_sampling_test -j2 -> pass
cmake --build build/release --target shell -j2 -> pass
cmake --build build/release --target unittest duckomo_fixture_tool region_sampling_test nearest_sampling_test spatial_layout_test -j2 -> pass
cmake --build build/official-matrix/v1.5.5/release --target duckomo_loadable_extension region_sampling_test -j2 -> pass
cmake --build build/official-matrix/v1.5.6/builds/cf0e88531eb34e8a357a8926a2d54e841245a6ffb6833ca783d15e9216620125/release --target duckomo_loadable_extension region_sampling_test shell -j2 -> pass
DUCKDB_EXTENSION_DIRECTORY=.../v1.5.4/linux_arm64 DUCKOMO_SPATIAL_EXTENSION=.../spatial.duckdb_extension build/release/test/unittest --test-dir "$PWD" "$PWD/test/sql/region_sampling.test" -> pass (176 assertions on each pinned runtime in the latest registered run; see N2-sql-matrix.md)
DUCKDB_EXTENSION_DIRECTORY=.../v1.5.4/linux_arm64 DUCKOMO_SPATIAL_EXTENSION=.../spatial.duckdb_extension build/release/test/unittest --test-dir "$PWD" "$PWD/test/sql/regions.test" -> pass (92 assertions on each pinned runtime)
build/release/test/native/nearest_sampling_test -> nearest_sampling_test: ok
DUCKOMO_SPATIAL_EXTENSION=.../v1.5.4/linux_arm64/spatial.duckdb_extension DUCKOMO_CORE_FUNCTIONS_EXTENSION=.../build/release/extension/core_functions/core_functions.duckdb_extension build/release/test/native/region_scan_test -> pass, including nearest direct-value/source, leading spatial axes, and 513-row multi-task scan checks
DUCKOMO_SPATIAL_EXTENSION=.../v1.5.4/official/spatial.duckdb_extension build/release/test/native/region_sampling_test -> pass, including 12-row all-variable CSV parity, leading spatial axes, and 513-row scan
build/official-matrix/v1.5.5/builds/8c86d97a161b1f2f5b88c7714fe5f36fd896d12c8b53acdc9b679a4fd88de8c9/release/test/native/region_sampling_test .../v1.5.5/spatial.duckdb_extension -> pass
build/official-matrix/v1.5.6/builds/cf0e88531eb34e8a357a8926a2d54e841245a6ffb6833ca783d15e9216620125/release/test/native/region_sampling_test .../v1.5.6/spatial.duckdb_extension -> pass
build/release/test/native/spatial_layout_test -> spatial layout checks passed
```

SQL cases verify explicit legacy separate and longitude-fastest flattened grids, nearest schema, a polygon gap with a point outside its area, 12 rows for 2×3×2 non-spatial positions, one stable nearest source index, both official-reader variable values for all 12 tuples against an independent SQL `EXCEPT ALL` reference, duplicate region IDs staying separate for shared source locations, EMPTY and outside-domain statuses, zero/exact/adjacent-above/adjacent-below `max_distance_m` thresholds, selected time axes, a selected NaN value retaining its source identity, `include_source`, points schema/behavior regression, strict outside-domain errors, and option validation. The registered three-version run passes the 176-assertion nearest suite on each pinned DuckDB runtime. The suite includes catalog/schema checks and method type/case validation. The complete 12-row accepted result is frozen in [US1-core-rows.csv](US1-core-rows.csv), SHA-256 `8bd2fe02e264d0f11b6107e61a9c9134797ca7f0fb2933d5fb94cd9d7ff5e4f0`; the generating SQL is [US1-core.sql](US1-core.sql), SHA-256 `f02ce8aed1b629d63932c5f8a2b53c90c1a01f895f88eda7e65b41297ef4760f`. Each row records the representative target, selected nearest source, distance, original non-spatial indices, source logical/point index, and both variable values/status/counts. The official-reader CSV hashes are temperature `07693c926d2365a0d33edf2dc03472fd3aa67b48e5624faf15beda3b67a479d3` and humidity `9b3ca031d4f7e1e63336d724ce276aa2eeecff718dd08f7765572e6449f91aca`; native comparison independently reconstructs each logical index and checks both original values. N2-sql-matrix.md and the current acceptance refresh record the three-version matrix and finite evidence boundaries.

The explicit-grid runtime smoke test exposed that the locator was being handed the wrong grid-definition holder. The binder now uses `spatial.identity_definition`, which is populated for legacy explicit grids as well as versioned and registered grids. The N0 baseline replay now also confirms the frozen 438 ordered points rows and stable read metrics match the implementation build.

## Reference limits and remaining work

- `test/data/sampling/manifest.json` records official OM reader values; `test/data/sampling/locator.reference.csv` records the independently computed locator. The native locator test validates all 61 non-empty locator rows, exact source indices and coordinates, and ordinary-distance tolerance.
- The SQL/native source/value comparison covers all 12 tuples for separate, longitude-fastest flattened, leading-spatial, and spatial-interleaved layouts; native parity also covers reverse steps and latitude-fastest flattening. SQL checks unmapped axes, a spatial-only file, duplicate semantic labels, a 513-row task sequence, mixed per-variable source nulls, and residual target/value filters. SQL checks malformed WKB and invalid MultiPolygon topology are rejected before WHERE/LIMIT can hide them. Exact and `nextafter` distance thresholds and a selected NaN source are exercised.
- Native `region_sampling_test` initializes the extension and checks official-reader CSV values against independently reconstructed logical indices for separate and leading-spatial layouts (all 12 rows and both variables), reverse steps, latitude-fastest flattened storage, an unmapped axis, and a spatial-only array; it also checks the 513-row scan. It passes against all three pinned builds. Failure recovery, cancellation and the registered local resource matrix are recorded in US3/N4 evidence; N5 local and controlled-transport subchecks are recorded separately.
- T014/T015 and the core implementations T016–T023 are complete. T024/T025 record the synthetic core matrix and row-level evidence; N1/N2 are now accepted for the declared synthetic/GFS025 scope, while N5 local and controlled-transport subchecks are recorded but complete acceptance remains open pending raw-source authorization and durable archive.
- T005’s earlier local Dongcheng boundary remains unresolved in provenance and licensing; a separate pinned geoBoundaries 2017 ODbL feature is now tracked with source metadata. The GFS object’s raw-source redistribution terms and durable archive route remain unresolved. Neither the local boundary nor the 112 MB GFS object was copied into tracked fixtures.
