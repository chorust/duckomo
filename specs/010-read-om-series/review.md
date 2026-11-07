# Feature 010 code review

**Verdict: changes required before milestone acceptance.** Two reproducible contract violations remain in the final implementation. The existing S2 / SC-002 / SC-007 blockers are correctly disclosed, but they now include demonstrated failures, not only missing evidence. No fixes were implemented in this review.

## Review basis and scope

Reviewed the settled Feature 010 working-tree implementation against `spec.md`, `contracts/sql.md`, `contracts/acceptance.md`, task obligations, registered series tests, and the final implementation evidence. Scope includes series planning/composition/registry, declaration and binding helpers, series validation scripts, and the intentional shared reader seams. Existing 008/community changes and 009 drafts were treated as preserved baseline, not Feature 010 changes. No 007 or new 009 implementation was reviewed or started.

HEAD is `80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9`. Because the milestone is uncommitted within an already dirty tree, `git diff HEAD` is only a discovery aid; ownership is established by the implementation baseline and preservation record rather than attributing every dirty hunk to 010. The current extension source digest independently matches the implementation's final digest, `4974560d43df2a1abcc67cb24755f57b9da36df37a92df3d0f2d34fae3aec516` ([source check](evidence/code-review/source-check.json)).

Standards and spec axes were reviewed directly. This session exposes no subagent/model-routing tool, so the requested preferred worker route and parallel independent reviewers could not be selected. This report does not claim independent subagent consensus. The repository constitution remains an unfilled template, as the spec itself records; no additional project-owned coding-standards document was located in the scoped repository search. Vendored DuckDB contribution rules were not treated as new DuckOMO requirements.

## Standards

No separate actionable coding-standard or Fowler-smell finding was established in the reviewed Feature 010 changes. The wrapper reuses owned ordinary reader binds, generated time axes, exact output signatures and balanced logical UNION ALL; it adds no series decoder or transport retry loop. The shared cacheability flag defaults to false and is enabled only for retained series children. This is a scoped review conclusion, not a blanket certification of every existing reader path.

Preservation verification independently rehashed all 155 baseline files: 146 match the original baseline, nine match the expected final Feature 010 versions, and none are missing or unexpectedly changed. This corroborates the recorded preservation of 008/community and both 009 draft files. In particular, `src/om/reader.cpp` and `test/native/remote_async_test.cpp` still match their pre-010 hashes. The HEAD diff's planned-cache/async-accounting hunks must not be mislabeled as newly introduced 010 work. Details: [preservation check](evidence/code-review/preservation-check.json).

## Spec

### R1 — P1: Validate spatial declarations before opening children, including all-missing results

**Location:** `src/scan/read_om_series.cpp:49` (`ValidateSeriesSpatialPresence`), call at line 117, child binding at line 143, empty return at line 155.

The wrapper checks only presence, NULLs and mutual exclusion for grid/domain/spatial_axes. It never validates the supplied spatial definition independently of a successful object bind. The reused ordinary reader opens and reads metadata first (`src/scan/read_om.cpp:2555` onward), then calls `BindSpatialConfiguration` at line 2591. If every open is initially absent, the skip path returns a typed empty relation without ever parsing the grid or resolving the domain.

**Reproduced on v1.5.4, v1.5.5, v1.5.6 and v2.0.0, linux_arm64:**

- With complete dimensions, L=4, dt=1h and a valid window, `grid := 42, spatial_axes := ['point']` against a nonexistent prefix succeeds with `count(*) = 0`.
- `domain := 'review_nonexistent_domain'` against that same prefix also succeeds with `count(*) = 0`.
- Against the present regular fixture, `grid := 42` fails with `read_om grid must be a named STRUCT`, but each pair's `strace` records a successful `openat(.../temperature/chunk_0000.om, O_RDONLY)` before the error.

This turns malformed requests into apparently valid missing-data results and violates the no-object-access rule for pure argument errors. The contract requires spatial definitions accepted by ordinary `read_om`, including empty-only queries: `contracts/sql.md:86`, “Validate input syntax, registry arithmetic and spatial definition”; FR-014 requires argument validation before object access, and SC-002 requires every pure argument-validation case to fail before access. FR-006 and FR-012 are affected as well; their existing “Passed” rows cover the exercised valid cases but cannot establish complete compliance.

**Requested follow-up, not implemented:** reuse/expose metadata-independent spatial parsing and registry/axis-declaration validation before entering the child-open loop. Keep actual shape/CRS/metadata reconciliation in the existing child binder. Add invalid grid/domain/axis cases for present, all-missing and slotless requests, with zero-access traces or controlled origin audits.

### R2 — P2: Strict mode silently ignores slotless candidates

**Location:** `src/scan/read_om_series.cpp:132`, `if (candidate.slots.empty) continue;`.

The unconditional early skip bypasses child binding and therefore bypasses strict missing validation. `contracts/sql.md:66` is explicit: “The candidate interval follows floor(a/span)..floor(b/span) even when a particular child's requested subinterval contains no slot. Strict mode still validates that candidate's presence.” FR-008 likewise makes strict validation a bind-time obligation.

**Reproduced on all four linux_arm64 pairs:** L=4, dt=1h, `strict := true`, a nonexistent prefix, and the window `[1970-01-01 03:00:00.000001, 1970-01-01 03:59:59.999999]` return `count(*) = 0`, exit 0. Candidate 0 exists mathematically but its object is absent. The same request with a window containing slots correctly errors on missing `chunk_0000.om`, confirming the strict-mode control.

The defect also applies to a slotless first candidate followed by later selected slots: the first object's absence can be omitted from strict coverage. The existing S3 passing scenarios do not exercise this case. Consequently FR-008 / SC-003 cannot be read as fully satisfied beyond their recorded matrix.

**Requested follow-up, not implemented:** perform required candidate presence/metadata validation independently of whether the candidate contributes rows, retaining typed-empty output and zero value decoding for a slotless result. Add strict missing/present slotless cases and a cross-chunk window with a slotless first candidate.

## Independent review evidence

Run command:

```text
python3 specs/010-read-om-series/evidence/code-review/probe.py
```

The [probe runner](evidence/code-review/probe.py) executed six cases per pair (24 executions in its final run), using the exact final CLI/loadable-extension artifacts named by the implementation's per-pair manifests. Each artifact's SHA-256 was verified before execution. SQL files, commands, exits, stdout/stderr and the four present-object access traces are retained beside [probe-results.json](evidence/code-review/probe-results.json). The host reports `aarch64`; no other platform is claimed. Each CLI invocation uses `-no-init`, `-unsigned`, `-bail`, an in-memory database and an explicit extension LOAD. No remote service or live producer object was contacted by these review probes.

| Probe | Expected | Observed on all four pairs |
|---|---|---|
| Strict missing, slotless window | Missing-object error | Exit 0, count 0 — R2 |
| Strict missing, window with slots | Missing-object error | Exit 1 with local missing diagnostic — control |
| Unknown domain, all missing | Argument error | Exit 0, count 0 — R1 |
| Non-STRUCT grid, all missing | Argument error | Exit 0, count 0 — R1 |
| Non-STRUCT grid, present fixture | Error before object access | Exit 1 after a successful fixture open — R1 |
| Case-insensitive array-name collision, all missing | Error | Exit 1, DuckDB duplicate-column diagnostic — no finding |

Only the present-object invalid-grid cases were traced. A zero count of trace events in the runner's console summary for other cases means no trace was collected, not proof of zero access; the JSON omits `fixture_open_events` for those cases. No complete acceptance suite, build, sanitizer matrix or remote regression suite was rerun during review.

## Evidence honesty and blocked-gate quality

The final implementation report clearly distinguishes task completion from acceptance. It retains local runner exit 2, keeps S2 blocked, keeps SC-002 and SC-007 blocked, and records FR-002/FR-007/FR-014 as partial. The local runner itself only audits zero chunk length for its S2 preflight and unconditionally retains the broader gate as blocked unless that preflight fails (`scripts/validate-series.py:2254` onward). The review did not find an invented overall S2 PASS.

The report also appropriately limits N160 evidence to synthetic fixtures, real producer evidence to two adjacent IFS025 chunks at one domain point, transport retries to inferred observations, and acceptance to linux_arm64. Controlled remote comparison explicitly records that SQL EXCEPT ALL was unavailable rather than claiming it ran. Those qualifications should remain intact.

There are two documentation issues to carry into reconciliation:

1. **The remaining work is not merely rerunning an otherwise complete S2 suite.** T010 is checked while promising time-conflict test authoring; the series fixture catalog and series SQL/native tests contain no end-to-end conflicting embedded-time fixture case. T016 is checked despite the incomplete no-access matrix. The footer honestly explains the blocker, but a concrete remaining-case checklist is needed to prevent all 42 checkmarks being mistaken for fulfilled acceptance obligations. The existing reader does contain a time-conflict guard in `src/scan/semantic_axes.cpp:378–404`; source inspection is not an executed series acceptance case.
2. **The report header's starting source HEAD is mislabeled.** `report.md:5` names `08e34c447bae34eaee3723cac61f2878b6bdf787`, the DuckDB v1.5.4 pin shown later in that report. `manifest.json.git_baseline.head`, `preservation-review.json.baseline_head`, and current repository HEAD agree on `80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9`. The manifest and verified final source digest retain usable provenance, but the prose should distinguish repository HEAD from engine pin. No historical evidence was edited here.

The new observations must be added to the eventual acceptance reconciliation without rewriting historical runs as failures they did not execute. Preserve S2/SC-002/SC-007's historical **blocked** results, and separately mark the reviewed spatial-validation and strict-slotless cases **failed**. Full milestone acceptance remains blocked on corrections and the remaining corpus. FR-006/FR-008/FR-012 and SC-003 now also need explicit qualification for these failures.

High-risk unresolved S2 evidence includes embedded time conflict under value-free projection, metadata-dependent shape/order/type/grid mismatch coverage on actual objects, and full pure-argument/overflow/4096-bound access auditing across the declared pairs. This review does not infer those results from the successful source builds, pure signature unit tests or cancellation/recovery evidence. Any later repair should stay within the series/shared seams and preserve the verified 008/009 baseline.

## Session boundary

Only this review report and `evidence/code-review/` artifacts were written. No production source, registered test, implementation evidence, task checkbox or neighboring draft was modified. No commit, push, amend, rebase, reset or stash was performed. No fixes or next milestone were started.

Standards: **0 findings**. Spec: **2 findings**, worst **P1 spatial prevalidation**. Separate documentation observations above do not change the historical recorded outcomes.
