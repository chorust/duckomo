# N7 Independent Reproduction

**Result: local 5×5 workflow checks passed; N7 gate not passed.** The recorded executor is an implementation worker subagent, which does not satisfy the validation contract's requirement for an executor who did not participate in implementation. This report supports only the stated local workflow and is not independent acceptance evidence for Feature 006.

## Subsequent human execution

The project user subsequently executed the quickstart and final source-parity SQL on DuckDB v1.5.5 and supplied passing outputs. See [user-manual-v1.5.5/results.md](user-manual-v1.5.5/results.md) and the retained [source-parity.sql](user-manual-v1.5.5/source-parity.sql). This is independent human execution evidence for the documented checks. The final source comparison launched a new process using only v1.5.5 paths and returned `differences=0`, six rows per input and `PASS`.

N7 remains partial: the initial quickstart transcript loaded spatial paths for two versions in one session, and complete user-captured runtime hashes/full run logs were not supplied. Do not assign those initial observations a verified matching-spatial identity. The original implementation-worker report below is preserved as historical evidence; its executor qualification limitation does not describe the subsequent human executor.

## Agent and context

- Executor: Codex AI agent, implementation worker subagent.
- Context: fresh task context in `/home/blizhan/repo/github/duckomo`; used only `specs/006-region-nearest-sampling/quickstart.md` for workflow and expected results. Did not consult prior 006 evidence, validation manifests, or other agents' findings before running the reproduction.
- Reproduction date: 2026-10-17 (workspace runtime date).
- Secrets: none used or recorded. Commands below contain only local paths and SQL fixture values.

## Runtime and input identities

SHA-256 identities were captured before execution:

| Artifact | Path | SHA-256 |
|---|---|---|
| DuckDB CLI (quickstart pinned v1.5.4 build) | `/home/blizhan/repo/github/duckomo/build/release/duckdb` | `fb0e09a6a3505e5b231281280304071cd9dc10ddd6f42b8c07d8f182ba7e0bad` |
| DuckOMO extension | `/home/blizhan/repo/github/duckomo/build/release/extension/duckomo/duckomo.duckdb_extension` | `a9aec833be443d481a0ce583ec529eea6f58fd97aeab16c42149ac64b359e7fa` |
| Matching spatial extension | `/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension` | `af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a` |
| 5×5 source fixture | `test/data/regions/regions.om` | `264fe0905d0681d0fedd08d60a43bf7974cbd161319df77e919f08b4cb582e36` |

## Commands and results

All DuckDB commands were executed from the repository root. The temporary SQL and captured output are `/tmp/n7-independent.sql` and `/tmp/n7-independent.out`.

1. Spatial dependency probe, following quickstart section 1 (exit code `0`):

```sh
build/release/duckdb -unsigned :memory: -c "LOAD '/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension'; SELECT ST_AsText(ST_PointOnSurface(ST_GeomFromText('POLYGON((100.1 20.1,100.2 20.1,100.2 20.2,100.1 20.2,100.1 20.1))')));"
```

Output: `POINT (100.15 20.15)`. Captured output SHA-256: `759d51b0e187ae441ebf033767580eea8a8273b573c8a84fd6e0d0bb569a47ad` (`/tmp/n7-spatial-probe.out`).

2. Quickstart section 2 local 5×5 demo, with an additional direct `read_om` comparison for the successful Gap nearest source (exit code `0`):

```sh
build/release/duckdb -unsigned :memory: -f /tmp/n7-independent.sql
```

The SQL file loads the identified DuckOMO and spatial extensions, defines the quickstart's three regions/grid/axes, runs its ordinary `read_om` query, points count, nearest materialization, grouped row/status query, and per-region nearest details query. It additionally joins the Gap nearest row to a direct `read_om(..., include_source := true)` row at the indicated source coordinates and same member/time.

- Retained SQL artifact: [`n7-independent/reproduction.sql`](n7-independent/reproduction.sql), SHA-256 `8b3292edbcd1ed4ee13feb7ddce0cea63478aece57d8ef334cf11e43838f9d90`.
- Retained stdout/stderr: [`n7-independent/reproduction.stdout.txt`](n7-independent/reproduction.stdout.txt), SHA-256 `563920fd7b8b5ad9d8c6f1d7ec459f8a879b2ada14df883f5719ea910f56cd07`.
- Spatial probe output: [`n7-independent/spatial-probe.stdout.txt`](n7-independent/spatial-probe.stdout.txt), SHA-256 `759d51b0e187ae441ebf033767580eea8a8273b573c8a84fd6e0d0bb569a47ad`.
- Both DuckDB commands exited `0`.

## Observations

- Ordinary `read_om` returned four cells for the requested rectangle: `(lat,lon,value)` = `(20,100,0)`, `(20,101,1)`, `(21,100,10)`, `(21,101,11)`. This confirms it is appropriate for ordinary grid/coordinate queries.
- `read_om_regions(..., method := 'points')` returned `source_records = 0` for the three sample regions.
- Nearest emitted six result rows per region (18 total): Empty 6, Gap 6, Outside 6, matching the two-member × three-time non-spatial combinations.
- State distinctions and coordinates/source behavior:
  - Empty: `empty_geometry`; target coordinates, source coordinates, value, and point index were NULL.
  - Gap: `ok`; target `(lat,lon)=(20.15,100.15)`, source `(lat,lon)=(20,100)`, distance `22883.490692907973` m, value status `ok`, value `0.0`, point index `0`.
  - Outside: `outside_domain`; representative target `(30.5,110.5)` was present, while source coordinates, value, and point index were NULL.
- Direct source comparison for Gap at member `0`, time `2026-01-01 00:00:00`: nearest value `0.0` equals direct `read_om` value `0.0`; nearest point index `0` equals direct point index `0`. The nearest source coordinates `(20,100)` are the direct query coordinates. This check completed successfully.
- No discrepancies observed in the requested local checks.

## Scope and repository state

No implementation, source, build artifact, fixture, or other project file was changed by this reproduction. The only project file written for this task is this report. Before writing the report, `git diff --cached --quiet` succeeded (`NO_STAGED_FILES=true`). The working tree already contained numerous unrelated modifications and untracked files; they were not treated as this task's changes and were not modified as part of the reproduction. No tests were added or updated.

## Validation summary

The quickstart dependency probe and local demo both exited `0`, and the observed output satisfies each requested local check. This evidence supports the described local reproduction only and does not satisfy N7's independent-executor gate requirement.
