# User-executed manual acceptance — DuckDB v1.5.5

Result: the user independently executed the synthetic quickstart and source-parity checks and reported success. This record transcribes the command/results supplied in the interactive session; it is not an automatically captured terminal log. The implementing assistant supplied the guide and final comparison SQL, and compiled this report. No claim is made that the user was isolated from implementation discussion.

## Executor, runtime and provenance

- Executor: human project user; implementation was performed by coding agents. No personal identity is inferred.
- User's visible interactive runtime banner: `DuckDB v1.5.5 (Variegata)`.
- User's final command: `duckdb -unsigned -f build/manual-acceptance-v1.5.5.sql`.
- Retained exact final SQL: [source-parity.sql](source-parity.sql), SHA-256 `6dcff88c81c0bef5c73c36a13bde96c8bb5874d3cc52961198f03c3b3ed90f7a`.
- Fixture: `test/data/regions/regions.om`, SHA-256 `264fe0905d0681d0fedd08d60a43bf7974cbd161319df77e919f08b4cb582e36` (workspace hash captured when documenting; not captured by the user before execution).
- The retained SQL loads the v1.5.5 duckomo build `8c86d97a161b1f2f5b88c7714fe5f36fd896d12c8b53acdc9b679a4fd88de8c9` and `build/regions-runtime/v1.5.5/official/spatial.duckdb_extension`.
- Earlier interactive quickstart execution first attempted the v1.5.4 duckomo artifact and received an explicit version mismatch, then successfully loaded v1.5.5 duckomo. That same session loaded a spatial path under v1.5.4 before a path under v1.5.5; therefore its loaded spatial identity is not established by those LOAD statements. The final source-parity command launched a new process and its retained SQL uses only v1.5.5 paths.
- No independent pre-execution executable/extension hashes, full initial quickstart SQL file, complete terminal transcript or user process exit code were supplied. Current workspace artifact identities must not be represented as user-captured run identities.

## User-observed checks

The user followed quickstart section 2 and posted these results:

- Ordinary `read_om`, member 0 at `2026-01-01 00:00:00`, rectangle lat 20–21/lon 100–101: `(20,100,0)`, `(20,101,1)`, `(21,100,10)`, `(21,101,11)`.
- Explicit points: `source_records = 0`.
- Nearest: Empty 6 / `empty_geometry`; Gap 6 / `ok`; Outside 6 / `outside_domain`.
- Gap representative `(lat,lon)=(20.15,100.15)`, source `(20,100)`, distance `22883.490692907973` m, value `0.0`, point index `0` at member 0/time 0.
- Empty target/source/value/index NULL; Outside representative `(30.5,110.5)` with source/value/index NULL.
- `max_distance_m := 20000`: Gap's six rows `beyond_max_distance`.
- `max_distance_m := 25000`: Gap's six rows `ok`.
- `max_distance_m := 0`: Gap's six rows `beyond_max_distance`.
- Empty and Outside retained their geometry states under all three thresholds. The user separately confirmed the diagnostic candidate source/distance and NULL value/source check passed; its raw output was not pasted.
- `out_of_bounds := 'error'` with Outside present and `LIMIT 0`: `Binder Error: [region_sampling] representative point is outside the nearest grid domain`.

## Final fresh-process source-parity output

User command:

```sh
duckdb -unsigned -f build/manual-acceptance-v1.5.5.sql
```

The user pasted all six rows (the terminal abbreviated source_lon visually; the SQL checks it explicitly):

| member | valid_time | value | source_lat | source_point_index | source_logical_index | value_status |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 2026-01-01 00:00:00 | 0.0 | 20.0 | 0 | 0 | ok |
| 0 | 2026-01-01 01:00:00 | 100.0 | 20.0 | 0 | 25 | ok |
| 0 | 2026-01-01 02:00:00 | NULL | 20.0 | 0 | 50 | source_null |
| 1 | 2026-01-01 00:00:00 | 1000.0 | 20.0 | 0 | 75 | ok |
| 1 | 2026-01-01 01:00:00 | 1100.0 | 20.0 | 0 | 100 | ok |
| 1 | 2026-01-01 02:00:00 | NULL | 20.0 | 0 | 125 | source_null |

Final output:

```text
differences  sampled_rows  source_rows  invalid_source_or_status  acceptance
0            6             6            0                         PASS
```

The bidirectional EXCEPT ALL compares values (including NULLs), original point and logical indices over every member/time combination. It also requires six rows in each input, source coordinates `(20,100)`, point index 0, and status `source_null` exactly when value is NULL.

## Disposition

The manual synthetic functional checks passed, with independent human execution evidence replacing the earlier implementation-worker-only provenance gap for the stated observations. Formal N7 remains partial pending a complete retained quickstart run with consistently established runtime identities; the earlier mixed-spatial session is not promoted to a verified matching-runtime run. N5/N6 and overall Feature 006 acceptance are unchanged. No additional functional failure was observed.
