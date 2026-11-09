# Quickstart Validation: read_om_series

**Status (2026-11-06 follow-up)**: Current-source acceptance is **PASS for the declared scope only**: Linux AArch64 (`linux_arm64`) and pinned DuckDB v1.5.4, v1.5.5, v1.5.6 and v2.0.0. The real producer result covers two adjacent IFS025 `temperature_2m` objects, one point and one boundary window; the other ten registered models have provenance plus synthetic runtime equivalence only, and N160 is synthetic. `read_om_series` is development-source-only, not a released feature identity. See the [follow-up report](evidence/acceptance-20261106-followup-linux-arm64/report.md) and [manifest](evidence/acceptance-20261106-followup-linux-arm64/manifest.json). Earlier 2026-08-03 and first 2026-11-06 evidence remain historical. SC-003/S4 fault runs showed injected non-absence faults failed without being silently skipped; under official HTTPFS, HTTP(S) 403/412 surfaced as unknown-status `[transport]`, and the controlled `corrupt` case returned HTTP 200 + `not-an-om-object` and failed at range-protocol validation, without exercising OM metadata/decode corruption. Feature 008 is not PASS; SC-006 remains relaxed / will not meet the original ≥20% gate, and T042/T043 remain unchecked.

## Prerequisites

Run from the repository root on Linux AArch64. Use an isolated pair build under `build/series-matrix/`; do not switch shared DuckDB checkouts. Set `DUCKOMO_SERIES_CLI`, `DUCKOMO_SERIES_EXTENSION`, and `DUCKOMO_SERIES_BUILD` to matching outputs from the selected pair, and set `DUCKOMO_SERIES_HTTPFS` to its official matching HTTPFS extension for controlled remote runs. For HTTPS, use the fixture CA/certificate/key paths through the three `--tls-*` options. Confirm CLI, loadable extension and HTTPFS identities in the build manifest; the validator uses `strace` to confirm DuckDB opened the requested extension before trusting results. The 1.5.x pairs use `test/data/grids/version-matrix.json`; v2.0.0 uses `test/data/engine-compatibility/version-matrix.json`.

The pinned pair commands used to reproduce isolated builds and local scanner regressions are:

```bash
scripts/build-version.sh --matrix test/data/grids/version-matrix.json --pair v1.5.4 --output-root build/series-matrix
scripts/validate.sh --matrix test/data/grids/version-matrix.json --pair v1.5.4 --output-root build/series-matrix --local-only
scripts/build-version.sh --matrix test/data/engine-compatibility/version-matrix.json --pair v2.0.0 --output-root build/series-matrix
scripts/validate.sh --matrix test/data/engine-compatibility/version-matrix.json --pair v2.0.0 --output-root build/series-matrix --local-only
```

Repeat the 1.5.x commands for v1.5.5 and v1.5.6. The `--local-only` workflow runs SQL/native/Python regressions without entering the separate broad remote-region gate. Do not change shared submodule checkouts or Community packaging to produce a matrix cell. Use artifact paths from each resulting build manifest, not arbitrary installed binaries.

## Generate deterministic inputs and run the local validator

These tools are implemented. Fixture generation uses the pinned official OM writer; the validator verifies fixture hashes, checks the exact requested extension load, and compares full schemas and row multisets against explicit `read_om` UNION ALL oracles.

`make test_release`, `make test_debug`, `make test_reldebug`, and `scripts/validate.sh` run this same generator before SQLLogic tests and write `build/series-fixtures/` (gitignored; do not commit the `.om` objects). Release CI leaves `DUCKOMO_BUILD_DEVELOPER_TOOLS` off and builds only `duckomo_fixture_tool` for that step. Platforms whose SQL is skipped upstream (`linux_arm64`, `osx_amd64`) stay skipped. This wiring does not extend the Linux AArch64 acceptance scope recorded above.

```bash
python3 scripts/generate-series-registry.py --check
python3 scripts/generate-series-fixtures.py \
  --fixture-tool "$DUCKOMO_SERIES_BUILD/test/tools/duckomo_fixture_tool" \
  --output-root build/series-fixtures
python3 scripts/validate-series.py \
  --duckdb "$DUCKOMO_SERIES_CLI" \
  --extension "$DUCKOMO_SERIES_EXTENSION" \
  --fixtures build/series-fixtures \
  --mode local \
  --output specs/010-read-om-series/evidence/local-run
```

Observed on the current source snapshot across all four pairs: all seven S1 scenarios matched their manual oracles; all 46 S2 cases passed (38 no-candidate-access preflight cases and 8 expected metadata-candidate access cases); and S5/S6 N160 domain-vs-explicit comparisons passed at worker caps 1/2/4. Per-pair outputs, including exact extension load traces, are under `evidence/acceptance-20261106-followup-linux-arm64/corrected-source/` (`v2.0.0` uses `local-corrected/`). The follow-up report links the source-bound pair identities and results.

A registered model supplies temporal defaults only. This example overrides its chunk length while inheriting `ecmwf_ifs`'s registered one-hour step; it also demonstrates the SQL alias for the root `value` column:

```sql
SELECT value AS temperature_2m, valid_time, lat, lon
FROM read_om_series(
  'build/series-fixtures/data/synthetic/temperature',
  model := 'ecmwf_ifs',
  chunk_time_length := 4,
  dimensions := ['lead','point','time'],
  grid := {'nx':3,'ny':2,'lat0':10.0,'lon0':100.0,
           'dlat':1.0,'dlon':1.0,'order':'lon_fastest'},
  spatial_axes := ['point'],
  valid_times := [TIMESTAMP '1970-01-01 03:00:00',
                  TIMESTAMP '1970-01-01 05:00:00']
)
ORDER BY valid_time, lat, lon;
```

For an unregistered layout, provide both temporal values; the model name does not select a spatial grid:

```sql
SELECT value AS temperature_2m, valid_time, lat, lon
FROM read_om_series(
  'build/series-fixtures/data/synthetic/temperature',
  model := 'synthetic_fixture',
  chunk_time_length := 4,
  time_step := INTERVAL '1 hour',
  dimensions := ['lead','point','time'],
  grid := {'nx':3,'ny':2,'lat0':10.0,'lon0':100.0,
           'dlat':1.0,'dlon':1.0,'order':'lon_fastest'},
  spatial_axes := ['point'],
  valid_times := [TIMESTAMP '1970-01-01 03:00:00',
                  TIMESTAMP '1970-01-01 05:00:00']
)
ORDER BY valid_time, lat, lon;
```


## Cross-boundary SQL and manual oracle

After loading the verified local extension with the matching CLI, execute from repository root. Inputs below use the deterministic three-chunk fixture set, L=4 and dt=1h. Load the actual extension path before executing this SQL.

```sql
CREATE TEMP VIEW actual AS
SELECT * FROM read_om_series(
  'build/series-fixtures/data/synthetic/temperature',
  chunk_time_length := 4, time_step := INTERVAL '1 hour',
  dimensions := ['lead','point','time'],
  grid := {'nx':3,'ny':2,'lat0':10.0,'lon0':100.0,
           'dlat':1.0,'dlon':1.0,'order':'lon_fastest'},
  spatial_axes := ['point'],
  valid_times := [TIMESTAMP '1970-01-01 03:00:00',
                  TIMESTAMP '1970-01-01 05:00:00']
);

CREATE TEMP VIEW expected AS
SELECT * FROM (
  SELECT * FROM read_om(
    'build/series-fixtures/data/synthetic/temperature/chunk_0000.om',
    dimensions := ['lead','point','time'],
    grid := {'nx':3,'ny':2,'lat0':10.0,'lon0':100.0,
             'dlat':1.0,'dlon':1.0,'order':'lon_fastest'},
    spatial_axes := ['point'],
    axes := {'time': {'axis':'time',
      'start':TIMESTAMP '1970-01-01 00:00:00','step':INTERVAL '1 hour'}}
  )
  UNION ALL
  SELECT * FROM read_om(
    'build/series-fixtures/data/synthetic/temperature/chunk_0001.om',
    dimensions := ['lead','point','time'],
    grid := {'nx':3,'ny':2,'lat0':10.0,'lon0':100.0,
             'dlat':1.0,'dlon':1.0,'order':'lon_fastest'},
    spatial_axes := ['point'],
    axes := {'time': {'axis':'time',
      'start':TIMESTAMP '1970-01-01 04:00:00','step':INTERVAL '1 hour'}}
  )
) u
WHERE valid_time BETWEEN TIMESTAMP '1970-01-01 03:00:00'
                     AND TIMESTAMP '1970-01-01 05:00:00';

DESCRIBE SELECT * FROM actual;
DESCRIBE SELECT * FROM expected;
SELECT * FROM actual EXCEPT ALL SELECT * FROM expected;
SELECT * FROM expected EXCEPT ALL SELECT * FROM actual;
SELECT valid_time, count(*) FROM actual GROUP BY valid_time ORDER BY valid_time;
```

The cross-boundary `[03:00,05:00]` SQL and both user-facing examples are exercised by the current four-pair S1 matrix. All seven S1 scenarios, including negative epoch, NULL slot and slotless subsecond window, passed with schema equality and empty two-way EXCEPT ALL results. Per-pair SQL and row evidence is in `evidence/acceptance-20261106-followup-linux-arm64/corrected-source/`.

## Missing and strict behavior

Current-source controlled HTTP, verified HTTPS and S3 missing/strict and fault matrices passed on all four pairs. Injected non-absence faults failed rather than being silently skipped; under official HTTPFS, HTTP(S) 403/412 surfaced as unknown-status `[transport]` errors. The controlled `corrupt` fixture served HTTP 200 with `not-an-om-object` and failed at the range-protocol stage; it did not exercise corrupt OM metadata parsing or decode failure. Each pair completed 117/117 controlled scenarios, including post-bind mutation/recovery, and the per-origin audits found no listing, metadata discovery or witness requests. See each pair's [controlled-remote evidence](evidence/acceptance-20261106-followup-linux-arm64/report.md). The 2026-08-03 run remains historical evidence for its earlier source snapshot.

The current local S2 corpus passed 46/46 cases on each pair, with per-case diagnostics and access traces retained. This covers 38 argument/preflight cases that opened no candidate object and 8 metadata-dependent mismatches that opened the expected present candidate(s). Controlled missing/strict and transport-fault behavior passed in all 117 current-source scenarios per pair. See [sql.md](contracts/sql.md) and the [follow-up acceptance report](evidence/acceptance-20261106-followup-linux-arm64/report.md).

## Projection, parallel execution and metrics

Run the actual SELECT directly before reading `duckomo_last_scan_metrics()`; views are rebound per query, so do not count metrics from an earlier DESCRIBE as executed scans. Repeat 1/2/4 workers using existing `threads` / `duckomo_max_threads` settings. Execute full result comparisons, a spatial subset, additional time/value filters, COUNT(*) and coordinate/time-only projection. A value predicate is a value dependency even if the output is only COUNT(*).

Observed on the current local run for all four pairs: synthetic N160 series results matched the explicit-grid manual UNION ALL at worker caps 1/2/4 (3,200 rows per value query). Each executed child retained a distinct scan ID. Value-dependent scans read/decode values; value-free COUNT scans recorded zero value index/data requests and zero decoded chunks. Worker caps and recorded query-owned memory bounds were respected. Per-pair queries and counters are under `evidence/acceptance-20261106-followup-linux-arm64/corrected-source/`.

The registered pushdown test also distinguishes bind-time metric records from executed scans: binds can register before optimizer elimination, so scan completion and I/O counters identify actual child execution. Value predicates remain value dependencies even when the SQL output is only `COUNT(*)`.

## Controlled remote and real-object gates

The controlled-remote runner is implemented over isolated HTTP, certificate-verified HTTPS and path-style S3 fixtures. Set `DUCKOMO_SERIES_HTTPFS` and the local test certificate paths, then run:

```bash
python3 scripts/validate-series.py \
  --duckdb "$DUCKOMO_SERIES_CLI" \
  --extension "$DUCKOMO_SERIES_EXTENSION" \
  --fixtures build/series-fixtures \
  --mode controlled-remote \
  --httpfs "$DUCKOMO_SERIES_HTTPFS" \
  --tls-cert "$DUCKOMO_SERIES_TLS_CERT" \
  --tls-key "$DUCKOMO_SERIES_TLS_KEY" \
  --tls-ca "$DUCKOMO_SERIES_TLS_CA" \
  --output specs/010-read-om-series/evidence/controlled-remote-run
```

The run writes commands, result/oracle data, per-child metrics and sanitized origin audits under its output directory. It must verify the matching official HTTPFS and exact requested DuckOMO extension. Do not place credentials in CLI arguments or evidence. Controlled fixtures verify software behavior; they do not establish real producer-object support.

The real-object mode accepts a frozen `--manifest`. Its checked-in evidence manifest freezes the two adjacent IFS025 chunk keys and identities, producer/registry provenance, explicit per-chunk time axes, and the independent boundary rows. The runner validates the local snapshot hashes, rechecks live HTTPS HEAD identities, verifies that DuckDB opened the requested DuckOMO and official HTTPFS extension files, and compares `read_om_series` with a freshly executed manual `read_om` UNION ALL and the frozen rows.

```bash
python3 scripts/validate-series.py \
  --duckdb "$DUCKOMO_SERIES_CLI" \
  --extension "$DUCKOMO_SERIES_EXTENSION" \
  --httpfs "$DUCKOMO_SERIES_HTTPFS" \
  --manifest specs/010-read-om-series/evidence/run-20260803-linux-arm64/t040/real-object-manifest.json \
  --mode real \
  --output specs/010-read-om-series/evidence/run-20260803-linux-arm64/t040/real-v1.5.5
```

The current-source frozen IFS025 real-object S7 run passed on all four pairs. Live HTTPS identities were rechecked and `read_om_series` matched both the manual `read_om` union and the frozen two-row boundary oracle. The 2026-08-03 reports remain historical evidence for source hash `4974560d43df2a1abcc67cb24755f57b9da36df37a92df3d0f2d34fae3aec516`. The real-data result covers only the two captured adjacent objects, one point and one boundary window; it does not establish broad producer-data support.

The current-source four-pair build, local S1/S2/SC-004/S5/S6 checks, controlled HTTP/verified HTTPS/S3 S3/S4/SC-004/S5/S6 scenarios, official HTTPFS native diagnostics and full S7 workflows passed on all four pairs. S4's demonstrated behavior is failure without silent skipping; HTTP(S) 403/412 remain unknown-status transport errors under official HTTPFS, and the `corrupt` fixture fails before OM payload parsing or decoding. Each pair's full `scripts/validate.sh --local-only` command exited 0; each also passed the 14 sanitizer checks and the real-object S7 comparison. Exact commands and outputs are linked from the [follow-up acceptance report](evidence/acceptance-20261106-followup-linux-arm64/report.md).

## Record results honestly

The current-source status maps S0–S7 and SC-001–SC-007 in the [follow-up acceptance report](evidence/acceptance-20261106-followup-linux-arm64/report.md); pair manifests, commands, fixtures, results and traces are in `evidence/acceptance-20261106-followup-linux-arm64/`. Feature 010 acceptance passes for the declared Linux AArch64 four-pair matrix. No x86_64 or multi-architecture result, broad real-producer claim, or Feature 008 completion is asserted. Feature 008 SC-006 remains relaxed / won’t meet the original ≥20% target, with T042/T043 unchecked.
