<h1 align="center">
  <img src="logo.png" alt="duckomo logo" width="160"><br>
  duckomo
</h1>

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
- **Polygon-region reads (experimental)**: v0.3.1 GitHub packages provide `read_om_regions` for raw grid records that can be grouped with SQL; full cross-platform acceptance of the current optimization remains open, so this is an experimental feature.

Each input names **one object**, not a directory, glob, or file list. The 005 region entrypoint requires an explicitly loaded spatial extension that matches DuckDB; ordinary `read_om` does not. Callers are responsible for the WKB `grid_lonlat` reference, containment uses planar `covers`, and SQL aggregates are equally weighted. Region support is still under validation; see the [005 quickstart](specs/005-polygon-region-statistics/quickstart.md). Projected/Gaussian grids are implemented, but per-grid real samples, full memory accounting, and independent reproduction have not all passed acceptance. Implementation is not a production-readiness claim; see [supported scope](docs/spec.md) and [grid evidence](docs/grid-domains.md).

The owner-approved [2026-10-10 scope amendment](specs/004-multi-grid-selection/contracts/gaussian-acceptance-20261010.md) skips real-object acceptance for N160/N320/N-region in this round because the surveyed public Open-Meteo sources contain no matching objects. Definitions and synthetic regressions remain; a skip is not a pass and does not block current-scope closure. Per the owner-approved [2026-10-10 pinned-producer-reference decision](specs/004-multi-grid-selection/contracts/pinned-producer-reference-20261010.md), `ecmwf_ifs` O1280 and the three projected grids are registered `coordinate-value-validated` (consistent with the published Open-Meteo objects, not native-GRIB equivalence); O1280 is backed by [real local checks](specs/004-multi-grid-selection/evidence/baseline-local/o1280-real-local-20261010/final.md) covering full HSURF values/coordinates, explicit/domain equivalence, spatial selection and sampled time series. Remote benefit, complete gates and H9 independent reproduction remain open. The Phase 8 cross-chunk entry remains an unapproved proposal.

## Install

### GitHub Release

Download a package from [GitHub Releases](https://github.com/chorust/duckomo/releases) that **exactly matches your DuckDB version and platform**, verify `SHA256SUMS`, and unzip it. Identify your environment first:

```sql
SELECT version();
PRAGMA platform;
```

The release matrix targets **DuckDB v1.5.4 / v1.5.5 / v1.5.6** on **Linux glibc x86_64 / ARM64 and macOS Intel / Apple Silicon**. Available assets and their validation records are authoritative. If no matching asset is published yet, build from source under [Dev](#dev).

GitHub binaries are unsigned; load only trusted code. Start `duckdb -unsigned`, then install. Release asset names carry the version and platform, and passing the URL directly to `INSTALL` makes DuckDB truncate the extension name at the first `.` (it is stored as `duckomo-v0.duckdb_extension`), so a later `LOAD duckomo` cannot find the file. Download the `.duckdb_extension.gz`, decompress it to the canonical name, and install locally instead:

```bash
DUCKOMO_VERSION=v0.3.1                                                    # DuckOMO version
DUCKDB_VERSION=$(duckdb -list -noheader :memory: "SELECT version();")      # detected from local DuckDB
PLATFORM=$(duckdb -list -noheader :memory: "PRAGMA platform;")            # detected from local DuckDB

curl -sfL "https://github.com/chorust/duckomo/releases/download/${DUCKOMO_VERSION}/duckomo-${DUCKOMO_VERSION}-duckdb-${DUCKDB_VERSION}-${PLATFORM}.duckdb_extension.gz" | gunzip > duckomo.duckdb_extension
```

```sql
INSTALL './duckomo.duckdb_extension';
LOAD duckomo;
```

Alternatively, download the Release ZIP manually (it contains the same `duckomo.duckdb_extension`) and the separate `SHA256SUMS` file, verify the ZIP before unzipping it, then run the same local install.

Release ZIPs cannot be passed directly to `INSTALL`, and the GitHub repository URL is not a DuckDB extension repository. See the [release guide](docs/releases.md) for downloads, checksums, installation, and tag publishing.

## Usage

### Read Open-Meteo public S3 data

Query GFS terrain from [Open-Meteo's public data](https://github.com/open-meteo/open-data):

```sql
LOAD duckomo;
INSTALL httpfs;
LOAD httpfs;

-- Optional: explicitly set the endpoint and region for Open-Meteo's public S3 bucket.
-- No credentials are needed. Anonymous access normally works without a secret;
-- this narrower scope can override an existing secret covering all of s3://.
-- CREATE SECRET openmeteo_public (
--   TYPE s3, PROVIDER config,
--   ENDPOINT 's3.us-west-2.amazonaws.com',
--   REGION 'us-west-2',
--   SCOPE 's3://openmeteo/'
-- );

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

`domain` selects the grid; `dimensions` declares the array axes. The public bucket is read anonymously; no secret is needed. Beware: an existing secret scoped to `s3://` (for example an OSS/MinIO config with a custom endpoint) takes over Open-Meteo requests and causes a 404. Check with `SELECT name, scope FROM duckdb_secrets();` and drop that secret or narrow its scope.

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

v0.3.1 supports `variable := 'column_name'` on `read_om`, `read_om_regions`, and `om_grid_info`. It binds one value array, which reduces metadata work for remote files with many variables. Names follow encoded SQL column paths, such as `weird%2Fname%25/value`, with an optional leading slash. A dimensions MAP must contain only the selected array. NULL, empty, missing, or non-value names fail during binding. Scans reuse bound metadata by default; `SET duckomo_revalidate_metadata=true` enables full-tree rereads and comparison of the same selected view.

### Polygon aggregation: Qinghai

The experimental `read_om_regions` entrypoint returns grid points covered by each WKB polygon. SQL supplies the aggregation; AVG is an equal-weight point average. Prepare eight Qinghai prefectures from [GADM 4.1](https://gadm.org/data.html), following its academic/non-commercial license:

```sh
mkdir -p build/manual-region-queries
curl -fL https://geodata.ucdavis.edu/gadm/gadm4.1/json/gadm41_CHN_2.json.zip \
  -o build/manual-region-queries/gadm41_CHN_2.json.zip
python3 - <<'PY'
import json, zipfile
from pathlib import Path
folder = Path('build/manual-region-queries')
with zipfile.ZipFile(folder / 'gadm41_CHN_2.json.zip') as archive:
    data = json.loads(archive.read('gadm41_CHN_2.json'))
assert data['crs']['properties']['name'] == 'urn:ogc:def:crs:OGC:1.3:CRS84'
data['features'] = [f for f in data['features'] if f['properties']['NAME_1'] == 'Qinghai']
assert len(data['features']) == 8
(folder / 'qinghai-gadm41.geojson').write_text(json.dumps(data, ensure_ascii=False))
PY
```

The archive explicitly declares CRS84 (x=longitude, y=latitude), used here as `grid_lonlat`. These are GADM 4.1 boundaries, not a promise of current administrative divisions. Use matching DuckDB, DuckOMO, HTTPFS, and spatial builds:

```sql
-- Install the matching v0.3.1 package above, then start duckdb -unsigned.
LOAD duckomo;
INSTALL httpfs; LOAD httpfs;
INSTALL spatial; LOAD spatial; -- matching DuckDB version; initial install needs network

CREATE TEMP TABLE qinghai AS
SELECT feature.properties.GID_2 AS region_id,
       ST_GeomFromGeoJSON(feature.geometry::JSON) AS geometry
FROM (SELECT unnest(features) AS feature
      FROM read_json_auto('build/manual-region-queries/qinghai-gadm41.geojson'));

SET VARIABLE qinghai_regions = (
    SELECT list({'id': region_id, 'wkb': ST_AsWKB(geometry)} ORDER BY region_id)
    FROM qinghai
);

SELECT region_id, count(*) AS grid_points,
       round(avg(temperature_80m), 3) AS mean_celsius
FROM read_om_regions(
    's3://openmeteo/data_spatial/ncep_gfs025/2026/10/14/0000Z/2026-10-14T0000.om',
    domain := 'ncep_gfs025', variable := 'temperature_80m',
    regions := getvariable('qinghai_regions'),
    region_crs := 'grid_lonlat')
GROUP BY region_id
ORDER BY region_id;
```

This snapshot contains `temperature_80m` and does not contain `temperature_2m`. Public objects expire; confirm a replacement key and variable before querying.

v0.3.1 adds eight SEAS5/EC46 O320 and two N160 domains and reuses decoder buffers to avoid repeated index/data reads. The complete Nino S3 example finished in about 24–26 seconds in measured runs. See the [seasonal domain guide](docs/seasonal-domains.md) for product names, monthly Nino queries, time/member axes, and raw-unit caveats.

### SEAS5 monthly SST

Monthly files carry time coordinates; no manual `axes.start` is needed:

```sql
SELECT valid_time, avg(value) AS mean_raw
FROM read_om(
  's3://openmeteo/data_run/ecmwf_seas5_monthly/2026/10/01/0000Z/sea_surface_temperature_mean.om',
  domain := 'ecmwf_seas5_monthly')
WHERE lat BETWEEN -5 AND 5 AND lon BETWEEN -170 AND -120
GROUP BY valid_time ORDER BY valid_time;
```

This sample's unit label and value magnitude are inconsistent, so the output retains raw values as `mean_raw`. See the guide above for the complete two-region polygon example.

### Regional SST averages: Nino

The ERA5 SST prefix is a directory. Choose an individual `chunk_*.om` object:

```sql
SET VARIABLE nino_regions = [
    {'id': 'nino34', 'wkb': ST_AsWKB(ST_GeomFromText(
        'POLYGON((-170 -5,-120 -5,-120 5,-170 5,-170 -5))'))},
    {'id': 'nino12', 'wkb': ST_AsWKB(ST_GeomFromText(
        'POLYGON((-90 -10,-80 -10,-80 0,-90 0,-90 -10))'))}
];

SELECT region_id, valid_time, count(*) AS grid_points,
       round(avg(value), 3) AS mean_celsius
FROM read_om_regions(
    's3://openmeteo/data/copernicus_era5/sea_surface_temperature/chunk_987.om',
    domain := 'copernicus_era5',
    dimensions := ['lat', 'lon', 'time'],
    axes := {'time': {'axis': 'time', 'start': TIMESTAMP '2026-10-01',
                      'step': INTERVAL '1 hour'}},
    regions := getvariable('nino_regions'),
    region_crs := 'grid_lonlat')
WHERE valid_time >= TIMESTAMP '2026-10-01'
  AND valid_time < TIMESTAMP '2026-10-02'
GROUP BY region_id, valid_time
ORDER BY region_id, valid_time;
```

The pinned [ERA5 configuration](https://github.com/open-meteo/open-meteo/blob/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/App/Era5/Era5Domain.swift), [chunk writer](https://github.com/open-meteo/open-meteo/blob/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/App/Helper/OmFileSplitter.swift), and [Unix time index](https://github.com/open-meteo/open-meteo/blob/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/OmTime/Time.swift) define 504 hourly steps per file. Chunk N starts at 1970-01-01 plus N × 504 hours; chunk 987 starts at 2026-10-01 00:00 UTC. The example selects its first day. Check shape and producer conventions when changing the object; explicit axis labels do not discover timestamps. These averages are not climatological anomalies or area-weighted Nino indices.

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

Ordinary builds do not require spatial runtime artifacts. To include region sanitizer tests, prepare the matching runtime with `scripts/fetch-region-runtime.py`, then pass `--region-sanitizers` to `scripts/build-version.sh`. `--extension-only` builds only the extension and skips the region runtime prerequisite.

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
| Polygon-region reads, time-grouped statistics, and validation | [005 quickstart](specs/005-polygon-region-statistics/quickstart.md) · [SQL contract](specs/005-polygon-region-statistics/contracts/sql-interface.md) |
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
