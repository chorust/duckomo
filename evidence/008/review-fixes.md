# Spec 008 review fixes

## Reproduction and implementation

Strengthened `remote_async_test` to assert filesystem-observed concurrency, not only a queue-length metric. Before the fix it failed with `FAIL: filesystem observes concurrent requests`.

- DuckDB 2.0 PARALLEL_ACCESS / DIRECT_IO positional reads no longer share the 1.5.x serial read mutex.
- Engine AsyncResult tasks execute a bounded set of serial request queues. The per-worker queue count obeys the configured inflight limit and a remaining-memory allowance recomputed each batch. Request payloads exceeding the physical cap are split.
- Inflight metrics measure actual executing requests. Error synchronization belongs to the scan, not a process-wide static mutex. Remaining queued work is skipped after the first error.
- Planned index/data buffers own Decoder accounts via their shared lifetime, including the decoder's cached last range. Plan containers and async task controls also have accounts. Allocation failures of planned buffers become ReaderError::Allocation.
- Useful bytes exclude unselected chunks, tail read-ahead and cached reuse. The snapshot no longer silently clips useful bytes to physical bytes. Native coverage checks positive gaps and physical=useful+gap.
- Added stricter cancellation (<1s and zero active reads), inflight upper bound, memory-pressure reduction to one and recovery of the budget. Added 1.5.x remote async=true compatibility coverage.
- New SQL tests use placeholders supported by both engine generations.
- Benchmark runner records extension SHA256 and per-process RSS, rejects mixed-build resume, and requires explicit SQL for all non-full selection scenarios.

## Verification

Exact executable paths, outputs and artifact SHA256 are in `review-fixes-tests.json`.

- v1.5.4 and v2.0.0: remote_session_test, reader_capacity_test, scan_metrics_v4_test pass.
- Both versions, with matching official spatial: `test/unittest 'test/sql/*'` passes 23 cases / 1319 assertions.
- v2.0.0 remote_async_test: actual concurrent reads=4, configured bound=4 (2/4/8 executor threads); cancellation approximately 53ms; default budget 524288 bytes falls to 65536 under 4MB pressure and restores after RESET.
- v2.0.0 spatial_callback_test passed with matching core_functions and DuckOMO extension overrides. This is not the complete aggregate validator.
- `python3 test/tools/benchmark_remote_io_test.py`: 2 tests pass.
- Runner smoke: local raw.om, async off/on, two repetitions; see `review-runner-smoke.json`. This verifies artifact/RSS output only, not remote performance acceptance.
- `git diff --check` passes.

## Remaining acceptance

SC-001–SC-009 remain open in the checklist until verified at their specified scope. Historical unsupported 46.1% speedup, zero-gap, RSS and final-build claims have been withdrawn. CI dispatch needs privileges (historical 403). Controlled S3/HTTP sweep, final aggregate validator, final sanitizer/TSan, RSS ratio and pre-feature local latency comparison have not been rerun here. 1.5.5/1.5.6 final artifacts were not rebuilt in this repair pass.

The scheduler bounds actively executing I/O; a batch still materializes its planned buffers before scheduling. Those buffers are now accounted, but the complete batch allocation is not reserved with BufferManager. The 256MB RSS acceptance therefore remains necessary and must not be inferred from the inflight limit alone. Sequential queues are scheduled through this pinned DuckDB's executor API, so available `threads` limits observable parallelism.
