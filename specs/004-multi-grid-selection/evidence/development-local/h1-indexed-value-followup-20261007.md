# H1 indexed-value comparison follow-up

**Recorded:** 2026-10-07

**Scope:** Correct the value-reference comparator's assumption that DuckOMO emits rows in ascending root-array order, then compare the already-generated Lambert development CSV by original `logical_index`. This is a partial development-build result, not a fixed matrix H1 result or producer-axis mapping proof.

## Lambert full-value result

The previous diagnostic stopped the value comparison at row 2 because it zipped DuckOMO's scan output against the official CSV by row. DuckOMO's natural scan order begins with logical indices `0, 120, 240, ...`; the official OM C export is ordered `0, 1, 2, ...`. The `logical_index` itself is the original root-array position, so matching by that key is the required comparison.

The comparator now stages the official Float32 values in a temporary, memory-mapped file and checks DuckOMO rows by `logical_index`. A compact bitset rejects duplicate indexes; every index must be in range and the full row count must be present. The query keeps its natural scan order and does not add a blocking 100M-row sort.

| Item | Value |
|---|---|
| Sample | `lambert_chmi_aladin_central_europe_2km_cape_chunk_4135` |
| Status | pass for this value-only comparison |
| Positions compared | 105,763,320 |
| Nulls | 0 |
| DuckOMO CSV SHA-256 | `f607a34e8f3b135d0ce345add396243ca4829971be9ccd59ba179aa4344c9b57` |
| Official OM C CSV SHA-256 | `7fc25500b7c6387f2408caf04322a2955f15f89cd401700016f26c4d8355e4dd` |
| Comparator SHA-256 | `ff30487e546991652357aa5f290b484a9f6a2f529906379beab42584b601a0d6` |

The comparison used the existing value CSV from `build/evidence-h1-dev-20261007-continue3/h1-reference-checks/` and the frozen official reference. It did not rerun DuckOMO or the full H1 command. The exact comparison command exited 0:

```sh
python3 - <<'PY'
import importlib.util
from pathlib import Path
script = Path("scripts/compare-grid-sample-references.py").resolve()
spec = importlib.util.spec_from_file_location("grid_sample_compare", script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
actual = Path("build/evidence-h1-dev-20261007-continue3/h1-reference-checks/lambert_chmi_aladin_central_europe_2km_cape_chunk_4135.duckomo-values.csv")
reference = Path("build/grid-references/revalidated-20261007/lambert-chmi-aladin-ce-cape-chunk-4135.official-reference.csv")
print(module.compare_values(actual, reference, 105763320, 0))
PY
```

The targeted `python3 test/tools/grid_reference_test.py` run passed all 6 checks, including permuted scan rows and duplicate-index rejection; `py_compile` and `git diff --check` also passed.

This clears the reported Lambert value row-order mismatch only. The object axes remain source-profile assumptions rather than independently verified producer metadata. Rotated and stereographic coordinate comparisons still exceed the frozen `1e-4°` limit, Gaussian references remain absent, and full H1 remains not-run.

## Coordinate arithmetic investigation

The four pinned Open-Meteo coordinate source files used here were fetched at commit `b06f4760fd1f997e5559bb380f64c5e496b4a509`; each SHA-256 matched `source-manifest.json`. `ProjectionGrid.getCoordinates` forms native coordinates as `Float(index) * step + origin`, while the independent reference generator currently evaluates the corresponding position expression in binary64 before applying its independent projection formula.

An unfrozen diagnostic fed binary32-rounded native positions into the independent double formulas and compared them with the existing DuckOMO development CSV. It still exceeded `1e-4°` near the projection singularities: rotated had 2,801 points over the limit and a maximum error of `0.0029511176°`; stereographic had 576 points over the limit and a maximum error of `0.0035057248°`. These experimental values are not imported as oracle evidence. The active coordinate references and tolerance were left unchanged pending reconciliation of source Float32 evaluation, the independent mathematical reference, and the production kernel.

| Pinned source | SHA-256 |
|---|---|
| `Sources/App/Gem/GemDomain.swift` | `d452ba91071ff74c5d9cb7aec758ff16abd1aed50383ce545c0ad65e4713f431` |
| `Sources/App/Domains/ProjectionGrid.swift` | `526cb8b4970898eb07cb55e33a2d9a81cd27271e195b9a767e1e4f9f87a77c69` |
| `Sources/App/Domains/RotatedLatLon.swift` | `b468d3cb70d3a92e17ada6b8c2c8de58e8fead46b15b8eca14bcd13067c1e09d` |
| `Sources/App/Domains/Stereographic.swift` | `225b3286b1161634810d41a920792d1fce5ee627f419cbf889cf1477428b8d81` |
