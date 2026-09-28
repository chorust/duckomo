# Phase 1 Evidence: `read_om`

Status: passed on the available Linux AArch64 host. The plan targets Linux x86_64; x86_64 validation remains outstanding.

## Pinned inputs and environment

| Component | Commit / value |
| --- | --- |
| DuckDB | `08e34c447bae34eaee3723cac61f2878b6bdf787` (`v1.5.4`) |
| OM file format | `d8855e418e2231ae8439f0c7e840fa3f93b371e3` |
| extension-ci-tools | `b777c70d30942cca5bef62d6d4fa23a13362f398` |
| Host | Linux AArch64; see [Phase 0 evidence](phase0.md) for kernel details |
| Query runtime | DuckDB CLI, the built extension, committed fixtures; no Python or Swift process |

## Fixture identity and independent references

The fixture tool used the pinned official OM writer and a separate official C decoder for each variable. Finite Float32 values were compared bitwise after a complete-file decode. The checked manifest contains 22 fixture/reference/negative files; every recorded SHA-256 matched. A second generation into a fresh temporary directory followed by `diff -qr test/data <temporary-directory>` returned 0.

| Fixture | Shape / sorted schema | SHA-256 |
| --- | --- | --- |
| `raw.om` | `[2,3]`; `value FLOAT` | `6a34044749250c0de21d65d40d4f6c270c3ae31d16ac623626bc6b2f7a01ced3` |
| `multi.om` | `[2,3]`; `/humidity FLOAT`, `/temperature FLOAT` | `49aad03bf849d858f46708aba34f673ac3d4176be996f359b47736157d60f25f` |
| `nested.om` | `[2,3]`; `/layer_a/value`, `/layer_b/temperature`, `/layer_b/value`, `/temperature`, `/weird%2Fname%25/value` (all FLOAT) | `dc2b75fa770f9b2814a15cb0223538953006608a8f6343923ad5d7f20eb273ae` |
| `special.om` | `[2,4]`; `value FLOAT`, NaN, ±Inf, and ±0 | `e2b29274a1ac920cb8fad7db177aa07dd4c51f480c94803f2ab0492a767595c0` |

`multi.om` has `/temperature` values 0–5 and `/humidity` values 100–105. `nested.om` has per-variable oracle CSVs for all five paths; the SQL test consumes all five variables together and matches their six reference values at each aligned row. Their individual CSV hashes and the two multi-variable CSV hashes are in `test/data/manifest.json`.

## Commands and exit codes

| Command | Exit / result |
| --- | --- |
| `make release` | 0 |
| `./build/release/test/native/schema_test` | 0 (`metadata/schema checks passed`) |
| `./build/release/test/unittest test/sql/read_om.test` | 0 (92 assertions) |
| `./build/release/duckdb -unsigned -c "LOAD '/home/blizhan/repo/github/duckomo/build/release/extension/duckomo/duckomo.duckdb_extension'; DESCRIBE SELECT * FROM read_om('/home/blizhan/repo/github/duckomo/test/data/nested.om', dimensions := map(['/layer_a/value', '/layer_b/temperature', '/layer_b/value', '/temperature', '/weird%2Fname%25/value'], [['row','column'],['row','column'],['row','column'],['row','column'],['row','column']])); SELECT count(*) FROM read_om('/home/blizhan/repo/github/duckomo/test/data/multi.om', dimensions := map(['/humidity','/temperature'], [['row','column'],['row','column']]));" :memory:` | 0; five FLOAT columns in sorted order, six rows |
| `repro_dir=$(mktemp -d /tmp/duckomo-fixture-repro.XXXXXX) && ./build/release/test/tools/duckomo_fixture_tool --output "$repro_dir" >/tmp/duckomo-fixture-regeneration.log && diff -qr test/data "$repro_dir"` | 0; deterministic assets and manifest |

The SQLLogicTest scans `raw.om`, `multi.om`, and all variables in `nested.om`, checks repeated multi-variable query results, NULL/Inf/±0 behavior, and the root-array schema. `schema_test` verifies full tree traversal, metadata-only `DESCRIBE` against a payload-corrupted copy, escaped names, malformed names/references, duplicate canonical paths, and rank/shape bounds.

## Rejection and alignment coverage

Binding rejected unsupported version, non-Float32 type, non-FPX compression, zero-length axes, illegal child references, and duplicate canonical paths. Dimension checks rejected missing/unknown keys, wrong map type, rank mismatch, duplicate/empty/NULL axes, differing ordered axis identities, and shape mismatch. Requests for latitude, longitude, or time fail as ordinary missing-column bindings; no synthetic coordinate columns are exposed. A missing axis map for multiple arrays fails before scanning.

No Phase 1 contract correction was needed. The scanner validates all metadata at bind, validates axis identity explicitly, and reads each array through the official reader using the same logical batch range. Bind and `DESCRIBE` do not decode payload data. `read_om` keeps projection and filter pushdown disabled for this full-read baseline.

## Boundary

Both raw and generic scans map NaN to SQL NULL, preserve positive and negative infinity, and retain the sign of zero. The available host is AArch64, so Linux x86_64 and a clean x86_64 environment without Python/Swift remain for final validation.
