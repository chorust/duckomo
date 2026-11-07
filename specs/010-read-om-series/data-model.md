# Data Model: read_om_series

**Status**: Draft design; no implementation or runtime proof, 2026-11-05.

## SeriesRequest

| Field | Meaning / validation |
|---|---|
| directory_prefix | Normalized single-variable directory, supported local/HTTP(S)/S3 transport, no filename/glob/query/fragment/run-selector |
| model | Optional exact registry key; not a grid selector |
| chunk_time_length_override | Optional positive int64 L; explicit value takes precedence |
| time_step_override | Optional positive fixed whole-second INTERVAL, no months |
| window_start/window_end | Finite normalized UTC microseconds; one exclusive endpoint form, start <= end |
| dimensions | Complete ordered declaration; exactly one `time`; shared list denotes root or map enumerates selected paths |
| spatial_configuration | Required existing domain or grid/spatial_axes; parsed/validated by existing helpers |
| strict | Non-NULL BOOLEAN, default false |
| include_source / variable | Existing supported per-object options and validation |

Unknown/malformed options, axes presence, invalid arithmetic and >4096 candidates fail before object access. No mutable global request state or persisted catalog.

## TemporalModelEntry → EffectiveTemporalLayout

A frozen entry has `model`, `chunk_time_length`, `temporal_resolution_seconds`, upstream repository/commit, source path/formula, archived meta snapshot path/hash and provenance status. Use exact producer data keys. It references the grid registry's upstream pin conventions, not its spatial identity hash or validation level.

Resolving a request produces `{L, step_microseconds, span_microseconds, L_source, step_source}`. A source is either the registered field or explicit override. Both fields must resolve, be positive and fit checked arithmetic. This internal provenance need not add SQL columns or change metrics JSON. It is recorded by the validation manifest.

## DeclaredSeriesSchema

- `arrays`: canonical path → FLOAT and ordered dimensions; deterministic UTF-8 path order; normalize root aliases and reject collisions.
- `output_signature`: current read_om array names/types, lat/lon, generated valid_time and optional existing om_source STRUCT type.
- `time_axis_name`: `time`, exactly once in each declaration.
- `spatial_definition`: existing validated domain/grid configuration; not an invented object shape.

This schema exists without object bytes, enabling all-missing zero-row output. Existing-child schema must match it exactly. Metadata-dependent rank/extent/identity validation applies only to objects actually opened successfully. A dimensions shared list is deliberately limited to a single root array because it cannot name unknown nested arrays in the all-missing case.

## ChunkDescriptor

For each candidate signed n, store `{index, URI, origin_microseconds, slot_window, state}`. URI construction uses the contract's signed zero-padded minimum width 4. The descriptor references the immutable request/layout rather than duplicating large structures. `slot_window` is the intersection of `[0,L)` with the closed requested time interval and may be empty; candidate presence is still checked in strict mode.

Derived coordinates are regular, not an allocated vector of L timestamps. Chunk count and all origin/final-slot computations are checked before open. Planning memory scales with candidate count and retained bind metadata, not the full output row count. Bound data follows existing query accounting and memory errors; there is no promise that the cap makes all requests fit.

## BoundChildDescriptor and homogeneity

A successful bind owns existing read_om FunctionData, callbacks/virtual columns, signature, immutable shape/dimension descriptors, grid_id/layout_id and query-scoped remote identity/session. A narrow internal interface exposes only these facts, keeping decoder and spatial implementation private.

Every existing child must match selected canonical paths, names/types, complete logical shape/order, L and spatial identity. Physical OM compression block shape is not a homogeneity key. Existing per-object validation of coordinates/time metadata and variable selection runs before cross-child comparison. Bind data is retained for the LogicalGet; discovery is not followed by a separate unpinned rebind.

## FailureClassification

Existing broad ReaderError code and OM code remain intact. Add optional machine-readable diagnostic category (such as confirmed object_not_found) at the existing error-classification boundary, with enough stage information to distinguish initial open from later failures. Preserve sanitized text and existing ordinary-read behavior. Local confirmed ENOENT is the corresponding absence case. No secret URL/body/header text is stored in public diagnostics.

Only the series binder consumes confirmed **initial-open** absence as a policy decision. Generic FileIo, unstructured transport, permission, metadata/read errors and object-identity changes are never absence.

## State transitions and ownership

```text
request → validated declarations/layout/candidate interval
candidate → initial bind/open
initial bind/open → confirmed absent → skipped (strict=false)
initial bind/open → confirmed absent → query failed (strict=true)
initial bind/open → existing object → per-object validation → homogeneity check → bound leaf
initial bind/open or validation → other failure → query failed
all candidates resolved → logical UNION ALL of retained leaves / typed empty plan
bound leaf → normal read_om execution → completed | failed | cancelled
```

All candidates bind before execution. A later read/open failure is never a transition back to skipped. On any failure, unwind retained child state and use existing metrics/resource lifecycle. Cancellation is checked during candidate binding and existing child execution. Prepared/cached statements must obey ordinary read_om rebind and object identity rules; no cross-query discovery cache or frozen missing decision is introduced.

## ScanRecord

Use existing query_id, scan_id and ScanMetrics objects, not a new series data structure. Attempted initial binds can have records distinct from executed leaves. Each executed child retains independent counters/status; skip/empty output adds no successful phantom scan. Query completion, cancellation and session isolation remain owned by the existing metrics lifecycle.
