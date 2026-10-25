# Continued Spec 008 verification

## Proven checks

- v1.5.4/v1.5.5/v1.5.6/v2.0.0 builds and SQL: 23 cases / 1319 assertions per version. Version-specific native remote_session, reader_capacity and metrics checks pass. See versions155156.json and stream-sql-final.json.
- Official HTTPFS on v1.5.6 and latest v2.0.0: 48/48 checks, matched artifact identities in httpfs156-summary.json and httpfs20-final-summary.json.
- Latest 2.0 ASan/UBSan target: 14/14 pass; sanitizer20-final.log.
- ThreadSanitizer instrumented DuckDB + extension: system allocator, `setarch aarch64 -R build/008/tsan/test/native/remote_async_test`, exit 0, no reported races; tsan-system-run.log. Initial jemalloc build reports two races in duckdb_je_arena_decay (tsan-run.log); that allocator was disabled rather than suppressing reports. ASLR workaround is required by this machine's TSan mapping constraints.
- All 17 Python test groups pass; python-all.json. New fixture delay mode passes remote_fixture_proxy_test.py (7 tests).
- Bidirectional version mismatch rejected cleanly; mismatch-final.json.
- Controlled O1280 parameter sweep: HTTP/S3, full/explicit domain spatial selection, 1/4 scan workers, 3 repeats, gaps {0,512,4096,16384}, caps {16384,65536,262144}; merge-sweep.json. Default request count 11591→840 and physical bytes 4125977→2741967. Spatial selection bytes unchanged at 2769; coordinates only 453.
- HTTP 256MB full/selected × 1/4 workers: async/sync process peak RSS 0.979–1.012, matching hashes; rss256.json.

## Changes made during verification

The local sync path now streams reads/decode using existing bounded buffers, avoiding planned payload allocation. Async batch planning shares identical encoded ranges before scheduling to avoid repeated fetches. The benchmark runner loads private S3 setup, separates scan workers from executor threads, records the query SQL hash, rejects incompatible resume, and distinguishes query-profiled elapsed_ms from process_elapsed_ms. Both 1.5 latency and 2.0 query.total_time profiler formats are supported. Controlled HTTP fixtures can inject documented per-range GET delay with `--read-delay-ms`.

## Unfinished gates — do not mark Spec complete

- SC-006 is not met: true concurrency alone does not yield stable ≥20% query speedup. Ordinary HTTP/S3 and 10ms-delay scenarios show <20% gains or regressions. Single-variable O1280 batches usually expose one request. Exact pending-range dedup helps but does not eliminate redundant subrange reads. Cross-batch bounded prefetch (T043) remains to implement and validate.
- SC-009 is not met across all combinations: matched-engine pre-feature/current 31/51-repeat comparisons show full-scan medians within 5% in the final 51-run measurement but some 2–5ms selected-query medians exceed the threshold. See local-stream-final.json / local-stream-51.json. Competing compilation load makes the result unsuitable for claiming a clean acceptance pass; rerun idle.
- Complete latest-build controlled S3 256MB RSS, polygon/nearest benchmark modes, cancellation ledger-residue proof and final cloud refresh remain open.
- Whole batches still allocate planned buffers before I/O scheduling; accounted bytes are not a BufferManager reservation. Bounded cross-batch work must address this before claiming full memory governance.
- CI dispatch retried for release.yml and community-build.yml: both HTTP 403; evidence/duckdb-2.0/final/ci-retry.json. Local working-tree changes are not pushed, so remote main would not validate them. No support promotion or publishing occurred.
- Earlier benchmark JSON files carry their actual artifact SHA256 and timing conditions; they are preserved as intermediate results. artifacts.json identifies current output hashes but does not retroactively make older measurements final-build evidence.

The full validate.sh entrypoint is rerunning at build/008/final-validation/aggregate20-last.log (OM C rebuild is the longest Python check). An earlier run passed SQL/native and reached Python checks; missing default-build native executable was fixed, and all Python checks now pass separately.

The final aggregate attempt exposed a build-selection bug: seasonal Python used default 1.5.4 CLI with the 2.0 spatial override. validate.sh now exports DUCKOMO_BUILD, and the matching 2.0 seasonal test passes; version-matrix Python 13/13 and validator Python 15/15 also pass. Full entrypoint rerun is at aggregate20-fixed.log.

A second integration-only issue was fixed: two blocked-region-identity tests assumed every CLI version exists in the production region matrix. They now create an explicit blocked evaluation-version record and verify rejection. Both execute under 2.0 (15 validator Python tests pass). Unified retry log: aggregate20-verified.log.

Idle 51-round matched local comparison is also recorded (local-idle-51.json). Repeated-query medians are within 5%, but fresh-process full/selected at one worker and selected at four workers exceed 5%; SC-009 is still not accepted. Latest 1.5.6/2.0 O1280 count and sum match exactly (cross-version-final-aggregate.json).

The benchmark profile parser has a dedicated regression test for both DuckDB 1.5 root latency and 2.0 query.total_time. benchmark_remote_io_test.py now passes 3 tests. Fixture delay and blocked-evaluation-runtime test changes are validated without changing the production support matrix.

FINAL unified result: validate.sh against the latest 2.0 build exits 0 after all SQL/native/Python checks and 48 official HTTPFS cases. Log: aggregate20-verified.log; remote summary: aggregate20-remote-summary.json.
