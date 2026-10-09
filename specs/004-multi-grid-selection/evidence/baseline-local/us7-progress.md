# US7 local implementation progress

Recorded 2026-10-07. This is implementation progress only; it is not an H0–H9 evidence run.

## Added contract coverage

- `scan_metrics_v4_test.cpp` now covers LIMIT-style early stop, downstream failure, cancellation, NULL for unobserved fields, and byte-for-byte embedding of the v3 snapshot in v4. `session_metrics_test.cpp` already covers connection-local metrics and one QueryEnd publication for multiple scans.
- `grid_evidence_test.py` defines guards for frozen UTC input, exact command exit codes, artifact hashes, per-domain evidence levels, coordinate tolerances, complete cost counters, strict full/local reduction, and exact client/server attempt/body-byte reconciliation.
- `scripts/validate.sh` registers the version-matrix and evidence-contract test programs. No tests or builds were run in this continuation.

## Remaining

- T086 still needs the H0–H8 runner to write complete scan/artifact/attempt evidence and reject a requested gate that is not run. The contract tests do not supply those artifacts.
- T088 sample query generation still lacks the required N160/N320/region real inputs and independent references. T089 requires an independent reviewer; T093 requires the requested validation and build runs.
- The HRES O1280 object remains supplemental Gaussian-family evidence. It does not satisfy N160, N320, or the N320-region acceptance targets.
