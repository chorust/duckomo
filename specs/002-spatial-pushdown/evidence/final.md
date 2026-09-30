# Phase 3 implementation and acceptance status

Date: 2026-09-29. The spatial feature passes all correctness, I/O, sanitizer, and independent quickstart reproduction gates in the current Linux AArch64 scope. Per the user's scope decision, Linux x86_64 support and validation are deferred; no x86_64 compatibility is claimed.

This record covers the original Phase 3 acceptance with `ncep_gfswave025`. The later 68-domain registry and its separate sample audit are documented in [regular domains](../../../docs/regular-domains.md); the original acceptance results below do not cover every newly registered domain.

## Requirements mapped to evidence

| Requirement | Evidence and result |
|---|---|
| FR-001–003: explicit grid/domain, axis/layout validation, fail-fast errors | `spatial.test` (203 assertions); [US1](us1.md); [SQL contract](../contracts/sql-interface.md). Unknown domains, shape/axis conflicts, invalid values and name collisions reject before output. |
| FR-004–005: stable coordinates and no guessing without configuration | [domain](domain.md), [US1](us1.md), no-grid SQL regression. All domain positions match the independent coordinate formula within `1e-9` degrees; no-grid schema remains unchanged. |
| FR-006–008: exact predicate semantics and seam fallback | [US2](us2.md), [US3](us3.md); SQL pushdown/composition tests. The full WHERE remains in DuckDB; seam OR is exact and marked fallback. |
| FR-009: zero value reads for empty/coordinates/cardinality | [US2](us2.md) and complete per-variable v2 sidecars. |
| FR-010–011: mixed dependencies, layout, extra axes and multiplicity | [US3](us3.md); native selection/I/O tests. Temperature and humidity are read for the mixed query; unrelated pressure reads/decode are zero. |
| FR-012–013: reproducible metrics and strict physical-read gate | [US2](us2.md), [US3](us3.md). The same temperature column reads 165,767 full-scan bytes versus 1,795 restricted bytes and decodes 503 versus 5 chunks. |
| FR-014: traceable real domain and independent oracle | [domain](domain.md), [US3](us3.md), pinned [domain manifest](../../../test/data/domain-manifest.json). Registry and equivalent explicit grid match all 1,038,240 positions and all 15 official-value references. |
| FR-015: user instructions and product docs | [Quickstart](../quickstart.md), [README](../../../README.md), [English README](../../../README.en.md), [product spec](../../../docs/spec.md), [architecture](../../../docs/architecture.md), [roadmap](../../../docs/roadmap.md), and the successful independent [quickstart review](quickstart-review.md). |
| FR-016: failure/cancel cleanup and recovery | [US3](us3.md), [sanitizer](sanitizer.md). Corruption/cancel paths fail without success metrics and a later query succeeds. |

## Success criteria

| Criterion | Current result |
|---|---|
| SC-001: explicit and verified-domain full-position correctness | Pass on AArch64. |
| SC-002: layout, boundary, seam and batch coverage | Pass on AArch64 spatial SQL/native tests. |
| SC-003: strict reduction in bytes and decoded chunks | Pass on the pinned multi-chunk `projection.om` sample only. |
| SC-004: zero value reads in the three required scenarios | Pass; every variable has explicit zero counters and complete decode accounting. |
| SC-005: composition, safe fallback, errors and recovery | Pass on AArch64 release and sanitizer checks. |
| SC-006: independent user reproduction | Pass on Linux AArch64. A verifier not involved in implementation reran the explicit grid, real domain, regional multiset comparison, and I/O checks; see [quickstart review](quickstart-review.md). |

## Pinned identities and platform results

- DuckDB: v1.5.4, commit `08e34c447bae34eaee3723cac61f2878b6bdf787`.
- OM C library: commit `d8855e418e2231ae8439f0c7e840fa3f93b371e3`.
- extension-ci-tools: commit `b777c70d30942cca5bef62d6d4fa23a13362f398`.
- Synthetic performance fixture: `projection.om`, SHA-256 `fb36284bf8f932c82b171c7bce28b6e767b95321d1d986dc0a1dbd3b84d88a43`.
- Real domain sample: 5,812,040 bytes, SHA-256 `0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd`.
- The upstream commit `34b9cea169395be9b4686f2b5b23eca26dfef7a2` is the traceable grid-definition source. It is not asserted to be the exact deployed producer commit which generated the sample.

[Linux AArch64 evidence](linux-aarch64.md) records `make release`, `make test`, the synthetic fixture regeneration/hash gate, the complete domain harness and SC results; all exited 0. [Sanitizer evidence](sanitizer.md) records eight passing ASan/UBSan tests. The independent quickstart review also passed on Linux AArch64. T042 was resolved by the user's scope decision: Linux x86_64 is deferred and has no compatibility claim.

## Residual scope and failure records

The registry contains only `ncep_gfswave025`; other domains, time/level/member mappings, Gaussian and projected grids, remote reads, and parallel scanning are outside this implementation. Read savings depend on physical OM chunk layout and are not promised for every file. Release performance values must not be taken from sanitizer runs or compared across architectures.

During implementation, an early evidence-validator test rejected its synthetic fixture because the byte totals did not reconcile, and the first expanded harness run applied projection-variable names to the 15-variable domain count sidecar. Both causes were corrected; the final `make test` plus domain harness and the final evidence helper checks pass. There are no known remaining test failures.

T042 is deferred by scope decision, not executed. T045 passed independently on Linux AArch64. T046 is complete for the current scope; this feature is accepted for Linux AArch64 only, with x86_64 support and validation deferred.
