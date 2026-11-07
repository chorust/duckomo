# F008-01 targeted review fix — Linux ARM64

## Result

`planned_cache` now has an explicit nested plan/window lifetime. The outermost owner starts with a clean lookup directory and clears it on every exit, including exceptions. Standalone `PlanSelection` and `DecodeSelectionImpl` calls own a selection scope; `ScanReadOmImpl` owns a scope across its complete scan invocation. That scan scope includes the inline `AsyncResultsExecutionMode::SYNCHRONOUS` branch and all plans in an output or prefetch window.

A cache directory clear drops only its lookup references. Returned `PlannedSelection` objects and pending async items continue to own the shared buffers needed by fetch/decode tasks. The single `data_cache` remains intact for adjacent range reuse. The existing fetch exception cleanup still invalidates `data_cache`, while the scope releases all planned entries.

The regression test exercises the public remote `DecodeSelection` path using an in-memory file whose path starts with `https://`; it makes no network request. Seven-row selections cover the 73×61 `raw_large.om` fixture with 7×13 chunks. A separate remote-path check confirms adjacent selections still reuse `data_cache` without another data read. A second-read failure injection verifies planned entries are released and the failed data range is invalidated. An injected `InterruptException` verifies the same release boundary when a selection is cancelled. The test also asserts that the selection scope depth returns to zero.

## Retained payload results

The baseline is from the original F008-01 review probe, built against the then-current `reader.cpp`. The after values come from the native regression test built against the fixed dirty-tree sources. Retained bytes after the fix count `data_cache` plus any planned-cache buffers, without double-counting the `data_cache` pointer if it is also present in the lookup directory.

| Start row | Before items | Before retained bytes | After items | After retained bytes |
|---:|---:|---:|---:|---:|
| 0 | 3 | 920 | 0 | 136 |
| 7 | 5 | 1,784 | 0 | 496 |
| 35 | 17 | 5,674 | 0 | 152 |
| 63 | 29 | 9,786 | 0 | 152 |
| 70 | 30 | 10,233 | 0 | 447 |

Across all 11 batches, `planned_cache` was empty after every call. Peak retained payload was 496 bytes, below the fixture decoder’s 4,462-byte single-range bound. The native test output records every batch in `reader-capacity-test.txt`.

The regression was first run against the unfixed code after adding the test: it exited 1 at row 0 with `remote synchronous decode must release planned-cache entries at the selection boundary`. It passed after the fix.

## Validation

Platform reported by `uname -a`:

```text
Linux lizhan-spark 6.17.0-1029-nvidia #29-Ubuntu SMP PREEMPT_DYNAMIC Wed Jul 1 00:13:52 UTC 2026 aarch64 aarch64 aarch64 GNU/Linux
```

`uname -m` output:

```text
aarch64
```

Commands run:

```sh
uname -m
uname -a
touch src/om/reader.cpp src/scan/read_om.cpp test/native/reader_capacity_test.cpp test/native/remote_async_test.cpp
cmake --build build/official-matrix/v2.0.0/builds/76da44b31663a1775355935545f257c7536d02f8866c3af58233a985c206f293/release --target reader_capacity_test remote_async_test --parallel 4
sha256sum src/om/reader.cpp src/include/duckomo/om_reader.hpp src/scan/read_om.cpp test/native/reader_capacity_test.cpp test/native/remote_async_test.cpp build/official-matrix/v2.0.0/builds/76da44b31663a1775355935545f257c7536d02f8866c3af58233a985c206f293/release/test/native/reader_capacity_test build/official-matrix/v2.0.0/builds/76da44b31663a1775355935545f257c7536d02f8866c3af58233a985c206f293/release/test/native/remote_async_test
```

The `touch` command only forced CMake to rebuild the current sources; it changed no file contents. The build log confirms compilation of the current `src/om/reader.cpp`, `src/scan/read_om.cpp`, `reader_capacity_test.cpp`, and `remote_async_test.cpp`. The compile command uses `-march=armv8-a -mtune=generic` and source paths under this checkout.

Commands run after the final build:

```sh
build/official-matrix/v2.0.0/builds/76da44b31663a1775355935545f257c7536d02f8866c3af58233a985c206f293/release/test/native/reader_capacity_test
build/official-matrix/v2.0.0/builds/76da44b31663a1775355935545f257c7536d02f8866c3af58233a985c206f293/release/test/native/remote_async_test
```

Both passed. `reader_capacity_test` passed the new retention, synchronous failure, injected cancellation, and remote `data_cache` reuse checks. `remote_async_test` passed async/sync result equality, cross-batch prefetch, multi-worker inflight limits, HTTP/read error handling, cancellation, and recovery. It now also sets DuckDB’s `debug_physical_table_scan_execution_strategy` to `SYNCHRONOUS` while `duckomo_async_io` is enabled; the inline async synchronous results matched and completed with no reads left in flight.

## Source and binary identity

Git HEAD (the source base; the checkout was dirty before this fix):

```text
80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9
```

SHA-256 values after the final build:

```text
src/om/reader.cpp:                         4949a0ff7632aa595f7a94215af9009d65dae7208facf826486d725d1550de91
src/include/duckomo/om_reader.hpp:         431d61fc552055097592f08938d52853c30545b5ec6516e5fc0fba7a40d76d04
src/scan/read_om.cpp:                      a49a7dae1e07975765c9f19e6929e8b9284f067b1b5604d26afdb2dbbcc9a741
test/native/reader_capacity_test.cpp:      e44e977001c237c73f768797437a5399648b0d334d523fe73821d932738a918c
test/native/remote_async_test.cpp:         0bd5c1952dc3c0f2185129f4feb4470482dd0a1c17200f288480715c9f90c124
reader_capacity_test binary:              06f7c5847e06684976610c5c832c3f05a3390445729e57cb1ad64b7ab5c11ff1
remote_async_test binary:                 f428b35470d55b0e14bfcab0f9dd64165de0fb723de64618bd1500bb48332f47
```

Both test binaries are ELF 64-bit AArch64 executables. Build output and the two native test logs are saved alongside this note.

## Scope and limits

No OOM was observed or tested. No real network request was made by the retained-cache probe. Only Linux AArch64 was built and run. I did not run the full 008 or 010 suites, other platform runners, or the throughput gate; SC-006 remains relaxed and does not meet the original ≥20% gate. T042 and T043 remain unchecked and were not run. This fix does not mark 008 or 010 PASS and does not close 010 S2, SC-002, or SC-007. `git diff --check` passed.
