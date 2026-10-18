# N3 projection and value I/O evidence

**Status: N3 registered local acceptance passed.** The latest selected gate run records N3 `pass` for the complete cross-version SQL/native projection, residual-filter, geometry/count zero-value-I/O, and same-object full-read matrix. N1/N2 pass for their declared profile scope. Overall 006 acceptance remains incomplete because N5 is partial and N6 is unfinished; the existing local N7 reproduction was run by an implementation worker and does not meet the independent-executor requirement.

## Reproduction package

- Earlier run: [`run-20261017-n0n3n4`](run-20261017-n0n3n4/). Latest matching gate package: [`run-20261017-sampling-validation-final`](run-20261017-sampling-validation-final/); focused N0/N3/N4 replay: [`run-20261017-n0n3n4-final`](run-20261017-n0n3n4-final/).
- Runtime: Linux AArch64, DuckDB v1.5.4; sampling SQL and native checks dispatched to the matching v1.5.4, v1.5.5, and v1.5.6 build trees and spatial package hashes.
- `region_sampling.test`: 176 assertions passed on each pinned build after the nearest metrics refresh.
- `region_sampling_test`: passed on each pinned build after metrics/status assertions were added; each also passed as UID/GID 65534 in an Ubuntu 24.04 container with a read-only worktree mount.
- Current DuckOMO extension SHA-256: v1.5.4 `a9aec833be443d481a0ce583ec529eea6f58fd97aeab16c42149ac64b359e7fa`; v1.5.5 `94d87241f57df140970b95bfa3e0c46476da76df4c3d8ba33ace3ba5bada6520`; v1.5.6 `75ecd0c5c273f6793e4c01ac38a058e720ec944b60b949600c31c88c91273824`.
- Runtime matrix spatial SHA-256 values: v1.5.4 `af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a`; v1.5.5 `b9129a7da7fde8eb7fb951d99a115cbf2e6fa34c8d41671086169174a1f5725d`; v1.5.6 `96b8d4b5d735e9ebe91da7badee4328dfbd07068ce4885d1d9b6de8c22370b14`.

## Projection matrix

The native projection ledger ran the same fixed 5×5, 12-axis-row source and reports actual successful variable I/O:

| Projection | Index bytes | Value bytes | Decoded chunks | Result |
| --- | ---: | ---: | ---: | --- |
| `COUNT(*)` / geometry fields (`sampling.status`, source identity) | 0 | 0 | 0 | 12 rows; exact geometry leaves retain their declared types |
| Full `sampling` STRUCT | 27 | 1,774 | 24 | Both bound variables read to provide actual status for every array |
| `sampling.variables` MAP | 27 | 1,774 | 24 | Both bound variables read |
| Variable-qualified status MAP | 13 | 864 | 12 | Only selected temperature array read |
| Direct temperature value | 13 | 864 | 12 | Humidity has no value index/data/decode I/O |
| Duplicate `sampling.status` leaf | 0 | 0 | 0 | Repeated projected leaf preserves values without extra value dependencies |

SQL assertions independently check the geometry/COUNT zero-I/O case, complete STRUCT dependency, variable-qualified MAP dependency, and duplicate status projection. The existing target/value filter query compares the pushed result with an independently materialized complete nearest scan filtered afterward using bidirectional `EXCEPT ALL`; all three pinned SQL suites passed.

## Same-object full-read comparison

`skippable-values.om` is the same immutable object for both queries; it contains one 1024×1024 FLOAT array with 16×16 chunks. Nearest consumes its selected value; the baseline aggregates every value from the whole array.

| Query | Value bytes | Decoded chunks |
| --- | ---: | ---: |
| Nearest selected value | 224 | 1 |
| Full-value baseline | 15,449,547 | 66,432 |

Both bytes and chunks are strictly lower for nearest on all three pinned builds. Official reader CSV and OM object hashes are bound through `test/data/sampling/manifest.json` and `om-fixtures.json`; `scripts/check-sampling-fixtures.py` passed with 16 OM fixtures and 29 variables (9 geometries and 62 nearest references).

## Source and command evidence

The historical run [`manifest.json`](run-20261017-sampling-validation-final/manifest.json) and `commands.jsonl` bind argv, exit code, duration, output SHA-256, source HEAD plus dirty-patch identity, selected runtime, extension and spatial package hashes. Manifest SHA-256 is `a5f045e417d2ad3a5e109561feecee0a31cad9442841e3d84c284c852eb2f589`; input manifest `inputs.json` SHA-256 is `576ee6c44a87a8270475929cc7f538a1024af502d47580b4307ffadd5f512061`, and `commands.jsonl` SHA-256 is `da6cc7b64d75913a9f9aaae4ffc3a06022c3f61775eb8439b4e5fc231a5ef8b2`. That run recorded N0/N3/N4 `pass` and N1/N2 `blocked` at that time; the current N1/N2 scoped disposition is pass in [`n1-n2-acceptance-refresh-20261019-final-7`](n1-n2-acceptance-refresh-20261019-final-7/). N5 is partial: real-data/transport checks passed, but raw-input redistribution and durable archive terms are unresolved; N6 remains incomplete and the recorded local N7 reproduction does not meet the independent-executor requirement.

Relevant current source hashes at capture:

- `test/sql/region_sampling.test`: `e39181f340425b35d88ea1d3864f5bf52d1b026f925cbcafbce26ad43586cdab`
- `test/native/region_sampling_test.cpp`: `e49fb0264a5eb820cf75b4476e020e3689385cf34ce3c1636e50677bd2a91325`
- `src/include/duckomo/metrics.hpp`: `67c69e360fe1c0c3ad3a0ee6629460fc2cadc0483016e19c508c00699d32ced4`; `src/scan/read_om.cpp`: `65b9d355a8c3325dbec6a4e00470f8e221064db81b108e75c1166276de02b8fc`.
- The latest N3/N4 package is `run-20261017-sampling-validation-final`; its N3 commands are N3-35/36, N3-40/41, and N3-45/46, all exit 0. The focused N0/N3/N4 replay is `run-20261017-n0n3n4-final` (manifest SHA-256 `444ed37c3d03a657b718c3eb7e8e1680252bf446604c85470f04e131189762f6`). No clean release build identity is asserted.

## Remaining acceptance

This evidence establishes the full N3 scope registered in the validation contract, including local projection dependencies, complete residual equivalence, and selected-value I/O reduction across the three pinned runtimes. N3 passes in the latest manifest. N4's resource/lifecycle evidence is recorded separately; N5 remote acceptance and N6 remain open, and the recorded local N7 reproduction does not meet the independent-executor requirement.
