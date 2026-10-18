# Post-run documentation reconciliation — 2026-10-17

This note records documentation changes made after the registered N0–N4 run. It does not change or extend that run's acceptance scope.

## Immutable run package

- Package: [`run-20261017-sampling-validation-final`](run-20261017-sampling-validation-final/)
- `manifest.json` SHA-256: `a5f045e417d2ad3a5e109561feecee0a31cad9442841e3d84c284c852eb2f589`
- `inputs.json` SHA-256: `576ee6c44a87a8270475929cc7f538a1024af502d47580b4307ffadd5f512061`
- `commands.jsonl` SHA-256: `da6cc7b64d75913a9f9aaae4ffc3a06022c3f61775eb8439b4e5fc231a5ef8b2`
- The manifest records N0/N3/N4 `pass`, N1/N2 `blocked`, N5–N7 `not-run`, and overall `blocked`.
- The run package and its manifests, commands, stdout, and generated results were not edited during this reconciliation.

`inputs.json` is a point-in-time source identity. Its hashes for documentation files describe their versions when the run started; several explanatory files have since changed. The current documentation is therefore not claimed as part of the captured source identity or as separately executed gate evidence.

## Current implementation and test identities

The implementation and test sources used by the relevant registered checks were not changed after capture. Current hashes match the run's recorded source inputs:

| File | SHA-256 |
| --- | --- |
| `test/sql/region_sampling.test` | `e39181f340425b35d88ea1d3864f5bf52d1b026f925cbcafbce26ad43586cdab` |
| `test/native/region_sampling_test.cpp` | `993e7d1af69c6ea4f2e0f82b090e342eb753db276749c478b7fa404c02ac6fd3` |
| `src/include/duckomo/metrics.hpp` | `67c69e360fe1c0c3ad3a0ee6629460fc2cadc0483016e19c508c00699d32ced4` |
| `src/scan/read_om.cpp` | `65b9d355a8c3325dbec6a4e00470f8e221064db81b108e75c1166276de02b8fc` |
| `src/grid/nearest_sampling.cpp` | `becfae16493b6f65a27c2652b1b9a3f4ef34fa7d0767f242cfde64b1df55e650` |
| `src/include/duckomo/nearest_sampling.hpp` | `c3caadc153d6ff0f1f92a121f5867e707b0f32bfa503e6e88c7a2f895530eadf` |

## Reconciliation made after capture

The following files received explanatory/status corrections to reflect the captured run, the implemented interface, and the remaining acceptance boundaries:

- Feature documentation: `spec.md`, `plan.md`, `tasks.md`, and `quickstart.md`.
- Contracts and reports: `contracts/sql-interface.md`, `contracts/validation-evidence.md`, `contracts/execution-and-metrics.md`, `evidence/N0-points-baseline.md`, `N1-locator.md`, `N2-sql-matrix.md`, `N3-io.md`, `N4-resources.md`, `T003-T004-fixture-generation.md`, `T005-real-inputs.md`, `US1-core.md`, `US2-compatibility.md`, `US3-states.md`, and `US4-projection.md`.
- Product documentation: `README.md`, `README.en.md`, `docs/spec.md`, `docs/architecture.md`, and `docs/roadmap.md`.

The project governance ledger `.specify/memory/roadmap.md` and roadmap extension state were left unchanged. The post-run edits correct stale descriptions and link the existing run package; they do not claim that the documentation itself was tested by that package.

## Acceptance boundary

N3 and N4 pass only for their registered local scopes described in their evidence files. N1 remains blocked on exhaustive binary64 rejection/profile evidence; N2 remains blocked on complete profile/input acceptance. N5–N7 were not run. Real input provenance and redistribution terms remain unresolved. These documentation updates do not promote any of those statuses or claim complete 006 delivery.
