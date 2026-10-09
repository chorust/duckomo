# Open-Meteo public S3 sample acquisition

**Captured**: 2026-10-06 UTC

**Scope**: T003 real-sample inventory and metadata check. This completes the four-family inventory item; it does not complete T004, any required N-grid coordinate/value gate, or a remote benefit gate.

## Provenance

- Open-Meteo's [open-data repository](https://github.com/open-meteo/open-data) at commit `4fd52ad16c417c49bff45fab4bf175e5ea5760f2` documents bucket `s3://openmeteo`, region `us-west-2`, object layout, and `[ny,nx,ntime]` for `data/` arrays. The pinned `README.md` SHA-256 is `85c2b2bee77119dfaa7b09ff96fac49bd06b8bb7cbad54e50537fbb7267c75a1`.
- The source grid definitions are pinned separately to Open-Meteo commit `b06f4760fd1f997e5559bb380f64c5e496b4a509`; each chosen object shape matches its declared `ny,nx` dimensions.
- Each OM object was downloaded from the public bucket, hashed locally, checked through the official OM C header/trailer/array-metadata API, and accepted by the local DuckOMO `read_om` binder. No downloaded object is relabeled as a synthetic fixture.
- S3 returned no `VersionId`. The multipart ETags below are not content hashes. The local SHA-256 fixes the downloaded bytes; it does not make future reads from the mutable public URI version-stable.

## Target grid inputs

| Target | Open-Meteo object | OM v3 size / SHA-256 | Root shape / chunks | Axis basis |
|---|---|---|---|---|
| Rotated lat/lon | `s3://openmeteo/data/cmc_gem_rdps_10km/cape/chunk_4337.om` | 68,016,448 bytes / `3db637197d27c65051e417b850f8c3656d1c5371ab78dcbd9fc05d0e440956f7` | `[1045,1140,114]` / `[1,26,114]` | `[latitude, longitude, time]`; Open Data `[ny,nx,ntime]` convention plus pinned `GemDomain.swift` |
| Stereographic | `s3://openmeteo/data/cmc_gem_rdps/cape/chunk_4337.om` | 23,835,720 bytes / `1d701b188c8c264532f0d8fa917400a66cf28aa1c8e53a7b40781a775333c7c1` | `[824,935,114]` / `[1,26,114]` | `[latitude, longitude, time]`; Open Data `[ny,nx,ntime]` convention plus pinned `GemDomain.swift` |
| Lambert conformal conic | `s3://openmeteo/data/chmi_aladin_central_europe_2km/cape/chunk_4135.om` | 48,727,672 bytes / `cef407b0aae43498485b8f6e25907c1ecf8a82c6cb8aa2185fe860f0c491132c` | `[837,1053,120]` / `[1,25,120]` | `[latitude, longitude, time]`; Open Data `[ny,nx,ntime]` convention plus pinned `ChmiDomain.swift` |

The outer S3 key supplies the weather-variable label `cape`; each object is a root Float32 array with variable path `/`. Axis names are not embedded in these arrays. The axis mapping is therefore recorded as a source convention, not as OM metadata. Chunk dimensions indicate possible spatial range skipping but no full/local cost comparison has been run.

All three target samples have `format_version=3`, with exact object metadata in [`sample-manifest.json`](../../../../test/data/grids/sample-manifest.json). They are in `build/s3-samples/openmeteo-v3/`, which is intentionally a generated/download cache rather than a checked-in binary-data directory.

## ECMWF HRES / O1280 Gaussian-family check

ECMWF HRES uses the O1280 octahedral reduced Gaussian grid. The public Open-Meteo object `s3://openmeteo/data/ecmwf_ifs/static/HSURF.om` is a real OM v3 object on O1280: 2,482,560 bytes, SHA-256 `2e8279f8bd12052ba5be5a0bcba2293dc4316097e434d6691f9e6711249e1fd3`, root shape `[1,6599680]`, chunks `[1,400]`. Its companion `data/ecmwf_ifs/static/meta.json` (658 bytes, SHA-256 `c8dd4aa8e6eb07e1708742fc0e28e23e2516857ca156f55596e97bad65d86bd0`) labels the CRS `Reduced Gaussian Grid O1280 (ECMWF)`. The object is the static `HSURF` field, not an HRES forecast time-series object.

The fixed static object has no embedded axis names, and its O1280 row/point order has not been mapped. It is useful as a real Gaussian-family OM v3 sample, but O/F grids are explicitly excluded from 004 acceptance; the spec requires N160, N320, and an N-region subset. The static object therefore cannot substitute for those N-grid inputs.

### HRES time-series object

Follow-up bucket listing confirmed that an actual ECMWF IFS HRES forecast time-series object is public at `s3://openmeteo/data/ecmwf_ifs/temperature_2m/chunk_817.om`. The fixed local copy is 353,999,720 bytes with SHA-256 `483dd0be2096d3e3fff6731509400f97591ebcfc237a941a37460f42a7a10e7f`; S3 reports ETag `"8aacc014058aca39a7a1ae9800fbef58-43"`, Last-Modified `2025-04-03T01:11:58Z`, and no VersionId. The official OM C metadata API reports root shape `[1,6599680,504]`, chunks `[1,6,504]`, and no embedded `crs_wkt`; DuckOMO binds it as a Float32 `value` column. The companion static metadata identifies the model grid as WGS84 Reduced Gaussian O1280. Axis semantics and O1280 local/parent point order remain unmapped; no value decode or coordinate reference was run on this time-series object. It is recorded in `sample-manifest.json` as supplemental HRES/O1280 evidence and does not satisfy N160/N320/N-region acceptance.

The sample copy was downloaded after matching a public S3 HEAD and GET for length, ETag, and Last-Modified, then checked for OM v3 magic and SHA-256. A local DuckDB `DESCRIBE SELECT * FROM read_om(...)` returned `value FLOAT`. This confirms a public native HRES object is available; it does not establish point order, independent values, or selective-read benefit.

The fixed official OM C API exported all 6,599,680 O1280 values. A one-thread DuckOMO `read_om` full scan was exported and compared row-by-row by logical array index: **6,599,680 compared, zero mismatches**. Both results had `NULL=0`, minimum `-999`, maximum `6366`, and sum `-3285892933`. The official reference CSV SHA-256 is `083300d4593e326236b76dfb2d55485058fdbf360a27bfab6a04a64d01eab91a`; the DuckOMO output CSV SHA-256 is `558dcb725586cb638bf129c2666c2c06be7f651bd0808e159f71ae095c9201be`. Details and exact commands are in [`o1280-supplemental-value-check.md`](o1280-supplemental-value-check.md) and [`value-reference-manifest.json`](../../../../test/data/grids/value-reference-manifest.json).

At the original acquisition, the exact target prefixes did not yield matching native OM v3 objects. The pinned Open Data README says that ensemble models are not published on AWS, but that statement no longer matches the live bucket: a 2026-10-06 UTC recheck found the `ecmwf_aifs025_ensemble` product in both `data/` and `data_spatial/`. Its current metadata describes WGS84 world coverage, while the 004 regional target is an N320 reduced-Gaussian subset. The precise prefix and metadata audit is in [`open-meteo-s3-live-audit-20261006-1717z.md`](open-meteo-s3-live-audit-20261006-1717z.md). The fixed source still has no full-grid N320 producer sample; HRES O1280 and the currently listed O320 seasonal metadata do not fill that gap.

## Commands and results

The following completed with exit code `0`:

```sh
cmake --build build/release-vcpkg --target duckomo_fixture_tool -j2
python3 scripts/fetch-openmeteo-grid-samples.py
build/release-vcpkg/duckdb -unsigned -csv :memory: "LOAD 'build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension'; DESCRIBE SELECT * FROM read_om('build/s3-samples/openmeteo-v3/cmc-gem-rdps-cape-chunk-4337.om'); DESCRIBE SELECT * FROM read_om('build/s3-samples/openmeteo-v3/chmi-aladin-ce-cape-chunk-4135.om');"
build/release-vcpkg/duckdb -unsigned -csv :memory: "LOAD 'build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension'; DESCRIBE SELECT * FROM read_om('build/s3-samples/openmeteo-v3/cmc-gem-rdps-10km-cape-chunk-4337.om'); DESCRIBE SELECT * FROM read_om('build/s3-samples/openmeteo-v3/ecmwf-ifs-hsurf-o1280.om');"
```

The DuckDB `DESCRIBE` calls returned `value FLOAT`. They validate binding, not full value decoding. `fetch-openmeteo-grid-samples.py` checked the public HEAD metadata and each downloaded copy's recorded size, ETag, Last-Modified, VersionId state, OM v3 magic, and SHA-256.

## Remaining evidence

- Independent coordinates and original producer/source positions: **not-run**.
- Required-target official full-value decode references and CSV hashes: **not-run**. O1280's supplemental full-value comparison is recorded separately.
- N160, N320 full, and N320 regional OM v3 inputs: **not acquired from this public bucket**.
- Cold full/local value-byte, decoded-chunk, and remote response-body comparison: **not-run**.
- Signed S3 authorization, service-side attempt accounting, and independent TLS/S3 audit: **not-run**.

T003 is complete as a frozen inventory: the manifest contains real OM v3 objects for rotated, stereographic, Lambert, and reduced-Gaussian families, with ECMWF IFS HRES O1280 as the Gaussian-family example. The manifest separately records N160, full N320, and N320-region sample coverage as `not-run`; HRES O1280 is not relabeled as any of those targets. This inventory completion does not make the missing N-grid objects or independent references non-blocking for H0/H1/H6.

The reference import entry point is available as `scripts/generate-grid-reference.py`. It verifies the exact argv, input/output hashes, official OM C decoder commit, and frozen coordinate tolerance. Its required-target sample mapping accepts only matching frozen definitions; the O1280 static and HRES time-series objects are excluded from N160/N320/N-region reference inputs. No required-target reference import was generated because the N-grid samples and independent source/oracle records are still absent.
