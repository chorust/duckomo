<p align="center"><img src="logo.png" alt="duckomo logo" width="160"></p>

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

## Usage

### Read Open-Meteo public S3 data

Query GFS terrain from [Open-Meteo's public data](https://github.com/open-meteo/open-data):

```sql
LOAD duckomo;
INSTALL httpfs;
LOAD httpfs;

SELECT value AS elevation, lat, lon
FROM read_om('s3://openmeteo/data/ncep_gfs025/static/HSURF.om',
  domain := 'ncep_gfs025',
  dimensions := ['lat', 'lon'])
WHERE lat BETWEEN 30 AND 30.5
  AND lon BETWEEN 110 AND 110.5
ORDER BY lat, lon;
```

Example output:

```bash
┌───────────┬───────┬────────┐
│ elevation │  lat  │  lon   │
├───────────┼───────┼────────┤
│ 1292.0    │ 30.0  │ 110.0  │
│ 1258.0    │ 30.0  │ 110.25 │
│ 1272.0    │ 30.0  │ 110.5  │
│ 1265.0    │ 30.25 │ 110.0  │
│ 1275.0    │ 30.25 │ 110.25 │
│ 1278.0    │ 30.25 │ 110.5  │
│ 1043.0    │ 30.5  │ 110.0  │
│ 1058.0    │ 30.5  │ 110.25 │
│ 1035.0    │ 30.5  │ 110.5  │
└───────────┴───────┴────────┘
```

`domain` selects the grid; `dimensions` declares the array axes.

### Query forecast variables and valid time

This example queries a GFS Wave spatial snapshot. Choose an available file from the [Open Data directory](https://github.com/open-meteo/open-data) and **replace the date and object key below**:

```sql
SET VARIABLE wave_file =
  's3://openmeteo/data_spatial/ncep_gfswave025/2026/10/01/0000Z/2026-10-01T0000.om';

SELECT wave_height, lat, lon, valid_time
FROM read_om(getvariable('wave_file'), domain := 'ncep_gfswave025')
WHERE lat BETWEEN 30 AND 40
  AND lon BETWEEN 140 AND 150
LIMIT 10;
```

Files with time metadata expose UTC `valid_time`, which you can use in time predicates.

| Directory | Typical layout | Geographic queries |
|---|---|---|
| `data_spatial/` | Spatial snapshot, usually `[lat, lon]` | Usually just `domain` |
| `data_run/` | One forecast run, usually `[lat, lon, time]` | Usually just `domain` |
| `data/` | Rolling series or static data; samples often lack axis metadata | `domain` + complete `dimensions` |

`dimensions` accepts a shared axis list or a per-variable MAP. See the [regular-grid guide](docs/regular-domains.md) for directory layouts and more examples.

### HTTPS, private S3, and read metrics

The same object is also accessible over HTTPS:

```sql
SELECT value
FROM read_om('https://openmeteo.s3.us-west-2.amazonaws.com/data/ncep_gfs025/static/HSURF.om')
LIMIT 5;
```

For private bucket credentials, see the [HTTPFS S3 documentation](https://duckdb.org/docs/current/core_extensions/httpfs/s3api). Configure threads and inspect the most recent scan:

```sql
SET threads = 4;
SET duckomo_max_threads = 2; -- 0 uses DuckDB's thread limit

SELECT query_id, scan_id, metrics::JSON
FROM duckomo_last_scan_metrics();
```

See the [interface overview](docs/spec.md) for metric definitions.

### Local OM

Replace the URI with a local path. Using the repository fixture:

```sql
SELECT value FROM read_om('test/data/raw.om') ORDER BY value;
```

```bash
┌───────┐
│ value │
├───────┤
│ 0.0   │
│ 1.0   │
│ 2.0   │
│ 3.0   │
│ 4.0   │
│ 5.0   │
└───────┘
```

See the [interface overview](docs/spec.md) for explicit grids, semantic axes, and source information.

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
