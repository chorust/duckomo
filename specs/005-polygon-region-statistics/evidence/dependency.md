# Spatial Adapter Dependency Gate

**Date**: 2026-10-11  
**Scope**: T006–T008 C++ adapter capability sub-check and ordinary-read-without-spatial regression. This is not the complete P0 gate.

## Result

The exact-signature adapter and worker-local expression evaluator pass against the three pinned engines and their official spatial packages. The v1.5.4 package reports `extension_version=28db190`, while that engine's `spatial.cmake` pin is `b68b309d371dba936c5bb362980e559b7756b16d`. The difference is resolved by upstream provenance review: the signed official spatial commit `28db190f7184bcf61eb01d291e0cba79849bddb6` points at the exact pinned v1.5.4 DuckDB source, descends from the engine-config pin, and changes only release workflow, dependency submodule pointers, and GEOS packaging files. No spatial function source changed. The runtime matrix now records both pins, while gating on the exact published package source identity.

## Runtime observations

All package SHA256 values below match `test/data/regions/runtime-matrix.json`. `install_path` is empty because each package was loaded directly by absolute path for the test. For v1.5.4, the matrix separately retains the engine config pin (`b68b309…`) and the authenticated source identity of the official published package (`28db190…`); the provenance review is recorded in [research R7](../research.md#r7--版本与证据治理).

| DuckDB | Engine source | Spatial SHA256 | `extension_version` | Engine config pin | Published package source | C++ adapter test |
|---|---|---|---|---|---|---|
| v1.5.4 | `08e34c447bae34eaee3723cac61f2878b6bdf787` | `af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a` | `28db190` | `b68b309d371dba936c5bb362980e559b7756b16d` | `28db190f7184bcf61eb01d291e0cba79849bddb6` | Passed |
| v1.5.5 | `d8cdaa33fda8df955cc76ef58a280f68f4cd43fa` | `b9129a7da7fde8eb7fb951d99a115cbf2e6fa34c8d41671086169174a1f5725d` | `eb1e57c` | `eb1e57c9d92c0f3f76eb03eaa52c315090f328cc` | `eb1e57c9d92c0f3f76eb03eaa52c315090f328cc` | Passed |
| v1.5.6 | `069cc9f9b5be802405797faecc284961b07c70ef` | `96b8d4b5d735e9ebe91da7badee4328dfbd07068ce4885d1d9b6de8c22370b14` | `04270fe` | `04270fe7bb4eb412765cce8afde0276a9d81d3ba` | `04270fe7bb4eb412765cce8afde0276a9d81d3ba` | Passed |

The native test requires an unextended connection to diagnose missing spatial functions before it loads the explicit package path. After load, it binds and evaluates the exact catalog signatures `ST_GeomFromWKB(BLOB) -> GEOMETRY`, `ST_IsValid(GEOMETRY) -> BOOLEAN`, `ST_Point(DOUBLE, DOUBLE) -> GEOMETRY`, and `ST_Covers(GEOMETRY, GEOMETRY) -> BOOLEAN`. It checks a polygon with a hole (outer and hole boundaries included; hole interior excluded), EMPTY, disjoint MultiPolygon, invalid overlapping MultiPolygon, a temporary same-name `st_point` macro, exception recovery, and separate copied expressions/executors retaining their worker state.

Latest `region_geometry_test` SHA256 values:

- v1.5.4 build: `2c2fdbb7a9598acafc452058ff3ecfc84fd160615863810f69d28a2e1486d45d`
- v1.5.5 build: `e9c4a937588a391eb1fb1f180727a599b00df1c5b8c788813e58fbc2f7e88c9d`
- v1.5.6 build: `3c39917d02505bf1de66e57e65ab2da5eac77ee56a0393c8db713c76701a6412`

Source hashes for the adapter check are `91fe13cee2bc652b9fff0becff2284d57bca73a8a20a6820d1b84c4c2f85e6a2` (`src/include/duckomo/region_geometry.hpp`), `9bbfefef0f608bb805893724f43dd9ea765ba3c67e848b280a615c7b0ab5417d` (`src/scan/region_geometry.cpp`), and `b2df1d593ac7506e0b39ce3f478011844c8727352c76d851f932fc6baa803bdd` (`test/native/region_geometry_test.cpp`).

## Ordinary read without spatial

Ran the existing SQLLogicTest with only the DuckOMO extension loaded:

```sh
DUCKOMO_CORE_FUNCTIONS_EXTENSION="$PWD/build/release/extension/core_functions/core_functions.duckdb_extension" \
DUCKOMO_TEST_EXTENSION="$PWD/build/release/extension/duckomo/duckomo.duckdb_extension" \
DUCKOMO_EXTENSION_PATH="$PWD/build/release/extension/duckomo/duckomo.duckdb_extension" \
build/release/test/unittest --test-dir "$PWD" test/sql/read_om.test
```

Result: **110 assertions passed**. The test does not load spatial. The native adapter test's no-spatial sub-check also passed by observing the `[spatial_dependency]` category before loading spatial. This confirms the ordinary `read_om` test path remains usable without the spatial dependency; broader ordinary-read and legacy-metrics regression remains for later validation tasks.

On 2026-10-11, the three adapter reproduction commands were rerun after the package identity review. All reported the expected DuckDB source ID, `extension_version`, and package SHA256, then exited 0 with `region_geometry_test: ok`. The isolated no-spatial SQLLogicTest also exited 0 with 110 assertions passed. This accepts the adapter capability sub-check required by T008; it does not claim full P0 or any US1–US4 scanner behavior.

## Reproduction commands

```sh
build/release/test/native/region_geometry_test \
  build/regions-runtime/v1.5.4/official/spatial.duckdb_extension \
  af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a

build/official-matrix/v1.5.5/builds/9999a90b3d7644bb187a44cfa14e00d3deb2e0ab808f6aabdc40cf245f1cfe70/release/test/native/region_geometry_test \
  build/regions-runtime/v1.5.5/official/spatial.duckdb_extension \
  b9129a7da7fde8eb7fb951d99a115cbf2e6fa34c8d41671086169174a1f5725d

build/official-matrix/v1.5.6/builds/ac0c26d68ee57c3afa343a1d6cdab8a99c8030433e1522128deb7b9c44823aa3/release/test/native/region_geometry_test \
  build/regions-runtime/v1.5.6/official/spatial.duckdb_extension \
  96b8d4b5d735e9ebe91da7badee4328dfbd07068ce4885d1d9b6de8c22370b14
```

The adapter capability sub-check is accepted for all three versions after the identity review and runtime-matrix update. This is not the complete P0 gate: full scanner integration, spatial-free dependency SQL coverage, P1 correctness and later product gates remain separate.
