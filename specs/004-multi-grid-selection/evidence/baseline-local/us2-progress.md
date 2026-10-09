# US2 Local Implementation Progress

Recorded 2026-10-06 from the repository root. This is implementation progress only; it is not an H2/H4/H5 gate report and does not claim the four producer families are spatially accepted.

## Build identity

| Input | Identity |
|---|---|
| Platform | Linux AArch64, GCC 13.3.0, CMake 3.28.3 |
| DuckDB | v1.5.4, source `08e34c447bae34eaee3723cac61f2878b6bdf787` |
| OM C | `d8855e418e2231ae8439f0c7e840fa3f93b371e3` |
| Build directory | `build/release-vcpkg`, vcpkg triplet `arm64-linux` |
| DuckDB CLI SHA-256 | `c7649dca212e23511766bf6b865c39f6d3e7687b5e38b4ab949559a0eb70beba` |
| DuckOMO extension SHA-256 | `2b3f926c53b9fcdb6db55efcdbd137ac477b1069fe27683c79edebb2e22bdfa1` |

## Checks run

| Command | Exit | Result |
|---|---:|---|
| `cmake --build build/release-vcpkg --target spatial_selection_test duckomo_loadable_extension unittest spatial_io_test -j2` | 0 | Native selector, loadable extension, SQL runner, and native spatial I/O target built |
| `build/release-vcpkg/test/native/spatial_selection_test` | 0 | Regular-grid selections preserve source order; 4096 flattened ranges stay restricted and 4097 widen to a full residual-filtered scan before storing extra ranges |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/spatial_pushdown.test` | 0 | 15 assertions passed, including safe seam OR selection |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/spatial_composition.test` | 0 | 14 assertions passed |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/multi_grid.test` | 0 | 78 assertions passed, including version-one safe OR selection and unsafe-branch fallback |
| `env DUCKOMO_EXTENSION_PATH=build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension build/release-vcpkg/test/native/spatial_io_test` | 0 | Safe seam OR returned 332 exact source rows; mixed value/spatial OR retained its full candidate set and matched the materialized source positions |
| `cmake --build build/release-vcpkg --target axis_selection_test spatial_selection_test unittest duckomo_loadable_extension spatial_io_test -j2` | 0 | Built selected-axis window scheduling, native selection, SQL runner, loadable extension and spatial I/O check |
| `build/release-vcpkg/test/native/axis_selection_test` | 0 | Selected fixed-axis combinations emit only eligible windows; duplicate member coordinates retain distinct source positions |
| `build/release-vcpkg/test/native/spatial_selection_test` | 0 | Native windows use the fastest spatial stride, preserve interleaved positions, and split long axes at 65,536 points |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/multi_grid.test` | 0 | 78 assertions passed after worker-side window preflight integration |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/spatial_pushdown.test` | 0 | 15 assertions passed after worker-side window preflight integration |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/spatial_composition.test` | 0 | 14 assertions passed after worker-side window preflight integration |
| `env DUCKOMO_EXTENSION_PATH=build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension build/release-vcpkg/test/native/spatial_io_test` | 0 | Full scan 10,541 rows, restricted scan 25 rows, safe seam OR 332 rows, unsafe OR 273 final rows; candidate accounting and zero-value reads passed |
| `cmake --build build/release-vcpkg --target axis_selection_test duckomo_loadable_extension spatial_io_test -j2` | 0 | Built the budget-aware batch mapper, extension, and spatial I/O harness |
| `build/release-vcpkg/test/native/axis_selection_test` | 0 | Rank-eight fragmented maps shrink before allocation; the worst-case position and segment mapping remains within 64 KiB |
| `env DUCKOMO_EXTENSION_PATH=build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension build/release-vcpkg/test/native/spatial_io_test` | 0 | Existing projected-grid local/full, empty, coordinate-only, mixed-filter, and seam checks pass |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/multi_grid.test` | 0 | 84 assertions passed, including interleaved spatial axes with selected value decoding and a Gaussian region whose local order differs from parent positions |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/spatial_pushdown.test` | 0 | 15 assertions passed after batch mapping changes |
| `cmake --build build/release-vcpkg --target scan_metrics_v4_test duckomo_loadable_extension unittest -j2` | 0 | Built v4 selector work counters and exact-count terminal handling |
| `build/release-vcpkg/test/native/scan_metrics_v4_test` | 0 | Exact/observed/upper counts, windows/ranges/fallbacks, legacy snapshots, early stop and failure semantics passed |
| `git diff --check` | 0 | No whitespace errors after v4 accounting changes |
| `git diff --check` | 0 | No whitespace errors |

## Implemented behavior in this progress record

- Spatial filters copy finite constant comparisons, BETWEEN bounds, safe AND terms, and one OR containing two complete longitude intervals over `[-180,180)`. All original DuckDB predicates remain for exact result filtering.
- Filter callback extraction no longer builds regular-grid ranges or scans grid coordinates on the optimizer path.
- Version-one grids apply copied necessary spatial terms through the same bound grid coordinate functions used by emitted coordinates. An unsafe OR remains a full candidate scan with its original predicate.
- Flattened regular-grid range materialization stops at 4096 ranges and widens to the full source range on the 4097th, retaining the residual filter.
- The scan now lazily issues native windows over the fastest spatial axis, intersects semantic-axis selections through actual source strides, and caps each window at 65,536 positions. Fixed-axis combinations are generated from selected semantic intervals without materializing a task list.
- `GetNextTask` claims one bounded descriptor under the task mutex; each worker performs coordinate preflight and selector-range construction after releasing the mutex. Windows exceeding the 4096-range or 256 KiB selector budget widen before their positions are emitted.
- Worker capacity is bounded by the selected native-window upper count and configured thread limit. Candidate rows and v4 observed/exact candidate counts are updated from completed preflight windows; fully scanned empty results are reported as empty selection.
- Selected logical positions are converted to official `DecodeSelection` offset/count vectors. Value-reading batches are capped using a rank-aware worst-case map bound before segment allocation; rank-eight fragmented maps remain below 64 KiB. Synthetic SQL coverage checks interleaved spatial axes and Gaussian local offsets against the source values.
- v4 selection metrics now count completed native windows, ranges, whole-window budget fallbacks, and repeated coordinate evaluations. Candidate exact values are cleared at query end for failures and early-stopped scans; empty semantic-axis selections publish exact zero without starting workers.

## Remaining work

- H2/H4/H5 are incomplete. T030–T032 still need comprehensive geometric, SQL multiset, cancellation and lifecycle tests. T039 requires fixed real sample profiling before deciding whether to add coalescing; T041–T042 still need parallelism, work-peak and evidence gates. This progress record does not claim those gates passed.
- T034–T038 and T040 are implemented and checked in `tasks.md`; focused native, SQL and spatial I/O regressions passed. Real producer samples and independent coordinate/value references remain absent, so this is not a full H2/H4/H5 or producer-support acceptance.

## Update 2026-10-07

T030–T032 are registered in `test/CMakeLists.txt` and `scripts/validate.sh`. Their local selector, SQL multiset, parallel/lifecycle, reader-capacity, synthetic four-grid work-peak, and preparation-cancellation checks passed. The full repository validation command and detailed local command manifest are recorded in [us2.md](us2.md). H2/H4/H5 remain `not-run` as complete gates for the missing real/independent references and remote-attempt bookkeeping; T039 also remains open because no acquired real sample has the required validated spatial mapping for profiling.
