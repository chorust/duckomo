# Real projected domain identity checks

**Captured:** 2026-10-07 UTC  
**Scope:** Registered versus explicit version-one grid identity/layout checks for the frozen Open-Meteo GEM rotated and stereographic OM v3 samples. `om_grid_info` reads schema and identity metadata only; this is not a coordinate, value, source-position, or remote-benefit oracle.

## Frozen objects

| Family | Object | SHA-256 |
|---|---|---|
| Rotated | `build/s3-samples/openmeteo-v3/cmc-gem-rdps-10km-cape-chunk-4337.om` | `3db637197d27c65051e417b850f8c3656d1c5371ab78dcbd9fc05d0e440956f7` |
| Stereographic | `build/s3-samples/openmeteo-v3/cmc-gem-rdps-cape-chunk-4337.om` | `1d701b188c8c264532f0d8fa917400a66cf28aa1c8e53a7b40781a775333c7c1` |

Both identities use the pinned producer definitions in `test/data/grids/definitions.json`; `dimensions` follows the frozen Open Data `[ny,nx,ntime]` convention. Axis labels are an explicit source profile and are not embedded in either OM object.

## Command and result

```sh
build/release-vcpkg/duckdb -unsigned -csv :memory: \
  < specs/004-multi-grid-selection/evidence/baseline-local/projected-domain-equivalence.sql
```

Exit code: `0`.

```text
grid_family,grid_identity_matches,layout_identity_matches
rotated_latlon,true,true
stereographic,true,true
```

The Lambert comparison is recorded in [`aladin-domain-equivalence.sql`](aladin-domain-equivalence.sql). This completes registered/explicit metadata identity checks for the three producer projected definitions currently registered with frozen real samples. Gaussian O1280 remains supplemental: its actual row lengths and point order have not been mapped, so it is not compared with the N160/N320 registry definitions. This evidence does not complete T004 or H0/H1.
