# Phase 0 Evidence: `read_om_raw`

Status: passed on the available Linux AArch64 host. The target platform in the plan is Linux x86_64; x86_64 build and clean-host checks remain for native CI or an x86_64 machine.

## Pinned inputs and environment

| Component | Commit / value |
| --- | --- |
| DuckDB | `08e34c447bae34eaee3723cac61f2878b6bdf787` (`v1.5.4`) |
| OM file format | `d8855e418e2231ae8439f0c7e840fa3f93b371e3` |
| extension-ci-tools | `b777c70d30942cca5bef62d6d4fa23a13362f398` |
| Host | `Linux 6.17.0-1029-nvidia aarch64` |
| Query runtime | DuckDB CLI plus the built extension and fixtures; no Python/Swift process is used |

## Fixture identity

All three files were produced with the pinned OM writer and were read by the separate official OM decoder path used by the fixture tool. Full-file FPX round trips passed before the reader tests.

| Fixture | Shape | Chunk shape | SHA-256 | Oracle CSV SHA-256 |
| --- | --- | --- | --- | --- |
| `raw.om` | `[2,3]` | `[1,2]` | `6a34044749250c0de21d65d40d4f6c270c3ae31d16ac623626bc6b2f7a01ced3` | `d877c8234157eba79c072172708c89a5a54d87aa83bd8a3307e0731dd1492ad3` |
| `special.om` | `[2,4]` | `[1,3]` | `e2b29274a1ac920cb8fad7db177aa07dd4c51f480c94803f2ab0492a767595c0` | `8cee3472479cfa1f7013cb9df3f7d24d95900548c9a2e630418af775ddc4ab7b` |
| `raw_large.om` | `[73,61]` | `[7,13]` | `fb6ef03698432659f539b7ea4476c8a017fb3a4a59266e017ecd19112ccadc8f` | `fb58024c567740e2ddd08b43088e5ba1084e2e5d7a75fce11f850aaf41fd813f` |

`raw_large.om` has 4,453 values and requires at least three DuckDB standard-size output batches. The native oracle check compared every Float32 result bitwise in last-axis-fastest order. Special values verified NaN→NULL, positive and negative infinity, and positive and negative zero. The SQL test covers `value FLOAT`, the six raw values, and missing, unreadable, truncated, and unsupported-version errors.

## Commands and exit codes

| Command | Exit |
| --- | ---: |
| `make release` | 0 |
| `./build/release/test/native/raw_reader_test` | 0 (`raw reader oracle checks passed`) |
| `./build/release/test/native/lifecycle_test` | 0 (`lifecycle checks passed`) |
| `./build/release/test/unittest test/sql/raw.test` | 0 (23 assertions) |
| `./build/release/duckdb -unsigned -c "LOAD '/home/blizhan/repo/github/duckomo/build/release/extension/duckomo/duckomo.duckdb_extension'; DESCRIBE SELECT * FROM read_om_raw('/home/blizhan/repo/github/duckomo/test/data/raw.om'); SELECT * FROM read_om_raw('/home/blizhan/repo/github/duckomo/test/data/raw.om') ORDER BY value;" :memory:` | 0 |

The CLI smoke query returned one `FLOAT` column and rows 0–5. Tests and the manual query are local-only; no network-backed reader or Python runtime is involved.

## Failure, cancellation, and cleanup

The lifecycle native test used the official decoder to locate the final `{70,52}` selection in `raw_large.om` (count `{3,9}`), then corrupted that selected FPX payload. Prefix scans of 2,048 and 4,096 rows succeeded; the complete scan failed on the final corrupt chunk, and a subsequent valid `raw.om` query succeeded. A long-running scan was interrupted and followed by a successful query. Across 100 valid/error query pairs, `/proc/self/fd` did not grow above its warmed baseline or increase monotonically.

The rank-eight metadata mutation is only used to verify bind-time rank acceptance. It does not re-encode chunk payloads, so it is not treated as a rank-eight data round-trip fixture. Rank 0, rank 9, a zero axis, shape product overflow, and non-FPX compression were rejected.

## Boundary

The scanner delegates header/trailer validation, metadata validation, index/data request planning, and FPX decoding to the pinned official OM C API. It supplies exact bounded local positional reads and divides logical rows into final-axis-contiguous selections no larger than a DuckDB vector batch. Bind validates the root array's type, compression, rank, positive axes/chunks, checked row count, and absence of children before any output is produced.

No Phase 0 contract correction was needed. Linux x86_64 and the clean environment without Python/Swift remain part of final cross-cutting validation.
