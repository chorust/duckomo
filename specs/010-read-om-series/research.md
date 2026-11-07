# Research: read_om_series

**Date**: 2026-11-05

**Status**: Repository-based design research complete for planning; feasibility builds and runtime gates **NOT RUN**. No network fetch or live-bucket experiment was performed. Phase 8 was read in full before artifacts were created.

## R1 — Temporal composition over the existing scanner

**Decision**: Bind the complete candidate set, then construct a logical UNION ALL of ordinary `read_om` child scans, each with an internally generated `axes.time` and an exact `valid_time` range filter. Prefer the existing DuckDB `bind_operator` callback so already-bound child data and object identities can be retained rather than opening objects for discovery and rebinding them from SQL strings. The root must expose the caller-provided bind index. A balanced union tree bounds recursion; the draft expansion cap is 4096 candidate chunks.

**Repository evidence**: `duckdb/src/include/duckdb/function/table_function.hpp` exposes `bind_operator` and `bind_replace`. `duckdb/src/planner/binder/tableref/bind_table_function.cpp::Binder::BindTableFunctionInternal` checks the returned root's bindings, resolves types and illustrates normal `LogicalGet` construction. `src/scan/read_om.cpp::GetReadOmFunction` already supplies projection pushdown, virtual columns, `ObserveComplexFilter`, NO_ORDER and current synchronous/asynchronous scan callbacks. `src/om_extension.cpp` is the registration entry point.

**Rationale**: Retaining the actual child bind data preserves per-object identity, metadata ownership and existing filter/parallel behavior. Neither an eager full-result materialization nor a new scan executor is needed. Use an internal child-binding descriptor with an opaque owned `FunctionData`, output signature, logical shape/layout fingerprint and table-function callbacks; keep reader internals private. Generate column references/filters as typed expressions, not interpolated SQL.

**Alternatives considered**: Textual bind replacement would need duplicate discovery opens to learn missing objects and schema, creating a race unless identity were transferred. A single custom physical series scanner would duplicate cancellation, projection, tasks, metrics and the in-progress 008 async lifecycle. Neither is selected.

**Feasibility boundary**: Only the checked-out engine source was inspected. Compile the narrow plan/binding seam against the pinned 1.5.x and 2.0 pairs during implementation before relying on it; compatibility wrappers may translate binder/operator APIs only. A failure here is a blocked design checkpoint, not permission to redesign decoder or 008 scheduling.

## R2 — Meaning of valid_times, slot math and axes

**Decision**: The series `valid_times` list is exactly two endpoints of a closed window, with `start`/`end` as an exclusive alternative. It must never reach the child's `valid_times` option. Generate `axes := {'time': {'axis': 'time', 'start': origin, 'step': dt}}`, using the actual declared time dimension. Require exactly one time dimension in every array, with extent L; other raw dimension names do not gain semantic columns.

**Repository evidence**: `BindTemporalConfiguration` in `src/scan/read_om.cpp` interprets existing `valid_times` as a complete coordinate vector and rejects combination with `axes.time`. `src/scan/semantic_axes.cpp` accepts `axis/start/step`, normalizes TIMESTAMP/TIMESTAMPTZ, rejects calendar-month intervals and checks final coordinate overflow. `src/scan/axis_selection.cpp` and `axis_filter.cpp` already select logical slots.

**Rationale**: Distinguishes two different public contracts while reusing existing time coordinate/filter machinery. Use signed checked microsecond arithmetic and mathematical floor, including pre-epoch values; no rounding window endpoints to seconds. Effective dt must be positive whole seconds, matching the producer registry; endpoint precision remains microseconds.

**Alternatives considered**: Passing two endpoints as child coordinate values would corrupt all arrays with L != 2. Deriving time from forecast availability would conflate NULL slots with a different cadence. Both are rejected.

## R3 — Frozen temporal registry and overrides

**Decision**: Add a small temporal registry with 11 exact data-model keys, using the same upstream pin/provenance conventions as the grid registry but a separate temporal data source. Do not mutate `test/data/grids/definitions.json`, grid identities or existing validation levels merely to register time. Generate deterministic C++ entries from `test/data/series/models.json`; record upstream source and frozen snapshot hashes there. `scripts/generate-series-registry.py --check` is a planned interface, not an existing tool.

**Repository evidence**: `test/data/grids/definitions.json` freezes Open-Meteo commit `b06f4760fd1f997e5559bb380f64c5e496b4a509`; `test/data/grids/source-manifest.json` records pinned producer material. `scripts/generate-grid-registry.py` demonstrates a frozen JSON → generated C++ workflow without runtime Python/network. Archived local snapshots under `specs/004-multi-grid-selection/evidence/baseline-local/open-meteo-s3-catalog-metadata-20261008/snapshots/data/<model>/static/meta.json` contain the following values; all 11 files were read locally during this planning session:

| Exact model key | chunk_time_length | temporal_resolution_seconds |
|---|---:|---:|
| ecmwf_ifs | 504 | 3600 |
| ecmwf_ifs025 | 104 | 10800 |
| ncep_gfs013 | 481 | 3600 |
| ncep_gfs025 | 481 | 3600 |
| dwd_icon | 253 | 3600 |
| dwd_icon_eu | 193 | 3600 |
| dwd_icon_d2 | 121 | 3600 |
| jma_gsm | 110 | 21600 |
| cmc_gem_gdps | 110 | 10800 |
| meteofrance_arpege_world025 | 210 | 3600 |
| ukmo_global_deterministic_10km | 193 | 3600 |

**Rationale**: These match the Phase 8 proposal without relying on live metadata availability. Implementation must verify the corresponding pinned producer length/cadence formulas and exact key mapping, retain snapshot hashes and report discrepancies before freezing the new data. Planning has checked local snapshot fields, not independently revalidated all producer formulas or any live bucket. `cmc_gem_gdps` is an exact temporal key; do not silently substitute `cmc_gem_gdps_15km` or assume grid domain aliases are temporal aliases.

**Overrides**: A supplied field wins over a registered field; the other registered field can be inherited. Unknown/omitted model requires both fields. `time_step` is INTERVAL, not an overloaded numeric/string field. No model-prefix string matching or spatial inference.

**Alternatives considered**: Runtime meta.json discovery adds a failure mode unrelated to reading existing chunks; embedding unproven values for every grid domain inflates support claims. Neither is selected.

## R4 — Missing-object classification without hiding faults

**Decision**: Skip only typed, confirmed object absence from the **initial open**. Use structured HTTP status 404 or local ENOENT; do not parse exception messages. Preserve current public ReaderError codes/text and add only the narrow machine-readable failure category needed by the series binder. Existing ordinary `read_om` errors must remain compatible.

**Repository evidence**: `src/om/remote_file.cpp::ClassifyRemoteFailure` already distinguishes structured 404, 403 and 412 and deliberately refuses to infer categories from raw HTTPFS message text. It currently converts the classification into sanitized text; `ReaderError` in `src/include/duckomo/om_reader.hpp` exposes only a broad code and OM error. This is an actual missing seam, not an existing typed 404 API. `RemoteReadSession::ConfirmIdentity` detects size/version changes (its direct diagnostic currently says object identity changed); the HTTP 412 path supplies `[object_changed]`.

**Rationale**: Add optional typed diagnostic metadata to ReaderError (category and initial-open stage), populated at the current classification boundary; cancellation remains separate. Do not alter transport, retries, merge/prefetch settings, version checks or async tasks. Keep the actual successful child bind state; a second open/read that fails never becomes a skip. Local permission errors and non-regular paths are not absence.

**Alternatives considered**: `FileExists` prechecks can collapse forbidden/unreachable into missing and introduce an unchecked race; searching messages for “404” violates existing redaction policy. Catching all FileIo errors would lose failures. All rejected.

**008 boundary**: The working tree contains unfinished 008 code, relaxed SC-006, community backfill and evidence. A later narrowly scoped classification addition must preserve those changes and prove old diagnostics remain compatible. If that cannot be isolated, stop the later implementation at the seam rather than expanding 008 work.

## R5 — Schema when every object is absent

**Decision**: Make complete caller declarations sufficient to name an empty relation. Existing supported values are Float32: `src/scan/schema.cpp::BuildBoundSchema` derives `value` for root `/` and canonical path names for nested arrays; spatial output is `lat DOUBLE, lon DOUBLE`, then generated `valid_time TIMESTAMP`, then optional existing `om_source` type. Reuse/factor these naming/output helpers so empty and real children use identical rules.

A dimensions shared list is the declared single-root profile. A dimensions map is an explicit selected-array manifest with exact complete coverage, deterministic UTF-8 canonical path order, root aliases normalized as today, and no case-insensitive collisions. Existing non-root/multiple-array data requires that map even if coordinates metadata would otherwise make a single-object call less explicit. Do not introduce public schema/sample/witness parameters. This explicit declaration rule is necessary to meet the all-missing no-rows contract without reading unrelated objects.

**Rationale**: There is no object metadata to infer from when all are missing. The declared logical schema is available without bytes; validation against actual time extent, non-time shape, selected arrays and spatial identity occurs for every existing object. Empty-only binding validates declaration syntax and domain/grid definitions, but does not fabricate an object shape or claim absent-object metadata validation. Grid logic remains delegated to existing parsers/identity helpers.

**Alternatives considered**: Erroring when all objects are missing violates the requested default absence policy. Guessing names from the variable-directory basename changes `read_om` behavior. Reading earlier/later chunks or live meta.json breaks the bounded window and reliability contract. None is selected.

## R6 — Metrics, projection and bounded resources

**Decision**: Reuse ordinary child metrics and worker settings. Publish no series aggregate or new summary API. Each actual child has its own scan_id; bind failures may retain existing records and optimizer-eliminated children need not appear as executed. Tests must distinguish attempted/bound/executed counts. Construct no additional value reads for metrics or shape comparison.

**Repository evidence**: `BindReadOmInternal` creates metrics before opening the object; `GetLastScanMetricsFunction` exposes query_id/scan_id/metrics. Metrics are query/session-scoped rather than a guaranteed one-row-per-requested-URI ledger. `GetReadOmFunction` supports a virtual cardinality column so COUNT(*) can avoid value reads.

**Rationale**: Copies the existing per-object observability contract accurately. Candidate expansion and child bind descriptors are bounded O(K), not O(points × slots). Existing query memory checks still apply; 4096 is a ceiling, not a promise that every such query fits memory. No series-owned data cache or global concurrency pool.

**Alternatives considered**: New 009-style summaries, URI-bearing labels or metric-schema cleanup are out of scope. Warm-cache throughput and cross-chunk I/O amortization claims are unsupported.

## R7 — Validation scope and evidence

**Decision**: Require SQL/native/controlled remote tests and a manual-union oracle in this feature's future tasks. Primary platform is only `linux_arm64`. Reuse existing 1.5.4/1.5.5/1.5.6 pins from `test/data/grids/version-matrix.json` and the 2.0 pair from `test/data/engine-compatibility/version-matrix.json`, recorded individually. Any absent runtime or failure remains blocked/failed; no x86_64 runner is assumed.

**Repository evidence**: Existing Makefile matrix-release/matrix-test targets and `scripts/validate.sh` run isolated pinned builds. The active host reports Linux aarch64. The constitution file contains placeholders; `.specify/extensions.yml` defines no hooks for specify/plan/tasks.

**Rationale**: Reusing current scanner behavior warrants regression checks, but this feature cannot retroactively complete 008 or claim acceptance across platforms. Real public examples in Phase 8 are not frozen executable evidence; a later real-object gate must acquire/freeze adjacent objects, query, oracle and exact identities, or remain blocked.

**Alternatives considered**: Reusing an old PASS, claiming a historical public chunk still exists, or adding Community packaging/CI platform expansion is excluded.
