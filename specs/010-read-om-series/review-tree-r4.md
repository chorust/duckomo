# DuckOMO whole-working-tree review — round 4

**Verdict: R3-01, R3-02, R3-03 and R3-04 are closed within their stated scope. No new severity-ranked defect was established. Feature 010's declared-scope follow-up PASS remains supported; this is not unconditional approval to merge the entire dirty tree as-is, and Feature 008 is not PASS.**

The remaining qualification is substantive but not a new finding: the dirty tree combines multiple workstreams, local agent/configuration files and large evidence histories that the submission policy excludes from a raw product PR. Feature 008 retains its separately documented incomplete acceptance matrix. Closing the R3 corrections does not certify those items or the eventual extracted submission. No additional production fix is requested by this review.

## Baseline and review method

- Repository: `/home/blizhan/repo/github/duckomo`.
- Host: `uname -m` returned **`aarch64`**; execution was on linux_arm64 only.
- HEAD and `origin/main`: **`80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9`**.
- Comparison: **HEAD against the entire dirty working tree**, using `git diff HEAD`, nonignored untracked inventory and submodule inspection. No three-dot range. The index had no staged diff.
- Tracked textual diff: 38 files, +2080/−393; 257,928 bytes. SHA-256: **`b817d7347a8e7c125208b12dfe8b155143964e6d85e5b2e08627d5c6613bfbeb`**.
- Nonignored untracked inventory before this report: 9,150 files. This includes the new series implementation, scripts/tests/fixture definitions, specs and evidence; the review was not limited to the R3 edit list.
- Nested roadmap checkout: only untracked `roadmap-config.yml` in its status; no parent gitlink change. DuckDB, OM C and extension-ci-tools pins remained as recorded. No submodule mutation was performed.
- Both review lines were performed directly in this session. The available tool interface did not expose subagent execution; no independent parallel review is claimed. The code-review skill's default commit-range convention was overridden by the user's explicit working-tree scope.

**Standards line:** no new documented-standard violation or actionable ownership/cleanup defect found. The project constitution remains an unfilled template, not a ratified standard. No applicable project AGENTS/CONTRIBUTING/CODING_STANDARDS file was found in the searched project scope; DuckDB's vendored contributor guide is not treated as a DuckOMO constitution. `git diff HEAD --check` passed. Ownership, exception cleanup, compatibility seams and test hygiene were reviewed as engineering judgments.

**Spec/behavior/docs-consistency line:** no new severity-ranked finding established. Re-read the tracked diff, current series planner/binder/registry, shared schema and scanner changes, local/remote error classification, cache lifetime changes, build/test wiring, current 010 requirements and acceptance wording, edited public guidance, 008 status, community publication/template changes and 009 readiness state. Evidence was checked through indexes, consolidated and per-pair manifests, reports and inventory rather than line-reading gigabytes of logs. Prior reviews supplied regression context, not a substitute for current code inspection.

## Source identity and PASS binding

| Identity | Observed value / interpretation |
|---|---|
| Follow-up PASS extension-source digest | `8f1f2624fc64951aa42e35625f90c7008ff18672c4e114000dfada4c7e6bef6e` |
| Current extension-source digest, recomputed | `5c8701f5162c5248d690af5cbc548135cc3c71af62a2232075805b8cfedcb7ee` |
| Follow-up tracked-diff binding | `05e9cd9929e79e9207e8e0159ff52d22fa672888016fe54ce7bad15d89aa350b`, 254,186 bytes |
| Current tracked-diff binding | `b817d7347a8e7c125208b12dfe8b155143964e6d85e5b2e08627d5c6613bfbeb`, 257,928 bytes |

The current digest matches the [R3 fix note](evidence/review-fix-r3-docs-p3/fix-note.md). The source digest includes `scripts/`, so the validator comment/cleanup edits change it even though production reader code did not change. Documentation changes also change the tracked-diff identity. **The four-pair matrix was run on `8f1f2624…`, not on `5c8701…`; this review does not rebind that evidence or claim a fresh full-matrix run.**

Freshly computed production hashes:

| Current file | SHA-256 | Comparison |
|---|---|---|
| `src/om/reader.cpp` | `4949a0ff7632aa595f7a94215af9009d65dae7208facf826486d725d1550de91` | Matches R3 and R3-fix record |
| `src/include/duckomo/om_reader.hpp` | `431d61fc552055097592f08938d52853c30545b5ec6516e5fc0fba7a40d76d04` | Matches R3 and R3-fix record |
| `src/scan/read_om.cpp` | `a49a7dae1e07975765c9f19e6929e8b9284f067b1b5604d26afdb2dbbcc9a741` | Matches R3 and R3-fix record |

R3 recorded these files while the aggregate digest matched the PASS snapshot. Thus these three files remain consistent with that snapshot through the recorded R3 binding. The retained build manifests contain aggregate source digests, not an independent per-file source archive; I do not claim a reconstructed byte-for-byte historical tree. Two in-memory attempts to reverse the validator edits did not reproduce the old aggregate digest and are not used as evidence of source equivalence.

The original `source_identity` remains intact. Of the four `status_notes` hashes, `spec.md` and `tasks.md` still match; `quickstart.md` and `contracts/acceptance.md` differ following the disclosed R3 wording edits. These are historical document bindings, not fresh checksums of the edited prose. The follow-up report's “current source” wording must be read as its dated run snapshot; it does not override the explicit hashes above.

## R3 closure verification

| Finding | Disposition | Current evidence |
|---|---|---|
| **R3-01, P2: conflicting public 010 status** | **Closed** | Both READMEs, `docs/spec.md`, `docs/roadmap.md`, `.specify/memory/roadmap.md`, `docs/architecture.md` and `evidence/submission-scope.md` point to the follow-up and distinguish historical runs. They retain the four pinned pairs/linux_arm64 limit, narrow IFS025 real-data scope, ten synthetic-equivalence-only models, synthetic N160 and development-source-only identity. |
| **R3-02, P3: accounting/traceability** | **Closed** | All four consolidated S1 counters are 7. Independently checked all 28 named S1 scenario JSON records: passed. Report line 46 now names authoritative `corrected-source/<pair>/local`, with v2.0.0 at `local-corrected`. Lines 56–60 disclose pre-correction replacement and local S2 failures and distinguish source digests. All 44 indexed evidence hashes and sizes match. |
| **R3-03, P3: S4 assertion overstatement** | **Closed by narrowed claims, not stronger testing** | Report S4 row, acceptance-contract S4 row/status, quickstart and public README/interface wording explicitly disclose unknown-status HTTP(S) 403/412 and the range-protocol failure of `corrupt`. `scripts/validate-series.py:1867` explains that the assertion checks query failure plus origin response, not category/text or ordinary-reader parity. |
| **R3-04, P3: replacement cleanup state** | **Closed** | `scripts/validate-series.py:1761` now uses `control(transport, target_key, "restore")` for both mutation actions before recovery. `series_remote.py:45` maps restore to `normal`; replacement is no longer used as restoration. Current validator and fixture-server unit suites pass. |

R3-02 detail: the retained outer v1.5.4 local manifest still records **38 passed / 8 failed** S2 cases. The authoritative corrected-source manifests record **46 passed / 0 failed / 0 blocked** each. The revised digest narrative distinguishes pre-correction `98b40a9b…` from PASS `8f1f2624…` and explicitly limits the source-delta reconstruction to available file/time and behavior records. It no longer presents the failed replacement run as corrected-source evidence.

R3-03 limits remain visible: the general acceptance-contract paragraph at line 43 still calls for comparison with ordinary `read_om`; the controlled fault helper does not implement a general category/text parity assertion. Nor does the `corrupt` response exercise valid-range corrupt OM metadata/decode. These are the already-disclosed assertion boundaries, not newly supplied coverage or a reopened wording finding. Historical per-run S4 summaries retain their original broader labels; the corrected consolidated interpretation governs this review.

R3-04 test scope: 25 validator tests and 8 server tests were freshly executed. They cover the helper expectations and fixture mutations/restoration; they do not execute the complete prepared-query mutation orchestration. Closure is based on the actual cleanup call, the server's restore semantics and passing targeted tests, not a claim that the 117-scenario matrix was repeated.

## Follow-up acceptance evidence audit

Base directory: [acceptance-20261106-followup-linux-arm64](evidence/acceptance-20261106-followup-linux-arm64/report.md).

| Check | v1.5.4 | v1.5.5 | v1.5.6 | v2.0.0 |
|---|---:|---:|---:|---:|
| Corrected-source S1 named scenarios passed | 7/7 | 7/7 | 7/7 | 7/7 |
| S2 per-case results passed | 46/46 | 46/46 | 46/46 | 46/46 |
| Controlled scenario status counts | 117/117 | 117/117 | 117/117 | 117/117 |
| Recorded SC-004 remote model/transport cells | 33/33 | 33/33 | 33/33 | 33/33 |
| Recorded official HTTPFS native exit | 0 | 0 | 0 | 0 |
| Recorded full local-regression exit | 0 | 0 | 0 | 0 |
| Real comparison: schema, manual union, frozen oracle, two-way differences | passed, 2 rows | passed, 2 rows | passed, 2 rows | passed, 2 rows |

These are checks of **retained execution evidence**, except for the targeted Python reruns described below. I parsed every pair's local, controlled and real manifest, checked all scenario status counts, the S2 case outcomes, the build input identities and the full-regression status records. All **44/44** indexed files exist and match both SHA-256 and byte size, including the corrected report. The indexed build manifests consistently bind the pinned pairs to `8f1f2624…`.

The local-only manifests retain overall `status: blocked` despite their local gates passing: local mode does not independently establish complete acceptance (`validate-series.py` leaves its nonfailed overall status blocked). This is not a failed local gate or a contradiction of the consolidated result; controlled, real and regression evidence are supplied separately. Likewise “built (runtime gates pending)” in a build manifest is not the later acceptance verdict.

The declared PASS is supported only for **linux_arm64 / pinned DuckDB v1.5.4, v1.5.5, v1.5.6 and v2.0.0**. Real producer data covers **two adjacent IFS025 `temperature_2m` objects, one point, one boundary window**. Other ten registry models have provenance plus synthetic runtime equivalence; N160 is synthetic. No released series package, broad producer-grid support or other-runner pass follows. Current top-level 010 guidance agrees with these limits.

## Previously closed findings and mixed-tree inspection

| Prior item | R4 disposition and current-file basis |
|---|---|
| F008-01 planned-cache retention | Still closed. Nested scope ownership in `om_reader.hpp`, standalone planning/decoding scope in `reader.cpp`, and scan-window scope in `read_om.cpp` remain present. Shared plans/pending items retain buffer ownership after lookup cleanup. The three hashes match R3. No fresh retention or RSS probe was run. |
| Historical 010 R1/R2 preflight/slotless defects | Still closed. `read_om_series.cpp` validates spatial declarations before candidate opens, binds and validates every present candidate before omitting a slotless leaf, and skips only typed initial-open absence in non-strict mode. Corrected S2 evidence remains passed. |
| F010-01 public status | Original issue remains fixed; the later stale-status recurrence is now closed as R3-01. |
| FCOM-01 community upgrade rendering | Still closed. Version/ref placeholders occupy actual YAML fields; documented procedure renders both, parses YAML and asserts the values. Local published identity remains `0.3.5-rc.1`; artifact hashes remain explicitly TODO. No fresh upstream fetch or signed install is claimed. |
| F009-01 readiness checklist | Still closed. Spec remains Draft with its legacy_v2/legacy_v3 clarification; both affected checklist items remain unchecked and the note explains why it is not ready for planning. No 009 implementation was found in the changed production paths. |
| FMISC-01 evidence submission policy | Still closed as a policy request. Current policy covers all three histories, slim PR evidence, external checksummed archive expectations, and local `.pi/`/roadmap exclusions. It explicitly says no archive has been created. |

Shared `read_om.cpp` and `om_reader.hpp` still mix 008 lifetime/reuse and 010 binding/error-classification changes. The series implementation relies on the new source/header registrations and associated fixtures/tests; it cannot be judged as a one-file patch. A future extraction must preserve these dependencies and be verified as the actual extracted result. No new production regression or merge-blocking defect was established from these hunks.

## Accepted policy and remaining limitations — not new findings

**Feature 008 is not PASS.** The user's explicit 2026-11-06 policy accepts SC-006 as relaxed / won't meet the original ≥20% gate. Approximately 0% measured improvement is not ≥20% evidence. Current 008 spec/checklist/status and performance report preserve that distinction. T042/T043 remain unchecked; this review does **not** recommend implementing them or reopening the throughput target. The already-documented SC-003–005 final evidence, SC-007/SC-008 matrix and other completion items are not certified by 010 acceptance. Earlier performance measurements remain bound to their recorded artifacts; no fresh benchmark was run.

Community publication does not establish current signed-artifact identity, and 009 remains Draft. The development-source-only series acceptance does not expand either workstream's status.

Fresh historical inventory: `run-20260803-linux-arm64/` contains **5,078 files / 1,181,103,584 bytes**, with `du -sh` reporting 1.2G. The first 2026-11-06 tree is about 55M and the follow-up about 350M. They remain in place. Inventory agreement is not a full historical content-hash audit. The policy's slim/archive packaging has not been carried out, so the entire unfiltered dirty tree is not an approved product-PR payload. `.pi/` contents and ignored local secrets were not reviewed or executed as DuckOMO product code.

## Fresh verification and commands

Working directory was the repository root; interpreter was `/home/blizhan/miniconda3/bin/python3`, **Python 3.13.13**. The tests imported the **current working-tree Python source**; no DuckDB/extension binary was executed in R4.

```sh
uname -m
git rev-parse HEAD origin/main
git status --short
git diff HEAD --stat
git diff HEAD --check
git diff --cached --stat
git submodule status
git -C .specify/extensions/roadmap status --short
git diff HEAD > /tmp/duckomo-r4.diff
sha256sum /tmp/duckomo-r4.diff src/om/reader.cpp \
  src/include/duckomo/om_reader.hpp src/scan/read_om.cpp
PYTHONDONTWRITEBYTECODE=1 python3 -I -c "import sys,pathlib; sys.path.insert(0,'scripts'); import version_matrix as v; print(v.extension_source_digest(pathlib.Path('.').resolve()))"
PYTHONDONTWRITEBYTECODE=1 python3 test/tools/validate_series_test.py
# Ran 25 tests — OK
PYTHONDONTWRITEBYTECODE=1 python3 test/tools/series_remote_test.py
# Ran 8 tests — OK
du -sh specs/010-read-om-series/evidence/*
```

Inline Python also parsed the consolidated/per-pair JSON, counted passed S1/S2/controlled cases, compared real-result flags, rehashed indexed evidence and status notes, counted nonignored untracked files, and inventoried historical file sizes. The in-memory reverse-edit experiments described above wrote no repository file and provide no historical-source proof. Source/diff reads and targeted searches supplied the static review evidence.

**Not run:** four-pair rebuild or 117-scenario matrix; fresh native/SQL acceptance; retention probe; ASan/UBSan/TSan; performance/RSS measurements; real-object refetch; community signed installation/artifact download; CI or another architecture. No claim of current-TU compilation is made. Large evidence histories were not line-reviewed and no full secret audit was performed.

## Completion boundary

New findings: **Standards 0; Spec/behavior/docs-consistency 0**. All four R3 findings are closed with the bounds above. Feature 010 declared-scope acceptance holds on its recorded snapshot; Feature 008 remains not PASS; whole-dirty-tree merge approval remains qualified by known acceptance and submission boundaries.

The only repository file written by this review is `specs/010-read-om-series/review-tree-r4.md`. Earlier reports, production code, tests, documentation and evidence histories were preserved. No commit, push, amend, rebase, reset, stash, evidence cleanup, implementation/review-fix phase or parent notification was performed.
