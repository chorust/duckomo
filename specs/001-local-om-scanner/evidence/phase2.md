# Phase 2 Evidence: projected `read_om` scans

Status: passed on the available Linux AArch64 host. The plan's target platform is Linux x86_64; no x86_64 execution evidence is available on this host.

## Inputs and policy

| Item | Value |
| --- | --- |
| DuckDB | `08e34c447bae34eaee3723cac61f2878b6bdf787` (`v1.5.4`) |
| OM file format | `d8855e418e2231ae8439f0c7e840fa3f93b371e3` |
| extension-ci-tools | `b777c70d30942cca5bef62d6d4fa23a13362f398` |
| Fixture | `test/data/projection.om`, SHA-256 `fb36284bf8f932c82b171c7bce28b6e767b95321d1d986dc0a1dbd3b84d88a43` |
| Logical shape / rows | `[83,127]` / 10,541 |
| Host / kernel | Linux AArch64 / `6.17.0-1029-nvidia` |
| Build and threads | release / one DuckDB thread |
| Cache policy | Each scenario uses a fresh CLI process; application cache disabled; OS page cache not cleared |

The pinned official OM writer regenerated the fixture and independent per-variable reference CSVs. The final regeneration and `diff -qr test/data <temporary-directory>` both exited 0.

## Release scenario results

`test/tools/duckomo_validation.cpp` started each exact manifest SQL statement in a fresh DuckDB CLI process, consumed every CSV result row, compared values/order against the official decoder references, and recorded child elapsed time and peak RSS in `build/evidence/*.json`.

| Scenario | Result rows / count | Variable data bytes | Total bytes | Read requests | Decoded chunks |
| --- | ---: | ---: | ---: | ---: | ---: |
| Full scan | 10,541 | 628,703 | 649,158 | 605 | 1,758 |
| Temperature only | 10,541 | 165,767 | 173,552 | 188 | 503 |
| Temperature output, humidity filter | 108 | 360,619 | 375,266 | 364 | 1,172 |
| `COUNT(*)` | count = 10,541 | 0 | 660 | 12 | 0 |

All four comparisons passed. The temperature-only scan read 462,936 fewer value bytes than the full scan (73.6% reduction). Humidity and temperature were read for the dependent filter; pressure had no index/data reads or decode calls. `COUNT(*)` fetched only the shared 660 metadata bytes and did not read index or value data. Per-variable byte/request/decode counts, exact SQL, elapsed time and peak RSS are in the four scenario JSON files. The final validation run observed 13.26 ms, 13.20 ms, 9.29 ms and 13.40 ms respectively; peak RSS was 26,316,800, 25,042,944, 25,821,184 and 23,277,568 bytes. These are descriptive measurements, not thresholds.

## Native lifecycle and projection checks

| Command | Exit | Result |
| --- | ---: | --- |
| `./build/release/test/unittest test/sql/projection.test` | 0 | 29 SQL assertions passed |
| `./build/release/test/native/projection_evidence_test` | 0 | Projection and lifecycle checks passed |
| `./build/release/test/tools/duckomo_validation --fixtures test/data --output build/evidence` | 0 | Four release scenarios passed |
| `make test` | 0 | 144 SQL assertions; all five native checks and the harness passed |
| `./scripts/validate.sh build/release` | 0 | SQL/native checks, 26 hashes, fixture regeneration diff, four scenario records and required fields passed |

The native test checked full-versus-single bytes and decoded chunks, zero physical reads for omitted variables, filter dependency reads, count-only behavior, decoder failure records excluded from performance comparisons, and 100 successful/corrupt query pairs. `/proc/self/fd` did not grow above the warmed baseline. A real interrupted SQL query produced a `cancelled` final sidecar status through DuckDB's query-end callback; the status prevented performance eligibility. A complete valid query after cancellation returned all 10,541 rows with complete successful metrics.

No application cache or OS page cache state was inferred from these byte counts. They represent successful application-level reads attributed to the official metadata, index and data phases.
