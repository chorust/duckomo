# AArch64 ASan/UBSan validation

Date: 2026-09-29. Environment: Linux AArch64, GCC 13, DuckDB v1.5.4 release tree. Command `make sanitizer-test` exited 0. CMake compiled separate instrumented copies with `-fsanitize=address,undefined -fno-omit-frame-pointer`; release performance artifacts remained the source for I/O figures.

The sanitizer build log shows `duckomo_sanitizer_loadable_extension` and `duckomo_extension_sanitized` compiling `src/grid/regular_grid.cpp`, `src/grid/spatial_layout.cpp`, `src/grid/domain_registry.cpp`, `src/scan/spatial_filter.cpp`, `src/scan/spatial_selection.cpp`, `src/scan/read_om.cpp`, the OM reader and local I/O boundary. `duckomo_spatial_selection_sanitizer` also compiles the regular-grid, layout, selection and batch implementation directly with sanitizer flags.

All eight instrumented CTest checks passed:

- batch, raw reader, schema, and generic lifecycle;
- spatial selection bounds/layout/overflow checks;
- spatial I/O including empty, mixed, seam fallback and decoder counters;
- spatial lifecycle including prepared rebinding and 100 success/failure cycles;
- projection evidence including corrupt scan/cancel recovery.

CTest reported `100% tests passed, 0 tests failed out of 8` (3.79 seconds). Spatial edge, overflow, corruption and cancellation checks passed. These sanitizer timings are not used as performance evidence; release bytes/chunks are recorded in [US2](us2.md).
