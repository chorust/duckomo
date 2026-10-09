# Source Float32 point-position reference revalidation

**Recorded:** 2026-10-07

**Superseded:** This first revalidation rounded the Float32 product only as part of the final sum, so it did not preserve the producer's separate multiply/add rounding. Do not use its coordinate artifacts or comparison counts as current source-position evidence. The corrected reference and comparison are recorded in [the separate-operation revalidation](reference-source-f32-separate-ops-20261007.md); the intermediate manifests are retained at [reference-manifests-pre-separate-f32-ops-20261007](../reference-manifests-pre-separate-f32-ops-20261007/).

**Scope:** Correct the independent projected-coordinate generator's source-point construction, regenerate the three available projection references, and compare them with the existing development-build coordinate outputs. This does not establish a fixed-matrix H1 result, actual producer axis mapping, or complete T004 coverage.

## Reference construction

The pinned producer's `ProjectionGrid.getCoordinates` evaluates each native position as `Float(index) * step + origin`. The independent generator previously evaluated this point expression in binary64. It now rounds the index, step, product, origin, and sum to binary32, then evaluates its independent projection formula in binary64. The source commit and file hashes are in `source-manifest.json`; the retrieved `ProjectionGrid.swift` hash matched that manifest. The code does not import DuckOMO's coordinate kernel.

The prior active coordinate, region, and value manifests plus the prior import record were archived at [reference-manifests-pre-source-f32-20261007](../reference-manifests-pre-source-f32-20261007/). The active coordinate manifest now references the regenerated artifacts and retains the existing `1e-4°` limit. The importer preserved the existing O1280 supplemental value record. New reference import record SHA-256: `622e284f6dce3b008e28f660bcfaaecb85bc6963cd547207fb0bef566699e60d`.

All three generator commands exited 0:

```sh
python3 scripts/generate-grid-coordinate-reference.py --root "$PWD" --sample-manifest "$PWD/test/data/grids/sample-manifest.json" --definitions "$PWD/test/data/grids/definitions.json" --source-manifest "$PWD/test/data/grids/source-manifest.json" --sample-id rotated_cmc_gem_rdps_10km_cape_chunk_4337 --output "$PWD/build/grid-references/source-f32-points-20261007/rotated-cmc-gem-rdps-10km-cape-chunk-4337.coordinates.csv"
python3 scripts/generate-grid-coordinate-reference.py --root "$PWD" --sample-manifest "$PWD/test/data/grids/sample-manifest.json" --definitions "$PWD/test/data/grids/definitions.json" --source-manifest "$PWD/test/data/grids/source-manifest.json" --sample-id lambert_chmi_aladin_central_europe_2km_cape_chunk_4135 --output "$PWD/build/grid-references/source-f32-points-20261007/lambert-chmi-aladin-ce-cape-chunk-4135.coordinates.csv"
python3 scripts/generate-grid-coordinate-reference.py --root "$PWD" --sample-manifest "$PWD/test/data/grids/sample-manifest.json" --definitions "$PWD/test/data/grids/definitions.json" --source-manifest "$PWD/test/data/grids/source-manifest.json" --sample-id stereographic_cmc_gem_rdps_cape_chunk_4337 --output "$PWD/build/grid-references/source-f32-points-20261007/stereographic-cmc-gem-rdps-cape-chunk-4337.coordinates.csv"
python3 scripts/generate-grid-reference.py --root "$PWD" --sample-manifest "$PWD/test/data/grids/sample-manifest.json" --records "$PWD/specs/004-multi-grid-selection/evidence/reference-revalidated-source-f32-20261007.json" --output-dir "$PWD/test/data/grids" --force
```

| Target | Rows | New reference SHA-256 |
|---|---:|---|
| `rotated_v3` | 1,191,300 | `6631558d233a93f89d69b4c8504fac04ecacc28fc7525b92783ac8faeb00261c` |
| `lambert_v3` | 881,361 | `dbc66a87803bb25e1a299ef63c8a0784f5006cd1999f953537b4fb81a0cf0762` |
| `stereographic_v3` | 770,440 | `3a176e8ecad5f6768f8d094c0e50dab214604ff5e3aa97855cf2019bf8cbca4c` |

The new generator SHA-256 is `88dc23434a639aaad008c520f24cbf7a53ca92b0131d7d8b18d435c4ed6003f3`. The active manifest SHA-256 is `1117bf66f33c749483779cc8ada4671bac9b54de5d5a472e14b95b947bb3360d`.

## Coordinate comparison

The existing development-build outputs in `build/evidence-h1-dev-20261007-continue3/h1-reference-checks/` were compared by `spatial_index` against the regenerated references. The complete comparison is recorded in [`coordinate-comparison-diagnostic.json`](../../../../build/grid-references/source-f32-points-20261007/coordinate-comparison-diagnostic.json), SHA-256 `a0a86c3361bd05dbed6e3a8933fbf06d955f8c6a113b75561d56e18a8228adf7`.

| Target | Rows compared | Positions over `1e-4°` | Maximum error |
|---|---:|---:|---:|
| `rotated_v3` | 1,191,300 | 1,778 | `0.0029521829°` |
| `lambert_v3` | 881,361 | 0 | `0.0000273209°` |
| `stereographic_v3` | 770,440 | 576 | `0.0035057248°` |

The comparison was diagnostic evidence from an existing development build, not a matrix run. Lambert coordinates meet the frozen tolerance for this sample. Rotated and stereographic do not; the tolerance was not changed. The full H1 gate remains not-run because Gaussian references and independent producer axis-order evidence are still missing.

The comparison can be reproduced with the same `compare_coordinates(actual, reference, row_count, nx, 1e-4)` function in `scripts/compare-grid-sample-references.py`; for all three samples, the exact paths, counts, output hashes, and results are in the diagnostic JSON. The reference importer was validated by `python3 test/tools/grid_reference_test.py` (7 checks); `python3 -m py_compile scripts/generate-grid-coordinate-reference.py scripts/compare-grid-sample-references.py test/tools/grid_reference_test.py` exited 0.
