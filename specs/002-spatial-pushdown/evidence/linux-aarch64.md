# Linux AArch64 release validation

Date: 2026-09-29. Environment: Linux AArch64, 64-bit, kernel `6.17.0-1029-nvidia`, GCC 13 toolchain, DuckDB v1.5.4. `uname -m` returned `aarch64`.

Commands and outcomes:

| Command | Exit |
|---|---:|
| `make release` | 0 |
| `make test` | 0 |
| `DUCKOMO_DOMAIN_FILE="$PWD/build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om" make test` | 0 |
| `scripts/validate.sh build/release` (invoked by `make test`; with the domain environment above) | 0 |
| `build/release/test/tools/duckomo_spatial_validation --root "$PWD" --fixtures "$PWD/test/data" --output "$PWD/build/evidence/spatial" --duckdb "$PWD/build/release/duckdb" --extension "$PWD/build/release/extension/duckomo/duckomo.duckdb_extension" --domain-file "$PWD/build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om"` | 0 |

The full `make test` run passed 390 SQL assertions across six files; all raw, projection, grid, layout, callback, selection, I/O, metrics, and lifecycle native checks passed. The synthetic fixture gate verified 37 SHA-256 entries and byte-for-byte regeneration. The real-domain harness validated the sample hash and ran full/restricted/empty/coordinates/count/mixed/fallback/optimizer-empty/domain scenarios.

Success-criterion results on this architecture:

| Criterion | Result | Evidence |
|---|---|---|
| SC-001: coordinate/value/domain correctness | Pass | [domain.md](domain.md), [US1](us1.md), [US3](us3.md); explicit grid and registry each match all 1,038,240 positions and 15 official values |
| SC-002: layouts and predicate boundaries | Pass | 203 spatial SQL assertions, 14 pushdown assertions, 13 composition assertions; regular-grid/layout/selection native tests |
| SC-003: actual read reduction | Pass | [US2](us2.md): temperature bytes 165,767→1,795 and decoded chunks 503→5 |
| SC-004: zero value reads | Pass | empty, coordinates-only, and spatial count sidecars have explicit per-variable zero counters |
| SC-005: composition, fallback, and recovery | Pass | [US3](us3.md): exact mixed/seam-OR multiset comparisons, prepared rebinding, 100 success/failure cycles, corruption/cancel recovery |
| SC-006: independent quickstart reproduction | Pass | The independent verifier reran the explicit grid, real domain, regional multiset comparison, and I/O checks in [quickstart-review.md](quickstart-review.md). |

The full spatial JSON and v2 sidecars are archived under [US2](us2/) and [US3](us3/). The final domain oracle record reports 5,049 ms and peak RSS 366,686,208 bytes on this run. Do not compare these platform-specific process measurements with x86_64.
