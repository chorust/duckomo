# US2 local selection, parallelism, and memory evidence

**Recorded:** 2026-10-07  
**Scope:** local synthetic subchecks and repository regression only. This record does not pass the complete H2, H4, or H5 gate.

## Commands and results

| Command or evidence | Exit | Result |
|---|---:|---|
| `DUCKOMO_EVIDENCE_DIR=build/evidence-validate-20261007-continue8 scripts/validate.sh build/release-vcpkg` | 0 | SQL/native regressions, 15 ASan/UBSan checks, fixture/reference hash verification and regeneration, and projection/dimensions/selection/parallel harnesses passed. Output: `build/evidence-validate-20261007-continue8/`. |
| Local US2 run in `us2-run-20261007-fix4/us2-local/manifest.json` | 0 for each recorded local command | Native selector, SQL multiset cases, parallel scan, lifecycle, reader capacity, synthetic work-peak comparisons, and preparation-cancellation checks passed. |
| `duckomo_grid_validation --cases H2,H4,H5` recorded in `us2-run-20261007-fix4/manifest.json` | 2 | Correctly failed closed with the full gates marked `not-run`; required real/independent and remote evidence is absent. This is not a local subcheck failure. |

The local harness reports `pass` for H2 materialized-selection subchecks, H4 parallel/lifecycle subchecks, H5 reader capacity, H5 four-grid synthetic work-peak, and preparation cancellation. The four work-peak fixtures use identical chunks within each size pair, at least 10x spatial-size growth, and the same narrow result. They are explicitly synthetic.

## Gate disposition

| Gate | Complete status | Remaining condition |
|---|---|---|
| H2 — selection correctness | `not-run` | Synthetic full/local multiset and selector cases pass; required producer-backed inputs and independent position references are incomplete. |
| H4 — parallelism and lifecycle | `not-run` | Local multi-worker/lifecycle checks pass; the frozen real-sample and complete independent gate evidence is absent. |
| H5 — memory and cancellation | `not-run` | Local capacity, four-grid synthetic work-peak, preparation cancellation, reader-buffer replacement failure, loopback attempt bounds, and cancelled-query QueryEnd release checks pass. The complete H5 gate remains `not-run`; controlled service evidence and server JSONL are tracked by T055. See [the latest H5 local refresh](h5-local-refresh-20261008-t054-closed/) and [US3 progress](us3-progress.md). |

## Evidence boundary

T003's four-family sample inventory is complete; T004 remains open. The real Open-Meteo objects do not yet have verified complete coordinate/source-position mappings. The ECMWF HRES O1280 sample is supplemental and its Gaussian row/point order is not mapped. The CHMI Lambert producer definition is now paired to its real sample, but its independent coordinate/value checks are not run. These objects cannot yet support a valid full/local repeated-block profile for T039 or replace the required N160, N320, or N320-region evidence. No complete H2/H4/H5 gate or producer support claim is made here.
