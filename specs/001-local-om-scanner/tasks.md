---
description: "Implementation tasks for the Phase 0–2 local OM scanner"
---

# Tasks: Phase 0–2 本地 OM 可用扫描器

**Input**: `specs/001-local-om-scanner/{spec,plan,research,data-model,quickstart}.md` and `contracts/`

**Prerequisites**: Linux x86_64; the fixed upstream commits in `research.md`. This repository currently contains design documents but no implementation.

**Tests**: Required by FR-012–014, SC-001–006 and `contracts/validation-evidence.md`. Write the specified SQL/native tests before their implementation and observe the expected failure. Fixture generation and its independent official-reader oracle precede those tests.

**Organization**: Story phases match the roadmap's implementation Phase 0 (US1), Phase 1 (US2), and Phase 2 (US3). These are distinct from the Spec Kit task phase numbers below.

## Format: `[ID] [P?] [Story] Description`

- `[P]` means the task may run alongside other ready tasks on different files.
- `[USn]` identifies a user-story task. Setup, foundation, and polish tasks have no story label.
- Paths are relative to the repository root. Each task names its concrete output file(s).

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Establish the fixed, reproducible extension build and test entry points.

- [X] T001 Add DuckDB `08e34c447bae34eaee3723cac61f2878b6bdf787`, OM `d8855e418e2231ae8439f0c7e840fa3f93b371e3`, and extension-ci-tools `b777c70d30942cca5bef62d6d4fa23a13362f398` as fixed submodules in `.gitmodules`, with gitlinks at `duckdb/`, `third_party/om-file-format/`, and `extension-ci-tools/`; use extension-template `cfaf3e236008e782d27f4341b0ee036002d0a449` as the scaffold reference only.
- [X] T002 [P] Change `.gitignore` so the root `Makefile` is tracked while generated Makefiles in build directories remain ignored.
- [X] T003 Create `CMakeLists.txt` and `extension_config.cmake` for a C++17 DuckDB v1.5.4 static/loadable `duckomo` extension; compile official `third_party/om-file-format/c/src/*.c` as GNU C11 PIC without `-march=native`, link both extension variants, and omit unused template dependencies.
- [X] T004 Create root `Makefile` with working `make release` and `make test` targets that use the fixed DuckDB/tooling checkout and produce the CLI, extension, and native tools at the paths in `quickstart.md`.
- [X] T005 Create the minimal extension registration and load entry points in `src/om_extension.cpp`; make a load smoke check possible before table functions are added.
- [X] T006 [P] Create `test/CMakeLists.txt` with build targets for `duckomo_fixture_tool`, `duckomo_validation`, native checks, and DuckDB SQLLogicTest files in `test/sql/`; wire these into `make test`.

**Checkpoint**: The fixed source tree builds, the extension loads, and the test runners can start with no Python dependency at query runtime.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Provide one safe official-reader path shared by all three stories. Finish this phase before story work.

- [X] T007 Define owned metadata buffers, borrowed OM variable lifetimes, decoder/scratch ownership, checked 64-bit shape products, typed reader errors, and query-scoped state in `src/include/duckomo/om_reader.hpp`; ensure decoder parameters remain alive through the final decode.
- [X] T008 Implement single-local-file path validation and bounded positional reads using `FileSystem::GetLocal(*context.db)` in `src/om/local_file.cpp` with declarations in `src/include/duckomo/local_file.hpp`; reject NULL/nonconstant paths, glob, URI, directory, unreadable files, and out-of-file/overflowing ranges with distinct error categories.
- [X] T009 Implement official OM v3 header/metadata/index/data request execution and RAII cleanup in `src/om/reader.cpp`; use official Sans-I/O offsets and sizes, preserve metadata backing storage, translate upstream errors, and never implement OM chunk or compression parsing locally.
- [X] T010 [P] Implement checked row-major linear-index to offset/count conversion and last-axis contiguous segments bounded by DuckDB's standard vector size in `src/scan/batch.cpp` with declarations in `src/include/duckomo/batch.hpp`; cover non-square shapes, rank 1–8, overflow, and batch boundaries.

**Checkpoint**: The reader can address a local OM logical slice without leaked handles or homemade byte-range planning.

---

## Phase 3: User Story 1 — 直接查询本地单变量文件 (P1) 🎯 MVP

**Goal**: `read_om_raw(path)` reads a supported v3 root Float32/FPX array directly into DuckDB, with values independently checked against the official reader.

**Independent Test**: Build and load the extension; scan `raw.om` and `special.om` without Python. Compare every position against the independent official oracle, including shape `[2,3]`, NULL for NaN, ±Inf, ±0, a non-square/cross-batch case, and failures followed by a valid query.

### Fixture and failing tests

- [X] T011 [US1] Implement `test/tools/duckomo_fixture_tool.cpp` using the fixed official OM writer for `raw.om` and `special.om`, plus a separate official-reader code path for reference CSV; include full-file FPX NaN/Inf roundtrip and a deterministic non-square, cross-batch root array, without calling scanner flattening code.
- [X] T012 [US1] Generate and commit `test/data/raw.om`, `test/data/special.om`, `test/data/raw_large.om`, their reference CSV files in `test/data/`, and `test/data/manifest.json` with SHA-256, exact generation command, upstream commit, format/layout/shape/chunk shape, expected schema, NULL positions, and zero Float32 comparison tolerance; record reproducible truncation mutations separately.
- [X] T013 [P] [US1] Write initially failing native per-position oracle tests in `test/native/raw_reader_test.cpp` for last-axis-fastest order, multiple batches, finite Float32 equality, NaN→NULL, ±Inf/±0, and root-array shape/rank/FPX support boundaries.
- [X] T014 [P] [US1] Write initially failing SQLLogicTest cases in `test/sql/raw.test` for `read_om_raw`, `value FLOAT`, six rows 0–5, Python-free load/query, NULL behavior, and nonexistent/unreadable/truncated/unsupported files with error categories.
- [X] T015 [P] [US1] Write initially failing failure-and-recovery checks in `test/native/lifecycle_test.cpp` for cancellation, partial scan error, successful query after failure, and no monotonically increasing file-descriptor count across 100 valid/error queries.

### Implementation and evidence

- [X] T016 [US1] Implement `read_om_raw` bind/global scan and bounded `DataChunk` output in `src/scan/raw_scan.cpp` with declarations in `src/include/duckomo/raw_scan.hpp`; require v3 root Float32/FPX, positive rank 1–8 axes/chunks, checked row count, and complete validation before output where metadata permits.
- [X] T017 [US1] Register `read_om_raw(path VARCHAR)` in `src/om_extension.cpp`, mapping NaN to DuckDB NULL, preserving ±Inf, and propagating read/cancel errors without treating partial output as a successful query.
- [X] T018 [US1] Run `test/native/raw_reader_test.cpp`, `test/native/lifecycle_test.cpp`, and `test/sql/raw.test`; record commands, exit codes, pinned commits, fixture hashes, file version, read boundaries, oracle comparison, Python-free query, and any necessary contract correction in `specs/001-local-om-scanner/evidence/phase0.md`. Do not advance to US2 if official FPX roundtrip or pinned APIs violate the contract; update `specs/001-local-om-scanner/contracts/sql-interface.md` and `docs/spec.md` before retrying.

**Checkpoint**: Phase 0 evidence demonstrates an independently verified local single-variable query.

---

## Phase 4: User Story 2 — 理解文件结构并查询全部变量 (P2)

**Goal**: `read_om(path, dimensions := ...)` exposes a stable schema and scans every supported root or hierarchical variable only when explicit axis evidence proves alignment.

**Independent Test**: `DESCRIBE` and full scans of `raw.om`, `multi.om`, and `nested.om` match the manifest; repeated queries preserve path names/types/order and each row's aligned values. Missing/wrong axes, ambiguous names, unsupported layout/type/version, and requested nonexistent coordinate columns fail as specified.

### Fixture and failing tests

- [X] T019 [US2] Extend `test/tools/duckomo_fixture_tool.cpp` to generate official `multi.om` and `nested.om` with independently read reference values, and reproducible unsupported-version/type/compression, zero-axis/empty, duplicate-name, illegal-reference, and shape-mismatch negative assets or documented mutations.
- [X] T020 [US2] Commit `test/data/multi.om`, `test/data/nested.om`, their reference CSV and negative assets, and update `test/data/manifest.json` with canonical variable paths, sorted expected schema, ordered axes, checksums, generation/mutation methods, and rejection reasons.
- [X] T021 [P] [US2] Write initially failing native metadata/schema checks in `test/native/schema_test.cpp` for full hierarchy traversal, escaped `%`/`/` names, invalid UTF-8/NUL/empty names, DuckDB identifier collisions, rank/shape overflow, and metadata-only `DESCRIBE`.
- [X] T022 [P] [US2] Write initially failing SQLLogicTest cases in `test/sql/read_om.test` for root `value`, sorted hierarchical paths, full aligned rows, repeated query stability, exact `dimensions MAP(VARCHAR, VARCHAR[])` key/rank/axis validation, same-shape-different-axis rejection, unsupported files, and unavailable latitude/longitude/time columns.

### Implementation and evidence

- [X] T023 [US2] Traverse the complete official OM v3 metadata tree in `src/om/metadata.cpp` with declarations in `src/include/duckomo/metadata.hpp`; accept only NONE containers and Float32/FPX leaf arrays, reject arrays with children and every unsupported node before scanning, validate file ranges/cycles, and avoid payload decode during bind.
- [X] T024 [US2] Build immutable `BoundSchema` in `src/scan/schema.cpp` with declarations in `src/include/duckomo/schema.hpp`; map root array to `value`, encode hierarchical paths (`%`→`%25`, `/`→`%2F`), sort by UTF-8 bytes, enforce unique DuckDB identifiers, and validate shape/chunk/rank/row count for every array.
- [X] T025 [US2] Parse and validate `dimensions MAP(VARCHAR, VARCHAR[])` in `src/scan/dimensions.cpp` with declarations in `src/include/duckomo/dimensions.hpp`; for two or more arrays require exact key coverage plus equal ordered axis IDs and shapes, reject missing/extra/duplicate/NULL/empty axes, and never infer coordinate semantics.
- [X] T026 [US2] Implement `read_om` bind/scan in `src/scan/read_om.cpp` with declarations in `src/include/duckomo/read_om.hpp`; use the complete schema and one shared logical range per batch, read all value variables as the Phase 1 baseline, preserve NULL/Inf, and release resources on error/cancel.
- [X] T027 [US2] Register `read_om(path VARCHAR, dimensions MAP(VARCHAR, VARCHAR[]) := NULL)` in `src/om_extension.cpp` with bind-time errors for invalid input and no exposed synthetic coordinate columns.
- [X] T028 [US2] Run `test/native/schema_test.cpp` and `test/sql/read_om.test`; record official-reference comparisons, `DESCRIBE`, support/rejection matrix, row alignment, hashes, commands, exit codes, and repeatability in `specs/001-local-om-scanner/evidence/phase1.md`; synchronize the proven support subset in `specs/001-local-om-scanner/contracts/sql-interface.md` and `docs/spec.md` if implementation findings require correction.

**Checkpoint**: Both root and hierarchical full scans are independently reproducible; projection is still disabled for a full-read baseline.

---

## Phase 5: User Story 3 — 仅为查询所需变量付出读取成本 (P3)

**Goal**: Projection scans only variables the DuckDB plan actually needs and produces measured evidence of reduced data reads without changing SQL results.

**Independent Test**: On fixed `projection.om`, compare fully consumed full-variable, single-variable, output-plus-filter, expression/reorder/duplicate, aggregation/sort, count-only, and empty-result queries with full-read reference values; validate per-variable actual bytes/requests/decoded chunks, elapsed time, peak RSS, and resource recovery.

### Fixture and failing tests

- [X] T029 [US3] Extend `test/tools/duckomo_fixture_tool.cpp` to generate `projection.om` with at least three independently addressable nonempty FPX payloads, non-square multi-chunk shape, and more than two DuckDB standard batches, using the independent official reader for reference values.
- [X] T030 [US3] Commit `test/data/projection.om` and its reference CSV; update `test/data/manifest.json` with hashes, physical chunk shapes, ordered axes, exact generation command, and expected rows for all projection scenarios.
- [X] T031 [P] [US3] Write initially failing SQLLogicTest cases in `test/sql/projection.test` for selected/reordered/duplicate columns, expressions, filter on an unselected variable, sort/aggregate dependencies, `COUNT(*)`, `WHERE FALSE`, and ordinary no-match filtering; compare with full-read materialized results where appropriate.
- [X] T032 [P] [US3] Write initially failing metric and lifecycle assertions in `test/native/projection_evidence_test.cpp` for single-variable bytes strictly below full scan, zero unneeded data reads/decodes, filter-variable reads, count-only zero index/data reads, 100 valid/error iterations, cancel/recovery, and failure/cancel metrics excluded from performance comparisons.

### Implementation and evidence

- [X] T033 [US3] Implement `ProjectionPlan` in `src/scan/projection.cpp` with declarations in `src/include/duckomo/projection.hpp`; map each requested `column_ids` slot to the immutable schema, deduplicate physical variables, preserve output order, and treat `COLUMN_IDENTIFIER_EMPTY` as an internal cardinality slot rather than a variable index.
- [X] T034 [US3] Enable `projection_pushdown=true`, keep `filter_pushdown=false` and `filter_prune=false`, register the BOOLEAN `COLUMN_IDENTIFIER_EMPTY` virtual column, and consume actual DuckDB `column_ids` in `src/scan/read_om.cpp`; count-only batches set cardinality without decoder/index/data reads, while residual SQL filters remain with DuckDB.
- [X] T035 [P] [US3] Define query-scoped `ScanMetrics` and evidence JSON fields in `src/include/duckomo/metrics.hpp`, including `schema_version`, query/fixture IDs, metadata/index/data byte and request classes, per-variable decode counts, failure completeness, elapsed time, environment/cache policy, and peak RSS.
- [X] T036 [US3] Instrument successful positional reads by metadata/index/data phase and variable in `src/om/local_file.cpp`; count returned bytes and requests only, preserve shared metadata separately, and reconcile totals without inferring chunk layout.
- [X] T037 [US3] Instrument successful `om_decoder_decode_chunks` ranges in `src/om/reader.cpp`, counting actual returned range lengths including repeats; mark failed-call decode counts incomplete and keep counters isolated per query.
- [X] T038 [US3] Implement `test/tools/duckomo_validation.cpp` as a subprocess-based release-build harness that consumes every result, writes `build/evidence/summary.json` plus scenario JSON, records exact SQL, fixture hashes, dependencies, elapsed time and child peak RSS, checks native/reference and SQL result equality, and exits nonzero on any missing or failed required evidence.
- [X] T039 [US3] Run `test/sql/projection.test`, `test/native/projection_evidence_test.cpp`, and the validation harness under the fixed single-thread/no-application-cache policy; record full vs narrow, filter dependency and count metrics, 100-query resource check, cancellation recovery, commands, hashes, environment and exit codes in `specs/001-local-om-scanner/evidence/phase2.md`.

**Checkpoint**: Actual successful I/O and decoder calls prove that unused variables cost zero data reads/decode; result comparisons and resource checks pass.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Make every stage repeatable by another verifier and close the documented exit gates.

- [X] T040 Create `scripts/validate.sh` to run SQLLogicTest, native oracle/schema/projection checks, fixture checksum verification, release metrics, and required evidence-field checks; return nonzero for any missing scenario or failed assertion.
- [X] T041 Add a sanitizer build/run target for native lifecycle, bounds, and corrupt-input checks in `test/CMakeLists.txt`; keep performance measurements on the normal release build and record sanitizer results separately.
- [X] T042 Update `specs/001-local-om-scanner/quickstart.md` with the tested build paths, exact fixture regeneration/validation commands, Python-free load/DESCRIBE/full/narrow SQL, and the recorded behavior of every example if real template or MAP syntax differs.
- [X] T043 Synchronize the proven Phase 0–2 support boundary, axis-alignment rule, and deferred coordinate/filter capabilities in `docs/spec.md` and `docs/architecture.md`; update `specs/001-local-om-scanner/contracts/sql-interface.md` and `specs/001-local-om-scanner/contracts/validation-evidence.md` if the measured implementation requires a contract change.
- [X] T044 Execute `make release`, `make test`, fixture regeneration/hash comparison, `scripts/validate.sh build/release`, native sanitizer checks, and a clean Linux environment without Python/Swift for the four SC-006 operations; document actual commands, exit codes, checksums, SC-001–006 outcomes, and any unresolved limit in `specs/001-local-om-scanner/evidence/final.md`.

---

## Dependencies & Execution Order

### Phase and story graph

```text
Setup (T001–T006) → Foundation (T007–T010) → US1 (T011–T018)
                                                   ↓
                                             US2 (T019–T028)
                                                   ↓
                                             US3 (T029–T039)
                                                   ↓
                                             Polish (T040–T044)
```

- US1 depends on the official-reader foundation and supplies the verified raw scan plus fixtures.
- US2 reuses US1's verified reader, fixture tool, and extension registration; its full-read behavior is independently testable before US3.
- US3 depends on US2's stable complete schema and full-read baseline. It must validate all file variables at bind even when scan projects only one.
- Within each story, generate/reference fixtures first; write the listed tests and observe their failure; implement until they pass; record actual evidence before moving on. A failed Phase 0 official FPX roundtrip blocks US2 pending a corrected contract and evidence.

### Parallel opportunities

- After T001, T002 may proceed alongside CMake work; after T003, T005 and T006 touch separate files.
- After T007, T010's logical batch helper can proceed alongside T008–T009's file/reader adapter.
- US1: after T012, T013 (`test/native/raw_reader_test.cpp`), T014 (`test/sql/raw.test`), and T015 (`test/native/lifecycle_test.cpp`) can be written independently.
- US2: after T020, T021 (`test/native/schema_test.cpp`) and T022 (`test/sql/read_om.test`) can be written independently.
- US3: after T030, T031 (`test/sql/projection.test`) and T032 (`test/native/projection_evidence_test.cpp`) can be written independently; T035's metrics header can be designed in its separate file while T033 develops projection mapping.

## Parallel Execution Examples

### User Story 1

```text
After T012: T013 native oracle checks || T014 SQL raw contract || T015 lifecycle checks
Then: T016 raw scan → T017 registration → T018 Phase 0 evidence
```

### User Story 2

```text
After T020: T021 metadata/schema checks || T022 SQL read_om contract
Then: T023 metadata → T024 schema → T025 dimensions → T026 read_om scan → T027 registration → T028 evidence
```

### User Story 3

```text
After T030: T031 SQL projection checks || T032 metrics/lifecycle checks
Then: T033 projection + T035 metrics contract (different files) → T034 scan wiring → T036/T037 instrumentation → T038 harness → T039 evidence
```

## Implementation Strategy

1. **MVP**: Complete setup, foundation, and US1. Stop at the Phase 0 checkpoint and verify the official oracle, NaN roundtrip, errors, and Python-free raw query.
2. **Increment 2**: Complete US2 and independently verify root/hierarchy schema plus full aligned scans before changing scan cost.
3. **Increment 3**: Complete US3 and validate actual reads/decode alongside exact SQL results; preserve the US2 full-read baseline as the comparison.
4. **Exit**: Complete polish and only claim SC-001–006 when `evidence/final.md` contains actual passing runs. Design documents or generated example JSON are not passing evidence.
