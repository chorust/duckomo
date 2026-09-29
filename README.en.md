# duckomo

[中文](README.md) | English

duckomo is a DuckDB C++ extension that reads Float32 arrays directly from local [Open-Meteo OM](https://github.com/open-meteo/om-file-format) files as SQL tables. `read_om` supports OM v3, FPX_XOR2D and PFOR_DELTA2D_INT16 compression, hierarchical variables with metadata attributes, and column projection, so queries read only the value variables they need. `read_om_raw` is an early validation entry point for a single root array; use `read_om` for regular queries.

The current implementation supports local files only and does not generate latitude, longitude, or time columns. Spatial and temporal pushdown, remote reads, and parallel scans are planned. See the [SQL interface contract](specs/001-local-om-scanner/contracts/sql-interface.md) for the complete format and query limits.

## Install

Build and load the extension from source. You need compilers supporting C11 and C++17, CMake, Make, Git, and the dependencies needed to build DuckDB. The repository pins DuckDB, extension-ci-tools, and the OM C library as submodules. From the repository root, run:

```sh
git submodule update --init --recursive
make release
```

The resulting binaries are `build/release/duckdb` and `build/release/extension/duckomo/duckomo.duckdb_extension`. This extension is built for the repository's pinned DuckDB v1.5.4 and can also be loaded by an official DuckDB installation of the same version and platform. The locally built extension is unsigned, so the CLI needs `-unsigned`. The bundled CLI is a convenient development tool for checking the version.

To build for a specific DuckDB release and run the SQL tests, use (v1.5.5 shown here):

```sh
./scripts/build-version.sh v1.5.5
duckdb -unsigned :memory:
```

This example requires the installed `duckdb` to be v1.5.5. Run `LOAD 'build/versions/v1.5.5/release/extension/duckomo/duckomo.duckdb_extension';` in the CLI. The script has no version allowlist: it fetches the requested `vX.Y.Z` tag, builds the extension, and runs the SQL tests. Source and artifacts stay under `build/versions/<version>/` without changing the pinned submodule. C++ extensions need a separate build for each target DuckDB version; actual compatibility issues can be fixed or documented when reported.

## Usage

Start a same-version DuckDB CLI (officially installed or built with this repository) from the repository root. `-unsigned` permits loading the locally built, unsigned extension; this example uses the pinned v1.5.4:

```sh
./build/release/duckdb -unsigned :memory:
```

Load the extension and read a single root array:

```sql
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';

DESCRIBE SELECT * FROM read_om('test/data/raw.om');
SELECT value FROM read_om('test/data/raw.om') ORDER BY value;
```

A file with multiple variables can be read directly when its arrays have matching shapes and ordered `coordinates` metadata. Otherwise, declare a complete, matching list of ordered axis names for **every variable**. Column names omit the leading `/`; nested names containing `/` still need SQL double quotes:

```sql
SELECT temperature
FROM read_om(
  'test/data/multi.om',
  dimensions := map(
    ['humidity', 'temperature'],
    [['row', 'column'], ['row', 'column']]
  )
)
WHERE humidity >= 103
ORDER BY temperature;
```

This query returns `3`, `4`, and `5`. Axis names in `dimensions` verify array alignment; they do not create `row` or `column` columns. Column projection reduces reads of unused variables. Ordinary `WHERE` filters run in DuckDB and do not narrow the array read range. NaN becomes SQL `NULL`. You can also use `read_om_raw('test/data/raw.om')` for a single root array.

## Architecture

```text
SQL → read_om table function → metadata and schema / column selection
    → local file adapter → official OM C reader → DuckDB DataChunk
```

- `src/scan/` binds arguments, validates variables and dimensions, plans column selection, and emits DuckDB data chunks.
- `src/om/` reads local files through the DuckDB file system and uses the official OM C reader for metadata and array decoding.
- `third_party/om-file-format/` contains the pinned official OM format implementation.

Scanning is currently single threaded. The OM reader owns chunk selection, byte requests, and decoding; DuckDB applies SQL filters. See the [architecture](docs/architecture.md) and [roadmap](docs/roadmap.md) for the design and upcoming phases.

## Development

Run these commands from the repository root:

```sh
make release                         # Build the CLI, extension, and development tools
make test                            # SQL, native checks, and projection validation
./scripts/validate.sh build/release  # Full SQL, fixture, checksum, and read-metrics validation
make sanitizer-test                  # ASan/UBSan checks
```

`validate.sh` also requires `jq`, `sha256sum`, `diff`, and `mktemp`; it writes evidence to `build/evidence/`. SQL cases are in `test/sql/`, native checks in `test/native/`, and reproducible OM fixtures in `test/data/`. See the [quickstart](specs/001-local-om-scanner/quickstart.md) for build and validation details and the [evidence](specs/001-local-om-scanner/evidence/) for recorded results.
