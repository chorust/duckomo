# US3 nearest status and source semantics

**Status: implemented local US3 profile; N2 passes for the declared profile scope and N6 remains incomplete.** The status and source semantics below are exercised by the current Linux AArch64 v1.5.4 build and the latest cross-version SQL/native run on v1.5.4/v1.5.5/v1.5.6. The N2 contract matrix passes for its declared scope; N6 has not run.

## Reproduction identity

- DuckDB CLI: `build/release/duckdb`, `v1.5.4 (Variegata) 08e34c44`.
- Matching spatial package: `build/regions-runtime/v1.5.4/official/spatial.duckdb_extension`, SHA-256 `af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a`.
- The native extension/test builds were rebuilt from the current dirty worktree; these are development artifacts, not clean release builds. DuckOMO extension SHA-256: v1.5.4 `a9aec833be443d481a0ce583ec529eea6f58fd97aeab16c42149ac64b359e7fa`; v1.5.5 `94d87241f57df140970b95bfa3e0c46476da76df4c3d8ba33ace3ba5bada6520`; v1.5.6 `75ecd0c5c273f6793e4c01ac38a058e720ec944b60b949600c31c88c91273824`.
- SQL suite: `test/sql/region_sampling.test`, 176 assertions passed on v1.5.4, v1.5.5, and v1.5.6 on 2026-10-17. Latest command stdout hashes: v1.5.4 `7aff9389adf92a976e6b8272cadd956186630a4f91b3bb287d0655dd06c41e4c`, v1.5.5 same, v1.5.6 same; see [`run-20261017-sampling-validation-final`](run-20261017-sampling-validation-final/).
- Native nearest parity/status suite: `region_sampling_test <matching spatial package>`, passed on all three pinned runtimes after the metrics and status-matrix assertion refresh.
- Permission denial was verified by running each native suite in an Ubuntu 24.04 Docker container as UID/GID 65534 with the workspace mounted read-only. Host UID switching and user namespaces remain unavailable; the container exercised the prepared-source permission removal test and recovered without modifying repository files.
- Other native failure/recovery cases cover prepared object mutation, truncation/short read, and decoder corruption.

## Frozen status rows

The reproducing statements are in `test/sql/region_sampling.test` under `EMPTY and out-of-domain polygons`, `Full per-variable map and candidate/source/target NULL semantics`, `Distance-limit failure`, and binary64 threshold adjacency. The core result rows are:

| Region condition | Geometry status | Variable status | source_count | valid_source_count | target lat/lon | candidate source index/coords/distance | om_source |
| --- | --- | --- | ---: | ---: | --- | --- | --- |
| EMPTY | `empty_geometry` | `empty_geometry` | 0 | 0 | NULL | NULL | NULL |
| Outside domain | `outside_domain` | `outside_domain` | 0 | 0 | present | NULL | NULL |
| Gap, zero max distance | `beyond_max_distance` | `beyond_max_distance` | 0 | 0 | present | present | NULL |
| Gap, accepted threshold | `ok` | `ok` | 1 | 1 | present | present | present when requested |
| Selected NaN source | `ok` | `source_null` | 1 | 0 | present | original selected source retained | present when requested |
| Selected `+Inf` / `-Inf` | `ok` | `ok` | 1 | 1 | present | original selected source retained | present when requested |

The SQL tests compare the exact computed distance to `max_distance_m` and `nextafter` neighbors. At the exact positive threshold the candidate is accepted; one representable binary64 step below it returns `beyond_max_distance` and retains candidate coordinate and distance. Zero-distance equality returns `ok` for a polygon whose representative point is exactly a grid point.

The full map/NULL test selects original axis tuple `[1,2,1]` for EMPTY and Outside and checks both variable statuses/counts, target NULLs, candidate field NULLs, and the complete `om_source` NULL value. The zero-threshold test checks that the target and candidate remain present, sampled source counts are zero, and `om_source` is NULL.

## Independent value and source checks

- Official OM reader CSV references: `small-separate.temperature.csv` SHA-256 `07693c926d2365a0d33edf2dc03472fd3aa67b48e5624faf15beda3b67a479d3`; `small-separate.humidity.csv` SHA-256 `9b3ca031d4f7e1e63336d724ce276aa2eeecff718dd08f7765572e6449f91aca`.
- SQL `EXCEPT ALL` covers all 12 non-spatial tuples, both variable values, and selected source index. The native test independently maps axis positions to logical indices and compares each value to the official-reader CSV.
- Native status checks verify EMPTY, Outside, and a valid Gap in one execution. Empty-source rows do not terminate the scan before the later valid region. Positive and negative infinity retain `ok` status and valid count 1.
- The same execution's metrics record 36 `region_axis_result` rows for 3 regions × 12 axis combinations before the SQL axis residual, exact completion, and per-variable totals of 12 `ok`, 12 `empty_geometry`, and 12 `outside_domain`; the points-only regions section is `applicable=false` with its exact/match counters NULL.
- Injected nearest value failures remain errors rather than `source_null`; after permission removal, object change, short read, and decoder corruption, a valid nearest query succeeds on the same connection.

## Requirement mapping and remaining scope

- FR-006/007: nearest distance and inclusive maximum threshold, including adjacent binary64 thresholds.
- FR-008: selected source remains fixed when its value is NaN; no farther point is substituted.
- FR-009/010: current tests cover three non-spatial axes, duplicate semantic labels, reverse/flattened/interleaved layouts, and original value columns.
- FR-011/012: target coordinates, candidate location, source index, and optional `om_source` are distinct fields with tested NULL behavior.
- FR-013/014: geometry and per-variable maps are tested for EMPTY, domain failure, accepted and over-distance candidates, NaN, and infinities. Strict outside-domain errors are asserted under `WHERE` and `LIMIT 0`.
- The locally implemented US3 status/source matrix and failure semantics are complete for the registered synthetic profile. N2 passes for the declared scope; real-input SC-004 work and the independent N6 audit remain open. Permission denial and the status matrix have cross-version command evidence.

## Output identity

- `test/sql/region_sampling.test` SHA-256: `e39181f340425b35d88ea1d3864f5bf52d1b026f925cbcafbce26ad43586cdab`; 176 assertions passed on all three runtimes.
- `test/native/region_sampling_test.cpp` SHA-256: `993e7d1af69c6ea4f2e0f82b090e342eb753db276749c478b7fa404c02ac6fd3` in the archived run input manifest; native suites passed on all three pinned runtimes, including UID 65534 permission-denial execution.
- `src/include/duckomo/metrics.hpp` SHA-256: `67c69e360fe1c0c3ad3a0ee6629460fc2cadc0483016e19c508c00699d32ced4`.
- `src/scan/read_om.cpp` SHA-256: `65b9d355a8c3325dbec6a4e00470f8e221064db81b108e75c1166276de02b8fc`.
- Latest N1/N2 scoped acceptance manifest: [`n1-n2-acceptance-refresh-20261019-final-7/manifest.json`](n1-n2-acceptance-refresh-20261019-final-7/manifest.json); the three-version SQL/native matrix passes for the declared scope. Historical N0–N4 manifest [`run-20261017-sampling-validation-final/manifest.json`](run-20261017-sampling-validation-final/manifest.json), SHA-256 `a5f045e417d2ad3a5e109561feecee0a31cad9442841e3d84c284c852eb2f589`, retains the gate statuses captured at that time.
