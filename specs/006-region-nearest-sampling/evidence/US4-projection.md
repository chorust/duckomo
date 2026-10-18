# US4 projection and resource evidence

Date: 2026-10-17

## Scope

Verified nearest sampling nested STRUCT projection, status dependencies, zero-value geometry/COUNT reads, local value reads, filters, same-object full-read reduction, memory scaling, worker parity, cancellation/recovery, permission denial, and sampling ASan/UBSan on Linux AArch64 with DuckDB v1.5.4, v1.5.5, and v1.5.6. The registered local N3 and N4 gate scopes are `pass`; the current scoped N1/N2 pass is recorded separately. N5 partial results are recorded separately and N6 audit remains open; the existing local N7 reproduction was run by an implementation worker and does not meet the independent-executor requirement.

## Implementation

`read_om_regions` enables `supports_pushdown_extract` only for the nearest-only `sampling` output. The global scan state consumes DuckDB `ColumnIndex` values, maps `sampling` leaf field indexes to typed output descriptors, and retains ordinary projection behavior for `read_om`, points mode, and `om_source`. Geometry leaves and `COUNT(*)` require no value decoders. A direct source array projection decodes only that array. The `sampling.variables` MAP leaf and complete STRUCT conservatively require every bound value array so their reported per-variable status cannot be fabricated.

## Commands and results

The following exact SQL/native suites passed for each matching DuckDB/spatial pair; sanitizer is recorded for the v1.5.4 pair in the validator package:

### 2026-10-17 metrics and permission refresh

- Rebuilt `duckomo_loadable_extension` and `region_sampling_test` from the current dirty worktree on v1.5.4, v1.5.5, and v1.5.6. DuckOMO extension SHA-256 values: v1.5.4 `a9aec833be443d481a0ce583ec529eea6f58fd97aeab16c42149ac64b359e7fa`; v1.5.5 `94d87241f57df140970b95bfa3e0c46476da76df4c3d8ba33ace3ba5bada6520`; v1.5.6 `75ecd0c5c273f6793e4c01ac38a058e720ec944b60b949600c31c88c91273824`.
- Native `region_sampling_test` passed on all three matching spatial runtimes. Each run includes the expanded nearest sampling metrics assertions and the 2,049-axis-row / 1,2,4-worker cases.
- Permission denial passed on all three runtimes in an Ubuntu 24.04 Docker container running as UID/GID 65534 with the worktree mounted read-only. The test removes read permissions after PREPARE, requires an I/O error instead of `source_null`, then checks normal queries remain usable.
- `test/sql/region_sampling.test` passed 176 assertions on each runtime. The v1.5.4 sampling ASan/UBSan CTest passed in 24.15 seconds (N4-50 stdout SHA-256 `3b7f06e1b51ecfa95c8410eced34e39c69e7bbf440a2e2067b5862173f04a958`).
- The historical registered package [`run-20261017-sampling-validation-final`](run-20261017-sampling-validation-final/) records N0/N3/N4 pass and N1/N2 blocked at that time. The current N1/N2 pass is recorded separately in [`n1-n2-acceptance-refresh-20261019-final-7`](n1-n2-acceptance-refresh-20261019-final-7/); N5 is partial and N6 remains incomplete; the existing local N7 reproduction does not satisfy the independent-executor requirement.

### 2026-10-16 task-boundary refresh

- Rebuilt the v1.5.5 and v1.5.6 `region_sampling_test` executables from the current shared source, then ran the native suite on v1.5.4/v1.5.5/v1.5.6 with each runtime's matching spatial package. All three returned `region_sampling_test: ok` after adding a 2,049-row fixture and four-region worker comparison.
- The value projection emitted all 2,049 rows, axis positions 0–2,048, and metrics recorded `sampling.max_task_rows=2048`, demonstrating the contractual upper bound and continuation in a second task. The status-map projection emitted all rows and stayed below the 512 KiB worker scratch bound.
- Worker configurations 1/2/4 returned the same 8,196-row result fingerprint for four shared-source regions.
- The v1.5.4 sampling SQL suite passed 176 assertions. Sampling ASan/UBSan rebuilt and passed CTest (`duckomo_sanitizer_region_sampling_test`, 24.15 seconds in the latest run; 23.97 seconds in this historical task-boundary refresh).
- Historical full `scripts/validate-sampling.py` run: [`run-20261017-sampling-validation-final`](run-20261017-sampling-validation-final/). Its N1/N2 blocked disposition is superseded by the current scoped pass package [`n1-n2-acceptance-refresh-20261019-final-7`](n1-n2-acceptance-refresh-20261019-final-7/). N5 is partial; N6/N7 remain outstanding.

- `test/sql/region_sampling.test` — 176 assertions on v1.5.4, v1.5.5, and v1.5.6. This includes geometry/COUNT zero-I/O metrics, full STRUCT all-variable dependency, variable-qualified MAP dependency, target/value residual filter equivalence, and duplicate projected status leaf.
- `test/native/region_sampling_test <matching-spatial-package>` — passed on all three versions. Projection ledger on each runtime: geometry and COUNT `0` index/data/decode; MAP/full STRUCT `27` index bytes, `1,774` value bytes, `24` decoded chunks; selected variable and direct value `13` index bytes, `864` value bytes, `12` decoded chunks.
- Same-object `skippable-values.om` comparison on all three: nearest `224` data bytes / `1` chunk, full value read `15,449,547` bytes / `66,432` chunks.
- 64×64 vs 256×256 (16× points): query-owned upper bound `61,865` / `86,533` bytes (1.40×); worker scratch peak `17,163` / `17,178` bytes; both nearest scans decoded 12 chunks, while full baselines decoded 3,185 / 50,952 chunks.
- 513-row multi-task case returned axis positions 0–512 and sum 6,156. Worker configurations 1/2/4 returned the same 1,539-row fingerprint and shared one source point.
- Cancellation after visible output produced `status=cancelled`, `scan_complete=false`, NULL exact result count, and released query-owned state; same-connection full scan then passed. LIMIT early stop left all exact count fields NULL/false and also recovered.
- Sampling ASan/UBSan: `cmake --build build/release --target region_sampling_test_sanitizer -j2` and `ctest --test-dir build/release/extension/duckomo/test --output-on-failure -R '^duckomo_sanitizer_region_sampling_test$'` — passed.
- `region_scan_test` ASan/UBSan was also corrected to distinguish a nearest bind-time object-change failure (before plan metrics exist) from post-plan decoder failure; the matching targeted sanitizer test now passes. This adjustment follows the emitted metrics: object-change publishes generic failure with no nearest plan, while decoder failure publishes `result_rows_exact=null`, `result_count_complete=false`, and terminal release.
- `python3 test/tools/sampling_evidence_test.py` — passed (9 tests); fixture hashes and `git diff --check` passed in the latest update cycle.

## Source identities

- `test/sql/region_sampling.test` SHA-256: `e39181f340425b35d88ea1d3864f5bf52d1b026f925cbcafbce26ad43586cdab` in the archived run input manifest.
- `test/native/region_sampling_test.cpp` SHA-256: `993e7d1af69c6ea4f2e0f82b090e342eb753db276749c478b7fa404c02ac6fd3` in the archived run input manifest (metrics completeness and status assertions).
- `src/include/duckomo/metrics.hpp` SHA-256: `67c69e360fe1c0c3ad3a0ee6629460fc2cadc0483016e19c508c00699d32ced4`.
- `src/scan/read_om.cpp` SHA-256: `65b9d355a8c3325dbec6a4e00470f8e221064db81b108e75c1166276de02b8fc`.
- `src/grid/nearest_sampling.cpp` SHA-256: `becfae16493b6f65a27c2652b1b9a3f4ef34fa7d0767f242cfde64b1df55e650`; `src/include/duckomo/nearest_sampling.hpp`: `c3caadc153d6ff0f1f92a121f5867e707b0f32bfa503e6e88c7a2f895530eadf`.
- `test/native/region_scan_test.cpp` hash and the full dirty-source identity are bound in the archived run inputs/manifest.
- Matching spatial package SHA-256: v1.5.4 `af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a`; v1.5.5 `b9129a7da7fde8eb7fb951d99a115cbf2e6fa34c8d41671086169174a1f5725d`; v1.5.6 `96b8d4b5d735e9ebe91da7badee4328dfbd07068ce4885d1d9b6de8c22370b14`.
- Source tree is dirty and contains unrelated work in progress; no clean-tree release identity is claimed.

## Limits

The task bound is now exercised directly at 2,048 rows; the 2,049th row is returned by a following task. Latest N3/N4 gate results and development binary identities are bound by [`run-20261017-sampling-validation-final/manifest.json`](run-20261017-sampling-validation-final/manifest.json); native suites pass on all three matching runtime pairs and permission denial passes under container UID/GID 65534. N3 and the registered local N4 acceptance scope pass. Controlled HTTP, HTTPS, and signed-S3 semantic differential subchecks passed as recorded in N5-dongcheng.md. N5 remains partial because raw-source redistribution and durable archive terms are unresolved. N6 full version-package audit is not complete; the recorded local N7 reproduction does not satisfy the independent-executor requirement.
