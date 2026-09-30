# Phase 3 implementation baseline

Date: 2026-09-29

Host: Linux AArch64. At the time of this baseline, the roadmap still named Linux x86_64 as a target; the later scope decision deferred it from Phase 3, and this run did not validate that architecture.

Commands:

- `make release` — exit 0; configured pinned DuckDB v1.5.4 (commit prefix `08e34c44`) and built the current extension/tests.
- `./scripts/validate.sh build/release` — exit 0.

Existing validation covered 159 SQL assertions across `raw.test` (23), `read_om.test` (107), and `projection.test` (29); five native checks; 29 fixture/reference/negative asset SHA-256 values; official writer fixture regeneration/diff; four release projection metric scenarios. Results: all passed.

This is a pre-change Phase 0–2 baseline only. It provides no spatial SQL, spatial I/O, real domain coordinate, or x86_64 evidence.
