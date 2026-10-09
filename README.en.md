# duckomo

[中文](README.md) | English

duckomo is a DuckDB C++ extension that queries Float32 arrays in local [Open-Meteo OM](https://github.com/open-meteo/om-file-format) files as SQL tables, without converting them first.

- **Reading**: OM v3, FPX_XOR2D / PFOR_DELTA2D_INT16 compression, root arrays, and hierarchical variables; NaN becomes SQL `NULL`.
- **Selective reads**: reads only value variables needed by output and filters; safe geographic predicates can further narrow the scan.
- **Coordinates**: generates `latitude` and `longitude` from an explicit regular grid, [68 regular domains](docs/regular-domains.md), or a version-one rotated, Lambert, stereographic, or reduced Gaussian definition.

The reader supports one local OM file, plus HTTP(S)/S3 range reads through the official `httpfs` extension. DuckDB can schedule scans in parallel; connection-level thread limits are available. Semantic axes include time, level, lead time, member, and run. Projected/Gaussian grids, opt-in `om_source`, and `om_grid_info` are implemented in the current work, but the 004 real-grid coordinate/value, remote-benefit, complete memory-ledger, and independent-reproduction gates have not passed; this is not a production-support claim. The public HRES O1280 object is supplemental Gaussian evidence and does not replace the required N160, N320, or N320-region samples. Linux AArch64 is the acceptance platform; Linux x86_64 support and validation are deferred. This refactor targets DuckDB v1.5.4, v1.5.5 and v1.5.6.

## Community installation (pending inclusion)

DuckOMO is preparing a DuckDB Community Extensions submission and is **not yet listed**. The first release targets **DuckDB v1.5.6 / Linux AArch64 (glibc)**. Once included and published, use a normal DuckDB installation:

```sql
INSTALL duckomo FROM community;
LOAD duckomo;
-- HTTP(S)/S3 reads also need official HTTPFS:
INSTALL httpfs;
LOAD httpfs;
```

The community builds, signs and hosts the extension. Its signed artifacts load without `-unsigned`. Extension `.gz` files stay outside the source repository, and a GitHub Release upload is not a submission prerequisite. See the [registration draft, CI and publication steps](docs/community-extensions.md).

## Build and load

Requires C11 / C++17 compilers, CMake, Make, Git, and DuckDB's build dependencies. From the repository root:

```sh
git submodule update --init --recursive
make release
./build/release/duckdb -unsigned :memory:
```

`make release` builds local DuckDB, DuckOMO and developer verification tools. Local reads need only DuckOMO. Remote reads use `INSTALL httpfs; LOAD httpfs;` for the official package matching the engine version/platform. Start DuckDB with `-unsigned` for unsigned DuckOMO artifacts.

In the CLI, load the extension and query the included fixture:

```sql
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';

DESCRIBE SELECT * FROM read_om('test/data/raw.om');
SELECT value FROM read_om('test/data/raw.om') ORDER BY value;
```

The output is a `value FLOAT` column containing `0` through `5`. All paths above are relative to the repository root.

The developer DuckDB submodule still pins **v1.5.4**. The community target is **v1.5.6**; community CI checks out its target engine, and the Makefile preserves that engine's version label. The pinned matrix supports v1.5.4, v1.5.5 and v1.5.6; `make matrix-release` defaults to v1.5.6:

```sh
./scripts/build-version.sh v1.5.6
python3 scripts/version_matrix.py fetch-runtime --root . \
  --matrix test/data/grids/version-matrix.json --pair v1.5.6
./build/official-matrix/v1.5.6/official/duckdb -unsigned :memory:
```

`fetch-runtime` fetches matching official CLI/HTTPFS packages. Load `build/official-matrix/v1.5.6/release/extension/duckomo/duckomo.duckdb_extension`. Each version has a separate build under the ignored `build/official-matrix/<version>/` directory. These local artifacts are unsigned and need `-unsigned` for development. Version validation and signed community publication have separate status records.

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

## Projected and Gaussian grids (in progress)

A version-one `grid` declares rotated latitude/longitude, spherical Lambert, stereographic, or reduced Gaussian geometry. Its closed field set must match the array's spatial axis order and lengths; unknown or conflicting CRS metadata rejects binding. Gaussian definitions provide a complete row table, and regional grids also provide their local-to-parent segments. See the [multi-grid SQL contract](specs/004-multi-grid-selection/contracts/sql-interface.md) for the exact types and examples.

```sql
SELECT value, latitude, longitude, om_source.logical_index
FROM read_om('test/data/raw.om',
  dimensions := map(['value'], [['y','x']]),
  grid := {'version':1,'type':'rotated_latlon','numeric_policy':'float64_v1',
           'earth':{'model':'sphere','radius_m':6371229.0},
           'layout':{'nx':3,'ny':2,'order':'separate'},
           'parameters':{'x0':0.0,'y0':0.0,'dx':1.0,'dy':1.0,
                         'north_pole_latitude':39.25,'north_pole_longitude':-162.0,
                         'rotation':0.0}},
  spatial_axes := ['y','x'], include_source := true)
ORDER BY om_source.logical_index;
```

`om_source` is an opt-in final column containing object, grid/layout, and original array-position identity. `om_grid_info(path, ...the same grid arguments...)` returns one row describing the definition, axes/strides, CRS, capabilities, and provenance without reading value arrays. See the [grid evidence table](docs/grid-domains.md) for per-domain evidence levels and the O1280 scope. The implementation and synthetic regressions exist; this example does not establish real production-grid acceptance.

Longitude is normalized to `[-180,180)`. Finite-constant `=`, `<`, `<=`, `>`, `>=`, `BETWEEN`, and safe `AND` predicates can narrow the scan. DuckDB always applies the complete `WHERE`. For a region across the antimeridian, use:

```sql
WHERE longitude >= 170 OR longitude < -170
```

`OR` and expressions that cannot be safely analyzed retain SQL filtering and may read the full domain. Read savings depend on OM chunk layout. Coordinate-only queries, spatial `COUNT(*)`, and selections proven empty do not read value arrays.

## Remote, parallel, and cached reads

HTTP(S) and S3 use official HTTPFS for range reads. Objects must remain stable during scans. Standard file size/version evidence detects observable conflicts; DuckOMO does not promise a fresh HEAD or scan snapshot. Provide S3 credentials through a DuckDB secret or the existing httpfs configuration.

```sql
INSTALL httpfs;
LOAD httpfs;
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';

SELECT value
FROM read_om('https://example.invalid/path/object.om')
LIMIT 10;
```

Configure an S3 secret before reading an S3 URI, for example with `CREATE SECRET ... (TYPE s3, KEY_ID ..., SECRET ..., REGION ...)`. Cross-query caching requires a strong ETag or S3 VersionId. Every query still probes object identity and range authorization. Weak or unversioned objects can be read but are not reused across queries. `read_om_raw(path)` remains local-only.

Thread limits are connection settings:

```sql
SET threads=4;
SET duckomo_max_threads=2;        -- 0 uses DuckDB's thread limit
```

Cache capacity is in bytes; shrinking it evicts entries immediately, and capacity zero stores nothing. Inspect one v4 JSON row per scan from the most recently ended SQL query:

```sql
SELECT query_id, scan_id, metrics::JSON
FROM duckomo_last_scan_metrics();
```

Metrics distinguish logical requests and successful standard file-interface reads. Remote transport body/attempts/responses are NULL with complete=false; local inputs report 0/true. The removed own cache reports false/0. Network evidence comes from test service logs. Time-coordinate index/data/decode costs are separate from value variables. `scan_complete=false` means the query succeeded but a consumer such as `LIMIT` stopped the scan early. `peak_rss_bytes` is process-scoped. `peak_query_owned_bytes` tracks the currently instrumented DuckOMO-owned vector capacities; it excludes DuckDB output vectors, and httpfs or engine internals. The full per-component memory ledger and remote-control memory audit remain in progress. Failed, cancelled, or incomplete measurements publish `null` and set `query_memory_count_complete=false`. See [Interface overview](docs/spec.md) for the v4 behavior and metric fields.

## Development and validation

```sh
make test                            # Build release and run local validation
./scripts/validate.sh build/release --local-only  # Validate an existing build without remote services
make sanitizer-test                  # ASan/UBSan checks
```

`validate.sh` also requires `jq`, `sha256sum`, `diff`, and `mktemp`. Evidence defaults to `build/evidence/`. Set `DUCKOMO_DOMAIN_FILE=/path/to/pinned.om` to run complete spatial validation with the real sample. When remote services are available, follow the [003 Quickstart](specs/003-dimensions-remote-parallel/quickstart.md) to configure and run G3; the remote gate does not pass without its services, real sample, and audit logs.

## Documentation and source

| Topic | Entry point |
|---|---|
| Parameters, output, and supported scope | [Interface overview](docs/spec.md) · [Full SQL contract](specs/002-spatial-pushdown/contracts/sql-interface.md) |
| Grid definitions and real sample coverage | [Regular-grid domains](docs/regular-domains.md) |
| Projected/Gaussian definitions and evidence levels | [Grid evidence table](docs/grid-domains.md) |
| Scan flow and read metrics | [Architecture](docs/architecture.md) |
| Upcoming capabilities | [Roadmap](docs/roadmap.md) |
| Query binding and scans / grids / local OM reads | `src/scan/` / `src/grid/` / `src/om/` |
| SQL cases / native checks / fixtures | `test/sql/` / `test/native/` / `test/data/` |

`read_om_raw` remains an early validation entry point for FPX root arrays. Use `read_om` for regular queries.

## Official release matrix

Run `scripts/build-version.sh v1.5.4` (also v1.5.5 and v1.5.6). Remote verification requires actual official CLI/HTTPFS packages and controlled HTTP/HTTPS/S3 services. See [reproduction instructions](docs/official-httpfs.md). Remove `duckomo_cache_enabled`, `duckomo_cache_capacity` and `duckomo_clear_cache()` from SQL; they are no longer registered.
