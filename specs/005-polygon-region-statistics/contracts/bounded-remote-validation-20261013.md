# 005 remote validation scope amendment — 2026-10-13

## Authority and applicability

The owner explicitly authorized changing the validation workload: “可以修改，不用全域” after observing the long full-domain remote runs. This amendment governs the remaining 005 P5/P6/P7 execution. It supersedes requirements to execute a full-domain remote baseline in spec US3, FR-020, SC-004, validation-evidence P5/P6/P7 and the corresponding task/quickstart wording. It does not change runtime row semantics, geometry boundaries, input budgets, value/NULL handling, dependency isolation, or the five target grids and three pinned versions. Existing execution records remain historical records with their original scope.

## Revised P5 baseline

- Keep the five frozen objects, polygon WKB, projections, complete time/member dimensions and expected region-record counts from `test/data/regions/real-targets.json`.
- Freeze the larger bounding window and both SQL queries in `test/data/regions/remote-windows.json` before running the revised measurements. `scripts/freeze-region-remote-windows.py` expands each polygon envelope by max(span, 0.25 degrees) per side; O1280 uses a 1-degree minimum to cover additional dense polar rows. Clamp only at geographic limits. The formula is selected from geometry/grid scale, not observed cost results.
- Baseline: materialize ordinary `read_om` over that window, then apply `ST_Covers` to the original polygon. Optimizer filtering must not move the exact polygon filter ahead of the materialized baseline.
- Subject: `read_om_regions` over the same object and polygon, with the same window predicate. Output identical value/coordinate/region/source-index columns, deterministically ordered. No LIMIT, arbitrary record sampling, time truncation, or omitted value columns.
- Compare the complete two result files exactly, including multiplicity, original logical source positions and NULLs. Baseline and region row counts must both equal the original frozen region count. All local/HTTP/HTTPS/signed-S3 result hashes must agree.
- For HTTP and signed S3, the region query must strictly reduce value data bytes, decoded blocks and service-sent bytes relative to the larger window baseline. A failing comparison is retained as failure, not redefined after measurement.
- Each scan uses a fresh matching official CLI process and disabled client caches. Save successful complete terminal metrics, actual request counts and service-log hashes. All four sources are required.

P5 reports declare `comparison_scope=bounded_window`, `full_domain_scanned=false`, and `full_domain_savings_claimed=false`, and pin the remote-window manifest SHA-256. The legacy internal record key `full` identifies the baseline slot only; it does not mean a full-domain scan under this amendment.

## P6 and P7

P6 requires three-version P0–P4 evidence and revised P5 evidence using the same frozen windows. Its auditor must reject missing/changed window identity and must not label old full-domain or interrupted runs as revised-window success. Native/sanitizer regressions remain required. Already completed domain references remain valid evidence; no new full-domain remote scans are required.

P7 requires an independent verifier to execute the quickstart's multi-region raw query, per-time statistics, empty/all-NULL distinction and at least one fixed-window remote baseline/region comparison. An AI verifier must be explicitly identified as AI; its fresh session must not have participated in implementation. The implementer cannot self-sign this gate.

## Scope limits and retained performance issue

A pass demonstrates exact results and measured savings for the specified bounded windows only. It does not demonstrate efficient full-domain remote throughput or full-domain-versus-polygon savings. The former SC-004 full-domain performance scope is superseded for this delivery, not silently counted as passed.

The interrupted 2026-10-13 full-domain run exposed repeated small reads. `BuildSelectedBatchSegments` splits output at the last logical axis; each segment invokes `DecodeSelection` separately. The OM decoder can merge requests within that call (defaults merge=512 bytes/max=64 KiB), but not across independent calls. Remote handles use DIRECT_IO/NO_CACHING; repeated chunk/LUT reads are not absorbed by read-ahead or cache. For gem_rdps_10km, shape [1045,1140,114], chunks [1,26,114] gives 45,980 physical chunks, while the observed full scan recorded 1,456,260 decodes and the same number of index and data requests. This is an implementation performance limitation, amplified by cold tests, not an unmergeable/random-access fixture design. Bounded-window validation does not fix it; chunk-aligned batching/coalescing needs a separate performance change with its own correctness, memory and remote-cost validation.
