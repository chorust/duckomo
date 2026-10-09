# US1 Local Implementation Checks

Recorded 2026-10-06 from the repository root as a US1 build snapshot. Later US2 implementation and build identity are recorded in `us2-progress.md`. This records local development checks only; it is not an H0/H1 acceptance report and does not upgrade any domain's support level.

## Build identity

| Input | Identity |
|---|---|
| Platform | Linux AArch64, GCC 13.3.0, CMake 3.28.3 |
| DuckDB | v1.5.4 (Variegata), source `08e34c447bae34eaee3723cac61f2878b6bdf787` |
| OM C | `d8855e418e2231ae8439f0c7e840fa3f93b371e3` |
| Build directory | `build/release-vcpkg`, vcpkg triplet `arm64-linux` |
| DuckDB CLI SHA-256 | `c7649dca212e23511766bf6b865c39f6d3e7687b5e38b4ab949559a0eb70beba` |
| DuckOMO extension SHA-256 | `b76bae80836f59100169bbe295444e207cdc19a61214e209967ff10cda1430af` |
| httpfs extension SHA-256 | `1b0bea6729c9891ea788e304d69b1e98722d91de038ff523568af1d43092671c` |

Identity commands `build/release-vcpkg/duckdb -version` and `sha256sum build/release-vcpkg/duckdb build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension build/release-vcpkg/extension/httpfs/httpfs.duckdb_extension` exited 0.

## Frozen inputs and synthetic fixtures

| File | SHA-256 | Scope |
|---|---|---|
| `test/data/grids/definitions.json` | `225caadf963c6f584850180f0e3c948cdbcebbf25a2a0d39aaef184b005f7da0` | Source-derived definitions, including the separately registered CHMI ALADIN production Lambert grid; formula vector remains non-domain |
| `test/data/grids/source-manifest.json` | `f119c17bd0c3adf759e1bf222878ac750a3ffa7b76e3af872937357dd12947bf` | Pinned source commit and source-file hashes; frozen four-family sample inventory and supplemental O1280 value comparison |
| `test/data/grids/sample-manifest.json` | `15220f0b6134f76a6cb462fed60ea27cf59c4e7d3a8799ca24c12ae060e8b8cc` | Synthetic fixtures, four real OM v3 grid-family examples, frozen skip-block candidates; required N-grid gates remain `not-run` |
| `test/data/grids/value-reference-manifest.json` | `85ad830be0aae1097f17238a07ef2e0c8a7ca5ab7ac9fd7b26bcfee887879e41` | Official OM C and DuckOMO full-value comparison for O1280; no coordinate/row-mapping evidence |
| `test/data/grids/canonical-vectors.json` | `47e2d856a1a54a2d714b382fa0343e9ed0f940b95de837a6c2374d2cd0dbd7db` | Shared native/generator identity vectors |
| `src/include/duckomo/generated_grid_registry.hpp` | `e629d9faef9261c87c4548a6f0ed960fd3ed3503e876ff1be6b8c88649b1a9cd` | Deterministic typed definitions, including CHMI ALADIN, expected axis profiles, canonical grid IDs, and parent IDs |
| `test/data/grids/layouts.om` | `2067ea75bbb596b194d89e7c9bf97fc8f985098d4be56bb431771e0700cc78a6` | Synthetic mixed-layout rejection probe |
| `test/data/grids/reverse.om` | `59917db155efbac8b2fc2b05da64029b6d73e2c5ccc217e6f8cdf958620b64c2` | Separate axes |
| `test/data/grids/x-fastest.om` | `dfb2a41faa2d8632ca0e34dcbdd24850a751b50dbc07cddccd902c74cc462d84` | Flattened longitude-fastest positions |
| `test/data/grids/y-fastest.om` | `a617a8ef5d686ef7407fadaae423d94c3a9876ad0c39dcc718f06da9a6f53f7f` | Flattened latitude-fastest positions |
| `test/data/grids/interleaved.om` | `ea1e85fed4dff84ae75345d87df3dea06a1b16c0877434a620eab8dab92bf8ca` | Two aligned values with five semantic axes between spatial axes |
| `test/data/grids/crs-conflict.om` | `74b4811d1710a9ac29bb2b7adef95878741eca71e7df43235a5254cc8d2348f4` | Synthetic unknown-CRS fail-closed probe |

The fixture tool's official OM C API full-array roundtrip passed for these files. These generated files are test inputs, not producer samples or coordinate/value oracles.

## Checks run

| Command | Exit | Result |
|---|---:|---|
| `cmake --build build/release-vcpkg --target regular_grid_test spatial_layout_test projected_grid_test gaussian_grid_test grid_identity_test duckomo_loadable_extension -j2` | 0 | Extension and local grid/layout targets built |
| `cmake --build build/release-vcpkg --target domain_reference_test grid_identity_test duckomo_loadable_extension -j2` | 0 | Generated domain registry, current extension, and legacy real-domain reference target built |
| `cmake --build build/release-vcpkg --target duckomo_fixture_tool -j2` | 0 | Fixture generator built |
| `build/release-vcpkg/test/tools/duckomo_fixture_tool --output test/data` | 0 | OM C API roundtrip passed; synthetic manifests and hashes regenerated |
| `python3 scripts/generate-grid-registry.py --input test/data/grids/definitions.json --check` | 0 | Checked-in generated header and canonical vectors are current |
| `python3 test/tools/grid_registry_test.py` | 0 | Two generations matched byte-for-byte; canonical vector corruption was rejected |
| `build/release-vcpkg/test/native/projected_grid_test` | 0 | Rotated, Lambert and stereographic vectors passed |
| `build/release-vcpkg/test/native/gaussian_grid_test` | 0 | N160/N320 row counts, Float32 edges, Legendre reference, independently expanded source segments and prefix overflow checks passed |
| `build/release-vcpkg/test/native/grid_identity_test` | 0 | Runtime identities matched regular/F32/Gaussian/layout canonical vectors; Gaussian Legendre-root and producer rule IDs differ |
| `build/release-vcpkg/test/native/spatial_layout_test` | 0 | Layout and original-position mapping checks passed |
| `build/release-vcpkg/test/native/regular_grid_test` | 0 | Legacy regular-grid checks passed |
| `env DUCKOMO_TEST_EXTENSION=build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension build/release-vcpkg/test/native/projection_evidence_test` | 0 | Existing projection metrics/lifecycle check passed |
| `build/release-vcpkg/test/native/grid_identity_test` | 0 | Runtime IDs matched generated IDs for all six definitions; source/profile metadata and Gaussian region parent identity loaded; non-WGS84 source ellipsoid parameters rejected |
| `env DUCKOMO_CORE_FUNCTIONS_EXTENSION=build/release-vcpkg/repository/v1.5.4/linux_arm64/core_functions.duckdb_extension DUCKOMO_TEST_EXTENSION=build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension build/release-vcpkg/test/native/domain_reference_test build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om test/data/domain-manifest.json build/evidence/spatial/reference` | 0 | Existing `ncep_gfswave025` regular domain matched all 15 official references at 1,038,240 positions each |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/multi_grid.test` | 0 | 87 SQL assertions passed, including duplicate/missing declaration rejection, source-axis-order, output-name collision, and registered-domain profile rejection |
| `build/release/test/unittest test/sql/multi_grid.test` with the test's `LOAD` path temporarily pointed at the current `build/release-vcpkg` extension, then restored | 0 | 89 assertions passed after adding unknown-type, signed-64-bit overflow, and unprojected-variable incompatibility cases; does not complete the missing four-family explicit/domain equivalence |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/read_om.test` | 0 | 108 existing SQL assertions passed |
| `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/spatial.test` | 0 | 203 existing SQL assertions passed |
| Same `unittest <absolute test file>` command for `raw`, `semantic_axes`, `axis_filter`, `parallel_scan`, `cache_metrics`, `projection`, `spatial_pushdown`, and `spatial_composition` | 0 each | 232 existing SQL assertions passed across eight files |

## Remaining H0/H1 gaps

- T003's four-family sample inventory is now complete. T004 remains open: independent coordinate/value/production region-point-order references do not cover the required N160/N320/N-region cases, and no required-target full value oracle is imported. Synthetic checks do not satisfy these gates.
- Supplemental O1280 is now fully decoded through the official OM C API and compared against a full DuckOMO output at all logical array positions. It has no Gaussian row/point or coordinate oracle, and O/F grids are outside 004; this evidence does not change the T003/T004 required N-grid status.
- N160/N320 native checks exercise both endpoints of every explicit f32 row; the N320 14,747-point region mapping remains derived from pinned source arithmetic. The native test independently expands the frozen source segments and checks all local-to-parent positions; actual object-local point order has not been verified from a real OM object.
- The generated typed definitions, expected profiles, canonical identities, and Gaussian parent identity are consumed by `domain_registry`; the fixed-domain binder preserves the existing legacy-domain path. T019 is complete. Registered-versus-explicit `grid_id` and layout identity checks pass for the real rotated, stereographic, and CHMI Lambert samples, plus a synthetic N160 identity-only fixture; see the equivalence SQL files and the T017 test record below. No real N160 mapping or O1280-to-N-grid equivalence is claimed.
- New-grid CRS handling is fail-closed for any non-empty `crs_wkt`; recognized source CRS profiles and Gaussian WGS84 profile comparison are not implemented. T025 remains open.
- Coordinate validation checks every declared position before results, uses constant space and a checked point-count bound, checks interruption at least every 256 evaluations, reads no values, and records attempted preparation evaluations in the v4 metrics model. It currently uses the full prewalk path rather than analytical proofs; T026 is complete.
- T017 binding/SQL coverage passes. It includes the three projected producer identity comparisons and a synthetic N160 registered-versus-explicit grid/layout identity check. The N160 fixture proves binding identity only; real Gaussian row/point mapping and H0/H1 coordinate/value acceptance remain unverified.
- Version-one grid predicates now filter logical source positions before value decoding. The lazy native-window selector and all-family selection evidence remain open; see `us2-progress.md`.
- This record does not establish H0/H1 acceptance, remote savings, 003 remote gates, independent reproduction, DuckDB 2.0 compatibility, or formal 2.0 support.

## 2026-10-07 inventory and producer-definition continuation

The fixed source sample inventory now includes four real OM v3 grid families: rotated (`gem_rdps_10km`), stereographic (`gem_regional`), Lambert (`aladin_central_europe_2km`), and reduced Gaussian (ECMWF IFS HRES O1280). HRES O1280 remains supplemental and does not satisfy the N160, full N320, or N320-region targets. T003 is checked as an inventory task; T004 and the corresponding complete coordinate/value gates remain open.

The CHMI object is now paired with the exact pinned production definition from `Sources/App/Chmi/ChmiDomain.swift`: 1053×837, first coordinate (38.599°N, 1.334°E), 2325 m spacing, Lambert origin (17°, 46.244°), sphere radius 6371229 m. Its source-compatible Float32 projected origin is frozen as x0=-1364242.5 m, y0=-717413.5 m. `lambert_formula_vector` remains a separate non-bindable formula vector.

The command `python3 scripts/generate-grid-registry.py --check` exited 0. `cmake --build build/release-vcpkg --target duckomo_loadable_extension duckomo_fixture_tool -j2` and then `cmake --build build/release-vcpkg --target shell unittest -j2` exited 0. `python3 test/tools/grid_registry_test.py` and `build/release-vcpkg/test/native/grid_identity_test` exited 0. `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/multi_grid.test` exited 0 with 147 assertions.

`test/data/grids/sample-manifest.json` was compared against a freshly generated fixture manifest in a temporary directory; the top-level family inventory and nested public Open-Meteo sample manifest matched exactly. The earlier CHMI-only identity check returned `grid_identity_matches=true`, `layout_identity_matches=true` for the real CHMI OM object. Later projected-sample checks and the synthetic N160 identity test completed T017 SQL binding coverage, as recorded below. No coordinate oracle, full CHMI value oracle, or remote benefit is claimed.


## 2026-10-08 matched baseline H1 reference subchecks

Build ID `5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d` ran `duckomo_grid_validation --cases H1` with the frozen sample, coordinate, and official-value manifests. DuckOMO coordinates were compared against the pinned producer Float32 reference at every spatial position; root-array values were compared to official OM C Float32/NaN values by original `logical_index`.

| Sample | Coordinate positions | Maximum coordinate error | Value positions | Null positions |
|---|---:|---:|---:|---:|
| Rotated GEM | 1,191,300 | `1.52587890625e-5°` | 135,808,200 | 40,819,713 |
| Lambert ALADIN | 881,361 | `1.52587890625e-5°` | 105,763,320 | 0 |
| Stereographic GEM | 770,440 | `3.0517578125e-5°` | 87,830,160 | 62,643,412 |

All 2,843,101 coordinate points met the frozen `1e-4°` tolerance. All 329,401,680 value positions matched their official Float32/NaN references exactly by logical index. The subcheck summary is `partial_pass`; all three available projected targets passed, while Gaussian N160, N320, and N320-region are missing. The coordinate queries select time index 0 through validator-declared synthetic `valid_time` labels and use the expected `[ny,nx,ntime]` profile. OM does not embed those axis names, so this run does not independently prove array-axis-to-producer-point mapping.

The H1 runner returned exit 2 because full H1 remains `not-run`; the top-level evidence manifest audit passed. The comparator command elapsed 2,597,603 ms and used a maximum 580,481,024 bytes RSS. Exact SQL, DuckDB/extension hashes, CSV hashes, per-sample results, and the top-level exit/audit records are in [the evidence directory](h1-20261008-5dac993/). This does not promote any domain support level or complete H0/H1.

## N160 identity-only SQL check on 2026-10-07

`cmake --build build/release --target duckomo_loadable_extension --parallel 2` exited 0. `build/release/test/unittest test/sql/multi_grid.test` exited 0 with 153 assertions. This includes the synthetic N160 registered-domain versus explicit version=1 `grid_id`/layout comparison. The test now loads the JSON extension from the matching build path; `test/tools/grid_registry_test.py` also checks that its copied N160 latitude and row-count arrays match `definitions.json` and that row longitude steps follow the frozen f32 rule. This closes T017's SQL binding coverage but does not establish real N160 producer coordinates, point order, values, or support.
