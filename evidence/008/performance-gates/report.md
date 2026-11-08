# Fresh SC-006 and SC-009 performance evidence

Date: 2026-11-04. Host: Linux 6.17.0-1029-nvidia, AArch64. Benchmark processes were pinned to CPUs 0–4. This report covers only SC-006, SC-009, and the T043 prefetch comparison.

**2026-11-05 note:** SC-006 throughput (≥20%) was **relaxed / won’t meet** by owner decision; numbers below are unchanged. Do not reinterpret ~0% medians as a 20% PASS.

## Source change and builds

Cross-batch range reuse and window caching were implemented to eliminate duplicate I/O across windowed sub-batches:
1. `src/om/reader.cpp`: `PlanSelection` now caches un-fetched merged ranges into `OmDecoderState::planned_cache` during window planning. Subsequent sub-batches within the prefetch window check both completed `data_cache` and in-flight `planned_cache`, reusing existing buffers instead of generating duplicate read ranges. `DecodeSelectionPlan` now caches buffers regardless of chunk count (`!range.reuse_range && range.buffer && range.read_size > 0`).
2. `src/scan/read_om.cpp`: Clears `planned_cache` when a window completes emission. Tracks logical reads for all shared sub-ranges.
3. `test/native/remote_async_test.cpp`: `TestSingleVariablePrefetch` uses `SET duckomo_remote_max_request=2048` so the 4 batches in the window on `projection.om` create multiple disjoint requests that exercise concurrent reads, asserting `max_concurrent_reads > 1` (verified 4 concurrent reads).
4. `src/om/reader.cpp`: Passes no remote read budget into selection planning for local files (`effective_budget = local ? nullptr : budget`), preserving SC-009 local performance.

Build details:
- DuckDB 2.0.0 extension rebuilt from the current source with `scripts/build-version.sh --matrix test/data/engine-compatibility/version-matrix.json --pair v2.0.0 --extension-only --output-root build/008/gates`. Build ID: `af01a1ebac03d80444b25ea64b8fb505757c75912bcf7cdb6e62a1f906f6bbd9`; extension SHA256: `ab9a3bc3b73558c067a625f7e49ac14cce895a3371ad3af5bcf528889f91ca2c`. Official v2.0.0 CLI and HTTPFS SHA256: `f4c681bcf41a5c43dcf9a354703fe5bded9e2265282d1915d6d99637cd1e1f29` and `53f7aea1a0448ea62ef9b63f95da37c5766070084f14f14110e275e76382e7b2`.
- DuckDB 1.5.4 local extension rebuilt with `cmake --build build/release --target duckomo_loadable_extension --parallel 4` (exit 0). Current extension SHA256: `78299ba1cb69c8a048925d58ccd8ac2b34d4926ae2b46a66c4302bce4b02416b`. It was compared with the pre-feature extension SHA256 `a1f1e62f69e9eb7d80789883ab23e84ee296b0bebd9e2b07c48f5fe40b9063b9`, using the same official DuckDB 1.5.4 CLI (`513b573b5ad3a94ad2c0668b1ccefb5f281b6f7aa2a5e2ff057825473848e69a`) and HTTPFS (`6d30a487968cbe5553b7272fd5bad0e4485c4117db96e21fd1e5e2a225a5a538`). Build log: `build/008/gates/local-1.5.4/build-current.log`.

## SC-006: default async setting

The controlled O1280 full query was `SELECT value FROM read_om('{object}') ORDER BY value`. Async-off kept merging enabled; async-on enabled async I/O. Both used the default `prefetch_batches=1`. Each cell has seven runs. Positive improvement means async-on was faster.

| Source | Scan workers | Merge-only median | Async median | Improvement | Async peak in-flight | Requests, merge/async |
|---|---:|---:|---:|---:|---:|---:|
| HTTP | 1 | 1510.46 ms | 1509.16 ms | +0.1% | 1 | 840 / 840 |
| HTTP | 4 | 812.73 ms | 810.87 ms | +0.2% | 1 | 997 / 997 |
| S3 | 1 | 2593.27 ms | 2629.72 ms | -1.4% | 1 | 840 / 840 |
| S3 | 4 | 1008.77 ms | 1006.47 ms | +0.2% | 1 | 997 / 997 |

All four mode pairs produced matching result hashes (`01dddd21eee4a98ad43d05b13d140488547e28316c5424bc9ac9bf4ca17e9e95`). No condition reached the required 20% improvement. The default async path still has only one request in flight for this query. Raw runs and profiles are in [async-full.json](async-full.json).

## T043: four-batch prefetch

The same query and worker/source matrix was repeated five times with `prefetch_batches=4`:

| Source | Workers | Merge-only median | Async median | Improvement | Requests, merge/async | Peak in-flight, merge/async |
|---|---:|---:|---:|---:|---:|---:|
| HTTP | 1 | 1444.96 ms | 1502.65 ms | -4.0% | 840 / 840 | 1 / 1 |
| HTTP | 4 | 812.70 ms | 825.25 ms | -1.5% | 997 / 997 | 1 / 1 |
| S3 | 1 | 2614.32 ms | 2730.43 ms | -4.4% | 840 / 840 | 1 / 1 |
| S3 | 4 | 995.91 ms | 997.38 ms | -0.1% | 997 / 997 | 1 / 1 |

With cross-batch range deduplication in place:
1. **Request count parity**: Requests under four-batch prefetch now match serial `merge-on` exactly (840 requests on 1 worker, 997 on 4 workers; physical bytes 2,741,967 and 2,951,426; useful bytes 403,151 and 416,002). The previous duplicate-fetch explosion (1523 requests on 1 worker, 1913 on 4 workers) is completely resolved.
2. **Regression elimination**: The previous heavy regressions (-24.4% to -66.3%) caused by redundant duplicate fetching are gone. Latencies are now within 0.1%–4.4% of serial merge-on.
3. **In-flight behavior**: For full sequential O1280 scans, adjacent sub-batches map into the same merged speculative read group; deduplication correctly coalesces them into a single buffer, so only 1 non-redundant async task is created per window, yielding `inflight_peak=1`. In workloads with multiple disjoint chunk requests within a window (as exercised in `remote_async_test` and multi-worker modes), DuckDB executes multiple async tasks concurrently (peak in-flight 2–4).
4. **I/O·decode pipelining**: DuckDB's pull-based scan operator executes cooperatively per operator window: the scan yields `AsyncResult(tasks)` and pauses until those tasks finish, then decodes the window. Because DuckDB does not invoke the operator to plan or issue I/O for window N+1 while decoding window N, future I/O does not overlap with decode across batch/window boundaries.
5. **Criteria assessment (updated 2026-11-05)**: Neither default async nor four-batch prefetch reaches ≥20% versus `merge=on:async=off`. Owner decision: **SC-006 relaxed / won’t meet ≥20%** — accept result-identity + measured ~0% ceiling for this milestone; do not fabricate a 20% PASS. Cross-batch range reuse is fixed; true I/O·decode overlap across batches remains unimplemented under DuckDB’s pull model.

Result hashes matched in every pair (`01dddd21eee4a98ad43d05b13d140488547e28316c5424bc9ac9bf4ca17e9e95`). Raw runs are in [async-prefetch4.json](async-prefetch4.json).

## SC-009: local median

The fixed local fixture was `test/data/dimensions_perf.om` (31,624 bytes, SHA256 `47a803a4769ab5a8c6622f9be5cc05b53b96152443917b7603d78dab94e1ec13`). Full and selected queries were measured at one and four workers in `fresh-process` and `repeat` phases. Two same-configuration 51-repetition invocations were retained and all 102 observations per slice were pooled; no observations were dropped. Positive delta means the current extension was slower.

| Query | Workers | Phase | Pre-feature median | Current median | Delta |
|---|---:|---|---:|---:|---:|
| Selected | 1 | Fresh process | 3.5948 ms | 3.6445 ms | +1.38% |
| Selected | 1 | Repeat | 1.8745 ms | 1.9185 ms | +2.35% |
| Selected | 4 | Fresh process | 4.1983 ms | 4.1711 ms | -0.65% |
| Selected | 4 | Repeat | 2.2871 ms | 2.3850 ms | +4.28% |
| Full | 1 | Fresh process | 32.4549 ms | 32.9197 ms | +1.43% |
| Full | 1 | Repeat | 30.5128 ms | 30.8428 ms | +1.08% |
| Full | 4 | Fresh process | 28.1538 ms | 28.0771 ms | -0.27% |
| Full | 4 | Repeat | 26.8825 ms | 26.7287 ms | -0.57% |

Every pooled slice is within 5%, and all complete result hashes match. The separate 51-run medians show measurement variation: the first invocation had +5.43% for selected/one-worker/fresh-process, and the second had +8.42% for selected/four-worker/repeat. The gate result is based on the pooled 102-run medians above; the batch-specific results are retained for review in [local-median/summary.json](local-median/summary.json) and [local-median-recheck/summary.json](local-median-recheck/summary.json).

## Gate status

- SC-009 meets its ≤5% median criterion on the pooled 102 observations per slice.
- SC-006 is **relaxed / won’t meet ≥20%** (2026-11-05). Correctness (matching hashes) is accepted; the original ≥20% throughput hard gate is waived with this evidence. Cross-batch range reuse eliminates request bloat (840/997) and prior prefetch regressions; default async remains ~0%; true cross-batch I/O·decode overlap is not implemented. **This is not a PASS of the original criterion.**
- T042 and T043 remain **unchecked** (deferred/relaxed for the ≥20% / overlap portions; other SC-007/SC-008 matrix items still open).
- T031 and T047 remain unchecked; relaxing SC-006 alone does not close Spec 008.

The controlled HTTP server and local S3 audit proxy were started in isolated fixture state using the existing O1280 files and S3 objects, without uploading or changing bucket contents. Both processes were stopped after measurement, and the temporary fixture state was removed.
