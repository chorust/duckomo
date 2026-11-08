# Implementation Plan: read_om_series 跨 chunk 时间序列入口

**Branch**: `main`（未创建/切换分支；feature identifier `010-read-om-series`） | **Date**: 2026-11-05 | **Spec**: [spec.md](spec.md)

**Input**: `specs/010-read-om-series/spec.md`, based on the complete Phase 8 proposal in `docs/roadmap.md` and the operator's scope/constraints.

## Summary

Add a new table function that resolves frozen model time parameters (or explicit overrides), maps a closed valid_time window to named chunk objects, binds each object with an automatically generated time axis, and combines homogeneous existing child rows with UNION ALL semantics. Ordinary `read_om` owns decoding, spatial identity/selection, projection, worker scheduling and metrics. Series provides request/slot arithmetic, initial-missing policy, schema reconciliation and logical composition only.

This session produces specify → plan → tasks artifacts and stops. It does not implement the function, run future gates, alter Community packaging or close adjacent specs. Status remains Draft with runtime SC-001–SC-007 pending. Research decisions are in [research.md](research.md); precise interface and future validation obligations are in [contracts/sql.md](contracts/sql.md) and [contracts/acceptance.md](contracts/acceptance.md).

## Technical Context

**Language/Version**: C++17 for the DuckDB extension; existing official OM C reader/writer; Python 3 for deterministic registry/fixture/validation tooling.

**Primary Dependencies**: Existing DuckDB table-function binder, LogicalGet/filter/UNION operators and compatibility wrappers; pinned official OM C (`d8855e418e2231ae8439f0c7e840fa3f93b371e3`); official HTTPFS for HTTP(S)/S3. No new scientific/geometry runtime dependency; spatial extension is not needed for ordinary explicit-grid series.

**Storage**: Read-only OM v3 objects in the fixed `data/<model>/<variable>/chunk_%04d.om` layout and equivalent local/controlled fixture prefixes. Frozen temporal JSON and generated C++ registry; no runtime discovery or persistent cache.

**Testing**: Existing SQLLogicTest/native CMake targets; planned targeted series SQL/native tests, Python tool tests, controlled HTTP/HTTPS/S3 origin audit, full-multiset manual UNION ALL oracle, relevant existing scanner/HTTPFS/session regressions and sanitizer targets. Test tasks are explicitly required by spec FR-016.

**Target Platform**: **linux_arm64 only** for primary acceptance. Plan isolated pinned v1.5.4/v1.5.5/v1.5.6 builds from `test/data/grids/version-matrix.json` and v2.0.0 from `test/data/engine-compatibility/version-matrix.json`. No architecture/runtime cell is declared passed by this plan; absent resources stay blocked.

**Project Type**: Existing DuckDB loadable extension plus narrow generator/validation tools.

**Performance Goals**: Preserve child selection/projection and zero value I/O for value-free queries. Control expansion memory and bind work with at most 4096 candidates; no percentage speedup, hot-cache, cross-object I/O merge or new async goal.

**Constraints**: Existing read_om single-object and error/identity semantics remain compatible; no new decode/grid implementation; no silent retry, message-based 404 parsing or inferred metadata. Preserve all pre-existing dirty changes, especially 008 code, SC-006 relaxation, Community backfill and evidence. No stash/reset/commit/push; no 007/008/009 implementation or Community packaging work.

**Scale/Scope**: One variable directory and one homogeneous selected schema per call. Initial temporal registry has 11 exact keys. Closed UTC windows, complete dimensions/spatial declarations, skip/strict initial absence and stable typed empty output. Cross-variable/model alignment and run selectors remain excluded.

## Constitution Check

*GATE: Checked before research and rechecked after design; these are document/scope checks, not runtime PASS gates.*

`.specify/memory/constitution.md` is an unfilled template. It defines no ratified principles or actionable gate; do not manufacture constitutional approval. Apply explicit operator constraints and current repository contracts instead:

| Source constraint | Design response | Planning review |
|---|---|---|
| Read full Phase 8 before artifacts | Completed source reading before initializing 010; proposal observations kept separate from evidence | Satisfied for document preparation |
| Series adds no decode/grid logic | Logical union of existing read_om leaves; only narrow binding/schema/error-category seams | Satisfied in design; runtime pending |
| Initial missing defaults to skip, strict errors; object changes propagate | Typed initial-open classification, retain actual child binds, no retry loop | Satisfied in contract; S3/S4 pending |
| Preserve dirty tree and adjacent features | Only new 010 artifacts plus required feature pointer updated this session; planned integration does not extend async/grid/nearest functionality | Verify preservation at session exit |
| Evidence/primary-platform honesty | All runtime gates NOT RUN, only linux_arm64 acceptance, real-object subgate separately required | Satisfied in documents |
| No Community packaging or publishing | No Community/workflow/release tasks; normal docs only in later implementation | Satisfied in scope |
| Stop after tasks | No implement hook invocation, parent polling, feature coding or test execution in this session | Required exit boundary |

**Post-design re-check**: Contracts and data model retain those boundaries. Frozen-registry source verification, all-missing schema parity and binder-version feasibility are explicitly scheduled obligations, not concealed assumed results. No design exception requires user confirmation during this already-authorized planning session.

## Project Structure

### Documentation (this feature)

```text
specs/010-read-om-series/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── sql.md
│   └── acceptance.md
├── checklists/requirements.md
└── tasks.md
```

Actual runtime `evidence/<run>/` is created only by later execution. `.specify/feature.json` is the template-required active feature pointer; it changes from 008 to 010, without modifying any 008 artifact. No roadmap/constitution/agent-context/Community edits are required by these templates.

### Source Code (repository root; proposed future changes)

```text
src/scan/series_plan.cpp                       # request validation, slot arithmetic, naming
src/scan/read_om_series.cpp                    # bind candidate set and logical union/empty relation
src/scan/series_registry.cpp                   # frozen temporal lookup/override resolution
src/include/duckomo/series_plan.hpp
src/include/duckomo/series_registry.hpp
src/include/duckomo/generated_series_registry.hpp
src/include/duckomo/read_om_binding.hpp         # narrow owned child-bind/signature seam
src/include/duckomo/read_om.hpp                 # new function factory
src/scan/read_om.cpp                           # expose reuse seam, ordinary callbacks unchanged
src/scan/schema.cpp + src/include/duckomo/schema.hpp  # shared declaration/output naming helpers
src/include/duckomo/om_reader.hpp               # typed diagnostic metadata only
src/om/remote_file.cpp + src/om/local_file.cpp   # existing classification/open boundaries only
src/compat/duckdb_api.cpp + src/include/duckomo/compat/duckdb_api.hpp # binder/operator API translation if needed
src/om_extension.cpp + CMakeLists.txt           # register/link new function/files

test/data/series/{models.json,fixtures.json}    # provenance and deterministic fixture definitions
scripts/generate-series-registry.py
scripts/generate-series-fixtures.py
scripts/validate-series.py
scripts/series_remote.py                       # dedicated adapter over existing fixture primitives
test/tools/duckomo_fixture_tool.cpp            # extend official-writer fixture recipe only
test/tools/{series_registry_test.py,series_fixtures_test.py,series_remote_test.py,validate_series_test.py}
test/native/{series_plan_test.cpp,series_binding_test.cpp,series_error_test.cpp}
test/sql/{read_om_series.test,read_om_series_validation.test,read_om_series_missing.test,read_om_series_pushdown.test}
test/CMakeLists.txt
README.md + README.en.md + docs/{spec.md,architecture.md} # later user guidance
```

**Structure Decision**: Small temporal planning and registry units plus a logical binder; reuse current scanner instead of introducing a second physical scan framework. New helpers expose immutable facts and ownership rather than raw reader internals. Proposed file names are tasks' implementation targets, not files created in this session.

## Phase 0 — Research decisions

[research.md](research.md) resolves design unknowns from repository evidence: callback/union composition, window versus coordinate-vector semantics, frozen temporal source strategy, typed initial-missing classification, declaration-derived empty schema, metrics lifecycle and platform/evidence scope. Direct local investigation was used; no external browsing or research workers were required/provided. No unresolved clarification markers remain.

The checked-out DuckDB has bind_operator, but its signatures and logical operator details still require actual compilation against every target pair. Producer cadence formulas still require verification when freezing the new registry. These are defined validation obligations with explicit blocked outcomes, not unknown product contracts left to the implementer.

## Phase 1 — Design and contracts

1. Normalize/validate constant input and compute a checked candidate interval before opens. Resolve L/dt and declaration-derived output schema through shared current naming/type rules. Enforce temporal/spatial declaration and 4096-candidate bounds.
2. Bind every candidate through a narrow wrapper over existing `BindReadOmInternal`, passing only inherited options plus generated axes.time. Preserve actual owned bind data and query-scoped identity. Expose a schema/layout fingerprint for comparison without decoding values.
3. Classify only confirmed initial-open absence as skipped in default mode. Strict and all other failures propagate. Introduce typed diagnostic metadata at existing classifier boundaries with unchanged ordinary public diagnostics; no transport/scheduler changes.
4. Require exact child signature/shape/layout homogeneity. Return a typed empty logical relation when no leaves remain; do not find a witness or manufacture metadata validation. Shared-list root and explicit-map array naming limitations are fixed by the SQL contract.
5. Build a balanced logical UNION ALL with exact child valid_time filters and the required root binding index. Keep normal LogicalGet callbacks, virtual columns and filter/projection behavior. Ensure cached/prepared binding follows existing rebind policy and does not freeze an initial missing decision across executions.
6. Validate S0–S7 via fixtures, independent manual union, controlled faults and actual pinned pairs. Metrics remain per child; attempted/bound/executed distinctions are explicit.

Outputs: [data-model.md](data-model.md), [SQL contract](contracts/sql.md), [acceptance contract](contracts/acceptance.md), [quickstart.md](quickstart.md). Tasks generation follows only after these files are complete.

## Delivery and risks for the later implementation

| Risk | Containment / checkpoint |
|---|---|
| Binder API differences across 1.5/2.0, root bindings or lost virtual cardinality columns | Early S0 seam compilation; compatibility adapter only; inspect EXPLAIN plus COUNT/projection tests before wider integration |
| Eager discovery then rebind loses object identity | Retain actual child bind data; never bind SQL strings after a separate existence pass |
| All-missing schema diverges from real schema | Factor current naming/output/source-type helpers; compare DESCRIBE of present/missing and named-array fixtures |
| Missing classifier hides permission/transport/change | Typed category at initial open only; compare old read_om errors and post-bind fault matrix |
| Large bind plans consume metadata memory or recursion | 4096 bound, balanced union, checked arithmetic and existing query memory failure/cancellation; no value materialization |
| UNION/filter boundary disables selection or causes repeated decoding | Per-child multiset/counter checks; original callbacks and full residual WHERE remain; no aggregate-only evidence |
| Frozen time keys drift or get confused with grid aliases | Separate temporal registry, exact data keys, per-entry source/snapshot hashes, explicit overrides |
| Rolling public objects disappear | Controlled fixtures for reproducibility; real adjacent-object support subgate freezes exact objects or remains blocked |
| Integration touches dirty 008 surfaces | Narrow additive seams only, preserve diff baseline; no async/merge/performance/Community task expansion |

## Complexity Tracking

No ratified constitution violations exist to justify. Necessary additions are one temporal planner, a small frozen temporal registry, and a logical binder plus narrow shared schema/error seams. A new decoder, spatial abstraction, scan executor, cache, metrics schema or series I/O scheduler is expressly excluded.
