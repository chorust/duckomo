# Final Implementation Evidence: Phase 0–2 Local OM Scanner

Date: 2026-09-28. Implementation and all executable repository gates passed on Linux AArch64. The plan names Linux x86_64 as the target; x86_64 execution and independent third-party verification are not available in this environment, so SC-006 remains partially verified.

## Final gate commands

| Command | Exit | Result |
| --- | ---: | --- |
| `make release` | 0 | Release CLI, static/loadable extension, fixture writer, harness and native checks built |
| `make test` | 0 | 144 SQL assertions, all five native checks and four-scenario release harness passed |
| `./build/release/test/tools/duckomo_fixture_tool --output /tmp/duckomo-final-fixture.fMCToO` | 0 | Fixed official writer and independent oracle regenerated assets |
| `diff -qr test/data /tmp/duckomo-final-fixture.fMCToO` | 0 | Regenerated files and manifest exactly match committed fixtures |
| `./scripts/validate.sh build/release` | 0 | SQL/native tests, all 26 manifest asset hashes, regeneration diff, harness output and evidence schema passed |
| `make sanitizer-test` | 0 | ASan/UBSan native checks passed 4/4; details in [sanitizer evidence](sanitizer.md) |

The fixture set was checked against the fixed dependencies: DuckDB `08e34c447bae34eaee3723cac61f2878b6bdf787`, OM file format `d8855e418e2231ae8439f0c7e840fa3f93b371e3`, and extension-ci-tools `b777c70d30942cca5bef62d6d4fa23a13362f398`. The projection fixture hash is `fb36284bf8f932c82b171c7bce28b6e767b95321d1d986dc0a1dbd3b84d88a43`; raw, multi-variable, nested, special-value and cross-batch fixture hashes are recorded in [Phase 0](phase0.md), [Phase 1](phase1.md) and [Phase 2](phase2.md). All 26 fixture, reference and negative-asset SHA-256 values passed.

## SC-001–SC-006 outcomes

| Success criterion | Outcome | Evidence |
| --- | --- | --- |
| SC-001: complete/narrow results match references at every checked position | Pass | Official-reader oracle checks for raw, special, large, multi, nested and projected variables; SQL comparisons against reference or fully materialized scans; SQLLogicTests passed 144 assertions total |
| SC-002: array, hierarchy, nonsquare and cross-batch cases | Pass | `[2,3]` root, nested multi-variable fixtures, and `[83,127]` projection fixture with 10,541 rows; hashes and schema/results recorded in earlier phase evidence |
| SC-003: unused projection has zero decode and fewer value bytes | Pass | Temperature-only read 165,767 data bytes versus 628,703 full-scan data bytes (73.6% fewer); humidity and pressure had zero data reads/decode in the narrow case |
| SC-004: filter, expression, reorder, duplicate, aggregation/sort, count and empty-result semantics | Pass | Projection SQLLogicTest compares projected results against full-read materializations and covers all listed cases; native metric checks and release harness passed |
| SC-005: invalid inputs fail correctly and failures/cancellation recover | Pass | Negative type/layout/version/axis/reference cases; 100 valid/error query pairs with no descriptor growth; corrupt-payload errors and real SQL cancellation followed by successful full recovery |
| SC-006: four documented operations in a Python/Swift-free query runtime, independently repeatable | Partial | In an isolated process with `env -i PATH=/nonexistent`, extension load, multi-variable `DESCRIBE`, full scan and temperature-only scan all exited 0 and returned expected columns/values. The available executor is the implementation environment on AArch64, not an uninvolved verifier on the planned x86_64 target. |

For the isolated SC-006 run, the process used the release DuckDB CLI, the built extension and committed fixtures; its environment contained only `PATH=/nonexistent`. The SQL operations were `LOAD`, `DESCRIBE` on the aligned `multi.om` schema, a full read ordered by `/temperature`, and a narrow `/temperature` read over the same file. It returned `/humidity FLOAT` and `/temperature FLOAT`; the full rows were `(100,0)` through `(105,5)`, and the narrow values were `0` through `5`. This proves that these operations did not need Python, Swift or a conversion step in this isolated process. It does not prove execution on x86_64 or independent reproduction by a separate verifier.

## Projection measurements

The four release subprocesses ran with one DuckDB thread and disabled application query cache. The OS page cache was not cleared. Full per-variable counters, exact SQL, elapsed times and peak RSS appear in [Phase 2 evidence](phase2.md) and `build/evidence/`.

| Scenario | Rows / count | Value data bytes | Metadata bytes | Total bytes |
| --- | ---: | ---: | ---: | ---: |
| Full scan | 10,541 | 628,703 | 660 | 649,158 |
| Temperature only | 10,541 | 165,767 | 660 | 173,552 |
| Temperature output + humidity filter | 108 | 360,619 | 660 | 375,266 |
| `COUNT(*)` | 10,541 | 0 | 660 | 660 |

The filter scenario read both humidity and temperature, while pressure incurred no index/data reads or decode. Count read shared metadata only. The byte measurements describe application-level successful reads, not device I/O.

## Platform and implementation boundary

The available host reports `Linux 6.17.0-1029-nvidia aarch64`. No x86_64 build/runtime or emulator result was produced. The sanitizer run instruments extension-owned C++ code and native checks; the pinned upstream OM C library remains an uninstrumented dependency. Local OM v3 Float32/FPX reads and explicit axis alignment are validated. Coordinate columns, filter-range pruning and remote I/O remain outside this Phase 0–2 implementation.
