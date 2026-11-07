# Linux AArch64 Build and Reader-Capacity Fix

The release build failed because the cancellation injection in `test/native/reader_capacity_test.cpp` called a message-taking `duckdb::InterruptException` constructor that is not available in the pinned DuckDB v1.5.4 API. The injection now uses the no-argument constructor; the test still throws the interruption during the second data read and checks cleanup of planned-cache state and the reusable data range.

## Observed results

- `make release` completed successfully. Full build output: [make-release.log](make-release.log).
- `build/release/test/native/reader_capacity_test` passed and printed `reader capacity bounds passed`. Output: [reader-capacity-test.log](reader-capacity-test.log).
- `test/sql/read_om_series_validation.test` passed all 69 assertions on the local release build. Output: [read-om-series-validation.log](read-om-series-validation.log).
- Focused series planner, binding, error/lifecycle and schema native tests passed on the local release build. The final-source pinned-pair results are recorded separately in [the current acceptance report](../acceptance-20261106-linux-arm64/report.md).

This is compatibility and regression evidence only. It does not claim Feature 008 PASS: Feature 008 SC-006 remains relaxed / won’t meet the original ≥20% target, and Feature 008 T042/T043 remain unchecked.
