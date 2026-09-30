# duckomo

[中文](README.md) | English

duckomo is a DuckDB C++ extension that queries Float32 arrays in local [Open-Meteo OM](https://github.com/open-meteo/om-file-format) files as SQL tables, without converting them first.

- **Reading**: OM v3, FPX_XOR2D / PFOR_DELTA2D_INT16 compression, root arrays, and hierarchical variables; NaN becomes SQL `NULL`.
- **Selective reads**: reads only value variables needed by output and filters; safe geographic predicates can further narrow the scan.
- **Coordinates**: generates `latitude` and `longitude` from an explicit regular grid or [68 registered domains](docs/regular-domains.md).

Currently supports one local file and a single scan thread. Extra axes such as time remain in the rows, but have no generated semantic columns. Remote reads, parallel scans, and projected/Gaussian grids are not implemented. Linux AArch64 is the validated platform; Linux x86_64 support and validation are deferred.

## Build and load

Requires C11 / C++17 compilers, CMake, Make, Git, and DuckDB's build dependencies. From the repository root:

```sh
git submodule update --init --recursive
make release
./build/release/duckdb -unsigned :memory:
```

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

## Development and validation

```sh
make test                            # Build release and run validate.sh
./scripts/validate.sh build/release   # Validate an existing build: SQL/native, fixture regeneration/hashes, projection metrics
make sanitizer-test                  # ASan/UBSan checks
```

`validate.sh` also requires `jq`, `sha256sum`, `diff`, and `mktemp`. Evidence defaults to `build/evidence/`. Set `DUCKOMO_DOMAIN_FILE=/path/to/pinned.om` to additionally run complete spatial validation with the real sample. See the [spatial query guide](specs/002-spatial-pushdown/quickstart.md) for the required file and hash, and the [validation record](specs/002-spatial-pushdown/evidence/final.md) for existing AArch64 results and independent reproduction.

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
