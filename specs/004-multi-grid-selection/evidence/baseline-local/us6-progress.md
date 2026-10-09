# US6 API compatibility implementation progress

Recorded 2026-10-06. This records adapter source work only; it is not a DuckDB 2.0 build or H8 result.

## Implemented

- `src/include/duckomo/compat/duckdb_api.hpp` and `src/compat/duckdb_api.cpp` isolate Identifier output names, typed table-function kwargs, and `BoundColumnRefExpression` binding/depth access.
- Table-function named-argument lookup now also goes through the compatibility layer. DuckDB 1.5.4 exposes a case-insensitive `named_parameter_map_t` keyed by strings; the pinned 2.0 API shape uses `named_argument_map_t` keyed by `Identifier`. The adapter iterates entries and compares their normalized name case-insensitively, so `read_om` no longer names either map type directly.
- `src/scan/read_om.cpp`, its filters/schema helpers, raw scan, and registration code consume the adapter for the API differences listed in T074.

## Not verified

- No DuckDB 1.5.4 or 2.0 compatibility build was run after these changes. T079 remains pending and T074's adapter is implementation-complete but build-unverified.
- ABI3 provider pairing, 2.0 httpfs patching, and H8 remain pending. No cross-pair extension load or compatibility gate was run.

## Matrix entry follow-up

- The baseline Makefile path remains available. `matrix-release` and `matrix-test` now route through the manifest/pair/output-root scripts without changing the shared DuckDB checkout.
- `scripts/validate.sh --matrix ... --pair ...` resolves the current input build identity, requires matching `build-manifest.json` and `stage.json`, and checks the DuckDB, DuckOMO, and httpfs artifact hashes before selecting the pair-specific release directory.
- Added paired-identity native and SQL compatibility checks to CMake and the validation inventory. These changes implement T072/T078; they have not been built or executed, so no ABI compatibility or build-verified claim is made.
- Static review found the matrix resolver validating patch/header/overlay SHA-256 values with the 40-character Git commit pattern. The resolver now uses separate 40-character commit and 64-character content-digest checks; `version_matrix_test.py` covers both lengths, bad patch identities, ordered one-time overlay staging, source isolation, and input-only build IDs. Those tests remain unrun.
