# US1 verification: explicit grid and verified domain

Date: 2026-09-29
Environment: Linux AArch64, DuckDB v1.5.4, release build.

Verification passed:

- `make release` exited 0 after adding the domain registry and the independent domain test.
- `build/release/test/unittest test/sql/spatial.test` passed 203 assertions, covering the value-only regression, explicit-grid DESCRIBE, coordinate outputs, layout/order cases, edge coordinates, configuration errors, coordinate-name conflicts, domain conflicts, and unknown domains.
- `regular_grid_test`, `spatial_layout_test`, `spatial_metrics_test`, `spatial_callback_test`, and `projection_evidence_test` passed. Projection checks confirm coordinate/cardinality/value slots preserve request order, duplicate value dependencies are decoded once, and the value-only projection path retains its behavior.
- `scripts/validate.sh build/release` exited 0, including raw/read_om/projection SQL and existing scanner evidence.
- `domain_reference_test` verified both the equivalent explicit grid and registered `ncep_gfswave025` across all 1,038,240 logical positions and all 15 variables. Each mode matched the fixed official-reader value references exactly, matched NULL positions, and matched independent coordinates within the predeclared `1e-9` degree tolerance. Full details and sample identity are in [domain.md](domain.md).
- A real-domain summary query returned 1,038,240 total rows, latitude range `[-90,90]`, longitude range `[-180,179.75]`, rectangle count 1,681, seam-OR count 57,680, and ordinary reversed-BETWEEN count 0.

The first registry entry is restricted to shape `[721,1440]`, matching per-variable `coordinates` metadata `[lat,lon]`, and the verified grid definition. Unknown names, other shapes, and explicit `spatial_axes` overrides fail at bind time. No path-based or shape-only inference is used.

This closes US1 correctness gates on AArch64 only. It establishes coordinate/value correctness and domain provenance; it makes no claim that spatial predicates reduce I/O.
