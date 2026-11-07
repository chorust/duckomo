# DuckOMO whole-working-tree review — round 3

**Verdict: needs more changes before merge; the entire dirty tree cannot be approved unconditionally as-is.** I found no new production-code defect. The Feature 010 follow-up PASS holds up for its declared scope: Linux AArch64 / linux_arm64, the pinned DuckDB v1.5.4, v1.5.5, v1.5.6 and v2.0.0 pairs, and real producer data limited to the frozen adjacent IFS025 sample. The follow-up evidence is bound to the exact current source and tracked diff.

However, one new **P2** documentation-consistency defect blocks merge. The top-level user-facing guides still show two different, superseded 010 statuses that contradict each other and the 010 spec set. Three new **P3** evidence and validation-honesty issues should also be fixed or explicitly accepted.

**Feature 008 is not PASS.** SC-006 stays relaxed and will not meet the original ≥20% target, by user decision (2026-11-05/06). That is accepted user policy, not measured ≥20% evidence, and this review does not treat it as a defect. T042/T043 remain unchecked.

## Baseline and method

- Repository: `/home/blizhan/repo/github/duckomo`; `uname -m` → **`aarch64`**. All execution was on this linux_arm64 host.
- HEAD `80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9` = `origin/main`.
- **Comparison: HEAD against the entire current working tree.** I used `git diff HEAD` (38 tracked files, +2080/−393), untracked non-ignored files, and submodule status. No three-dot range was used, and the index had no staged diff.
- Untracked non-ignored files: **9,148**, up from 5,089 in round 2. The increase is mostly the two new 2026-11-06 evidence trees: 4,129 files, 350M follow-up + 55M first run.
- Submodules: no gitlink changes. `.specify/extensions/roadmap` only has its untracked `roadmap-config.yml`.
- **Tracked-diff identity:** `git diff HEAD | sha256sum` = `05e9cd9929e79e9207e8e0159ff52d22fa672888016fe54ce7bad15d89aa350b`, 254,186 bytes. This exactly matches `source_identity` in the follow-up `manifest.json`.
- **Extension source digest:** I recomputed it with `scripts/version_matrix.py::extension_source_digest` on the current tree. It is `8f1f2624fc64951aa42e35625f90c7008ff18672c4e114000dfada4c7e6bef6e`, exactly the follow-up's recorded digest. So the follow-up builds, controlled runs and full regressions were produced from the current `src/`, `test/`, `scripts/`, `third_party/om-file-format` and build files. They are not stale-binary evidence.
- Review lines:
  - **Standards line:** `git diff HEAD --check` passes. `.specify/memory/constitution.md` is still an unfilled template, and no `AGENTS.md` or `CONTRIBUTING.md` exists. I inspected ownership, cleanup and test hygiene as review judgment, not as documented rules.
  - **Spec/behavior/docs-consistency line:** I re-read the current files changed since r2, the follow-up report/manifest/per-pair manifests, the controlled-remote validator and fixture server, the 010 spec set, top-level guides, 008 status, community and 009 files.
  - Both lines were done directly in this session. No subagents were used.

### What changed since round 2 (by mtime against `review-tree-r2.md`)

Within `src/`, `test/native`, `test/sql` and `scripts/`, only these changed:

- `test/native/reader_capacity_test.cpp`: `InterruptException` no-arg constructor for the v1.5.4 API.
- `test/sql/read_om_series_validation.test`
- `scripts/generate-series-fixtures.py`
- `scripts/validate-series.py`: replacement assertion and audits.

Specs and docs were also updated (010 spec/tasks/quickstart/contracts), and the two new evidence trees and fix note were added. **Production sources are byte-identical to round 2:**

| File | SHA-256 (round 3) | Same as r2 |
|---|---|---|
| `src/om/reader.cpp` | `4949a0ff…0de91` | yes |
| `src/include/duckomo/om_reader.hpp` | `431d61fc…d76d04` | yes |
| `src/scan/read_om.cpp` | `a49a7dae…a741` | yes |
| `test/native/remote_async_test.cpp` | `0bd5c195…c124` | yes |
| `test/native/reader_capacity_test.cpp` | `1d92daf7…453d` | changed (test-only, see fix note) |

## New findings

| ID | Severity | Line | Summary |
|---|---:|---|---|
| R3-01 | **P2** | Spec/docs | Top-level guides and roadmap state two different superseded 010 statuses, contradicting each other and the 010 spec set |
| R3-02 | P3 | Spec/evidence | Follow-up report/manifest accounting errors: `s1_scenarios: 0`, wrong local-evidence path, undisclosed pre-correction S2 failure, no description of the source delta |
| R3-03 | P3 | Validation honesty | Controlled S4 fault scenarios assert only "query failed"; "structured 403/412" and "corruption" labels overstate what the HTTP/HTTPS cases demonstrate |
| R3-04 | P3 | Standards (test hygiene) | Replace-mutation cleanup leaves the fixture in `replacement` state instead of `restore` |

### R3-01 (P2) — user-facing 010 status is stale and self-contradictory

**Fix status:** Fixed in this review-fix. Top-level status and roadmap references now point to the declared-scope follow-up PASS; evidence scope limits are stated consistently. See [fix note](evidence/review-fix-r3-docs-p3/fix-note.md).

**Locations:**

- `README.md:19`, `:25`, `:175`: Chinese guide. Says S0/S1/S3–S7 passed and **S2 / SC-002 / SC-007 Blocked**. Links `run-20260803-linux-arm64/report.md` (the historical snapshot). `README.md:23` also links that report as "current validation boundary".
- `README.en.md:19`, `:23`, `:25`, `:176`: English guide. Says S0/S1/S2/S5/S6 passed, **SC-003/SC-004, S3/S4, full S7 Blocked, "Feature 010 is not PASS"**. Links the *first* 2026-11-06 report.
- `docs/spec.md:5`, `:15`, `:123`: same intermediate "not PASS" status as README.en.md.
- `docs/roadmap.md:21`, `:31`; `.specify/memory/roadmap.md:321`; `docs/architecture.md:3`: same intermediate status, or link the first 11-06 report as current.
- `specs/010-read-om-series/evidence/submission-scope.md:13–15`: still calls the 2026-08-03 report "the final report". It requires preserving "S2 / SC-002 / SC-007 Blocked" outcomes and does not cover the two new trees.

**Conflicting sources:** `specs/010-read-om-series/spec.md:7`, `tasks.md:176`, `quickstart.md:3`, `contracts/acceptance.md:3`/`:18`, and the follow-up report all say **PASS for the declared linux_arm64 four-pair scope**.

**Impact:**

- A reader gets three incompatible answers depending on the document:
  1. S2 blocked (README.md)
  2. S3/S4/S7 blocked and not PASS (README.en.md / docs/spec.md / roadmap / architecture)
  3. PASS (010 spec set)
- The Chinese and English READMEs also contradict each other.
- This is the same class as closed F010-01 (public guidance keeping an intermediate status). This time it is an under-claim, not an over-claim, so there's no safety risk. It is still a merge-blocking consistency defect.
- The tracked-diff identity matches the follow-up manifest, so these files were left unchanged by the follow-up session.

**Evidence:** `grep -n -i "read_om_series\|blocked\|PASS" README.md README.en.md docs/spec.md docs/roadmap.md docs/architecture.md .specify/memory/roadmap.md`. Lines are quoted above.

**Expected correction (not performed):** Update these guides to the follow-up scope wording and keep its limits:

- linux_arm64 only; pinned v1.5.4/v1.5.5/v1.5.6/v2.0.0
- real data = two adjacent IFS025 `temperature_2m` objects, one point, one boundary window
- the other ten models are provenance + synthetic equivalence only
- N160 synthetic
- development-source-only, not a released feature identity
- no Feature 008 PASS

### R3-02 (P3) — follow-up report/manifest accounting errors

**Fix status:** Fixed in this review-fix. The four S1 counts are set to the seven verified scenarios per pair; local report paths, retained S2 failure and source-digest history are documented. See [fix note](evidence/review-fix-r3-docs-p3/fix-note.md).

Location: `evidence/acceptance-20261106-followup-linux-arm64/`.

1. `manifest.json` → `pairs[*].local.s1_scenarios` is **`0` for all four pairs**. The underlying per-pair evidence (`corrected-source/<pair>/local/report.md`, `scenarios/*.json`) shows **seven S1 scenarios passed** per pair: one_chunk, single_boundary_slot, unaligned_cross_boundary, three_chunks_and_null, alternate_start_end, negative_epoch, slotless_subsecond_window. No script in `scripts/` emits `s1_scenarios`, so the consolidated manifest was hand-assembled. It understates its own evidence.
2. `report.md:46` says local reports are "under each pair's `local/` directory; v2.0.0's final canonical-fixture rerun is `v2.0.0/local-corrected/`". The top-level `v1.5.4/`, `v1.5.5/`, `v1.5.6/`, `v2.0.0/` directories are **pre-correction runs**:
   - They used build root `build/series-acceptance-sc004-20261106/`, extension source digest `98b40a9b…60cd3`, and v1.5.4 extension SHA `e3533804…7e14cb`.
   - The authoritative runs are under `corrected-source/<pair>/local` (v2.0.0: `corrected-source/v2.0.0/local-corrected`), as `manifest.json` correctly records.
   - The report sentence sends readers to the wrong source snapshot.
3. The report discloses two intermediate failures: the 412 replacement assertion and the v2.0.0 S2 fixture root. It does **not** disclose that pre-correction `v1.5.4/local/manifest.json` also **failed S2**, with 8/46 metadata cases ("observed candidate-object access differs from the case contract"), before `v1.5.4/local-corrected` passed. Retained, so not hidden, but the summary is incomplete.
4. The report calls the 412-expectation run "the first corrected-source replacement assertion". That run (`v1.5.4/controlled-remote`) is on the pre-correction digest `98b40a…`, not on `8f1f26…`. The report also never says what changed between the two source digests. Because the follow-up's source digest equals the current tree, the PASS is still bound correctly; this is a traceability gap, not a wrong binding.

**Impact:** Evidence-index honesty and traceability. None of these turns a PASS into a FAIL. I verified the 44 hashed files in `manifest.json → evidence_file_hashes`: 44/44 exist and match.

### R3-03 (P3) — controlled S4 fault scenarios: assertions weaker than labels

**Fix status:** Fixed in this review-fix by narrowing the report and public wording to the demonstrated failure behavior and HTTPFS diagnostics; no validator assertion was strengthened and the matrix was not rerun. See [fix note](evidence/review-fix-r3-docs-p3/fix-note.md).

**Location:** `scripts/validate-series.py:1845–1890` (`execute_expected_failure`) and `:2023–2050`; `scripts/series_remote.py:35–44`, `:257–267`.

For injected faults, a scenario passes when the query exits nonzero and the audit saw the injected status. It does **not** check the error category or the sanitized text, and it does not compare against ordinary `read_om` on the same fault. `contracts/acceptance.md:43` asks for that comparison. Observed diagnostics on v1.5.4 (`corrected-source/v1.5.4/controlled-remote/manifest.json`):

| Fault | HTTP / HTTPS diagnostic | S3 diagnostic |
|---|---|---|
| `http_403`, `body_404_status_403` | `[transport] … no structured HTTP status was supplied` | `[access_denied] (HTTP 403)` |
| `http_412` | `[transport] … no structured HTTP status was supplied` | `[object_changed] (HTTP 412)` |
| `http_500` | `[http] (HTTP 500)` | `[http] (HTTP 500)` |
| `corrupt` | `remote OM range read failed … [transport]` | same |

- The HTTP/HTTPS 403/412 behavior matches a pre-existing official-HTTPFS limitation that `test/native/official_httpfs_test.cpp:53–58` explicitly accepts as an "honest unknown-status fallback". It is not a new production regression.
- Series-vs-ordinary parity on post-bind removal is asserted natively in `test/native/series_error_test.cpp:438–441` with a fake filesystem.
- **The overstatement:** `report.md:21` says "Structured and unclassified faults … passed". For HTTP/HTTPS, 403/412 were *not* classified as structured; they only failed safely.
- The `corrupt` fault serves `200` plus `not-an-om-object` to HEAD and ranged GET. The query therefore fails at the range-protocol stage and never reaches OM metadata parsing or decoding. Spec US2 scenario 3 ("corrupt metadata or decode failure → fails") is not exercised by this case. I found no `read_om_series` SQL or native test that serves a well-formed range response with corrupt OM bytes.
- SC-003 itself ("never silently skipped") is satisfied by the evidence; the issue is only the wording of what was shown.

**Impact:** The "S4 passed" label is broader than the assertion. Either strengthen the validator (category/parity checks; a valid-range, corrupt-payload case), or narrow the report wording to "failed, not skipped; HTTP(S) 403/412 surface as unknown-status transport errors under official HTTPFS".

### R3-04 (P3) — replace-mutation cleanup does not restore normal state

**Fix status:** Fixed in this review-fix. Replace and remove mutation paths now both restore fixture state before recovery. See [fix note](evidence/review-fix-r3-docs-p3/fix-note.md).

**Location:** `scripts/validate-series.py:1761–1765`.

After `post-bind-replace`, cleanup calls `control(transport, target_key, "replace", replacement=target_key)` rather than `"restore"`. The fixture server (`series_remote.py:362–365`, `:463–468`) leaves the object in `state="replacement"`, pointing at itself. Bytes and ETag equal the original, so the recovery scenario passes. Any later scenario or audit that reads `object_state` sees `replacement`, not `normal`. The remove branch correctly uses `restore`.

**Impact:** Test-hygiene only. It is harmless today because post-bind mutations are the last per-transport fault steps before N160, and the audit ignores `state` there. It is fragile if scenarios are reordered or state-aware audits are added.

### Replacement assertion change (requested focus) — verified correct

`summarize_post_bind_mutation` (`validate-series.py:543–582`) now requires all of:

- a post-mutation response for the target with `state=="replacement"` and status 200
- a bound ETag and a post-mutation ETag, with the two different
- a nonzero exit with `object identity changed` in the diagnostic

On all three transports for v1.5.4, the bind audit shows HEAD 200 + ranged 206s with the original ETag. The post-mutation audit shows a changed ETag, and the query fails with the identity diagnostic. Recovery passes after cleanup.

- The separate injected `http_412` scenario still exists per transport.
- `test/tools/validate_series_test.py:160–176` covers both the positive case and the unchanged-ETag negative case.
- The fixture's real behavior (HEAD 200 with a new ETag, no `If-Match` from HTTPFS) makes the old 412 expectation incorrect, so the change fixes the assertion and doesn't weaken it.
- The audit doesn't require the 200 to come from a HEAD specifically; the report's "200 HEAD" wording is slightly narrower than the check. Cosmetic.

## Verification of the 010 follow-up PASS claim

| Claim | Verified how | Result |
|---|---|---|
| Evidence produced from current source | Recomputed extension source digest and `git diff HEAD` hash | **Match** (`8f1f26…`, `05e9cd…`/254,186 B) |
| 010 status-note docs pinned | sha256 of spec/tasks/quickstart/contracts/acceptance vs `manifest.status_notes` | 4/4 match |
| Manifest-hashed evidence intact | Rehashed 44 `evidence_file_hashes` entries | 44/44 present and matching |
| 117/117 controlled scenarios, SC-004 33 cells | Per-pair `controlled-remote/manifest.json` (v1.5.4 sampled in depth; four-pair summary via consolidated manifest) | v1.5.4: 117 passed, 0 non-passed; gates S3/S4/S5/S6/SC-004 passed |
| SC-004 registered vs explicit | `corrected-source/v1.5.4/local/scenarios/sc004-*.json` (11 files) | `registered_only_count = explicit_only_count = 0`, schema/bounds equal. The per-run `report.md` prints `rows None vs N` / `EXCEPT ALL: None` for SC-004 rows, a report-template key mismatch; the JSON is authoritative |
| S2 46/46 | Per-pair local report/manifest | 38 preflight + 8 metadata, all passed on corrected-source runs |
| Full `validate.sh --local-only` exit 0 | `full-regression-status.json` | exit 0 on all four pairs (06:39–06:46Z) |
| Official HTTPFS native | `corrected-source/v1.5.4/official-httpfs/result.json` | exit 0; extension SHA matches pair build |
| Real-object S7 | consolidated manifest `real_object_s7` | passed, 2 rows, matches manual union and frozen oracle. Not re-fetched in this review |
| No live runners left | All `*.pid` files in follow-up tree | all processes dead |
| Scope limits in manifest/report | `limitations`, `feature_008`, report "Preservation and limits" | linux_arm64 four pairs only; IFS025 two-object real data only; other ten models synthetic; no 008 PASS. **Correct** |

**Conclusion:** the declared-scope PASS is supported by evidence bound to the current tree. The P3 items (R3-02/R3-03) concern accounting and wording precision, not a false PASS. The PASS is **not** reflected in the top-level guides (R3-01).

## Prior findings — regression check

| ID | Status in round 3 | Basis |
|---|---|---|
| F008-01 planned_cache lifetime (P1) | **Still closed** | `reader.cpp`, `om_reader.hpp`, `read_om.cpp` byte-identical to r2 hashes. Only test change is the no-arg `InterruptException`, built and passed per `review-fix-20261106-linux-arm64/fix-note.md` and covered by the four-pair full regressions on `8f1f26…`. I did not re-run the retention probe this round |
| F010-01 public guidance (P2) | **Original over-claim remains fixed; same class recurs in the under-claim direction → R3-01** | see R3-01 |
| FCOM-01 community upgrade (P2) | **Still closed** | `description.yml.in:8` `version: @DUCKOMO_VERSION@`, `:21` `ref: @DUCKOMO_SOURCE_REF@`. `community/validation.json` remains `0.3.5-rc.1`, artifact hashes still an explicit TODO |
| F009-01 checklist (P2) | **Still closed** | `checklists/requirements.md:16–17` unchecked, `:37` names the open legacy_v2/v3 clarification; spec status Draft |
| FMISC-01 evidence scope (P2) | **Policy still exists; content now stale (part of R3-01)** | `submission-scope.md` doesn't cover the two new 2026-11-06 trees (4,129 files, ~405M) and still names the 08-03 report as final |

## Feature 008 status (unchanged, not evaluated for PASS)

- `specs/008-duckdb2-async-io/spec.md:136`, `checklists/success-criteria.md:10` and `evidence/008/final/status.md:20` consistently record SC-006 as **relaxed / won't meet ≥20%**, measured at about 0%, and explicitly "not a PASS".
- `tasks.md:126–127` keep T042/T043 unchecked, and true cross-batch I/O·decode overlap is not implemented.
- This matches the user's explicit policy (允许 SC-006 未达 ≥20%). It is recorded here as **accepted user policy**, not as a defect, and not as ≥20% evidence.
- I make **no recommendation to implement T042/T043**.
- The follow-up run did not evaluate 008 (`manifest.feature_008.pass_claimed = false`). The remaining SC-007/SC-008 matrix items are unchanged. **Feature 008 is not PASS.**

## Remaining known limitations (not new defects)

- 010 acceptance scope:
  - linux_arm64 only; no x86_64, macOS or other-runner claim
  - real producer data = two adjacent IFS025 `temperature_2m` objects, one point, one boundary window
  - the ten other registered models have provenance plus synthetic runtime equivalence only
  - N160 synthetic
  - `read_om_series` is development-source-only
- Under official HTTPFS, HTTP/HTTPS HEAD 403/412 surface as unknown-status `[transport]` (pre-existing, accepted in `official_httpfs_test.cpp`).
- Ordinary `read_om` rebinds to a replaced object while the series child rejects it (`series_error_test.cpp:468`, documented design).
- Community: no fresh upstream fetch, signed install or artifact-hash verification; the TODO is explicit.
- 009: Draft, no implementation.
- Mixed-hunk ownership (008/010 in `read_om.cpp`, `om_reader.hpp`; README mixing workstreams) still needs care when extracting a product PR, per r2.
- `run-20260803-linux-arm64/`: **5,078 files, 1,181,103,584 bytes** (re-counted this round), matching the recorded inventory. Untouched.

## Commands run in this review

Working directory `/home/blizhan/repo/github/duckomo`, host `aarch64`:

```sh
uname -m; git rev-parse HEAD origin/main; git status --short; git submodule status
git diff HEAD --stat; git diff HEAD --check; git diff --cached --stat
git diff HEAD | sha256sum; git diff HEAD | wc -c
git ls-files --others --exclude-standard | wc -l
git -C .specify/extensions/roadmap status --short
python3 -I -c "import sys,pathlib; sys.path.insert(0,'scripts'); import version_matrix as v; print(v.extension_source_digest(pathlib.Path('.').resolve()))"
sha256sum src/om/reader.cpp src/include/duckomo/om_reader.hpp src/scan/read_om.cpp test/native/reader_capacity_test.cpp test/native/remote_async_test.cpp
sha256sum specs/010-read-om-series/{quickstart.md,contracts/acceptance.md,spec.md,tasks.md}
find src test/native test/sql scripts -newer specs/010-read-om-series/review-tree-r2.md -type f
python3 -m unittest discover -s test/tools -p '*series*test.py'   # Ran 33 tests, OK
python3 test/tools/series_registry_test.py                         # passed
python3 test/tools/series_fixtures_test.py                         # passed (temp output under /tmp)
find specs/010-read-om-series/evidence/run-20260803-linux-arm64 -type f | wc -l   # 5078
find … -type f -printf '%s\n' | awk '{s+=$1}END{print s}'                          # 1181103584
```

I also ran inline Python reads of the follow-up `manifest.json`, the per-pair local/controlled manifests and scenario JSON, the 44-entry evidence rehash, and a liveness check on recorded PIDs. A narrow grep of the follow-up tree's JSON/SQL/MD/log files for obvious AWS key patterns found no hits; this is not a full secret audit.

## Not run

- No build or rebuild of any pair.
- No DuckDB/extension binary executed.
- No re-run of the four-pair S1–S7 matrix, controlled HTTP/HTTPS/S3 service, official HTTPFS native test or real-object fetch.
- No F008-01 retention probe re-run.
- No ASan/UBSan, performance/RSS benchmarks, community signed install or other architectures.
- No line review of the 1.2G historical tree or the full 350M follow-up logs; judged via indexes, manifests, reports and sizes.
- Only v1.5.4 controlled-remote/local evidence was sampled in depth; the other three pairs were checked via the consolidated manifest and full-regression status.

## Completion boundary

The only repository change in this phase is this file, `specs/010-read-om-series/review-tree-r3.md`. `review-tree.md`, `review-tree-r2.md` and `review.md` are preserved. No production, test, script, doc, task or evidence file was modified. No commit, push, amend, rebase, reset, stash, evidence deletion or move, review-fix or implement phase was performed.

**Counts:**
- New findings: P2 1 (R3-01), P3 3 (R3-02, R3-03, R3-04). No new production-code defect.
- Prior: F008-01, FCOM-01, F009-01 still closed. F010-01/FMISC-01's class recurs as R3-01 (stale guidance and stale submission scope).
- 010 linux_arm64 four-pair PASS: supported, within declared scope.
- Feature 008: not PASS; SC-006 relaxed by user policy.
