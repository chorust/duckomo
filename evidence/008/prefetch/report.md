# Async / cross-batch prefetch results

Async has not demonstrated a consistent speedup on the measured workloads. The 20% target remains unmet. Local performance observations are retained without changing the 5% target; see ../final/local-idle-51.json and ../final/status.md.

## Implementation and default

DuckDB 2.0 exposes an experimental `duckomo_async_prefetch_batches` setting, range 1–4, default **1** (no cross-batch prefetch). Setting 4 allows up to four output batches from the current task to be planned and fetched with the engine AsyncResult before they are decoded/output one batch at a time. This is bounded look-ahead, not a background network/decode overlap pipeline. No new threads are created. The window is further limited by max_inflight and a remaining-memory share; memory pressure disables it. Only single-variable, unfiltered, non-region remote scans are eligible. Local scans and 1.5.x ignore it.

The actual pinned engine lacks the originally planned ASYNC task executor / ManagedAsyncMemoryGovernor APIs. This implementation uses the available AsyncResult scheduling protocol. Payloads are accounted by shared ownership; they are not BufferManager reservations. A maximum of four output batches does not constitute a bound on all compressed payload allocations for arbitrarily large OM chunks.

## Measurements

`window-explicit.json` records the opt-in setting in every run and the matched extension SHA256. HTTP O1280, one scan worker, 256MB, 3 repetitions, query-profiled medians:

| Mode | Median query ms | Requests |
|---|---:|---:|
| sync merge on | 1351 | 840 |
| async, prefetch_batches=4 | 1910 | 1523 |

Identical complete result hashes. Approximately 41% slower with the window in this run. This is why the default is 1.

Earlier window-enabled measurements (before making the feature opt-in) are preserved in window-o1280.json: HTTP 1/4-worker medians 1189→1650 / 766→1270ms; S3 2447→2636 / 987→1767ms. Requests increase from 840 to 1523 (one worker). 256MB process RSS async/sync ratios are 0.994/0.985 (HTTP 1/4) and 1.000/1.004 (S3 1/4). These are empirical observations, not proof of general memory safety.

Controlled 10ms GET-delay dimensions workload (`window-dimensions.json`): one-worker 245→292ms, four-worker 198→318ms; requests 18→49 / 22→72. Concurrent look-ahead loses the completed-range reuse available to sequential batches; exact pending-range dedup only handles identical intervals, so overlapping requests remain.

## Verification

- New native single-variable case demonstrates four real concurrent reads, complete cardinality/value equality across emitted batches, zero active reads after completion, cancellation drain and same-connection recovery.
- Existing multi-variable, per-worker inflight, errors/redaction, cancellation and pressure/recovery cases still pass. Default-window LIMIT budget checks pass.
- TSan, system allocator and ASLR disabled: passes with no reports (window-tsan-run.log).
- 2.0 full SQL: 23 cases / 1323 assertions, including default=1, opt-in=4 and rejected zero setting.
- Final ASan/UBSan 14/14 pass (asan.log); all 1.5.4/1.5.5/1.5.6 builds and SQL pass (versions15-sql.json). Both engine generations run 23 cases / 1323 assertions. Matched artifact hashes are in artifacts.json.

## Remaining optimization

Making this faster requires avoiding overlap/duplicate fetches across planned batches, coordinating useful-byte accounting for shared subranges, and then overlapping bounded future I/O with current decode. That is additional work; the experimental window is not claimed to complete the planned fully overlapped pipeline or SC-006. No positive performance claim is made for either async or this window.
