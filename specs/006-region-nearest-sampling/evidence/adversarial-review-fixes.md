# Adversarial review fixes

This repair pass addresses five P1 findings and three P2 notes from three independent fresh-context reviewers. Historical evidence packages are preserved. Their passing binaries did not establish that the source present at review time passed: a temporary clean locator build failed while the old binary and N1 validator passed.

## Changes

- Account for two framing bytes per 128 elements in the supported FPX/PFor compressed-chunk bound, with checked arithmetic and the existing parser padding. The pinned FPX encoder uses 128-element blocks, adding a predictor byte before a PFor representation whose selected encoded size is bounded by raw elements plus a header. An official-encoder 2048×2048 finite random Float32 chunk exercises the former bound failure and now decodes with exact first-value bits.
- Compute regional longitude endpoint spans and target offsets at consistent precision. Fractional origins/steps, both axis directions, closed endpoints and adjacent outside values have regressions. Classify global coverage independently of latitude axis direction.
- Reject a temporal mapping onto a spatial axis when binding nearest, including metadata/valid_times mappings. COUNT and valid_time projections both fail at bind. Points behavior is retained.
- Reconcile capability reference qualification with the contract: exact periodic closure and the first-to-last regional span are separate checks. Two previously rejected cases are legal regional grids; the 377-case profile now has 203 accepted and 174 rejected cases. Reference CSV and manifest hashes were updated explicitly.
- Remove the second account for the same task vectors. The single-thread boundary workload verifies that its 81,920-byte vector capacity is charged once.
- Resolve runtime build directories through their manifests. `scripts/sampling_build.py` builds the actual test/extension targets, rejects source changes during the build, and records source digest, engine commit and artifact hashes. Validation rejects missing/stale/replaced artifacts and records the build identities and executable hashes. Matrix builds and sampling-validation integrate this capture.
- Describe N5 honestly: the validator currently records blocked status and executes no real/remote subchecks; actual reproduction commands remain in N5-dongcheng.md.

## Validation

- Current-source reader-capacity regressions pass on DuckDB v1.5.4, v1.5.5 and v1.5.6.
- The evidence test suite passes 15 tests, including stale-source/replaced-artifact rejection and mid-build source drift.
- The fixture checker passes for 16 OM fixtures, 29 variables, 9 geometries and 62 nearest references.
- Current-source v1.5.4 sampling SQL passes 212 assertions. The three-version N0–N4 run captures the full SQL/native/lifecycle/memory matrix and the v1.5.4 ASan/UBSan check.

The first repair run, `build/evidence/sampling/adversarial-fixes-20261017/`, passed N1–N4. All N0 rows matched, but physical LUT reuse reduced index requests below the old frozen counters in two versions. The comparison now requires exact logical work, value-data costs and decoded counts, while physical index reuse and corresponding legacy total reads may decrease but must not exceed the frozen baseline. A regression test rejects increased physical costs and changed logical/value-data work. The final rerun is recorded separately in `build/evidence/sampling/adversarial-fixes-20261017-final/`; its terminal manifest reports N0, N1, N2, N3 and N4 all pass. `inputs.json` records source and artifact identities.

N5 remains partial, N6 is incomplete and N7 still requires an independent executor. This repair pass does not claim overall Feature 006 acceptance.
