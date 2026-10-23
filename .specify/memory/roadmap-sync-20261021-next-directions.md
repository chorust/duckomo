# Roadmap Sync Report: DuckOMO

> 2026-10-22 supersession: 001 SC-006, 003 R21/R22 and 004 C004-01–04 have closed within their approved scopes. See the [current ledger](roadmap.md) and [004 final H0–H9 evidence](../../specs/004-multi-grid-selection/evidence/current-20261021/report.md). The reconciliation and proposed next steps below retain their original 2026-10-21 meaning.

**Date**: 2026-10-21
**Roadmap**: [ledger](roadmap.md)
**Verdict**: PROCEED WITH UPDATES

## Summary

Read-only reconciliation of six populated spec directories, the ledger, current task indexes, product roadmap and retained final reports at HEAD 6b34e52. Only this report is added; no lifecycle or implementation changes. No builds, tests or artifact-hash audit rerun.

| Divergence type | Count |
| --- | ---: |
| orphan-spec | 0 |
| phantom-entry | 0 |
| dependency-contradiction | 0 |
| superseded-ADR | 0 |
| roadmap-stale | 1 |

No ADR directory is configured on disk. No lifecycle change is proposed: 001 implemented; 002, 005 and 006 verified within their declared scopes; 003 and 004 in-progress.

## Findings

| ID | Severity | Category | Finding | Suggestion |
| --- | --- | --- | --- | --- |
| F1 | Recommendation | roadmap-stale | The spatial computation proposal still describes nearest and method as unimplemented in its opening/example text, although 006 is accepted. | Refresh current/future wording when preparing the next spatial spec; retain the proposal's historical design provenance. |

The previous sync report is a historical snapshot: its 006 orphan and 005 current-build acceptance gaps were subsequently resolved. Do not reuse its priority list without the later ledger and 005 current-20261020 report.

## Evidence and boundaries

- [003 current tasks](../../specs/003-dimensions-remote-parallel/tasks.md): R21 independent HTTPFS reproduction and R22 final mapping remain open. R22 must identify any residual requirements; closure is not automatic after R21.
- [004 current tasks](../../specs/004-multi-grid-selection/tasks.md): C004-01–04 cover complete projected/O1280 gates, per-grid remote evidence, three-version H8 and independent H9. Skipped N-grid real samples, removed custom cache/ABI and exited 2.0 are not restored.
- [005 current acceptance](../../specs/005-polygon-region-statistics/evidence/current-20261020.md): P0–P7 pass for fixed windows, three versions and AArch64, with explicitly independent AI P7 and approved non-increasing costs.
- [006 final](../../specs/006-region-nearest-sampling/evidence/final.md): N0–N7 accepted for synthetic regular spherical grids and fixed GFS025, with independent human N7. Later 005 regressions cover shared nearest paths, not a claim that every 006 gate was rerun on HEAD.
- [Performance experiment](../../specs/005-polygon-region-statistics/evidence/contiguous-rectangle-performance-20261020.md): rectangular batches can reduce decoding while increasing physical bytes; some real small windows are unchanged. Nearest remains exhaustive.

## Recommended Actions — proposals only

1. Close 003 R21/R22 first, checking artifact/source applicability before reuse. Prepare 004's current-scope gate inventory alongside this work; complete C004-01–04 as the reliability track. Optionally reuse the independent session to address 001 SC-006, with separate assertions and records.
2. Default next product spec: narrowly scoped regular-grid bilinear in read_om_regions. Reuse target/layout/reader paths; freeze a single explicit variable, DOUBLE output, up to four sources, boundary/seam/NULL/zero-weight rules and post-computation WHERE semantics. Validate independent affine fields, source/weight identity, bounded I/O, points/nearest compatibility and lifecycle. These are proposed scope choices, not an approved contract.
3. Alternative product priority: series if cross-object time windows are the dominant user workflow. Start with one frozen model/layout and explicit time and missing-object policies; compare with manual UNION ALL. Do not silently adopt the draft's skip-404 default. Polygon-series composition needs its own explicit scope.
4. Benchmark many-region nearest before optimizing its locator; preserve numeric policy, deterministic ties, domain and source identities against exhaustive references.
5. Defer Line, area-weighted computation and broad scientific operators until a concrete use case selects them. Area computation requires support-cell, physical-area, variable-quantity and missing-coverage semantics that point sampling does not establish.

Suggested sequence: 003 closure → small bilinear spec/implementation, with 004 validation progressing separately → choose series or further spatial work from actual workflow demand. No new numbered spec is created by this report.
