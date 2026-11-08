# SQL Contract: read_om_series

**Status**: Draft for review, 2026-11-05. This function is not implemented by this session. Normative behavior refines the Phase 8 proposal; examples are future validation inputs, not executed evidence.

## Signature and options

```sql
read_om_series(
  directory_prefix VARCHAR,
  model := VARCHAR,
  chunk_time_length := BIGINT,
  time_step := INTERVAL,
  valid_times := [TIMESTAMP start_utc, TIMESTAMP end_utc],
  -- alternatively: start := TIMESTAMP, "end" := TIMESTAMP
  dimensions := VARCHAR[] | MAP(VARCHAR, VARCHAR[]),
  domain := VARCHAR,
  -- alternatively: grid := <existing read_om grid value>, spatial_axes := VARCHAR[]
  strict := BOOLEAN,                -- default false
  include_source := BOOLEAN,        -- default false, existing read_om semantics
  variable := VARCHAR               -- optional existing per-object array selection
)
```

All supplied arguments must be constants/foldable at binding, as with current per-object options. No per-row directory/window, LATERAL-dependent discovery, or table-valued input. NULL for any explicitly supplied required/value option is invalid; presence of `axes` is always rejected, even NULL or `{}`. Unknown named arguments are rejected. `axes` should be registered as a rejected option so its error explains automatic time generation.

| Input | Contract |
|---|---|
| directory_prefix | Required nonempty local or HTTP/HTTPS/S3 directory prefix for one variable's `data/` layout. One optional terminal slash is normalized; append `/chunk_<index>.om` exactly once. Preserve path case and escaped components; do not decode/re-encode the prefix or concatenate raw SQL. |
| model | Optional exact temporal registry key. Registered keys supply L/dt defaults. Omitted/unregistered key requires both overrides. Does not select or infer domain, variable name or directory. |
| chunk_time_length | Effective L is a positive signed-64-bit integer slot count, checked with timestamp and expansion arithmetic. A registered default may be overridden individually. |
| time_step | Positive fixed INTERVAL, no months/calendar years, whole-second precision after combining days and microseconds, checked signed-64-bit microsecond representation. Registry seconds convert to this type. A registered default may be overridden individually. |
| valid_times | Exactly two non-NULL finite TIMESTAMP or TIMESTAMPTZ endpoints; normalize to UTC microseconds, then require start <= end. This is a window, never a child per-slot list. |
| start / end | Both required if either is used; forbidden together with valid_times even when values agree. Same finite timestamp rules. TIMESTAMP is interpreted as UTC; TIMESTAMPTZ is an absolute instant. Output is UTC TIMESTAMP. |
| dimensions | Required complete ordered axis declaration. Exactly one axis named `time` per selected array. Existing arrays must match declared rank/order, shared shape and effective L. A shared list declares one root array; use a complete map for non-root or multiple selected arrays. Raw non-time axes retain current semantics and multiplicity. |
| domain / grid / spatial_axes | Required explicit spatial configuration accepted by existing read_om. Domain and grid are mutually exclusive as today; explicit grid requires spatial_axes. Model does not supply this configuration. Existing domain/grid parsing, shape/axis constraints and metadata conflict checks apply. |
| strict | Non-NULL BOOLEAN; default false skips confirmed initial absence only. True requires every candidate object. |
| include_source | Existing optional om_source output type/content; per-child opaque object identity and logical index stay local to that object. No new URI column. |
| variable | Existing per-object selector; every child sees the same selection. Dimensions must exactly describe selected arrays, including empty-only queries. Does not expand directories or align different variables. |

Reject a prefix containing glob characters (`*`, `?`, `[`, `]`, `{`, `}`), fragments, URI userinfo or query strings; query-bearing/signed **directory prefixes** have no well-defined signature for generated children in this milestone. Existing single-object signed URLs in `read_om` remain governed by that function. Reject prefixes ending in `.om` and recognized `data_run/` / `data_spatial/` components. A local fixture need not literally contain a `data/` path segment, but must obey the same chunk naming/layout contract. Do not list directories, glob, infer the model from a path, fetch `static/meta.json`, or inspect a chunk outside the candidate interval.

## Registry and override precedence

Resolve each field independently: supplied override > registered field > missing-parameter error. A misspelled/unknown model is permitted only with both explicit parameters and produces no hidden registry inference. Initial built-ins are exactly the 11 keys/values in [research.md R3](../research.md#r3--frozen-temporal-registry-and-overrides). Temporal registration does not confer spatial support, grid identity or evidence status. Effective overrides define the caller's layout assertion; existing object time extent must still equal L.

## Chunk selection, timestamps and ordering

Let a/b be endpoint microseconds since 1970-01-01 00:00:00 UTC, d the step in microseconds and L the slot count.

```text
span = checked(L * d)
first = floor_div(a, span)
last  = floor_div(b, span)
candidates = inclusive signed integer interval [first, last]
origin(n) = checked(n * span)
slot_time(n,i) = checked(origin(n) + i*d), 0 <= i < L
result = UNION ALL children WHERE a <= valid_time AND valid_time <= b
```

Floor division rounds toward negative infinity, not zero. Check span, candidate count, first/last origin and final generated slot against finite timestamp/int64 limits before any object access. At most **4096 candidate chunks**, including initially missing ones; reject larger intervals with a diagnostic suggesting smaller windows. No configurable unlimited expansion is added. Use a balanced logical UNION tree; this is not a guarantee that any 4096-child query fits its memory budget.

Chunk index formatting follows signed decimal printf minimum width 4, zero-padded, with no truncation: 0 → `chunk_0000.om`, 921 → `chunk_0921.om`, 10000 → `chunk_10000.om`, -1 → `chunk_-001.om`. Negative-time requests are supported mathematically; nonexistent producer keys follow ordinary absence policy.

The Phase 8 prose cited `chunk_921` shorthand; generated keys use this explicit padded spelling. Frozen real-object acceptance must record the actual exact keys and must not assume historical examples still exist.

The candidate interval follows floor(a/span)..floor(b/span) even when a particular child's requested subinterval contains no slot. Strict mode still validates that candidate's presence. For an unaligned slotless window, no value rows are read, and the result is typed empty. Epoch boundaries do not produce duplicate slots: n has slots `[n*L, n*L+L-1]`, n+1 starts at the next slot.

Generated child declaration uses current syntax:

```sql
axes := {'time': {'axis': 'time', 'start': <origin TIMESTAMP>, 'step': <dt INTERVAL>}}
```

Do not pass series `valid_times`, start/end, model, overrides or strict as child options. Existing metadata time that conflicts with the generated axis is an error, even when value columns are pruned. No semantic lead_time/member/level/run override is available while user axes are forbidden. Non-time dimensions remain raw unless existing metadata semantics already apply without a user declaration.

Alignment means all children expose the same logical column signature and each row carries its absolute valid_time; it is UNION ALL concatenation, never a temporal join or pivot. Duplicate timestamps across points/other axes remain. Output ordering is unspecified; consumers must ORDER BY explicitly.

## Schema and homogeneous objects

For empty binding and reconciliation, use current read_om naming/output rules:

1. Declared supported arrays are FLOAT, ordered by canonical path in UTF-8 byte order. Root `/` is `value`; non-root `/foo/bar` is `foo/bar`. A shared dimensions list denotes root `/` only. A map key `/` (or root-only alias `value`) denotes root; non-root keys should use absolute canonical paths to avoid alias ambiguity. Normalize supported aliases exactly once and reject duplicates, missing/unknown entries and case-insensitive column collisions.
2. Append `lat DOUBLE`, `lon DOUBLE`, then `valid_time TIMESTAMP`, then optional `om_source` with the existing source STRUCT type and field order. Use shared existing output/type helpers, not an independently copied source type. No synthetic lead or other semantic columns are added from dimension names.
3. Bind every existing candidate through the current reader with generated time axis. Require identical selected paths, names, types, full logical shape/dimension order (time L included), non-time shape, grid_id and layout_id. No UNION BY NAME coercion, missing-column filling, implicit casts or broadcasts. Physical compression chunk shapes need not match.
4. All arrays covered by current per-object schema alignment remain covered. `variable`, when used, narrows the selected set by its existing semantics before the declared manifest is checked.
5. If every candidate is initially absent and strict is false, return zero rows of this declared schema. Validate input syntax, registry arithmetic and spatial definition, but do not pretend unavailable metadata passed rank/extent/identity checks. No witness read or externally fetched schema is allowed.

The metadata-free all-missing contract requires these complete declarations. Existing objects are always validated before execution, so an explicit SELECT/COUNT/LIMIT cannot turn malformed objects into accepted data. An outer optimizer may remove execution after binding; metrics must reflect that distinction.

## Absence and failure matrix

| Event | strict = false | strict = true |
|---|---|---|
| Initial HTTPFS open reports structured 404 | Skip child; absent period has no rows | Fail with existing object_not_found diagnostic |
| Initial local open confirms ENOENT | Skip child | Fail with existing local missing-file diagnostic |
| First/middle/last/all children absent | Union present children, or typed empty | Fail on a missing candidate |
| 401/403, 5xx, timeout, rate limit, no structured status, extension/configuration error | Fail with existing sanitized category | Same |
| Corrupt header/metadata, shape/schema conflict, unsupported grid or decode failure | Fail | Same |
| Successful bind, later 404/removal or changed identity/412 | Fail under existing read/identity contract | Same |
| Existing slot contains NaN | Ordinary row with NULL value | Same |
| Cancel/interruption or allocation failure | Propagate and clean up | Same |

Classification uses structured metadata, never error-message substring matching or FileExists false. Preserve existing messages, redaction and identity comparison; do not invent an object_changed bracket where the current identity path does not supply one. No series retry/rebind loop is permitted. Standard HTTPFS policy is reused. Multi-object reads are not an atomic bucket snapshot.

## Scan behavior and observability

Each retained leaf uses the original `read_om` callbacks, virtual/cardinality columns, spatial/time selection, residual WHERE, projection and session worker limits. No series-owned decode/grid/async/physical range logic. Do not merge adjacent objects into a shared physical read, cache, or budget-independent worker pool.

`duckomo_last_scan_metrics()` keeps its existing columns and JSON. Query_id is shared according to the existing query lifecycle, scan_ids stay distinct. Each executed child publishes its own record; attempted missing opens may leave existing failure records. No absent child is relabelled successful, no aggregate counters are fabricated, and optimized-away leaves need not appear as completed scans. Compare counters per executed child, not by assuming number of metrics rows equals number of candidate URIs. Preserve session isolation, error/cancel cleanup and subsequent-query recovery.

## Example (planned syntax)

```sql
SELECT value AS temperature_2m, valid_time, lat, lon
FROM read_om_series(
  's3://openmeteo/data/ecmwf_ifs/temperature_2m',
  model := 'ecmwf_ifs',
  dimensions := map(['value'], [['lead','point','time']]),
  domain := 'ecmwf_ifs',
  valid_times := [TIMESTAMP '2023-01-04 22:00:00',
                  TIMESTAMP '2023-01-05 02:00:00']
)
WHERE lat BETWEEN 29 AND 30 AND lon BETWEEN 106 AND 107
ORDER BY valid_time, lat, lon;
```

Root-array naming is explicitly corrected to `value AS temperature_2m`; the roadmap example's bare `temperature_2m` is not evidence that a root array has that name. Dates/object availability and padded keys above are not verified live. Use deterministic fixtures in [quickstart.md](../quickstart.md) for acceptance; freeze any later real-object run independently.

## Milestone exclusions

No cross-variable/model auto-alignment, run discovery or selectors, arbitrary glob, adjacent-object physical read coalescing, bilinear interpolation, point-site nearest, metadata-request optimization project, metrics cleanup or new async I/O implementation. No Community package/workflow changes.
