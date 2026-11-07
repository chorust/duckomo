# Acceptance Contract: read_om_series

**Status (2026-11-06 follow-up)**: Current-source acceptance is **PASS for the declared scope only**: Linux AArch64 (`linux_arm64`) and pinned DuckDB v1.5.4, v1.5.5, v1.5.6 and v2.0.0. The real producer result covers two adjacent IFS025 `temperature_2m` objects, one point and one boundary window; the other ten registered models have provenance plus synthetic runtime equivalence only, and N160 is synthetic. `read_om_series` remains development-source-only, not a released feature identity. Feature 008 is not PASS; SC-006 remains relaxed / will not meet the original ≥20% gate, and T042/T043 remain unchecked. See the [follow-up acceptance report](../evidence/acceptance-20261106-followup-linux-arm64/report.md) and [manifest](../evidence/acceptance-20261106-followup-linux-arm64/manifest.json). The earlier 2026-11-06 report records the preceding incomplete run; the 2026-08-03 report is historical evidence.

## Gates and traceability

| Gate | Scenario / required assertions | Requirements | Success criteria | Primary implementation tests / artifacts (planned) |
|---|---|---|---|---|
| S0 | Binder/operator seam compiles on each pinned engine; frozen source mapping and generated registry reproducible; source/artifact identities captured | FR-003, FR-014, FR-016–017 | SC-004, SC-007 | `test/native/series_binding_test.cpp`, `test/tools/series_registry_test.py`, `test/data/series/models.json`, manifest |
| S1 | One/two/three-chunk closed windows, endpoint form equivalence, exact boundary, unaligned/single instant/negative times, NaN vs absent; full schema and multiset oracle | FR-001–005, FR-010 | SC-001–002 | `test/sql/read_om_series.test`, `test/native/series_plan_test.cpp` |
| S2 | All invalid option/prefix/window/interval/overflow/count cases rejected before access; time metadata conflicts, dimensions/time length, schema and spatial layout mismatch rejected | FR-002–007, FR-012, FR-014–015 | SC-002, SC-007 | `test/sql/read_om_series_validation.test`, native planner/binding tests, zero-access audit |
| S3 | First/middle/last/all-missing × skip/strict × transports; typed empty schema equal to present schema; strict policy survives outer LIMIT/WHERE | FR-008–009, FR-012 | SC-003 | `test/sql/read_om_series_missing.test`, `test/tools/series_remote_test.py`, controlled origin audit |
| S4 | Injected non-absence faults fail rather than being silently skipped; structured categories are recorded where the transport supplies them. Under official HTTPFS, HTTP(S) 403/412 surface as unknown-status `[transport]` errors. The controlled `corrupt` fixture returns HTTP 200 with `not-an-om-object` and fails at range-protocol validation; it does not exercise corrupt OM metadata parsing or decode failure. Post-bind replacement/disappearance, redaction, cancellation and recovery are also checked | FR-008–009, FR-016 | SC-003, SC-007 | `test/native/series_error_test.cpp`, controlled remote test, existing remote-session/HTTPFS tests |
| S5 | Existing regular and reduced-Gaussian fixture/domain or explicit-grid equality, spatial/time/value/residual predicates, source identity, column prune, value-free counts, 1/2/4 workers | FR-006–007, FR-010–011 | SC-001, SC-005 | `test/sql/read_om_series_pushdown.test`, `test/tools/validate_series_test.py`, validation runner |
| S6 | Distinct executed child scan_ids/counters, attempted/missing vs executed distinction; no new metric schema/summary and no relabelled missing success | FR-011, FR-013 | SC-006 | pushdown/metrics SQL and controlled origin audit |
| S7 | Actual linux_arm64 compatibility pairs and existing scanner regressions; source-bound validation records, optional real public adjacent objects frozen and compared | FR-016–017 | SC-001–007 | `scripts/validate-series.py`, `specs/010-read-om-series/evidence/<run>/` |

S0 through S7 passed for the recorded four-pair Linux AArch64 matrix. S3/S4 passed using current-source controlled transports: injected non-absence faults failed rather than being silently skipped; HTTP(S) 403/412 surfaced as unknown-status transport errors under official HTTPFS, and the controlled `corrupt` response failed at range-protocol validation without exercising OM metadata/decode corruption. SC-004 passed for all eleven registered entries with no discovery requests, and S7 passed through the full local regression workflow, official HTTPFS diagnostics and frozen real-object comparison. The [follow-up report](../evidence/acceptance-20261106-followup-linux-arm64/report.md) identifies the tested real-data scope and platform limits. These results do not establish Feature 008 completion, another architecture, or broad producer-data support.

## Required fixture matrix

Future deterministic generator `scripts/generate-series-fixtures.py` uses the existing official OM fixture-writing support, not a new decoder. Commit fixture definitions/manifests and reference data under `test/data/series/`; generate build-local objects into `build/series-fixtures/data/synthetic/temperature/`.

- Three adjacent root Float32 chunks 0000/0001/0002 with shape `[1,6,4]`, dimensions `['lead','point','time']`, dt=1h. A 2×3 explicit regular grid has x-fastest flattened point order: latitudes 10,11; longitudes 100,101,102. Define finite values by an independent deterministic rule `1000*n + 100*p + i` and include a documented NaN slot. Use the pinned official writer and retain compression tolerance/reference round-trip rules; test exact series-vs-read_om equality on the identical bytes.
- Gap variants remove first, middle, last or all three objects from separate prefixes. Do not delete a fixture used concurrently by another test. Preserve a present set for schema comparison. Include a present-but-all-NaN variant to distinguish no rows from NULL values.
- Time vectors include exact boundaries at 04:00/08:00, `[03:00,05:00]`, a three-chunk range, singleton `[04:00,04:00]`, slotless `[03:00:00.000001,03:59:59.999999]` and negative epoch windows with signed padded keys.
- Malformed/nonhomogeneous variants cover wrong L, missing/extra time dimension, rank/non-time extent/order differences, selected array names/types, conflicting embedded time metadata and spatial identity mismatch. Also include logically homogeneous objects with different physical OM compression block shapes, which must remain accepted.
- A named-array manifest fixture exercises complete dimensions maps, deterministic array order, variable selection, duplicates/aliases and root naming. Shared-list multi-array calls are rejected before execution.
- Existing reduced-Gaussian fixtures qualify only their current synthetic grid scope; add a compatible multi-chunk derivative through the same writer without changing the grid definition or claiming producer validation.

A raw shared dimension named `lead` is not a request for semantic lead_time output. Only generated time is added by series.

## Independent oracle and comparisons

The oracle must enumerate known chunk indices and supply each origin/time_step explicitly. It must not call the new registry resolver, expansion helper or series planner to decide expected indices/timestamps. Compare schema names/order/types and full row multisets (including NULL equality, duplicate rows and om_source contents where applicable); use EXCEPT ALL in both directions or a stable canonical row representation, not only COUNT/hash or a sort that loses duplicates. Row ordering is checked only when SQL requests ORDER BY.

For the regular fixture, independently compute timestamp and point/value expectations as an additional sanity check. The manual baseline still uses the official read_om decoder; no new scientific decoder is permitted.

## Remote and object-identity checks

Run the same frozen files through local, controlled HTTP, controlled HTTPS with a verified local certificate, and controlled S3-compatible service using matching official HTTPFS. Use a series-specific fixture adapter and existing fixture primitives; leave 008's benchmarks and evidence untouched. Retain sanitized origin request logs sufficient to prove no listing, no meta.json and no unrelated object probes, and zero requests on pure argument errors.

Initial-open 404 is skippable only with structured status. A server returning a body containing “404” with HTTP 403/500 or no structured status must fail. Initial absence is not an identity event after binding. Freeze a deterministic execution barrier/hook in the test harness to replace or remove an object after successful bind, then execute and require existing identity/read failure without silent restart. Compare ordinary read_om category and sanitized public text on the same fault. Keep direct size/version change and HTTP 412 cases distinct because existing messages differ.

Existing HTTPFS may own retries: audit that series adds no query/chunk rebind retry, and distinguish origin transport retries from a series restart. Cancellation and error cleanup must permit a subsequent successful query and preserve isolation of two sessions. Missing attempts must not acquire value decoders or successful value-read records.

## Performance and metrics boundaries

No new speedup percentage, warm-cache target or cross-chunk physical coalescing gate. For equivalent queries, compare per-child semantic selection and actual value index/data bytes/decodes against manual read_om using identical source/settings/filters; investigate increases caused by failed pushdown instead of masking them in an aggregate. Coordinate/time-only and COUNT(*) without value predicates require exactly zero value index/data reads and decodes in every executed child. Bind metadata reads and transport-specific metadata overhead are allowed and must be reported separately.

Record query_id, scan_id, operation/status, selected variables and existing counters from `duckomo_last_scan_metrics()`. Do not assume all candidates executed: outer empty filters/LIMIT/optimizer elimination can reduce execution. Missing initial opens may retain failed bind metrics under the current lifecycle. Demonstrate a full-consuming query with at least two executed leaves, then an elimination case, and do not fabricate aggregate or missing-success metrics.

## Platform / version / evidence rules

Primary and only declared acceptance platform: **Linux AArch64 / linux_arm64**. Planned pair list: v1.5.4, v1.5.5, v1.5.6 from `test/data/grids/version-matrix.json`; v2.0.0 from `test/data/engine-compatibility/version-matrix.json`. Resolve exact pinned commits/runtime hashes from those files during the later run; do not mutate their release state, invent runners or imply 008 completion. If a pair or a controlled transport is unavailable, mark its cell blocked and the affected gate incomplete.

For each actual run capture command/exit status, UTC start/end, host/arch, engine/OM/toolchain commits, source snapshot (including dirty diff identity), extension/runtime hashes, fixture/model-registry hashes, query/oracle text hashes, rows/diffs, metrics, sanitized server logs and limitations. Store actual run artifacts only after execution under `specs/010-read-om-series/evidence/<run>/`. Status vocabulary: pending, blocked, failed, passed; a passed cell links to actual output. Never backfill a synthetic PASS.

A real producer validation is required before claiming real `data/` support: freeze at least two adjacent available chunks from one registered model, their exact keys, identities, parameter provenance and manual-union result. Report separately from controlled fixtures, keep large/private inputs under existing retention rules, and leave this subgate blocked if public data is unavailable. The Phase 8 chunk_817/921/922 anecdotes provide no acceptance credit.

A final review must map each FR and SC to executed checks and retained outcomes. Independent reproduction, if subsequently performed, records the actual independent actor and pair/platform; this planning session makes no such claim.
