# 003 Dimensions, Remote and Parallel Reads — Final Evidence

**Status: partial implementation evidence; full G0–G7 release acceptance has not passed.**  
Validation date: 2026-10-02. The source tree was dirty on `main`; base commit: `50b51787a480d4abf11e45488fa44e5c60fc6394`.

## Build and fixed inputs

- Platform: Linux AArch64, kernel `6.17.0-1029-nvidia`.
- Build: Release, `build/release-vcpkg`, vcpkg triplet `arm64-linux`.
- DuckDB: v1.5.4, commit `08e34c447bae34eaee3723cac61f2878b6bdf787`; rebuilt CLI reports `v1.5.4 (Variegata) 08e34c44`.
- OM C: `d8855e418e2231ae8439f0c7e840fa3f93b371e3`.
- extension-ci-tools: `b777c70d30942cca5bef62d6d4fa23a13362f398`.
- Paired httpfs source: `c3f215ab360f04dc3d3d5305fa81849c0121f111`, patch revision `duckomo-httpfs-range-v2`. Patch/header hashes are pinned in `third_party/httpfs-patches/manifest.json`.
- DuckDB CLI SHA-256: `c7649dca212e23511766bf6b865c39f6d3e7687b5e38b4ab949559a0eb70beba`.
- DuckOMO extension SHA-256: `9525cbab840069aee7c7f16df1794d35bd720c7aa4bde52119f37e1d28d7517d`.
- httpfs extension SHA-256: `1b0bea6729c9891ea788e304d69b1e98722d91de038ff523568af1d43092671c`.
- Remote validation runner SHA-256: `bdbe50efe97b075c3b869524452d7a031dd63b57752a4af9f2929ca72c7d1699`.
- Synthetic manifest SHA-256: `f2051be5acc62edf28faa48616890bde404674d57c1b4a2a97081cdc3cd2efc9`.
- Performance fixture `dimensions_perf.om`: SHA-256 `47a803a4769ab5a8c6622f9be5cc05b53b96152443917b7603d78dab94e1ec13`, 131,072 rows.
- Fixed real sample is present at `build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om`; SHA-256 `0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd`, 5,812,040 bytes. It matches `test/data/domain-manifest.json`.

## Gate results

| Gate | Requirements | Result | Evidence |
|---|---|---|---|
| G0 | Existing local SQL/schema/projection/time compatibility | **PASS locally** | `scripts/validate.sh build/release-vcpkg`, final run exit 0; 10 SQLLogicTests and registered native checks passed |
| G1 | FR-001–005, SC-001: five semantic axes and layouts | **PASS locally** | Fresh dimensions harness; coordinate oracle, alternate order, and five-axis outputs in `local-run/2026-10-02/dimensions/` |
| G2 | FR-006–009, SC-002/004: filtering correctness and lower/zero value I/O | **PASS locally** | Full/local data bytes: 1,000,800 / 4,680; decoded chunks: 4,096 / 8. Coordinate-only, count, and empty cases show zero value index/data/decode reads |
| G3 | FR-010–012, SC-003: local/HTTP/S3 parity, real sample, server body audit and failures | **NOT PASSED — FULL GATE NOT RUN** | Controlled HTTP/S3 endpoints, setup SQL, and audit logs were not configured. The supplemental public HTTPS probe below is not this gate |
| G4 | FR-013–014, SC-005/007: local/HTTP/S3 task coverage and performance | **LOCAL PORTION PASS; FULL GATE NOT PASSED** | Fresh local 1/2/4 runs consumed 131,072 rows with two tasks and two active workers; remote runs were not executed |
| G5 | FR-015–016, SC-006: remote cache savings, capacity, version and revocation | **NOT PASSED — NOT RUN** | No controlled HTTP/S3 run; local cache checks passed |
| G6 | FR-017–019, SC-007: server reconciliation, failure/cancel cost and isolation | **NOT PASSED — NOT RUN** | Local v3/session checks passed; no remote service logs or remote fault run are available |
| G7 | FR-020, SC-008: independent four-part quickstart reproduction | **NOT PASSED — NOT RUN** | Quickstart contains commands, dependency identities, service lifecycle instructions, expected results, and a reviewer checklist. No independent reviewer run is recorded |

The final local validator regenerated fixtures and verified 45 fixture/reference/negative-asset hashes. It passed 10 SQLLogicTests, all registered native checks, 14/14 ASan/UBSan checks, four projection scenarios, and the dimensions G0–G2 plus local G4 harness. The fresh run outputs are retained in [`local-run/2026-10-02/`](local-run/2026-10-02/).

The coordinate oracle is `test/data/dimensions-coordinates.csv` (SHA-256 `b07fff171a18f92503b5d22d9c669f2ebcb1c3ce9603a14e232e12ff2833b54e`); the value reference is `test/data/dimensions_perf.reference.csv` (SHA-256 `415c5838c4532214def2c18479d8c0b8e6d3c273dfec85699edcc809efe5e89c`). The harness reported no oracle mismatch.

### Local G4 raw measurements

These are the five full-process elapsed times from the fresh `local-run/2026-10-02/dimensions/summary.json`; each process RSS is retained in its corresponding `g4_t*_r*.metrics.json`. The extension cache was disabled.

| Worker limit | Elapsed times (ms) | Median (ms) | Peak RSS per run (bytes) |
|---:|---|---:|---|
| 1 | 25.112, 17.142, 15.372, 15.881, 16.586 | 16.586 | 31,760,384; 31,424,512; 31,903,744; 31,485,952; 31,592,448 |
| 2 | 14.615, 15.986, 15.933, 22.617, 22.564 | 15.986 | 32,157,696; 32,346,112; 31,854,592; 32,186,368; 32,145,408 |
| 4 | 15.706, 20.908, 14.299, 16.098, 19.693 | 16.098 | 32,776,192; 32,833,536; 32,239,616; 32,022,528; 32,182,272 |

Both local parallel medians were below the single-worker median. This does not substitute for required HTTP/S3 G4 measurements.

### T059 query memory and scan metrics

The release CLI read all six rows from `test/data/raw.om`. The resulting v3 profile is retained at [`local-run/2026-10-02/t059.metrics.v3.json`](local-run/2026-10-02/t059.metrics.v3.json): `status=success`, `scan_complete=true`, `peak_query_owned_bytes=528249`, `query_memory_count_complete=true`, and `query_memory_scope=duckomo_owned_buffer_decoder_selection_capacities`. Process RSS was `30986240` bytes and is labeled `memory_scope=process`; it is not presented as query-exclusive memory. The same profile separates coordinate counters from value-variable counters. The native v3 test also verifies coordinate decode failure makes both coordinate and aggregate decode completeness false, and failed queries keep unproven memory as null/false.

Exact local profile command (exit 0):

```sh
DUCKOMO_METRICS_V3_OUTPUT="$PWD/evidence/003-dimensions-remote-parallel/local-run/2026-10-02/t059.metrics.v3.json" \
  build/release-vcpkg/duckdb -unsigned :memory: <<'SQL'
LOAD 'build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension';
SET duckomo_max_threads=1;
SELECT * FROM read_om('test/data/raw.om');
COPY (SELECT metrics FROM duckomo_last_scan_metrics())
  TO '/tmp/duckomo-t059-metrics.csv' (FORMAT CSV, HEADER false);
SQL
```

## Commands and exit codes

| Command | Exit | Scope |
|---|---:|---|
| `cmake --build build/release-vcpkg --target scan_metrics_v3_test --parallel 2` | 0 | Built the v3 metrics test after adding coordinate-failure aggregation coverage |
| `build/release-vcpkg/test/native/scan_metrics_v3_test` | 0 | v3/v2 fields, memory high-water aggregation, failure completeness, coordinate failure propagation, and URI redaction |
| `cmake --build build/release-vcpkg --target httpfs_abi_test --parallel 2` | 0 | Rebuilt stale ABI test binary against paired ABI v2 |
| `build/release-vcpkg/test/native/httpfs_abi_test` | 0 | Static/loadable paired-extension identity and local-reader independence |
| `cmake --build build/release-vcpkg --target duckomo_loadable_extension httpfs_loadable_extension --parallel 2` | 0 | Rebuilt matching loadable extensions |
| `cmake --build build/release-vcpkg --target duckdb --parallel 2` and `cmake --build build/release-vcpkg --target shell --parallel 2` | 0 | Rebuilt DuckDB library and CLI so validation used the current static extension code |
| `cmake --build build/release-vcpkg --target unittest duckomo_dimensions_validation duckomo_remote_validation --parallel 2` | 0 | Rebuilt SQLLogicTest runner and current local/remote harnesses |
| `scripts/validate.sh build/release-vcpkg` | 1, then 0 after rebuilding stale artifacts | The first run used an old ABI v1 test binary. The final rebuilt run passed all local checks; remote G3–G6 were explicitly skipped because endpoint variables were unset |
| Local `raw.om` v3 profile query described above | 0 | Verified successful scan memory peak, complete flag, process RSS scope, and per-array metric structure |
| `test/tools/duckomo_remote_validation ...` | Not run | Controlled HTTP/S3 endpoints, setup SQL, audit logs, and remote environment were unavailable |
| Independent quickstart replay | Not run | Requires an implementer-independent reviewer and the controlled remote service |

The remote command to run after provisioning and sourcing `run.env` is:

```sh
build/release-vcpkg/test/tools/duckomo_remote_validation \
  --root "$PWD" --fixtures "$PWD/test/data" \
  --output "$PWD/evidence/003-dimensions-remote-parallel/remote" \
  --duckdb "$PWD/build/release-vcpkg/duckdb" \
  --extension "$PWD/build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension" \
  --httpfs "$DUCKOMO_HTTPFS" --http-base "$DUCKOMO_HTTP_BASE" \
  --s3-base "$DUCKOMO_S3_BASE" --s3-setup "$DUCKOMO_S3_SETUP" \
  --server-log "$DUCKOMO_SERVER_LOG" --real-file "$DUCKOMO_REAL_FILE" \
  --real-manifest "$DUCKOMO_REAL_MANIFEST"
```

### Supplemental anonymous Open-Meteo HTTPS probe (not a gate pass)

On 2026-09-30, the pinned public URL from `test/data/domain-manifest.json` accepted an anonymous `Range: bytes=0-0` request with HTTP 206 and `Content-Range: bytes 0-0/5812040`. Its response included an S3 expiration header for 2026-10-06, so this live object is not a durable substitute for a controlled fixture.

The paired DuckDB/httpfs/DuckOMO build read the object over `https://` without AWS credentials. A complete `COUNT(*)` query returned 1,038,240 rows (`success`, `scan_complete=true`). A second query compared the complete `(latitude, longitude, wave_height)` multiset from the local pinned file and the public HTTPS URL using bidirectional `EXCEPT ALL`; it returned `differing_rows=0`. The public scan reported 403,208 response-body bytes over 330 transport attempts in the recorded rerun; an earlier identical comparison reported 420,537 bytes over 336 attempts. This is a live HTTPS check, not the `s3://` path, controlled server audit, synthetic fixture parity, or G3–G6 acceptance.

## Requirements trace

- **FR-001–005 / SC-001:** G1 passed locally; five-axis and alternate-axis-order results are in `local-run/2026-10-02/dimensions/`.
- **FR-006–009 / SC-002/004:** G2 passed locally; full-vs-local value-byte/decode reduction and zero-value-I/O cases are in the fresh dimensions evidence.
- **FR-010–012 / SC-003:** G3 did not pass because the controlled HTTP/S3 run was not executed. The public HTTPS probe does not prove source parity or server-body acceptance.
- **FR-013–014 / SC-005/007:** local G4 results are recorded above. Remote task coverage, parity, and timings were not run.
- **FR-015–016 / SC-006:** local cache tests passed; remote cold/hot comparison, version/revocation checks, and S3 audit were not run.
- **FR-017–019 / SC-007:** local v3 metrics now record query-owned buffer/decoder/selection capacity peaks on successful queries; failure/unknown behavior is covered by native checks. Process RSS remains labeled process-wide. Remote body reconciliation and full remote failure/cancellation audit were not run.
- **FR-020 / SC-008:** implementation and quickstart documentation are synchronized, but independent reproduction is not recorded.

## Remaining work

- **T070:** the four quickstart procedures, commands, dependency pins, service start/stop instructions, and expected results are documented. An independent reviewer still needs to execute them and record commands, exit codes, and evidence.
- **T073:** keep Phase 4–5 marked partial until G3–G7, including the independent G7 replay, have passed. The current roadmap accurately records the partial status; it is not verified for full release.

Unexecuted gates remain **not passed**. Do not mark the Phase 4–5 roadmap entry verified from this partial evidence.
