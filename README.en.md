# duckomo

[中文](README.md) | English

duckomo is a DuckDB C++ extension that queries Float32 arrays in [Open-Meteo OM](https://github.com/open-meteo/om-file-format) files (local or HTTP(S)/S3) as SQL tables, without converting them first.

[Features](#features) · [Install](#install) · [Usage](#usage) · [Dev](#dev) · [Docs](#docs) · [Acknowledgements](#acknowledgements)

## Features

- **Query OM directly**: OM v3, Float32, FPX_XOR2D / PFOR_DELTA2D_INT16, root arrays, and hierarchical variables; NaN becomes SQL `NULL`.
- **Selective remote reads**: read HTTP(S)/S3 objects through DuckDB's official HTTPFS extension without downloading the entire file first.
- **Column pruning and spatial selection**: read only output/filter dependencies. Safe geographic, time, and other axis predicates narrow candidates while DuckDB retains the full `WHERE`.
- **Multiple grid mappings**: regular latitude/longitude, [68 regular domains](docs/regular-domains.md), rotated latitude/longitude, Lambert, stereographic, and reduced Gaussian.
- **Multiple variables and semantic axes**: align variables by ordered axis identity; support `valid_time`, `level`, `lead_time`, `member`, and `run`, without implicit transposition, broadcasting, or joins.
- **Parallelism and observability**: DuckDB-scheduled scans, thread limits, scan metrics, opt-in `om_source` positions, and `om_grid_info` descriptions.

Each input names **one object**, not a directory, glob, or file list. Projected/Gaussian grids are implemented, but per-grid real samples, full memory accounting, and independent reproduction have not all passed acceptance. Implementation is not a production-readiness claim; see [supported scope](docs/spec.md) and [grid evidence](docs/grid-domains.md).

## Install

### GitHub Release

Download a package from [GitHub Releases](https://github.com/chorust/duckomo/releases) that **exactly matches your DuckDB version and platform**, verify `SHA256SUMS`, and unzip it. Identify your environment first:

```sql
SELECT version();
PRAGMA platform;
```

The release matrix targets **DuckDB v1.5.4 / v1.5.5 / v1.5.6** on **Linux glibc x86_64 / ARM64 and macOS Intel / Apple Silicon**. Available assets and their validation records are authoritative. If no matching asset is published yet, build from source under [Dev](#dev).

GitHub binaries are unsigned; load only trusted code. Start `duckdb -unsigned`, then install:

```sql
INSTALL '/path/to/duckomo.duckdb_extension';
LOAD duckomo;
```

Release ZIPs cannot be passed directly to `INSTALL`, and the GitHub repository URL is not a DuckDB extension repository. See the [release guide](docs/releases.md) for downloads, checksums, installation, and tag publishing.

### Official HTTPFS

Remote reads use DuckDB's **official extension**, not a DuckOMO-specific HTTPFS build:

```sql
INSTALL httpfs;
LOAD httpfs;
```

Local OM reads do not need HTTPFS. DuckOMO is not yet listed in Community Extensions, so do not use `INSTALL duckomo FROM community` yet. See [community preparation](docs/community-extensions.md) for signed installation plans.

## Usage

### Read Open-Meteo public S3 data

[Open-Meteo Open Data](https://github.com/open-meteo/open-data) is available in the public bucket `s3://openmeteo/`, in `us-west-2`. Load the extensions and create an anonymous S3 secret scoped to that bucket. **No AWS Access Key is needed**:

```sql
LOAD duckomo;
INSTALL httpfs;
LOAD httpfs;

CREATE SECRET openmeteo_public (
  TYPE s3,
  REGION 'us-west-2',
  SCOPE 's3://openmeteo/'
);
```

Start with the GFS static terrain object, whose key does not contain a rolling forecast date, and query a region directly:

```sql
SELECT value AS elevation, latitude, longitude
FROM read_om('s3://openmeteo/data/ncep_gfs025/static/HSURF.om',
  domain := 'ncep_gfs025',
  dimensions := map(['value'], [['lat', 'lon']]))
WHERE latitude BETWEEN 30 AND 30.5
  AND longitude BETWEEN 110 AND 110.5
ORDER BY latitude, longitude;
```

`domain` explicitly selects the grid. This object lacks axis-name metadata, so `dimensions` declares `[lat, lon]`. Value-only queries can omit spatial configuration, but multiple variables still need matching ordered axis identities. Grids are never inferred from paths, directory names, or shapes.

### Query forecast variables and valid time

Forecast objects roll over, and older dates may be removed. Follow the [Open Data directory guide](https://github.com/open-meteo/open-data) to select an existing object, or list prefixes level by level with anonymous AWS CLI access:

```sh
aws s3 ls s3://openmeteo/data_spatial/ncep_gfswave025/ \
  --no-sign-request --region us-west-2
```

This example queries a GFS Wave spatial snapshot. **Replace the date and object key with a sample that still exists**:

```sql
SET VARIABLE wave_file =
  's3://openmeteo/data_spatial/ncep_gfswave025/2026/10/01/0000Z/2026-10-01T0000.om';

SELECT wave_height, latitude, longitude, valid_time
FROM read_om(getvariable('wave_file'), domain := 'ncep_gfswave025')
WHERE latitude BETWEEN 30 AND 40
  AND longitude BETWEEN 140 AND 150
LIMIT 10;
```

Files with Int64 `time` coordinates or scalar `valid_time` metadata automatically expose UTC `valid_time TIMESTAMP`. Add a `valid_time = TIMESTAMP '...'` predicate to select a time. Supply explicit `valid_times` when time metadata is missing; time is never guessed from filenames.

| Directory | Typical layout in audited samples | Geographic queries |
|---|---|---|
| `data_spatial/` | Spatial snapshot, usually `[lat, lon]` | Usually just `domain` |
| `data_run/` | One forecast run, usually `[lat, lon, time]` | Usually just `domain` |
| `data/` | Rolling series or static data; samples often lack axis metadata | `domain` + complete `dimensions` |

Directory names do not guarantee an object's axis order or format compatibility. When metadata is missing, `dimensions` must cover every value variable. See the [regular-grid guide](docs/regular-domains.md) for directory differences, alignment, and per-domain sample coverage.

### HTTPS, private S3, and read metrics

The same public object is also accessible over HTTPS, without an S3 secret:

```sql
SELECT value
FROM read_om('https://openmeteo.s3.us-west-2.amazonaws.com/data/ncep_gfs025/static/HSURF.om')
LIMIT 5;
```

For private buckets, configure DuckDB secrets using the [official HTTPFS S3 documentation](https://duckdb.org/docs/current/core_extensions/httpfs/s3api); keep credentials out of shared SQL. Remote objects must support range reads and remain stable during scans. DuckOMO does not promise scan snapshots or a forced refresh on every query. Its own range cache and former cache SQL have been removed.

```sql
SET threads = 4;
SET duckomo_max_threads = 2; -- 0 uses DuckDB's thread limit

SELECT query_id, scan_id, metrics::JSON
FROM duckomo_last_scan_metrics();
```

Metrics distinguish logical requests from successful standard file-interface reads. DuckOMO does not directly observe HTTPFS network traffic. Spatial read savings depend on OM chunk layout, not just result row counts; see the [interface overview](docs/spec.md).

### Local OM

Replace the URI with a local path; grid and dimension parameters follow the same rules:

```sql
SELECT value FROM read_om('test/data/raw.om') ORDER BY value;
-- The repository fixture returns 0 through 5.
```

`read_om_raw` is an early FPX root-array validation entry point; use `read_om` for normal queries. See the [interface overview](docs/spec.md) and [multi-grid contract](specs/004-multi-grid-selection/contracts/sql-interface.md) for explicit `grid`, other semantic axes, `include_source`, and `om_grid_info`.

## Dev

Requires C11 / C++17 compilers, CMake, Make, Git, Python 3, and DuckDB's build dependencies. From the repository root:

```sh
git submodule update --init --recursive
make release
./build/release/duckdb -unsigned :memory:
```

Load the development build directly in SQL:

```sql
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';
```

The developer submodule pins DuckDB v1.5.4. Use `scripts/build-version.sh v1.5.6` for another pinned engine (v1.5.4 / v1.5.5 are also supported). Extension binaries are not interchangeable across versions. See the [HTTPFS validation guide](docs/official-httpfs.md) for official runtimes and complete reproduction steps.

```sh
make test                                       # Build and run local SQL/native/tool checks
./scripts/validate.sh build/release --local-only  # Validate an existing build
make sanitizer-test                             # ASan / UBSan
python3 test/tools/release_tools_test.py           # Release contract checks
```

Existing in-depth acceptance primarily covers Linux AArch64; new platforms depend on their actual CI results. Complete remote validation needs controlled HTTP/HTTPS/signed-S3 services and audit logs. Local tests or Release smoke checks do not replace real-grid or independent-reproduction gates.

Source entry points: `src/scan/` for binding/scanning, `src/grid/` for grids/layouts, and `src/om/` for OM reading. Tests and fixtures live in `test/sql/`, `test/native/`, and `test/data/`.

## Docs

| Topic | Documentation |
|---|---|
| Parameters, output, supported scope, and metrics | [Interface overview](docs/spec.md) |
| Public data directories, axes, and 68 regular domains | [Regular-grid guide](docs/regular-domains.md) |
| Projected / Gaussian definitions and real-sample evidence | [Grid evidence](docs/grid-domains.md) · [SQL contract](specs/004-multi-grid-selection/contracts/sql-interface.md) |
| Module responsibilities, scan flow, and I/O | [Architecture](docs/architecture.md) |
| GitHub downloads, installation, and tag publishing | [Release guide](docs/releases.md) |
| Official HTTPFS support and controlled validation | [HTTPFS validation guide](docs/official-httpfs.md) |
| Community registration and signed publication | [Community Extensions](docs/community-extensions.md) |
| Development stages and remaining acceptance | [Roadmap](docs/roadmap.md) |

## Acknowledgements

- [DuckDB](https://github.com/duckdb/duckdb): the SQL engine, extension API, and official HTTPFS remote filesystem.
- [Open-Meteo OM File Format](https://github.com/open-meteo/om-file-format): the OM format and official C reader/decoder used by this extension.
- [Open-Meteo Open Data](https://github.com/open-meteo/open-data) and upstream weather-data providers: public samples, model data, and grid-definition sources.
- [DuckDB extension-ci-tools](https://github.com/duckdb/extension-ci-tools): the community build and cross-platform distribution toolchain.
- [AWS Open Data](https://aws.amazon.com/opendata/): the public data hosting program.

Project code is licensed under [Apache-2.0](LICENSE). Weather-data licenses and attribution requirements follow the respective providers' terms.
