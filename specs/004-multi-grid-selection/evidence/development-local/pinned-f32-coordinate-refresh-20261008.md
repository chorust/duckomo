# Pinned producer Float32 coordinate references

**Recorded:** 2026-10-08  
**Scope:** Refresh projected coordinate references from the pinned Open-Meteo implementation and compare them point by point with existing development-build coordinate CSVs. This is a coordinate subcheck only. It does not pass H1, prove the OM array axis mapping, or complete T004.

## Source and numeric method

The reference generator now has two explicit modes. `independent_math` keeps the binary64 mathematical transforms. `pinned_producer_coordinates` ports the pinned producer's Float32 operations and calls the platform C Float32 math entry points through Python's standard `ctypes`; it does not import DuckOMO or its projection implementation. This mode is labeled as a producer reference, not as an independent mathematical oracle. It is tied to the host math library and does not claim cross-platform Float32 bit identity.

The binary64 diagnostic mode remains useful for checking the formulas. Around the rotated and stereographic projection singularities, however, the pinned producer's Float32 result can differ from the binary64 result by more than the fixed `1e-4°` limit. The old diagnostic and its hashes remain in [`reference-source-f32-separate-ops-20261007.md`](reference-source-f32-separate-ops-20261007.md); the tolerance was not changed.

The source commit is [`b06f4760fd1f997e5559bb380f64c5e496b4a509`](https://github.com/open-meteo/open-meteo/tree/b06f4760fd1f997e5559bb380f64c5e496b4a509). The exact hashes are in [`test/data/grids/source-manifest.json`](../../../../test/data/grids/source-manifest.json), SHA-256 `3666d27d5118bdfe53311ccf8eb36d3c7624dfcb40b09022071bb58c03e87112`. The coordinate and position rules were checked against:

| Pinned source | SHA-256 | Relevant code |
|---|---|---|
| `Sources/App/Domains/ProjectionGrid.swift` | `526cb8b4970898eb07cb55e33a2d9a81cd27271e195b9a767e1e4f9f87a77c69` | [`getCoordinates`](https://github.com/open-meteo/open-meteo/blob/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/App/Domains/ProjectionGrid.swift#L80-L94) constructs Float32 positions and calls the inverse projection. |
| `Sources/App/Domains/RotatedLatLon.swift` | `b468d3cb70d3a92e17ada6b8c2c8de58e8fead46b15b8eca14bcd13067c1e09d` | [`inverse`](https://github.com/open-meteo/open-meteo/blob/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/App/Domains/RotatedLatLon.swift#L54-L61). |
| `Sources/App/Domains/LambertConformalConic.swift` | `6f27864ff27eb4157548a61147eee5db00bed1ffa82779ec4a2a0b8db2306067` | [`inverse`](https://github.com/open-meteo/open-meteo/blob/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/App/Domains/LambertConformalConic.swift#L77-L91). |
| `Sources/App/Domains/Stereographic.swift` | `225b3286b1161634810d41a920792d1fce5ee627f419cbf889cf1477428b8d81` | [`inverse`](https://github.com/open-meteo/open-meteo/blob/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/App/Domains/Stereographic.swift#L61-L66). |
| `Sources/App/Domains/Gridable.swift` | `939431d274047ec6d46695f43a839aeddb524646d3ab035e5a249aba6458b679` | Producer linear point order. |
| `Sources/OmTime/NumberExtensions.swift` | `53eab0d10e3a682939a22a0ad8cfc28392db273fc7ccac12ba7a1ce398e50ad9` | Float32 degrees/radians operation order. |

The pinned `Gridable` code maps a linear producer point to `y = point / nx`, `x = point % nx`. This fixes the producer's point rule. The OM v3 arrays do not embed axis names, so it does not prove that each downloaded object's axes correspond to that producer order.

## Generation and imported artifacts

The three commands used `--method pinned_producer_coordinates`; a representative exact invocation is:

```sh
python3 scripts/generate-grid-coordinate-reference.py \
  --root "$PWD" \
  --sample-manifest "$PWD/test/data/grids/sample-manifest.json" \
  --definitions "$PWD/test/data/grids/definitions.json" \
  --source-manifest "$PWD/test/data/grids/source-manifest.json" \
  --sample-id rotated_cmc_gem_rdps_10km_cape_chunk_4337 \
  --method pinned_producer_coordinates \
  --output "$PWD/build/grid-references/pinned-producer-f32-20261008/rotated-cmc-gem-rdps-10km-cape-chunk-4337.coordinates.csv"
```

The Lambert and stereographic invocations used their corresponding frozen sample IDs and output filenames in the same directory. The complete argv, inputs, hashes, row counts, methods, and artifact hashes are recorded per reference in [`reference-pinned-producer-f32-20261008.json`](../reference-pinned-producer-f32-20261008.json), SHA-256 `e4cdadfad341b63343da97acdac57907121cc5285ac19c490ad407a461a9b65c`. The coordinate generator SHA-256 is `c47a59ada9b2307e3f27981c0e969f91341f45b08487274050788717b5accbcf`; sample manifest SHA-256 is `72af60d4769c2fc7b262864f1fb8e4f434e8a89ec463824f482c39103dc1939f`; definitions SHA-256 is `225caadf963c6f584850180f0e3c948cdbcebbf25a2a0d39aaef184b005f7da0`.

The import command was:

```sh
python3 scripts/generate-grid-reference.py \
  --root "$PWD" \
  --sample-manifest "$PWD/test/data/grids/sample-manifest.json" \
  --records "$PWD/specs/004-multi-grid-selection/evidence/reference-pinned-producer-f32-20261008.json" \
  --output-dir "$PWD/test/data/grids" --force
```

The resulting [`coordinate-reference.json`](../../../../test/data/grids/coordinate-reference.json) has SHA-256 `47712d94197d708b7ef1d48a4679d4eb5a38aecc324cc95d60d7b613314d33c7`; its status remains `partial_not_run`, with only the three projected targets covered. [`value-reference-manifest.json`](../../../../test/data/grids/value-reference-manifest.json) retains the three existing official OM C value references and the supplemental O1280 record. [`region-reference.json`](../../../../test/data/grids/region-reference.json) still has no producer GRIB/archive point-order references.

| Target | Points | Reference CSV SHA-256 |
|---|---:|---|
| `rotated_v3` | 1,191,300 | `be2598b5dd60b80248d58ee9f215732eafb8dd133e8305997cec90506e382552` |
| `lambert_v3` | 881,361 | `c9aebe8aa88a2ce11e3c7789be475b6051c90ebfeece7f2e5412bbfc2dbff719` |
| `stereographic_v3` | 770,440 | `d6556760901be1a17873825f680c3df7e4e51b11704042ae8967653c232d61f0` |

## Coordinate comparison

The new references were compared with the complete coordinate CSVs from `build/evidence-h1-dev-20261007-continue3/`, produced by the development DuckOMO build. The comparison used `compare_coordinates` in `scripts/compare-grid-sample-references.py` with the frozen `1e-4°` limit. The platform was Linux AArch64 with glibc 2.39.

| Target | Compared | Over limit | Maximum error |
|---|---:|---:|---:|
| `rotated_v3` | 1,191,300 | 0 | `1.52587890625e-5°` |
| `lambert_v3` | 881,361 | 0 | `1.52587890625e-5°` |
| `stereographic_v3` | 770,440 | 0 | `3.0517578125e-5°` |

The per-file actual/reference hashes and complete comparison summary are in [`coordinate-comparison.json`](pinned-f32-coordinate-refresh-20261008/coordinate-comparison.json), SHA-256 `d6e675fa80f3982786b9145bd19d0fb6a1afcc131f4478ef5c2135c9cb00e4ad`. This confirms the three coordinate outputs against the pinned producer Float32 mode; it does not establish source-axis mapping or official value-position alignment.

An H1 rerun with the new references was started against baseline build ID `15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8` but interrupted while writing a full value CSV (exit 130). Its partial files and an explicit non-evidence note are preserved at `build/evidence-h1-aborted-producer-f32-20261008/`. No completed H1 manifest was produced. The previous full H1 attempt records a value source-position mismatch in `build/evidence-h1-dev-20261007-continue3/h1-reference-checks/h1-reference-comparison.json`.

T004 and full H1 remain open: the N160, full N320, and N320-region producer objects and references are missing; Gaussian region point order and production GRIB/archive region references are missing; real OM axis-to-producer mapping and the prior value-position mismatch still need closure. No grid support or full/local benefit claim is promoted by this coordinate-only pass.
