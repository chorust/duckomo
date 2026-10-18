# T003–T004 synthetic sampling references

**Status:** generated fixtures and independent oracles are verified; locator behavior is assessed separately in [N1](N1-locator.md), scan/value parity in [US1](US1-core.md), and current fixture identity in the latest [run package](run-20261017-sampling-validation-final/).

## Regeneration

From the repository root, run:

```sh
cmake --build build/release --target duckomo_fixture_tool --parallel
build/release/test/tools/duckomo_fixture_tool --sampling-output test/data/sampling
python3 scripts/generate-sampling-reference.py --output test/data/sampling --self-check
python3 scripts/check-sampling-fixtures.py --root test/data/sampling
```

The fixture tool uses the pinned official OM v3 C reader at `d8855e418e2231ae8439f0c7e840fa3f93b371e3` to decode the generated files and produce the full `index,value` CSV references. The Python reference generator imports no production locator and independently enumerates grid positions using the frozen binary64 haversine formula. A Decimal implementation computes a 90 digit scientific-distance reference. Rectangle interior points are analytic midpoints. Standard 2D little-endian WKB, axis and point ordering, source indices, statuses, hashes, and selected official-reader values are written to `manifest.json`.

## Current generated coverage

- 5×5 multi-variable small fixtures with three semantic axes, separately placed and interleaved spatial axes, positive and negative latitude/longitude steps, and both flattened point orders.
- Per-variable NaNs are at different locations. Full official-reader CSVs contain all original values and preserve NaNs.
- 64² and 256² value fixtures use the same `[1,1,1,16,16]` chunk shape and two aligned variables. The expanded case has 16× the spatial points.
- A four point exact distance tie checks the original point index tie break. Additional targets cover an exact grid point, a gap, outside-domain state, and a periodic seam. Geometry inputs include a hole, empty polygon, and disjoint MultiPolygon; complex polygon representative positions deliberately have no Python-produced expectation.
- `om-fixtures.json` binds each OM file's byte length/SHA and each official reader CSV's SHA. `manifest.json` binds to the OM manifest and hashes each WKB. `check-sampling-fixtures.py` verifies every file binding and all official-reader CSV row counts.

## Verification captured

The original 2026-10-15 capture generated nine OM files, 18 variable references, and 38 nearest/status references across nine profiles and seven geometries. The current fixture set has since expanded: latest `check-sampling-fixtures.py` output verifies 16 OM fixtures, 29 variables, 9 geometries and 62 nearest references; OM manifest SHA-256 `f7006f72bb33b02690deff4d9900587bae85e8ad7d622e5fea468faa85b2863e`, reference manifest SHA-256 `08798b8365a1b03591865477273049807f2a50b4b50556ff804a62dffd5e8cfd`. The latest sampling validation command reran fixture checks successfully. Native locator and SQL/value acceptance are recorded separately; the current N1/N2 declared-scope passes and N5 provenance/archive limits are described in their gate evidence.
