# O1280 real-object local validation — 2026-10-10

**Result:** local checks **pass**, not a complete H0–H9 acceptance. `ecmwf_ifs` remains `metadata-checked`: independent original-GRIB scanning/order correspondence and the remote/reproduction gates are not established by this run.

## Frozen inputs and actual build

No new object, format conversion or historical-manifest rewrite was performed. Before executing queries, the runner verified these existing frozen objects and saved the planned SQL, commands, tolerances, source/build hashes and tracked patch in `freeze.json`:

| Object | Shape | SHA-256 |
| --- | --- | --- |
| `s3://openmeteo/data/ecmwf_ifs/static/HSURF.om` | `[1,6599680]` | `2e8279f8bd12052ba5be5a0bcba2293dc4316097e434d6691f9e6711249e1fd3` |
| `s3://openmeteo/data/ecmwf_ifs/temperature_2m/chunk_817.om` | `[1,6599680,504]` | `483dd0be2096d3e3fff6731509400f97591ebcfc237a941a37460f42a7a10e7f` |

The **current-build r3** run is authoritative for the final runtime code in this change. The parent directory retains the earlier successful r2 run; `failed-r1/` preserves the earlier validation-driver failure. These are separate runs, not rewrites of one result.

- CLI reports `v1.5.4`, source ID `08e34c44`, Linux AArch64. Full dependency commits and CMake cache hash are in [r3 freeze](current-build-r3/freeze.json).
- The CLI **statically links DuckOMO**. Its hash, not an assumption that `LOAD` replaces an already linked extension, fixes the executed implementation. Rebuilding CMake target `duckdb` only rebuilt the shared library; target **`shell`** was required to refresh the actual CLI. Both shell and loadable extension were rebuilt before r3.
- CLI SHA-256: `9413d24646b4d94a4dc932958f11315e596213301762c065c7bd90db3783b008`.
- Matching loadable extension SHA-256: `98f29a8e57e06d12d880be071095fadb1f093f07afb8f450a15bbc37a204e093`.
- Official fixture/oracle tool SHA-256: `cacb0cba26b4f39b371c521a303d86edf8d65570084f4f0124bceb7332bed275`.
- Freeze SHA-256: `872751c898c1375cf280583e4a4856ed38fef4f6cf20d18d4b72c1d881d78068`.
- [r3 manifest](current-build-r3/manifest.json) SHA-256: `295b61def10706b7ef22eb0534f4ef259d8f438b274e242adc74710ed546f2c1`.
- [Commands and exits](current-build-r3/commands.json): all **21 commands exit 0**. Every `read_om` query completely consumed its scan; per-scan v4 metrics are retained. `grid_info` is a metadata descriptor operation: its successful terminal profile deliberately has `scan_complete=false`, not a completed data scan.

This is a local development build with exact artifact identity, **not** a newly accepted version-matrix pair. The registry's `build_pair=null` remains appropriate for the incomplete acceptance level.

## Real input uncovered a binding bug

The actual HSURF object embeds `coordinates = "lat lon"`; the historical manifest's `names_embedded=false` description was incomplete for this object. The historical manifest was **not** edited. Its `[1,6599680]` shape represents a singleton first dimension and a flattened O1280 point dimension named `lon`, not a regular lat/lon grid.

Previously the registered domain required `point`, while overriding source axes with `['y','point']` correctly failed strict metadata alignment. The fix explicitly registers `flattened_axis_alias = "lon"`, keeps canonical `point` callers, and uses the registered alias only when binding the declared/inferred profile. The normal point-count/layout checks still apply; there is no shape-based guessing, metadata override, or automatic grid discovery.

A full-shape synthetic fixture bearing the same metadata reproduced the failure before the fix. Its regression now passes and still rejects an explicit declaration conflicting with that metadata. The real queries then use:

- HSURF: `read_om(path, domain := 'ecmwf_ifs')`, keeping inferred `lat/lon` names; explicit equivalent uses `spatial_axes := ['lon']`.
- chunk_817: `dimensions := ['y','point','time']`, `domain := 'ecmwf_ifs'`, and explicitly supplied time labeling; explicit equivalent uses `spatial_axes := ['point']`.

## Results

| Check | Observed result | Evidence boundary |
| --- | --- | --- |
| Full HSURF values/source positions | **6,599,680 positions**, zero Float32 bit mismatches, zero NULLs; logical/point/parent indices and original axis indices match | Values from the independently invoked fixed official OM C decoder; not a GRIB spatial-order reference |
| Full HSURF producer coordinates | All 6,599,680 positions; maximum latitude and longitude error **0°**, within pre-frozen `1e-4°` | Python implementation of pinned Swift inverse `getPos` + Float32 `getCoordinates`, without reading registered row values or calling DuckOMO's mapping; independent implementation, **not independent original-GRIB evidence** |
| Explicit/domain equivalence | Full static output CSVs identical by hash; all 10,080 polar time records also identical | Same declared source profile and projected columns |
| Spatial selection | Polar **20**, equatorial box **58**, seam OR **361,888**, out-of-domain **0** records | Both directions of `EXCEPT ALL` against the completely exported static baseline return `[0,0]` for all four predicates |
| Source/count/coordinate-only and descriptor | Value index bytes, data bytes and decoded chunks **all zero** | Local only; descriptor completion is distinct from data-scan completion |
| Actual time-array values | Polar first row (20 points), one equatorial point and final southern point: **22 × 504 = 11,088 records**, zero official Float32/NULL mismatches | **1,056 finite values and 10,032 NULLs**; sampled spatial points, not the entire 3,326,238,720-value array |
| Half-open time filtering | **60 records = 20 points × slots 1,2,3**; matching original source/time indices, values and NULL positions | Time labels explicitly derive from the pinned `chunk k * 504 * 3600` convention; this does not independently establish the sample's historical meteorological validity dates |
| Local selection I/O | Full static `data_bytes=4,102,073`, `decoded_chunks=27,649`; equatorial box `data_bytes=178`, `decoded_chunks=3` | Actual repeated decoder work is counted; these are local value-read counters, **not remote total body/attempt evidence** |

The official oracle gained a bounded root-array `--oracle-slice` adapter so selected real time series can be compared without allocating the full 3.3-billion-value array. It delegates selection/decompression to the fixed official OM C API, not DuckOMO's scanner. Synthetic known-value tests verify non-zero offsets and slice-local indices, and reject out-of-bounds, wrong-rank, zero-count and signed offsets.

## Reproduce without overwriting history

From the repo root, with frozen objects already cached:

```sh
cmake --build build/release --target shell duckomo_loadable_extension duckomo_fixture_tool -j 4
python3 scripts/validate-hres-o1280.py \
  --output build/hres-o1280/validation-NEW-RUN
```

The output directory **must not exist**. The runner freezes inputs and complete planned SQL/commands before execution, exports and fully consumes results, compares official values/producer coordinates, performs bidirectional selection comparisons, and saves failures with non-zero exit status. The frozen exact driver/generator are also retained in `current-build-r3/source-snapshot/scripts/`; the tracked source delta is `tracked.patch`.

Raw r3 artifacts remain in ignored `build/hres-o1280/validation-20261010-r3/` (about 1.14 GB, including two full static CSVs and the official reference). This committed-ready evidence directory retains the manifests, commands, SQL, v4 metrics, stdout/stderr, source snapshots/patch and small CSVs. Larger CSVs are **not** checked in; their sizes/hashes remain in the manifest and can be reproduced. Parent-directory artifacts belong to r2, not r3.

`failed-r1/` records a **driver** assertion failure: the first runner incorrectly required data-scan completion for `grid_info`. The descriptor itself succeeded without value I/O. The check was corrected for this distinct operation; strict complete-consumption checks for every actual `read_om` scan remain intact. r2 and r3 ran in new directories.

## Regression verification

- Full HRES synthetic end-to-end check, including embedded-axis alias, metadata conflict and official slice checks: pass.
- Native Gaussian and grid identity tests: pass.
- Registry regeneration/canonical vectors and invalid alias-profile tests: pass.
- Evidence contract tests: **13/13 pass**.
- Coordinate/reference tests with matching CLI/extension configured: **20/20 pass**, no optional skips in this execution.

## Still required for complete O1280 acceptance

1. ~~**Original spatial-order evidence:** original GRIB/archive scanning flags, `pl`, row endpoints and OM point/value correspondence. A source-compatible producer port, OM logical-index comparison and a legacy axis-name attribute cannot substitute for this requirement.~~ **Amended 2026-10-10 by owner decision** ([pinned-producer-reference-20261010.md](../../../contracts/pinned-producer-reference-20261010.md), owner: 可以认为一致，按照 Open-Meteo 自己的 Swift 项目就是这样的): for these Open-Meteo-produced objects the pinned Swift source, verified through an independent port over all points, is accepted as the coordinate/point-order reference, so this item no longer blocks `coordinate-value-validated`. The boundary survives as a semantic note: consistency is with the published Open-Meteo objects, not with ECMWF native GRIB Gaussian latitudes, and the original-GRIB requirement remains for any non-Open-Meteo-producer object.
2. Remaining complete H1 coverage, including **full time-series value coverage** where required; this run samples 22 spatial series only.
3. O1280-specific controlled HTTP(S)/S3 same-content results and cold full/local total-body/value/decode reductions, with complete independent service attempt logs; signed-S3 authorization/TLS cases as applicable. No remote request was used in this run.
4. Complete H4/H5/H7/H8 conditions, including the prescribed memory comparisons and full spatial-relation suite; a local peak or bounding box query does not complete those gates.
5. **H9 reproduction by a person/agent not involved in implementation**. Implementer self-tests do not satisfy it.

Accordingly this follow-up closes the missing **local real domain/spatial/time query execution record**, not all O1280 gates or Phase 6. N-grid owner-approved skips do not extend to these O1280 requirements. Historical sample/value/reference manifests and gate results remain unchanged.
