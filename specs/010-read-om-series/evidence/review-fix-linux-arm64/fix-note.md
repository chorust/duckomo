# Feature 010 review fixes: R1 and R2

This note records only the two settled review findings. The implementation is based on repository HEAD `80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9` plus the dirty Feature 010 worktree; no commit was created.

## Changes

- **R1 — spatial declarations before child access.** `read_om_series` now parses and validates the declared grid/domain and required spatial axes against the explicit dimensions before entering the child-open loop. This applies to present candidates, all-missing windows, and slotless windows.
- **R2 — strict validation for slotless candidates.** Each candidate is bound and validated before an empty time-slot selection is omitted from the retained plan. Strict mode therefore checks missing-child presence and metadata even when that candidate contributes no selected time. Omitted candidates still produce typed-empty output and perform no value decoding.

SQL regressions are in `test/sql/read_om_series_validation.test` and `test/sql/read_om_series_missing.test`. They cover invalid grid, domain, and axes declarations; strict missing slotless candidates; a slotless first candidate followed by later chunks; a present strict slotless candidate with zero value decoding; and a present slotless candidate with a time-extent mismatch.

## Fresh linux_arm64 verification

`build-artifacts.json` records the pair pins, CLI identities, freshly linked extension hashes, and restoration checks. The two changed DuckOMO translation units were compiled with each pair's generated CMake rules and linked against its existing pair-matched static archive. The previously reviewed extension at each matrix path was restored byte-for-byte after the fresh artifact was captured.

| DuckDB pair | R1/R2 probes | SQL regression files |
|---|---:|---:|
| v1.5.4 | 17/17 passed | 2/2 passed |
| v1.5.5 | 17/17 passed | 2/2 passed |
| v1.5.6 | 17/17 passed | 2/2 passed |
| v2.0.0 | 17/17 passed | 2/2 passed |

Across the 68 probes, all 48 invalid-spatial-declaration cases failed before any fixture chunk open. The strict present slotless case returned zero rows; its metrics showed zero value data requests, zero value bytes, and zero decoded chunks. The slotless time-extent mismatch failed during metadata validation. Strict missing slotless cases attempted the expected missing chunk, and the cross-chunk case stopped at missing `chunk_0000.om` without opening `chunk_0001.om`. Per-case SQL, `strace` traces, commands, outputs, and hashes are retained beside this note in this directory.

## Acceptance status and task clarification

These targeted review fixes do not complete S2. Historical S2, SC-002, and SC-007 remain **blocked**; FR-002, FR-007, and FR-014 remain **partial** as recorded in the run report. The T010/T016 checkmarks record the earlier authoring and local execution pass; they do not certify the full S2 acceptance corpus. T010's planned embedded-time-metadata conflict case is still absent, and T016's zero-access observation covered representative invalid inputs rather than the full matrix.

Remaining S2 acceptance cases:

- Exercise chunk/slot-count and timestamp-boundary overflow inputs, recording that pure argument failures open no candidate objects.
- Add an actual-object conflict between explicit time parameters and embedded time metadata, including a candidate with no selected slots.
- Complete metadata-dependent mismatch cases for declared type, shape, dimension order, spatial-axis profile, and grid/domain identity against present objects.
- Run the completed invalid-input and mismatch corpus on all four pinned pairs and retain per-case access evidence; keep metadata reads for metadata-dependent checks distinct from pure argument validation.

The historical report header now labels repository source HEAD `80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9` separately from the v1.5.4 DuckDB engine pin `08e34c447bae34eaee3723cac61f2878b6bdf787`. Its historical gate outcomes and final-source digest were left intact.
