# Feature Specification: read_om_series 跨 chunk 时间序列入口

**Feature Branch**: `main`（仅规格；未创建或切换 git 分支）

**Created**: 2026-11-05

**Status**: Draft — implemented in the working tree; current-source acceptance passed S0–S7 and SC-001–SC-007 on the four pinned Linux AArch64 pairs. Feature 010 acceptance is PASS for that declared scope. See the [follow-up acceptance evidence](evidence/acceptance-20261106-followup-linux-arm64/report.md). Real producer coverage is limited to the frozen IFS025 adjacent-object boundary sample; no broad data support is claimed. Feature 008 completion and other architectures are not asserted.

**Input**: Feature 010, sourced from the complete `docs/roadmap.md` section “Phase 8 — 跨 chunk 时间序列入口（待评审提案）”. This session is authorized for specify → plan → tasks only. The roadmap's historical observations are motivation, not acceptance evidence.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - 一次查询跨越 chunk 边界 (Priority: P1)

分析者提供变量目录、model、完整维度与空间声明及有效时间窗口，直接得到跨对象的时间序列，不再计算 chunk 编号和逐个手写时间轴。

**Why this priority**: Removes the four manual steps identified in Phase 8 while retaining ordinary single-object results.

**Independent Test**: On deterministic adjacent chunks, compare the complete result multiset against independently constructed `read_om` calls with explicit time axes and a manual UNION ALL, including a boundary timestamp and NULL slots.

**Acceptance Scenarios**:

1. **Given** two adjacent homogeneous chunks, **When** a closed window crosses their boundary, **Then** every existing slot in the window has exactly the same values, coordinates and multiplicity as the manual baseline; the boundary slot occurs once per spatial/non-time combination.
2. **Given** a window inside one chunk, **When** using series, **Then** its schema and rows match the equivalent filtered `read_om`; a root Float32 array is still named `value`, not automatically renamed after the directory.
3. **Given** unaligned endpoints or `start = end`, **When** querying, **Then** only actual slots satisfying `start <= valid_time <= end` are returned; an interval containing no slot returns a typed empty relation.
4. **Given** `valid_times := [a,b]` or the equivalent `start := a, "end" := b`, **When** querying, **Then** results are identical. Mixed forms, missing endpoints, NULL/infinite timestamps, reversed windows or explicit `axes` fail before object access.

---

### User Story 2 - 历史缺口可跳过，真实故障可辨认 (Priority: P1)

分析者读取有历史缺口的目录时默认获得已有时段；需要完整覆盖时可选择严格模式。不能把权限、网络、对象变化或损坏误认为历史缺口。

**Why this priority**: Missing chunks are an expected property of this layout, but hiding other failures would make results misleading.

**Independent Test**: Use controlled local and HTTP/S3 fixtures with first/middle/last/all objects missing, plus structured 403/404/412/500, transport failure and replacement after binding; verify rows or error categories for both policies.

**Acceptance Scenarios**:

1. **Given** one or more chunks absent at initial object open, **When** `strict` is omitted or false, **Then** absent periods contribute zero rows, with no synthesized NULL rows; all chunks absent also returns the declared typed empty schema.
2. **Given** a required chunk absent, **When** `strict := true`, **Then** the query fails with the existing missing-object diagnostic, even if an outer LIMIT/WHERE could avoid consuming it.
3. **Given** access denied, unclassified transport failure, corrupt metadata or decode failure, **When** either policy is used, **Then** the query fails; only confirmed initial object absence is skippable.
4. **Given** a chunk bound successfully and subsequently replaced or removed, **When** reading it, **Then** existing `read_om` identity/read diagnostics propagate, including `[object_changed]` where supplied; series does not skip it, restart the object, or silently retry the query.

---

### User Story 3 - 登记时间参数与自定义布局都明确可控 (Priority: P2)

常见 model 使用冻结的时间参数；私有或未登记 model 可显式指定槽数和步长，无需依赖远程 meta.json。

**Why this priority**: Time coordinates cannot be recovered from these objects, so parameter provenance and explicit overrides determine correctness.

**Independent Test**: Resolve every initial registry entry against frozen repository source material; query the same synthetic series using a registered model and explicit equivalent overrides, then exercise unknown models and invalid overrides without remote metadata discovery.

**Acceptance Scenarios**:

1. **Given** a registered model, **When** overrides are absent, **Then** its frozen `chunk_time_length` and `temporal_resolution_seconds` determine the axes; querying does not fetch `static/meta.json` or list the prefix.
2. **Given** an unknown or omitted model, **When** both positive `chunk_time_length` and fixed `time_step` are supplied, **Then** the series can be read with an explicit valid spatial declaration; omitting either temporal parameter fails clearly.
3. **Given** a registered model with one or both explicit overrides, **When** binding, **Then** supplied fields take precedence and remaining fields use the registry; override values, not a guessed directory name, determine time.
4. **Given** a chunk whose declared time-axis extent differs from the effective slot count, or conflicting embedded time metadata, **When** binding, **Then** the query fails; series does not reinterpret dimensions or change cadence to fit.

---

### User Story 4 - 沿用单对象筛选、裁剪、并行与指标 (Priority: P2)

用户继续用现有 domain/grid、WHERE、投影、线程设置和指标分析每个对象，不承担一套新的网格或读取语义。

**Why this priority**: The new entry point is temporal composition; scientific and I/O behavior already belongs to `read_om`.

**Independent Test**: Compare series and manual baseline on existing regular and reduced-Gaussian grid fixtures, local/HTTP(S)/S3 and 1/2/4 workers; inspect per-scan metrics for value projection, coordinate-only, COUNT(*) and spatial/time predicates.

**Acceptance Scenarios**:

1. **Given** an existing supported grid/domain and complete dimensions, **When** applying spatial/time/value filters and column projection, **Then** coordinates, values, NULLs and residual WHERE behavior match per-object `read_om` with the same generated time axis.
2. **Given** COUNT(*) or coordinate/time-only projection without value dependencies, **When** scanning, **Then** every executed child retains zero value index/data reads and zero value decodes; ordinary bind metadata reads are allowed.
3. **Given** the same query under 1/2/4 workers, **When** all results are collected, **Then** result multisets are identical. Output order requires an explicit ORDER BY.
4. **Given** multiple executed child scans, **When** inspecting `duckomo_last_scan_metrics()`, **Then** each retains its own `query_id`, `scan_id` and metrics row; series does not collapse them into a fabricated aggregate or create a successful scan for an absent object.

### Edge Cases

- Exact chunk start/end, subsecond endpoints, negative Unix times, a slotless interval, leap day and timezone conversion use absolute UTC instants and mathematical floor, not local calendar arithmetic.
- Duplicate time labels across different spatial/non-time rows are valid; UNION ALL preserves multiplicity. No cross-object deduplication, interpolation, resampling or sort is implied.
- A missing object differs from an existing object containing NaN: missing contributes no rows; an existing NaN slot contributes its ordinary row with NULL value.
- Empty/NULL prefix, filename input, glob, fragment, query-bearing prefix, unsupported run-selector layout, NULL strict, nonpositive/overflowing parameters and oversized expansion fail clearly.
- Schema, non-time shape, dimension order and spatial identity mismatch between existing chunks fail rather than coercing columns, filling missing variables or transposing data.
- All-missing output cannot infer metadata. It uses the declaration-derived Float32 schema described in the SQL contract, never probes outside the requested chunk interval, and makes no claim that absent objects passed metadata validation.
- Cancellation and failure release child resources; a subsequent valid query in the same session succeeds.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Expose a separate `read_om_series(directory_prefix, ...)` table function for one variable directory in the producer `data/` chunk layout, supporting existing local/HTTP/HTTPS/S3 transports. Keep `read_om` single-object input semantics unchanged.
- **FR-002**: Require exactly one finite closed UTC window: a two-element `valid_times` list, or both named `start` and `end`. Treat this list as endpoints, not the per-slot coordinate list accepted by `read_om.valid_times`.
- **FR-003**: Resolve slot count L and fixed step dt from a frozen built-in model registry, then apply explicit `chunk_time_length` / `time_step` overrides. Without a registry entry require both overrides. Do not infer model/domain from the prefix or fetch runtime model metadata.
- **FR-004**: Map chunk n and slot i to `epoch + (n*L+i)*dt`, using checked arithmetic and mathematical floor for chunk selection; address only `chunk_%04d.om` children of the supplied prefix. Apply the exact closed time window to every child result.
- **FR-005**: Generate the per-object time axis automatically. Reject any user-supplied `axes`, including NULL/empty declarations, to prevent double declaration. Non-time semantic axes requiring user `axes` are outside this milestone; raw non-time dimensions remain supported.
- **FR-006**: Require complete ordered dimensions and an explicit domain or grid plus spatial_axes as required by existing `read_om`. Model supplies time parameters only, never a spatial declaration. Reuse current validation, coordinate identity, layout and Float32 value semantics.
- **FR-007**: Require homogeneous existing chunks: selected array paths/names/types, non-time shape/order, time extent L, and resolved grid/layout identity must agree. Reject conflicting time metadata and all mismatches without casts, auto-transpose, broadcast or join. OM compression block shape may differ if logical shape/identity agrees.
- **FR-008**: Default `strict := false`: skip only confirmed object absence during initial binding/open; missing periods produce no rows. `strict := true` fails on any required missing child. Validate the selected chunk set before execution so missing policy is not hidden by LIMIT.
- **FR-009**: Propagate all other failures with existing sanitized diagnostics and object identity policy. Never downgrade post-bind replacement/disappearance to an initial missing chunk; do not add a series retry loop or alter existing HTTPFS transport policy.
- **FR-010**: Combine homogeneous child rows with UNION ALL semantics using their materialized `valid_time`; preserve every row and NULL. No sorted output, temporal joins or time-derived value repair is promised.
- **FR-011**: Preserve per-child domain/grid, spatial and time predicate selection, column pruning, residual WHERE, worker limits and metrics. Series adds no decoder, grid calculation, read-range merge policy, async scheduler or cache.
- **FR-012**: Return a stable typed empty relation when all required objects are absent in skip mode or no slot is selected. Derive names/types from complete dimensions (shared list denotes the single root `value`; a map enumerates selected arrays) and current `read_om` output rules; optional `om_source` keeps its existing type. Require existing children, when present, to match those declarations.
- **FR-013**: Retain one published metrics row per child scan actually represented by the existing metrics lifecycle; no new metrics schema or 009 summary interface. Missing open attempts may retain existing failure records, but must not be labelled successful scans. Optimizer-eliminated branches do not imply executed scans.
- **FR-014**: Validate arguments and expansion bounds before object access, never list/glob the prefix or search outside the calculated chunk interval for a schema witness. Bound expansion at 4096 chunks per call in this draft; explain how to split larger windows.
- **FR-015**: Explicitly reject glob/file/run-selector inputs; exclude cross-variable/cross-model auto-alignment, `data_run/` and `data_spatial/` run selection, and merging adjacent objects into one physical read.
- **FR-016**: Provide SQL/native and controlled remote acceptance checks for the scenarios above, manual UNION ALL oracles, registry reproducibility, and existing scan regressions. Retain failed/not-run outcomes and bind evidence to actual source, artifacts, fixtures and commands.
- **FR-017**: Scope primary acceptance to `linux_arm64` only. Plan compatibility checks for existing pinned engine pairs without claiming they ran or that 008 is complete. Do not implement/extend 007, 008 or 009, or change Community packaging.

### Key Entities *(include if feature involves data)*

- **Series request**: directory prefix, optional model, effective temporal parameters, closed UTC window, dimensions and spatial declaration, strict policy and inherited supported options.
- **Temporal model entry**: model identifier, frozen L/dt, source pin/snapshot and explicit provenance; independent of grid validation status.
- **Chunk descriptor**: signed index, URI, derived UTC origin, selected slots, and per-object bind/identity state.
- **Series schema**: declaration-derived output signature, reconciled with each existing object's schema and spatial layout.
- **Child scan record**: existing per-object metrics identity, status and counters; no synthetic series-wide successful read.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For single-chunk, adjacent two-chunk and three-chunk windows, there are zero schema or row-multiset differences from a manually enumerated reference, including boundary and NULL cases.
- **SC-002**: Both window forms return identical results for 100% of finite valid inputs in the acceptance matrix; every pure argument-validation case fails before object access, while metadata-dependent validation rejects incompatible existing objects before execution.
- **SC-003**: First/middle/last/all-missing scenarios match the declared skip/strict behavior in every tested transport; 403, 412, unclassified transport, corruption and post-bind disappearance are never silently skipped.
- **SC-004**: Every initial model entry has reproducible source provenance and matches explicit equivalent parameters; series makes zero runtime registry-discovery or directory-list requests.
- **SC-005**: At 1/2/4 workers, inherited filter/projection queries have zero result differences from their per-object references; value-free queries perform zero value index/data reads and decodes in each executed child.
- **SC-006**: Every executed child retains a separate existing scan-metrics identity, with no absent child reported as a successful scan; planned versus executed scans are distinguishable in validation records.
- **SC-007**: Boundary, overflow, schema mismatch, cancellation and recovery checks complete on the actual `linux_arm64` engine pairs recorded in the acceptance manifest, and any unavailable pair/gate stays explicitly pending or blocked.

## Assumptions

- Phase 8 is a proposal. This specification and its design decisions remain Draft for review; writing them does not approve implementation or establish runtime support.
- Existing `data/` series contain regular epoch-anchored slots; uneven forecast availability is represented by producer NaN→NULL values, not inferred cadence changes.
- Initial temporal registry scope is the 11 exact model keys in the Phase 8 table (the GFS row contains two keys), sourced from frozen local snapshots and pinned producer material. Time registration never upgrades a model's grid/evidence status.
- Review defaults: `time_step` is a positive fixed INTERVAL with whole-second precision and no calendar months; finite TIMESTAMP inputs mean UTC, TIMESTAMPTZ inputs normalize to UTC; overrides may replace individual registered fields; expansion limit is 4096. Exact details are in `contracts/sql.md`.
- All-missing schema uses existing Float32 array/output naming rules; it cannot certify the shape or identity of unavailable bytes. No new public schema parameter or external metadata dependency is added.
- `.specify/memory/constitution.md` is an unfilled template, not a ratified constitution. This feature records checks against explicit user scope and existing reader contracts without claiming constitutional approval.
- This session preserves the dirty tree, performs no stash/reset/commit/push, makes no Community changes and stops after tasks. Implementation is reserved for a separate `luna:max/worker` session; this planning session uses the operator-selected model.
