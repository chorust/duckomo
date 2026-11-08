# Review-fix R3 — documentation and P3 corrections

**Date:** 2026-11-06  
**Host:** `uname -m` → `aarch64` (Linux AArch64 / `linux_arm64`)  
**Starting HEAD:** `80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9`

## Files changed in this review-fix

- Public status and scope: `README.md`, `README.en.md`, `docs/spec.md`, `docs/roadmap.md`, `.specify/memory/roadmap.md`, and `docs/architecture.md`.
- Acceptance wording: `specs/010-read-om-series/contracts/acceptance.md` and `specs/010-read-om-series/quickstart.md`.
- Evidence scope/index: `specs/010-read-om-series/evidence/submission-scope.md`.
- Follow-up evidence prose and bookkeeping: `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/report.md` and `manifest.json`.
- Validator cleanup and assertion explanation: `scripts/validate-series.py`.
- Finding status pointers: `specs/010-read-om-series/review-tree-r3.md`.
- This fix note: `specs/010-read-om-series/evidence/review-fix-r3-docs-p3/fix-note.md`.

No production reader source or test file was changed in this review-fix.

## R3-01 — public status consistency

Updated the cited Chinese and English README sections, interface spec, roadmap entries, architecture summary, and submission-scope note to use the follow-up report as the current evidence pointer. They now describe PASS only for Linux AArch64 / `linux_arm64` and pinned DuckDB v1.5.4, v1.5.5, v1.5.6, and v2.0.0. The real producer scope is stated as two adjacent IFS025 `temperature_2m` objects, one point, and one boundary window. The other ten registered models are provenance plus synthetic runtime equivalence only; N160 is synthetic. `read_om_series` is identified as development-source-only and not a released feature identity. No broader platform or production-grid support is claimed.

The docs also retain the Feature 008 boundary: Feature 008 is not PASS, SC-006 remains relaxed and will not meet the original ≥20% gate, and T042/T043 remain unchecked. The first 2026-11-06 tree and `run-20260803-linux-arm64/` are labeled historical. `submission-scope.md` explicitly keeps the 5,078-file, 1,181,103,584-byte historical tree out of a raw product PR.

Verification: searched all cited public files for the superseded status wording and current report links. Remaining references to the first 2026-11-06 report and the 2026-08-03 report are explicitly historical. Chinese and English summaries describe the same acceptance scope and limitations. Targeted `git diff --check` passed.

## R3-02 — follow-up manifest and report accounting

Verified the seven named S1 scenarios (`one_chunk`, `single_boundary_slot`, `unaligned_cross_boundary`, `three_chunks_and_null`, `alternate_start_end`, `negative_epoch`, and `slotless_subsecond_window`) in each authoritative corrected-source local directory. All 28 per-pair scenario JSON files report `passed`. Set each of the four `pairs[*].local.s1_scenarios` values to `7`; no other counters were changed.

Corrected the consolidated report to point to `corrected-source/<pair>/local/`, with v2.0.0 at `corrected-source/v2.0.0/local-corrected/`. Disclosed the retained pre-correction v1.5.4 local S2 outcome: 38/46 passed; all eight metadata-access cases failed the access audit with “observed candidate-object access differs from the case contract.” That run used the fixture manifest under `build/series-fixtures-sc004-20261106/`, while the SQL corpus resolves its candidate paths under canonical `build/series-fixtures/`. The corrected-source run used the canonical fixture root and passed 46/46. The failed run remains in place.

Corrected the source-digest history for the post-bind replacement expectation. The `v1.5.4/controlled-remote` run expecting HTTP 412 used pre-correction digest `98b40a9b76f0a63be35165f85187c6f714287f45505fc2d5c5b716109d460cd3`, not the PASS run digest `8f1f2624fc64951aa42e35625f90c7008ff18672c4e114000dfada4c7e6bef6e`. The retained run shows that this replacement expectation failed: the fixture returned HTTP 200 with a changed ETag and the query reported `object identity changed`; the separate injected HTTP 412 case passed. Between the two build-manifest timestamps, the only hashed source inputs with newer modification times were `scripts/validate-series.py` and `test/tools/validate_series_test.py` (the four hashed top-level build files were not newer). The corrected validator checks the HTTP 200 replacement response, changed ETag, and identity diagnostic; its unit test covers changed and unchanged ETags. The retained build manifests contain aggregate digests rather than per-file hashes, so they do not support a line-by-line reconstruction beyond those file and behavior records.

The follow-up manifest's original `source_identity` remains unchanged: extension source digest `8f1f2624…` and tracked dirty-diff SHA-256 `05e9cd9929e79e9207e8e0159ff52d22fa672888016fe54ce7bad15d89aa350b`. Updated both recorded `report.md` hashes and its byte size after editing that prose. The manifest has no self-hash entry. Rehashed all 44 `evidence_file_hashes` entries after the update; all 44 match.

## R3-03 — controlled S4 wording

Narrowed the follow-up report, acceptance contract, quickstart, and both READMEs to the observed result: injected non-absence faults failed without being silently skipped; under official HTTPFS, HTTP(S) 403/412 surfaced as unknown-status `[transport]`; the controlled `corrupt` response was HTTP 200 with `not-an-om-object` and failed at range-protocol validation, before OM metadata parsing or decode. No claim is made that these HTTP(S) cases showed structured 403/412 classification or corrupt-OM-payload decoding.

Added a short validator comment stating that `execute_expected_failure` checks only a nonzero query exit and the expected origin response in the audit; it does not check error category/text or ordinary `read_om` parity. No validator assertions were strengthened, and no new corrupt-payload fixture was added.

## R3-04 — replacement cleanup

Changed post-bind mutation cleanup to issue `restore` for both remove and replace actions before running recovery. The fixture's replacement state therefore returns to normal even when replacement bytes or ETag match the original.

## Validation performed

- `python3 test/tools/validate_series_test.py` — **25 tests passed**.
- `python3 test/tools/series_remote_test.py` — **8 tests passed**.
- Parsed `scripts/validate-series.py` with Python AST and checked that post-mutation control calls restore state; passed.
- Parsed the follow-up manifest, verified the four S1 counters and all 28 corrected-source S1 scenario results, and rehashed all 44 evidence entries; passed.
- Targeted `git diff --check` on the changed documentation, evidence, review, and validator files; passed.
- `uname -m` returned `aarch64`.

No four-pair rebuild, full Feature 010 suite, Feature 008 suite, controlled-remote 117-scenario matrix, or full S3/S4/S7 acceptance was run for this review-fix.

## Source binding after the validator edit

Recomputed `scripts/version_matrix.py::extension_source_digest` after this review-fix: **`5c8701f5162c5248d690af5cbc548135cc3c71af62a2232075805b8cfedcb7ee`**. It does not match the follow-up PASS snapshot digest `8f1f2624fc64951aa42e35625f90c7008ff18672c4e114000dfada4c7e6bef6e` because this review-fix changed `scripts/validate-series.py`, which is included in the digest. That file is the only source-digest input changed in this review-fix. The original follow-up `source_identity` and dirty-diff binding were left intact; the old runs were not rebound to this digest.

Production source hashes remain unchanged:

- `src/om/reader.cpp`: `4949a0ff7632aa595f7a94215af9009d65dae7208facf826486d725d1550de91`
- `src/include/duckomo/om_reader.hpp`: `431d61fc552055097592f08938d52853c30545b5ec6516e5fc0fba7a40d76d04`
- `src/scan/read_om.cpp`: `a49a7dae1e07975765c9f19e6929e8b9284f067b1b5604d26afdb2dbbcc9a741`

## Preserved boundaries

No Feature 008 or 010 full suite or matrix was run. T042/T043 and Feature 007 bilinear were not implemented. No evidence tree was deleted, moved, compressed, uploaded, or rewritten; the first 2026-11-06 evidence and the historical `run-20260803-linux-arm64/` tree remain intact. No commit, push, amend, rebase, reset, or stash was performed.
