# Public O1280 Gaussian-family value check

**Captured**: 2026-10-06 UTC  
**Scope**: Supplemental real OM v3 value decoding only. This does not complete T003/T004 for the required N160, N320, or regional N sample, and it does not establish O/F support.

## Fixed input

The Open-Meteo public object `s3://openmeteo/data/ecmwf_ifs/static/HSURF.om` is OM v3, 2,482,560 bytes, SHA-256 `2e8279f8bd12052ba5be5a0bcba2293dc4316097e434d6691f9e6711249e1fd3`, shape `[1,6599680]`, chunks `[1,400]`. Its companion metadata labels the CRS `Reduced Gaussian Grid O1280 (ECMWF)`. The array has no embedded axis names. The object is a static `HSURF` field, not an HRES forecast time-series object; no O1280 row/point mapping was inferred from its shape.

ECMWF HRES uses an O1280 octahedral reduced Gaussian grid, so the public object supplies a genuine O1280 Gaussian-family OM example. The 004 acceptance scope explicitly excludes O/F grids and requires N160, N320, and an N-region subset. The object is therefore supplemental only.

## Full value comparison

The fixed official OM C API exported all 6,599,680 values:

```sh
build/release-vcpkg/test/tools/duckomo_fixture_tool \
  --oracle build/s3-samples/openmeteo-v3/ecmwf-ifs-hsurf-o1280.om \
  --csv build/s3-samples/openmeteo-v3/ecmwf-ifs-hsurf-o1280.official-reference.csv
```

Exit code: `0`. Reference CSV size: `83,004,797` bytes. SHA-256: `083300d4593e326236b76dfb2d55485058fdbf360a27bfab6a04a64d01eab91a`.

DuckOMO read and exported the complete array with one thread, assigning the sequential output position as the comparison index:

```sh
build/release-vcpkg/duckdb -unsigned -csv :memory: \
  "LOAD 'build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension'; SET threads=1; COPY (SELECT row_number() OVER () - 1 AS index, value FROM read_om('build/s3-samples/openmeteo-v3/ecmwf-ifs-hsurf-o1280.om')) TO 'build/s3-samples/openmeteo-v3/ecmwf-ifs-hsurf-o1280.duckomo-output.csv' (HEADER, DELIMITER ',');"
```

Exit code: `0`. DuckOMO CSV size: `96,204,157` bytes. SHA-256: `558dcb725586cb638bf129c2666c2c06be7f651bd0808e159f71ae095c9201be`.

The two CSVs were compared row-by-row by logical index, parsing each value as a Python double. Result: **6,599,680 positions compared, 0 mismatches**. Both summaries were `count=6,599,680`, `NULL=0`, `min=-999`, `max=6366`, and `sum=-3285892933`.

The machine-readable hashes and statuses are in [`value-reference-manifest.json`](../../../../test/data/grids/value-reference-manifest.json). The full CSVs remain in the ignored `build/s3-samples/openmeteo-v3/` cache and are not checked into the repository.

## Remaining scope

- O1280 coordinates, row lengths, and local-to-parent point mapping: **not-run**.
- Cold full/local read byte and decoded-block comparison: **not-run**.
- N160, N320, and N320-region OM v3 inputs and their independent position references: **not acquired / not-run**.
- Signed S3 authorization, service-side attempt audit, and controlled TLS audit: **not-run**.

This result confirms that one public real Gaussian-family OM v3 object can be fully decoded and compared. It does not change the N-grid gate status or any support level.
