# US4 Local Implementation Progress

Recorded 2026-10-06. This is implementation evidence only; it is not an H3 acceptance report.

## Implemented

- Safe coordinate constraints with an empty intersection now resolve during scan initialization. The scan publishes exact zero and completes without claiming native windows or creating value decoders.
- Full-domain shortcuts are used only for projected and Gaussian definitions whose output domain is guaranteed. Legacy regular grids keep coordinate preflight unless their conditions are absent.
- Native-window preflight records zero coordinate work when the predicate is proven empty or covers the complete guaranteed domain. Otherwise selection remains conservative and the original DuckDB WHERE is retained.
- Coordinate-only and cardinality-only projection slots do not enter the value dependency set. Worker state creates decoder state only for required value columns; `DecodeSelection` is reached only for those columns. Empty scans return before opening worker readers.
- Added `grid_zero_io_test.cpp` and `grid_zero_io.test`. The interleaved fixture proves duplicate geographic positions retain all 32 time/level/lead/member/run records, and a single 256 KiB value chunk proves the coordinate path does not read its value index/data or decode chunks. The value-filter contrast reads temperature while humidity remains at zero cost.
- Registered both tests in `test/CMakeLists.txt` and `scripts/validate.sh`.
- Implemented the H3 local synthetic runner path in `duckomo_grid_validation`: it invokes the matching native and SQL tests, writes their command/exit/RSS records, and audits per-scenario v4 variable costs. It reports the H3 gate itself as not-run until the US5 source/info cases are included.

## Checks run

| Command | Exit | Result |
|---|---:|---|
| `cmake --build build/release-vcpkg --target spatial_selection_test duckomo_loadable_extension unittest -j2` | 0 | Built predicate proof helpers and scan shortcut |
| `build/release-vcpkg/test/native/spatial_selection_test` | 0 | Contradictory bounds, normalized-longitude limits, safe unions, and exact full-domain boundaries passed |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/multi_grid.test` | 0 | 84 assertions passed |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/spatial_pushdown.test` | 0 | 15 assertions passed, including contradictory coordinates with the residual WHERE retained |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/spatial_composition.test` | 0 | 14 assertions passed |
| `env DUCKOMO_EXTENSION_PATH=build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension build/release-vcpkg/test/native/spatial_io_test` | 0 | Coordinate-only, empty, mixed-value, and local selection value-read assertions passed |
| `build/release-vcpkg/test/native/grid_zero_io_test` | 0 | Version-one coordinate/count/empty paths have zero value index/data/decode counters; temperature-only filtering leaves humidity at zero; interleaved multiplicity and 256 KiB chunk fixture passed |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/grid_zero_io.test` | 0 | 17 assertions passed for full/local coordinates, COUNT, contradiction, reversed BETWEEN, disjoint domain, and value residuals |
| `build/release-vcpkg/test/tools/duckomo_fixture_tool --output /tmp/duckomo-grid-zero-io-fixtures-20261006` | 0 | Generated OM v3 large-chunk fixture and official C decode CSV; fixture SHA-256 `26501603c5bf7994d02a3f3a1f5369c85fb0adc95c7da1390172135caf973b52` |
| `build/release-vcpkg/test/tools/duckomo_grid_validation --root /home/blizhan/repo/github/duckomo --manifest test/data/grids/sample-manifest.json --cases H3 --duckdb build/release-vcpkg/duckdb --extension build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension --output specs/004-multi-grid-selection/evidence/baseline-local/h3-local-20261006-v2` | 2 | Expected incomplete-gate exit: synthetic H3 subcases passed and 18 per-variable metric snapshots were audited; the full H3 status remains `not-run` pending US5 source/info coverage |
| `git diff --check` | 0 | No whitespace errors |

## Remaining

## Follow-up: v4 publication and local H3 (2026-10-06)

- `duckomo_last_scan_metrics()` now publishes schema v4 with the frozen schema-v3 profile nested under `legacy_v3`; `DUCKOMO_METRICS_V3_OUTPUT` remains schema v3 and `DUCKOMO_METRICS_V4_OUTPUT` writes terminal v4 profiles. QueryEnd sets the authoritative terminal marker after status and memory finalization.
- The local H3 runner now audits v4 `reads.variables`, terminal outcome, and complete coordinate-preparation evaluation counts. The 18 synthetic scenarios passed; exact per-scenario coordinate costs are in `h3-local-20261006-v3/h3-local/zero-io-metrics.json`.
- `time_test`, `reader_capacity_test`, and `axis_selection_test` each exited 0. Native session metrics passed; `cache_metrics.test` passed all 25 assertions. v3/v4 sidecar output was parsed and checked for its expected schema and v4 terminal markers.
- T062 is recorded complete for its available local scope in `us4.md`. There was no remote endpoint or signed S3 fixture configuration, so remote H3 was unavailable and unrun. Full H3 remains `not-run` pending US5 source/info coverage. No remote-benefit or real-sample gate is claimed.
