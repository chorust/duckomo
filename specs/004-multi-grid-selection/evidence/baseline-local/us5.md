# US5 local source and spatial-input evidence

Recorded: 2026-10-07; updated 2026-10-08. Earlier runs use baseline build ID `15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8`; the latest full-public-source H3/H7 subcheck uses matching baseline build ID `5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d`. Complete H3/H7 gates remain `not-run`.

## Frozen run

The runner used `test/data/grids/sample-manifest.json` (SHA-256 `72af60d4769c2fc7b262864f1fb8e4f434e8a89ec463824f482c39103dc1939f`), baseline build ID `15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8`, DuckDB CLI SHA-256 `dea62fdc3792705921f8326719a6aedf9927f949a0209411cd3c7211f8972c09`, and DuckOMO extension SHA-256 `8bed889b8f9cf3947aab3b7d94620e0ea6c0d4f4a22c5d26ffc4214411309c3e`. The run manifest records exact argv, input/output hashes, commands, and exit codes in [the current H3/H7 run](us5-20261007-current-15f751-final/manifest.json). The evidence manifest audit passed. The harness returned exit 2 because both requested full gates remain `not-run`.

The command was:

```sh
build/grid-matrix/baseline-1.5.4/builds/15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8/release/test/tools/duckomo_grid_validation \
  --root "$PWD" --manifest "$PWD/test/data/grids/sample-manifest.json" \
  --cases H3,H7 \
  --duckdb "$PWD/build/grid-matrix/baseline-1.5.4/builds/15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8/release/duckdb" \
  --extension "$PWD/build/grid-matrix/baseline-1.5.4/builds/15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8/release/extension/duckomo/duckomo.duckdb_extension" \
  --output "$PWD/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261007-current-15f751-final"
```

## H3 local checks

After building the matching `grid_zero_io_test`, `source_identity_test`, and `core_functions_loadable_extension` targets, all five local commands exited 0: the zero-I/O native test and SQLLogicTest, source-identity native test and SQLLogicTest, and `grid_info.test`. The v4 metrics assertion also exited 0 across 18 scenarios, including zero value index/data/decode for coordinate-only, source, and info cases; the interleaved value-filter case read only its filtered variable. See [`h3-local/zero-io-metrics.json`](us5-20261007-current-15f751-final/h3-local/zero-io-metrics.json).

This is local synthetic and public-sample smoke evidence. Full H3 remains `not-run` because required public remote-source cases are incomplete.

## H7 local checks

The independent oracle and DuckOMO output matched the complete point/polygon relation multiset for all points in four small fixtures: rotated latitude/longitude, Lambert conformal conic, stereographic, and reduced Gaussian. Their row counts were 6, 6, 6, and 8. For every fixture, source positions reconstructed the reference positions, and both `om_source` and `om_grid_info` reported zero value index/data/decode reads. The full per-case report, SQL, CSVs, and hashes are in [the H7 subcheck](us5-20261007-current-15f751-final/h7-local/manifest.json).

Three hash-fixed local public projected OM v3 objects also passed a limited smoke check over their first four spatial positions. For each rotated GEM, stereographic GEM, and CHMI Lambert sample, explicit-grid and registered-domain source identities matched, and both interfaces reported zero value reads. Object hashes and shapes are recorded in the H7 report.

The producer `[ny,nx,ntime]` mapping for these objects follows the pinned producer README; this run does not independently establish producer axis order. It does not compare complete producer source positions or point/polygon relation multisets, and it does not include real N160, full N320, or N320-region objects. Public remote source identity and cross-URI checks also remain `not-run`. Therefore H7 is partial local evidence and its full gate remains `not-run`.

## 2026-10-08 pinned producer coordinate prefix

The refreshed baseline H7 run at [bounded source prefix evidence](us5-20261008-bounded-source-prefix/) checks the first four natural scan rows for each public projected sample. The validator requires the exact logical/axis/point sequence, compares each longitude/latitude against the hash-pinned `pinned_producer_coordinates` reference at `1e-4°`, and verifies explicit/domain source positions and layouts match. Maximum coordinate error was `1.52587890625e-5°` for rotated GEM, `1.52587890625e-5°` for stereographic GEM, and `5.841255187988281e-6°` for Lambert. All six source/info paths reported zero value index/data/decode reads; each source row carried an object ID and `content_verified=false`. The manifest pins the sample, source, and coordinate-reference manifests and records the CLI/extension and generated output hashes. Its evidence audit passed; the H7 harness returned 2 because full-gate prerequisites remain unmet.

The first attempt added `ORDER BY logical_index LIMIT 4`, which scanned the full public grid before returning a four-row prefix. It was interrupted with exit 130 and is explicitly marked as partial in [the interrupted-run note](us5-20261008-pinned-source-positions/README.md). The passing rerun uses `LIMIT 4` without a full-grid sort and fails if the returned source positions are not the expected ordered prefix. It still does not independently prove the producer's complete OM axis-to-point mapping.

## 2026-10-08 public source/info follow-up

The rebuilt baseline H3/H7 runner now also invokes `grid_spatial_reference.py --validate-public-sources`. In [the audited run](us5-20261008-h3-public-source-audited/), three hash-pinned projected OM v3 samples each checked the first four natural source positions in both explicit-grid and registered-domain views. The source identities matched within each pair, the pinned Float32 coordinate references matched within `1e-4°`, and all 12 explicit/domain source/info metric reports recorded zero value index bytes, data bytes, and decoded chunks. The samples were CMC GEM rotated (`3db63719…` object hash), CMC GEM stereographic (`1d701b18…`), and CHMI ALADIN Lambert (`cef407b0…`). The H3 public-source subcommand and all H3 local commands exited 0. The run pins sample manifest SHA-256 `72af60d4…`, coordinate-reference manifest `47712d94…`, DuckDB CLI `dea62fdc…`, DuckOMO extension `8bed889b…`, and public-source report `b7c8e451…` in the nested manifest.

The four synthetic H7 relation/source-position checks also passed. The combined H3/H7 harness returned 2 because both full gates remain `not-run`; the top-level evidence manifest audit passed. This run adds bounded local public-source coverage only. It does not establish full OM array-axis-to-producer point order, Gaussian N160/N320/N320-region source mapping, H1/H2 public identity coverage, or remote/cross-URI source evidence. T070 remains open.

## 2026-10-08 explicit/domain identity SQL follow-up

The next baseline run, [with SQL identity evidence](us5-20261008-h3-public-source-identity-sql/), adds an explicit SQL comparison over the same four-row natural prefix for each of the three public projected samples. A materialized-prefix `FULL OUTER JOIN` compares the complete `om_source` STRUCT and coordinates by logical index. Each SQL result reports 4 joined rows, 0 unmatched rows, 0 mismatches, and `all_equal=true`. The run records and verifies a separate value-read metrics snapshot for that SQL query; all 15 source/info/identity reports across the three samples show zero value index bytes, data bytes, and decoded chunks.

The H3 nested manifest reports `local_synthetic_status=pass` and `full_gate_status=not-run`; all seven H3 commands, including the public-source validator, exited 0. H7 synthetic checks also passed. The combined runner returned 2, and the top-level evidence manifest audit exited 0. The nested manifest pins the sample, source, coordinate-reference, CLI, extension, SQL, report, CSV, and metrics hashes. This remains partial local evidence: the full OM axis-to-producer point order, Gaussian N-grid positions, H1/H2 full public-source cases, and remote/cross-URI comparisons remain open.

## 2026-10-08 all public source positions

The latest matched baseline H3/H7 runner at [the all-position run](us5-20261008-all-public-source-positions-5dac993-h3-local/) invokes `grid_spatial_reference.py --validate-public-sources --all-public-source-positions`. The command streams all spatial points at time index 0 for the three hash-pinned local public projected samples: 1,191,300 rotated GEM positions, 770,440 stereographic GEM positions, and 881,361 Lambert positions. Explicit-grid and registered-domain source identities match row by row. Their pinned Float32 coordinate comparisons are within the frozen `1e-4°` threshold; the largest error is `3.0517578125e-5°`. All source, grid-info, value, index, and decode read counts are zero. H3's 18 zero-I/O scenarios, native/SQL source identity checks, grid-info SQL check, public all-position validator, and four synthetic H7 relation/source-position checks exited 0.

The full H3 and H7 gates remain `not-run`; the combined harness returned 2 and the evidence manifest audit passed. The OM arrays do not embed axis names, so this result still relies on the pinned producer README's `[ny,nx,ntime]` convention and does not independently prove full OM axis-to-producer point order. Required Gaussian N160/N320/N320-region objects, H1/H2/H6 public-source coverage, and remote/cross-URI checks remain open. The validator's synthetic `valid_times` select only time index 0 and are explicitly not producer time values.

## Registration and disposition

The H7 runner is registered in CMake and `scripts/validate.sh`; the quickstart points to this current-build evidence. H3 local source/info checks are included in the same runner. Earlier runs against build ID `62ee9b4ccc0603f6dc27ebc9f8adef0a9eb032964de335cd5e2ade330a9889a2` remain preserved in their original directories; this run rebuilt and recorded the checks against the current pair artifact.

T071 is complete as an implementation/evidence-registration task. T070 remains open for missing public-source coverage across H1/H2/H6, full cross-URI source evidence, and independent producer axis-order validation. No H0–H9 gate or producer support level is promoted by this record.
