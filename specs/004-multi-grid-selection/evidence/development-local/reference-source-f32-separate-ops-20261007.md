# Source Float32 point-position reference revalidation

**Recorded:** 2026-10-07

**Scope:** Reproduce the pinned producer's Float32 native point construction in the independent coordinate generator, regenerate the three available projected references, and compare them against an existing development-build output. This is not a fixed-matrix H1 run, a producer axis-order proof, or complete T004 evidence.

## Reference construction

At the pinned Open-Meteo commit `b06f4760fd1f997e5559bb380f64c5e496b4a509`, `ProjectionGrid.getCoordinates` forms each native position as `Float(index) * step + origin`. The independent generator now rounds the multiplication result to Float32 before adding the Float32 origin and rounding the sum. It then evaluates the projection formula in binary64, without importing DuckOMO's coordinate kernel. This corrects the earlier generator's binary64 point-position arithmetic. The first, incomplete Float32 attempt is explicitly marked superseded in [its report](reference-source-f32-revalidation-20261007.md).

The active manifests and previous import record were archived at [reference-manifests-pre-separate-f32-ops-20261007](../reference-manifests-pre-separate-f32-ops-20261007/). The new generator SHA-256 is `12e398204844aeb9af301420713fdfe50f84d2040ea2149519d5ec9656a2b360`; the new import-record SHA-256 is `cef3d52c60a3c1a66995e863ba4f9b85c1bab42ba14149d2517a25780dc3a2c2`.

All three coordinate generator commands and the reference import exited 0:

```sh
python3 scripts/generate-grid-coordinate-reference.py --root "$PWD" --sample-manifest "$PWD/test/data/grids/sample-manifest.json" --definitions "$PWD/test/data/grids/definitions.json" --source-manifest "$PWD/test/data/grids/source-manifest.json" --sample-id rotated_cmc_gem_rdps_10km_cape_chunk_4337 --output "$PWD/build/grid-references/source-f32-separate-ops-20261007/rotated-cmc-gem-rdps-10km-cape-chunk-4337.coordinates.csv"
python3 scripts/generate-grid-coordinate-reference.py --root "$PWD" --sample-manifest "$PWD/test/data/grids/sample-manifest.json" --definitions "$PWD/test/data/grids/definitions.json" --source-manifest "$PWD/test/data/grids/source-manifest.json" --sample-id lambert_chmi_aladin_central_europe_2km_cape_chunk_4135 --output "$PWD/build/grid-references/source-f32-separate-ops-20261007/lambert-chmi-aladin-ce-cape-chunk-4135.coordinates.csv"
python3 scripts/generate-grid-coordinate-reference.py --root "$PWD" --sample-manifest "$PWD/test/data/grids/sample-manifest.json" --definitions "$PWD/test/data/grids/definitions.json" --source-manifest "$PWD/test/data/grids/source-manifest.json" --sample-id stereographic_cmc_gem_rdps_cape_chunk_4337 --output "$PWD/build/grid-references/source-f32-separate-ops-20261007/stereographic-cmc-gem-rdps-cape-chunk-4337.coordinates.csv"
python3 scripts/generate-grid-reference.py --root "$PWD" --sample-manifest "$PWD/test/data/grids/sample-manifest.json" --records "$PWD/specs/004-multi-grid-selection/evidence/reference-revalidated-source-f32-separate-ops-20261007.json" --output-dir "$PWD/test/data/grids" --force
```

| Target | Rows | Reference SHA-256 |
|---|---:|---|
| `rotated_v3` | 1,191,300 | `8505b8429fe895ba98d84f4131897d92e407ceb013898b0857796aea9d6cfc17` |
| `lambert_v3` | 881,361 | `dbc66a87803bb25e1a299ef63c8a0784f5006cd1999f953537b4fb81a0cf0762` |
| `stereographic_v3` | 770,440 | `d4ef94214c0ad99ccac9f0c1a0a5ebeae6c0c894a16de3da0727e8f8798ff975` |

The regenerated references were imported with the existing frozen `1e-4°` maximum. The coordinate manifest remains `partial_not_run` for the missing Gaussian targets. No value oracle or supplemental O1280 evidence was promoted.

## Coordinate comparison

The existing development-build CSV outputs were compared by `spatial_index` against the regenerated references using `compare_coordinates` in `scripts/compare-grid-sample-references.py`. Complete per-sample counts, output hashes, reference hashes, first exceeded position, and maximum errors are recorded in [`coordinate-comparison-diagnostic.json`](../../../../build/grid-references/source-f32-separate-ops-20261007/coordinate-comparison-diagnostic.json), SHA-256 `4d6cf10a72de09e372f36c8ed20a0f036387c012b25d28a113000b690fb35c0b`.

| Target | Rows | Positions over `1e-4°` | Maximum error |
|---|---:|---:|---:|
| `rotated_v3` | 1,191,300 | 1,377 | `0.0029518928°` |
| `lambert_v3` | 881,361 | 0 | `0.0000273209°` |
| `stereographic_v3` | 770,440 | 370 | `0.0013966075°` |

Lambert is within the frozen coordinate limit on this development output. Rotated and stereographic still fail it. The tolerance was not changed, and the evidence does not establish actual producer axis mapping or complete H1. The full H1 gate remains not-run while Gaussian samples and independent point-order evidence are missing.

The H1 value comparison by `logical_index` remains recorded separately in [the indexed-value follow-up](h1-indexed-value-followup-20261007.md): Lambert's 105,763,320 values matched the official OM C decode. That value result does not turn the coordinate checks or H1 gate into a pass.
