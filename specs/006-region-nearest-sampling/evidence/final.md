# Feature 006 acceptance status

**Overall status: not accepted.** This report reconciles the current evidence available on 2026-10-19. Gate passes apply only to their documented scopes; partial subchecks do not promote a gate to pass. Historical packages remain unchanged.

## Adversarial review repair

A later adversarial review found that the previous validator could run stale binaries, and a clean current-source locator build failed despite the historical N1 pass. Historical packages below remain records of those executions, not proof for newer source. The [repair report](adversarial-review-fixes.md) records the code, oracle, build-identity and documentation corrections, plus the new three-version N0–N4 rerun. Its final local package is `build/evidence/sampling/adversarial-fixes-20261017-final/`; source and per-artifact hashes are bound in its inputs/build manifests. The subsequent private-scope [N5 execution](N5-remote.md) and [N6 audit](N6-audit.md) pass; N7 remains partial.

## Gate summary

| Gate | Current disposition | Evidence and boundary |
| --- | --- | --- |
| N0 — points baseline | Pass for the registered local replay | [`N0-points-baseline.md`](N0-points-baseline.md) and the historical three-version validation package. Frozen points rows and stable metrics match. |
| N1 — locator | Pass for synthetic regular spherical grids and the fixed NCEP GFS025 profile | [`N1-locator.md`](N1-locator.md) and the current scoped acceptance package. The finite capability matrix and 12 fixed GFS025 targets do not claim arbitrary binary64 or exhaustive target-space coverage. |
| N2 — SQL and input contract | Pass for the same declared profile scope | [`N2-sql-matrix.md`](N2-sql-matrix.md) and the current scoped acceptance package. The selected SQL/native/parser matrix passes; exhaustive parser and precedence cross-products are outside the contract. |
| N3 — projection and value I/O | Pass for the registered three-version local matrix | [`N3-io.md`](N3-io.md) and the historical registered package. Geometry/COUNT has zero value I/O; nearest reads fewer bytes and chunks than a same-object full read. |
| N4 — resources and lifecycle | Pass for the registered local matrix | [`N4-resources.md`](N4-resources.md) and the historical registered package. Worker, bounded-memory, lifecycle, recovery and v1.5.4 sanitizer checks pass for the stated scope. |
| N5 — real data and remote | Pass for the owner-authorized private scope | [N5-remote.md](N5-remote.md) and [current package](private-n0-n5-final/final.md): current three-version real/synthetic local/HTTP/HTTPS/signed-S3, exact source parity, denial/recovery, private archive retrieval and artifact identities pass. Raw object is not publicly distributed; no independent hardware backup is claimed. |
| N6 — version matrix and audit | Pass for the registered scope | [N6-audit.md](N6-audit.md) and [audit package](private-n6-final/final.md) verify current three-version N0–N5 execution, source/artifact/query/output identities, transport inventory and private archive retrieval; rejection tests pass. |
| N7 — independent guide reproduction | Human manual checks passed; formal gate remains partial | [`N7-independent.md`](N7-independent.md) preserves the original worker run and links the subsequent [human v1.5.5 execution](user-manual-v1.5.5/results.md). The user's fresh-process source comparison returned PASS; initial quickstart spatial identity and complete user-captured run identities/logs remain unresolved. This does not close N5/N6 or overall acceptance. |

The latest passing scoped N1/N2 command run is [`n1-n2-acceptance-refresh-20261019-final-7`](n1-n2-acceptance-refresh-20261019-final-7/). An intervening `final-5` run is preserved with N1 pass and N2 fail after one v1.5.5 worker-count scheduling assertion; the exact same binary passed three of four immediate reruns, and the complete `final-6` matrix then passed. Its manifest and review record bind the exact commands, inputs, source snapshot and finite declared scope.

## Remaining work

N5 and N6 now have registered passing current-artifact evidence. T057, T061, T062 and T063 are complete for their stated scopes. N7 remains partial: retain a complete independent matching-runtime quickstart execution/identity record; the prior mixed-spatial session is not promoted to verified matching-runtime evidence. T058/T059 still contain unimplemented formal N7 evidence-interface/negative-test requirements; T060 and T065 await their full dependency and final delivery reconciliation. The private archive/retrieval is established outside build cleanup, without public redistribution or an independent hardware-backup claim.

The points default and the existing `read_om_regions` entry point remain the compatibility contract. No roadmap governance status is changed by this evidence report. Do not treat implementation completion, a partial subcheck, or a historical package's captured status as overall Feature 006 acceptance.
