# N4 nearest resources and lifecycle evidence

**Status: N4 registered local acceptance passed.** The latest selected gate run records the three-version worker/task/lifecycle matrix, six isolated memory processes, and matching-spatial v1.5.4 sampling ASan/UBSan as `pass`. Controlled HTTP/HTTPS/signed-S3 scenarios belong to N5; overall 006 and N6 acceptance remain open; the recorded local N7 reproduction was run by an implementation worker and does not meet the independent-executor requirement.

## Runtime and input identity

- Platform: Linux AArch64.
- DuckDB: v1.5.4, v1.5.5, v1.5.6.
- Matching spatial package SHA-256: v1.5.4 `af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a`; v1.5.5 `b9129a7da7fde8eb7fb951d99a115cbf2e6fa34c8d41671086169174a1f5725d`; v1.5.6 `96b8d4b5d735e9ebe91da7badee4328dfbd07068ce4885d1d9b6de8c22370b14`.
- Native sampling binary passed on all three builds against its matching spatial package after the 2026-10-17 metrics assertion refresh. Current DuckOMO extension SHA-256: v1.5.4 `a9aec833be443d481a0ce583ec529eea6f58fd97aeab16c42149ac64b359e7fa`; v1.5.5 `94d87241f57df140970b95bfa3e0c46476da76df4c3d8ba33ace3ba5bada6520`; v1.5.6 `75ecd0c5c273f6793e4c01ac38a058e720ec944b60b949600c31c88c91273824`.
- Permission denial passed on all three runtime pairs in an Ubuntu 24.04 Docker container as UID/GID 65534, with the workspace mounted read-only. The prepared read is denied after permissions are removed, is not reported as `source_null`, and subsequent connection work succeeds.
- Sampling ASan/UBSan was configured against the v1.5.4 matching spatial file and passed in the latest package: CTest `duckomo_sanitizer_region_sampling_test` (24.15 seconds; stdout SHA-256 `3b7f06e1b51ecfa95c8410eced34e39c69e7bbf440a2e2067b5862173f04a958`).

## Bounded memory and full-value counters

| Fixture and operation | Query-owned upper bound | Worker scratch peak | Locator CPU | Nearest bytes/chunks | Full-value bytes/chunks |
| --- | ---: | ---: | ---: | ---: | ---: |
| 64×64 points, 12 rows | 61,865 B | 17,163 B | 211,744 ns (v1.5.4) | 1,233 B / 12 | 20,392 B / 3,185 |
| 256×256 points, 12 rows | 86,533 B | 17,178 B | 3,000,224 ns (v1.5.4) | 1,290 B / 12 | 346,377 B / 50,952 |

The v1.5.4 values above are from the six isolated-process measurements in [`run-20261017-sampling-validation-final/manifest.json`](run-20261017-sampling-validation-final/manifest.json); each process result is bound by its N4 stdout hash. Latest locator CPU times are v1.5.5 195,712 ns and 3,023,552 ns, and v1.5.6 314,704 ns and 4,888,192 ns. All three versions report the same owned-memory bounds and reader counters. The larger grid has 16× the points and 1.40× the query-owned upper bound; worker work peak grows 1.00× (17,163 to 17,178 B), below the 2× ceiling. The 256×256 process takes more locator CPU, as expected from exhaustive spatial search. Both nearest scans emit the same number of rows and decode 12 selected chunks; the full baseline aggregates the same variable over the complete source array.

A separate `skippable-values.om` same-object comparison reads 224 bytes/1 chunk for nearest and 15,449,547 bytes/66,432 chunks for the full baseline. These counters are from successful OM value reads and completed decoder chunks.

## Worker and task checks

- A 513-position non-spatial axis produced all original indices 0–512 over multiple bounded tasks and sum 6,156.
- Worker limits 1, 2, and 4 produced the same ordered-result fingerprint over 1,539 region-axis rows and one shared source index; metrics observed the configured active-worker high-water mark.
- The exact signed-64 result-count limit is accepted; multiplying past that limit raises `ShapeOverflow` before cursor execution.
- Nearest execution is further capped at 256 rows per task, dynamically reduced to meet 512 KiB worker scratch; the current multi-task assertion exercises 513 rows but does not construct an exact 2,048-row task.
- The scheduler uses an 8 MiB owned budget with a 512 KiB control reserve and up to 512 KiB scratch per worker; worker concurrency is bounded by this accounting.

## Cancellation, early stop, failure, and recovery

Cancellation after a visible result returns an interrupt/cancel error. Terminal metrics report `status=cancelled`, `scan_complete=false`, `result_rows_exact=null`, and `query_owned_released_at_terminal=true`. A full query on the same connection succeeds afterward.

`LIMIT 1` returns the visible row while metrics keep `scan_complete=false`, `result_rows_exact=null`, `result_count_complete=false`, `exact_candidate_records=null`, and `count_complete=false`. A later full scan on the same connection succeeds.

Nearest object mutation, truncation, and injected value lookup/decode corruption remain errors and never become `source_null`; the native nearest suite checks same-connection recovery. The sanitizer suite also checks decoder-failure cleanup. Permission denial is verified for all three runtimes under the container's unprivileged UID; host user namespaces remain restricted.

The points lifecycle sanitizer assertion was updated after its previous failure conflated nearest's inapplicable `match_count_complete=null` with points' incomplete boolean. It now checks nearest counts only once the nearest plan has been published; the pre-plan object-change case checks terminal failure/release without demanding plan-specific metrics. Targeted `duckomo_sanitizer_region_scan_test` and `duckomo_sanitizer_region_sampling_test` both pass.

## Gate boundary

The latest validator package reports N4 `pass` for the registered local N4 matrix. Its manifest is [`run-20261017-sampling-validation-final/manifest.json`](run-20261017-sampling-validation-final/manifest.json), SHA-256 `a5f045e417d2ad3a5e109561feecee0a31cad9442841e3d84c284c852eb2f589`; `inputs.json` SHA-256 `576ee6c44a87a8270475929cc7f538a1024af502d47580b4307ffadd5f512061`; `commands.jsonl` SHA-256 `da6cc7b64d75913a9f9aaae4ffc3a06022c3f61775eb8439b4e5fc231a5ef8b2`; sanitizer stdout is N4-50 with SHA-256 `3b7f06e1b51ecfa95c8410eced34e39c69e7bbf440a2e2067b5862173f04a958`. The focused N0/N3/N4 run is [`run-20261017-n0n3n4-final`](run-20261017-n0n3n4-final/), manifest SHA-256 `444ed37c3d03a657b718c3eb7e8e1680252bf446604c85470f04e131189762f6`. The memory comparison enforces both query-owned upper-bound and worker work-peak growth at no more than 2×. N5 real-data and controlled-transport subchecks are recorded separately; raw-source authorization and durable archive remain unresolved. This N4 evidence does not complete N5 or N6 audit; the recorded local N7 reproduction does not satisfy the independent-executor requirement.
