# duckomo

[中文](README.md) | English

duckomo is a DuckDB C++ extension that queries Float32 arrays in local [Open-Meteo OM](https://github.com/open-meteo/om-file-format) files as SQL tables, without converting them first.

- **Reading**: OM v3, FPX_XOR2D / PFOR_DELTA2D_INT16 compression, root arrays, and hierarchical variables; NaN becomes SQL `NULL`.
- **Selective reads**: reads only value variables needed by output and filters; safe geographic predicates can further narrow the scan.
- **Coordinates**: generates `latitude` and `longitude` from an explicit regular grid or [68 registered domains](docs/regular-domains.md).

The reader supports one local OM file, plus HTTP(S)/S3 range reads through the paired `httpfs` extension. DuckDB can schedule scans in parallel; connection-level thread limits and a bounded range cache are available. Semantic axes include time, level, lead time, member, and run. Projected/Gaussian grids are not implemented. The implementation is present, while full remote performance, server-audit, and independent-reproduction gates remain to be run. Linux AArch64 is the acceptance platform; Linux x86_64 support and validation are deferred.

## Build and load

Requires C11 / C++17 compilers, CMake, Make, Git, and DuckDB's build dependencies. From the repository root:

```sh
git submodule update --init --recursive
make release
./build/release/duckdb -unsigned :memory:
```

`make release` builds the paired httpfs artifact. Local reads only need duckomo. Remote reads require the httpfs artifact from the same build to be loaded before duckomo; do not substitute another httpfs version.

In the CLI, load the extension and query the included fixture:

```sql
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';

DESCRIBE SELECT * FROM read_om('test/data/raw.om');
SELECT value FROM read_om('test/data/raw.om') ORDER BY value;
```

The output is a `value FLOAT` column containing `0` through `5`. All paths above are relative to the repository root.

The repository pins DuckDB **v1.5.4**. The extension can also be loaded by an official DuckDB installation of the same version and platform. Local extensions are unsigned, so the CLI needs `-unsigned`. To build for another release and run the SQL tests:

```sh
./scripts/build-version.sh v1.5.5
./build/versions/v1.5.5/release/duckdb -unsigned :memory:
```

Then load `build/versions/v1.5.5/release/extension/duckomo/duckomo.duckdb_extension`. The script accepts a `vX.Y.Z` tag and stores source and artifacts in `build/versions/<version>/`. Each target DuckDB version needs its own build; passing the script's SQL tests provides compatibility evidence for that build.

## Reading `data/`, `data_run/`, and `data_spatial/`

Local OM files from all three directories use `read_om()` without a directory-specific read mode. The reader parses the file's internal metadata. HTTP(S)/S3 URIs can also be passed directly to `read_om()`; each input still names one object.

| Directory | Typical axes and metadata in audited samples | Parameters for latitude/longitude queries |
|---|---|---|
| `data_spatial/` | Usually `[lat, lon]`; most have complete `coordinates` | Usually just `domain`; add `dimensions` when axis metadata is missing |
| `data_run/` | Usually `[lat, lon, time]`; most have complete `coordinates` | Usually just `domain`; add `dimensions` when axis metadata is missing |
| `data/` | Bindable samples lack `coordinates` | `domain` + complete `dimensions` covering every value array |

Value-only queries can omit grid parameters; multiple arrays without matching ordered axis metadata still require `dimensions`. Generating coordinates requires an explicit `domain`, or `grid` + `spatial_axes`; the grid is never inferred from the directory, filename, or shape. Use the actual file's axis order; directory names cannot replace axis declarations. Different time positions at the same coordinates remain separate rows; files with time coordinates append `valid_time`.

See [Open-Meteo regular grids and directory differences](docs/regular-domains.md) for SQL examples, missing-axis declarations, older OM format limits, and sample coverage by domain.

## Query valid time

Int64 `time` coordinate arrays in `data_run` and Int64 scalar `valid_time` metadata in `data_spatial` are interpreted as UTC Unix seconds and automatically append `valid_time TIMESTAMP`. Arrays map to the declared `time` axis and preserve the actual intervals; scalars apply to the entire spatial snapshot. Time columns also work without `domain`:

```sql
SELECT value, latitude, longitude, valid_time
FROM read_om('build/s3-samples/data_run/ncep_gfs025/2026/09/28/0000Z/cloud_cover_50hPa.om',
  domain := 'ncep_gfs025')
WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120
  AND valid_time = TIMESTAMP '2026-09-28 03:00:00';
```

The input may be a local file or a supported remote URI. The `TIMESTAMP` column represents UTC without session-timezone conversion. Time filters can narrow safe candidate positions, while DuckDB still applies the complete `WHERE` clause.

When time metadata is missing, supply a UTC timestamp for each time position with `valid_times := [TIMESTAMP '...', ...]`. Its length must match the `time` axis; missing axis metadata also requires `dimensions`. Spatial snapshots without a `time` axis accept a single timestamp. The list must be nonempty with finite, non-NULL timestamps and cannot override conflicting file time coordinates. Files without time metadata or explicit `valid_times` retain their existing output; valid time is never guessed from paths or forecast reference time.

Declare other semantic axes explicitly with `axes`. This preserves the existing `valid_time` output name for the `time` axis: `run` produces `run TIMESTAMP`, and the other axes produce `level DOUBLE`, `lead_time INTERVAL`, and `member` with its input type.

```sql
SELECT value, valid_time, member
FROM read_om('test/data/raw.om',
  dimensions := map(['value'], [['time_axis','ensemble']]),
  axes := {
    'time': {'axis':'time_axis', 'start':TIMESTAMP '2026-09-30 00:00:00',
             'step':INTERVAL '1 hour'},
    'member': {'axis':'ensemble', 'values':[10,20,30]}
  })
WHERE valid_time=TIMESTAMP '2026-09-30 01:00:00' AND member=20;
```

Time and run use UTC microseconds. `lead_time` rejects month components. `level` requires a closed `kind` / `unit` pair. Integer members remain `BIGINT`; strings are not converted to numbers. Explicit coordinates retain their logical positions, including duplicates. See the [SQL contract](specs/003-dimensions-remote-parallel/contracts/sql-interface.md) for types, conflicts, and filter fallback rules.

## Query multiple variables

Variables with matching shapes and consistent ordered `coordinates` metadata align automatically. Otherwise, use `dimensions` to declare a complete, matching list of ordered axis names for **every value variable**:

```sql
SELECT temperature
FROM read_om('test/data/multi.om',
  dimensions := map(
    ['humidity', 'temperature'],
    [['row', 'column'], ['row', 'column']]
  ))
WHERE humidity >= 103
ORDER BY temperature;
```

The result is `3`, `4`, `5`. Although `humidity` is absent from the output, it is still required by the filter. `dimensions` validates alignment; it does not generate `row` / `column` columns or override conflicting file metadata. Quote nested column names such as `"surface/temperature"`.

## Geographic queries

A complete grid and spatial axis identity append `latitude DOUBLE` and `longitude DOUBLE` after the value columns. This example assigns a 3×2 demonstration grid to `raw.om`:

```sql
SELECT value, latitude, longitude
FROM read_om('test/data/raw.om',
  dimensions := map(['value'], [['lat', 'lon']]),
  grid := {'nx':3, 'ny':2, 'lat0':10.0, 'lon0':100.0,
           'dlat':1.0, 'dlon':2.0, 'order':'separate'},
  spatial_axes := ['lat', 'lon'])
WHERE latitude >= 11 AND longitude < 104
ORDER BY latitude, longitude;
```

Returns `(3, 11, 100)` and `(4, 11, 102)`. `lat0` / `lon0` are origins; `dlat` / `dlon` are steps. Flattened spatial axes can also use `lon_fastest` or `lat_fastest`; see the [spatial query guide](specs/002-spatial-pushdown/quickstart.md).

For an Open-Meteo file already downloaded locally, select a registered grid:

```sql
SELECT wave_height, latitude, longitude
FROM read_om('/path/to/local-gfswave.om', domain := 'ncep_gfswave025')
WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120;
```

This example requires a file matching the domain's grid and containing `wave_height`. Domain names are the prefixes after `data/`, `data_run/`, or `data_spatial/` in AWS object keys. Specify the domain explicitly; it is never inferred from the path or shape.

Files without `coordinates` still need complete `dimensions`; `coordinates = 'lat lon'` alone does not define a geographic grid. Binding checks the name, axes, shape, and WKT BBOX when present. See [regular-grid domains](docs/regular-domains.md) for sample downloads, directory differences, and compatibility limits.

Longitude is normalized to `[-180,180)`. Finite-constant `=`, `<`, `<=`, `>`, `>=`, `BETWEEN`, and safe `AND` predicates can narrow the scan. DuckDB always applies the complete `WHERE`. For a region across the antimeridian, use:

```sql
WHERE longitude >= 170 OR longitude < -170
```

`OR` and expressions that cannot be safely analyzed retain SQL filtering and may read the full domain. Read savings depend on OM chunk layout. Coordinate-only queries, spatial `COUNT(*)`, and selections proven empty do not read value arrays.

## Remote, parallel, and cached reads

HTTP(S) and S3 use the paired httpfs build for range reads. The service must support HEAD, a one-byte range probe, and exact `206 Content-Range` responses. Provide S3 credentials through a DuckDB secret or the existing httpfs configuration.

```sql
LOAD 'build/release/extension/httpfs/httpfs.duckdb_extension';
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';

SELECT value
FROM read_om('https://example.invalid/path/object.om')
LIMIT 10;
```

Configure an S3 secret before reading an S3 URI, for example with `CREATE SECRET ... (TYPE s3, KEY_ID ..., SECRET ..., REGION ...)`. Cross-query caching requires a strong ETag or S3 VersionId. Every query still probes object identity and range authorization. Weak or unversioned objects can be read but are not reused across queries. `read_om_raw(path)` remains local-only.

Thread limits and the application range cache are connection settings:

```sql
SET threads=4;
SET duckomo_max_threads=2;        -- 0 uses DuckDB's thread limit
SET duckomo_cache_capacity=67108864;
SET duckomo_cache_enabled=true;
CALL duckomo_clear_cache();       -- this connection; returns cleared entries and bytes
```

Cache capacity is in bytes; shrinking it evicts entries immediately, and capacity zero stores nothing. Inspect one v3 JSON row per scan from the most recently ended SQL query:

```sql
SELECT query_id, scan_id, metrics::JSON
FROM duckomo_last_scan_metrics();
```

Metrics distinguish logical requests, successful reads below the range cache, and response-body bytes reported by the httpfs observer. Time-coordinate index/data/decode costs are separate from value variables. `scan_complete=false` means the query succeeded but a consumer such as `LIMIT` stopped the scan early. `peak_rss_bytes` is process-scoped. `peak_query_owned_bytes` tracks DuckOMO metadata, decoder, and selection buffer vector capacities; it excludes the shared range cache, DuckDB output vectors, and httpfs or engine internals. Failed, cancelled, or incomplete measurements publish `null` and set `query_memory_count_complete=false`. See [Interface overview](docs/spec.md) for the current behavior and metric fields.

## Development and validation

```sh
make test                            # Build release and run validate.sh
./scripts/validate.sh build/release   # Validate an existing build: SQL/native, dimensions, local metrics, fixed fixtures
make sanitizer-test                  # ASan/UBSan checks
```

`validate.sh` also requires `jq`, `sha256sum`, `diff`, and `mktemp`. Evidence defaults to `build/evidence/`. Set `DUCKOMO_DOMAIN_FILE=/path/to/pinned.om` to run complete spatial validation with the real sample. When remote services are available, follow the [003 Quickstart](specs/003-dimensions-remote-parallel/quickstart.md) to configure and run G3; the remote gate does not pass without its services, real sample, and audit logs.

## Documentation and source

| Topic | Entry point |
|---|---|
| Parameters, output, and supported scope | [Interface overview](docs/spec.md) · [Full SQL contract](specs/002-spatial-pushdown/contracts/sql-interface.md) |
| Grid definitions and real sample coverage | [Regular-grid domains](docs/regular-domains.md) |
| Scan flow and read metrics | [Architecture](docs/architecture.md) |
| Upcoming capabilities | [Roadmap](docs/roadmap.md) |
| Query binding and scans / grids / local OM reads | `src/scan/` / `src/grid/` / `src/om/` |
| SQL cases / native checks / fixtures | `test/sql/` / `test/native/` / `test/data/` |

`read_om_raw` remains an early validation entry point for FPX root arrays. Use `read_om` for regular queries.
