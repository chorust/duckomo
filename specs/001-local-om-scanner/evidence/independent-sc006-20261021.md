# Independent AI verification of spec 001 SC-006

**Decision: SC-006 can close for the supplied DuckDB v1.5.4 / DuckOMO pair on Linux AArch64, under the owner's explicit acceptance of independent AI verification. This is not human verification.** All four required operations were actually executed, and their schemas and complete results independently asserted. No unresolved SC-006 requirement remains in this scoped reproduction. No acceptance checkbox, roadmap, source, guide, contract, existing evidence, or shared build artifact was edited.

## Verifier and scope

I am OpenAI Codex, with the supplied agent identity “based on GPT-6.” This fresh session did not implement DuckOMO or its budget repair, and no work was delegated. Visible session/thread ID: `01a15d61-e13b-7d10-91d4-c575806a80d9`. The exact serving model/revision was not exposed; the declared identity is not an independently measured model ID. See `identity.json` for platform and session details.

I read spec 001, its quickstart and both contracts, plus the current `docs/spec.md` and `docs/interface-migration.md`. Immutable copies and hashes are included. The quickstart's prebuilt-artifact workflow was followed with the CLI/extension paths explicitly supplied by the owner instead of its historical `build/release` paths. CSV output, `-bail`, `-init /dev/null`, in-memory databases and single-thread execution provide machine-checkable, isolated results. No OM file was converted or regenerated; reference CSVs were read only by the verifier, never as DuckDB query input.

## Executed and asserted

| Requirement / fixture | Observed result and independent assertion |
|---|---|
| LOAD | Explicit absolute-path LOAD succeeded; `version()` was `v1.5.4` and `duckdb_extensions()` reported DuckOMO loaded. Every applicable scan process loaded the supplied extension. |
| DESCRIBE raw | Exactly `value FLOAT`, through both `read_om_raw` and `read_om`; full DESCRIBE metadata checked. |
| DESCRIBE multi | Exactly `humidity FLOAT`, `temperature FLOAT`, in that order, with the quickstart's complete dimensions MAP. |
| DESCRIBE projection | Exactly `humidity FLOAT`, `pressure FLOAT`, `temperature FLOAT`, in that order; all three variables explicitly mapped to row/column axes. |
| Raw full and narrow | Complete six rows, exactly 0 through 5; general `read_om` also matched, and COUNT was 6. |
| Multi full and narrow | All six aligned tuples `(100,0)` through `(105,5)` matched the frozen references. Temperature-only output was exactly 0 through 5. Humidity filter `>=103` produced exactly 3,4,5. |
| Projection full and narrow | All 10,541 aligned three-column rows and all 10,541 temperature-only rows compared against independent official-reader CSVs, with no LIMIT or count wrapper around the value scans. Non-square `[83,127]` fixture spans multiple output batches. COUNT was 10,541. |
| Projection filtering | `humidity>=103` correctly returned zero rows. A nonempty `humidity>=50` case returned exactly 5,091 reference-matched temperature values, checking the unprojected filter dependency. |
| Missing/nonfinite values | Complete eight-row `special.om` scans through both entry points matched the reference, including the single NaN→SQL NULL, +Inf, -Inf, +0, -0 and finite values. Full schemas, per-row `signbit`/NULL predicates, and aggregate null/zero counts were asserted. |
| No Python/Swift query runtime | All 36 CLI invocations received exactly the six-variable environment in each run JSON, including `PATH=/nonexistent`. Four required operation categories, complete projection scans and special-value checks were also exec-traced. Each of the 14 traced runs executed only the official CLI; no Python or Swift executable was launched. |

Fixture and reference SHA-256 values match the frozen `test/data/manifest.json`; its pinned OM commit matches the build provenance. Finite values were compared with zero Float32 tolerance by reconstructing IEEE Float32 bits from CLI/reference decimal text. Row count, column order, full aligned tuples and NULL identity were checked separately. Multi/projection comparisons use explicit ORDER BY; they do not assert a general unordered SQL output guarantee. For the special fixture, the supplemental unsorted single-thread full scans also matched the frozen logical sequence and signed-zero bits exactly. The comparator self-check deliberately rejects changed values, changed zero signs, SQL NaN in place of NULL, and missing rows.

Each `runs/*.json` contains exact argv, SQL, working directory, complete environment, expected results, exit code and assertion outcome. Matching `.sql`, `.stdout`, `.stderr` and (where applicable) `.exec.trace` files contain complete, untruncated evidence. Full result sets were consumed. All 36 CLI processes exited 0 with empty stderr. Merely successful exits were not treated as correctness evidence.

## Retained failures and their resolution

There were **33 passing and 3 failing recorded assertions**, not a falsely reported clean first pass:

1. `special-read_om_raw-full` and `special-read_om-full` initially failed bitwise CSV checks because the sorted -0 value was emitted as `0.0`. SQL `signbit(value)` still distinguished the negative zero. The later complete unsorted scans preserved -0 exactly; per-row signed-zero checks also passed.
2. `official-cli-negative-zero-control` failed because my first control assumption was wrong: a single constant -0 actually prints `-0.0`. The initial generalized “CSV rendering” attribution in `supplement-results.json` is superseded by `zero-check-results.json` and this report.
3. A matching two-value **ORDER BY control without loading DuckOMO** reproduced both displayed values as `0.0` while their pre-sort signbit columns remained false/true. Its unsorted counterpart retained -0. This isolates the observed behavior to the official engine's sorted-output path for this query, rather than demonstrating lost signed-zero data in DuckOMO.

The initial verifier and supplement orchestration exited 1 on these assertions. Their records remain untouched. `zero-check.py` and final closure checks passed; no product fix or contract relaxation was made. `closure.json` identifies the required passing evidence and the resolved failures explicitly.

## Artifact identity and provenance

| Input | SHA-256 |
|---|---|
| Official CLI `build/official-matrix/v1.5.4/official/duckdb` | `513b573b5ad3a94ad2c0668b1ccefb5f281b6f7aa2a5e2ff057825473848e69a` |
| Extension `build/audit-remediation-20261021/v1.5.4/duckomo.duckdb_extension` | `a1f1e62f69e9eb7d80789883ab23e84ee296b0bebd9e2b07c48f5fe40b9063b9` |
| Build `identity.json` | `523dc92b9511743c26e3972637ba23421f8d1a3178fd03550734a9ca1cc589b5` |
| `evidence/code-audit-remediation-20261021.json` | `bf1dbc86e8c37c1e1203261c4c6a726b5963a305318bc5f86c7180e1f663ebc0` |

Build ID: `fb49547ec3531e42eeb415ba7cf985989b57a4e1853f48425fc3ec187ca5f245`. The extension hash matches both provenance documents; the CLI hash matches the base manifest's official runtime entry. All hashed input documents, fixtures, references and executables remained unchanged across execution and finalization. Fixture hashes are individually listed in `inputs.json` and the manifest's input inventory.

**This is an isolated changed-translation-unit compile/relink using unchanged pinned base objects, not a clean full rebuild.** Build provenance was checked for identity consistency; it was not regenerated or independently rebuilt. Host: Linux AArch64, kernel `6.17.0-1029-nvidia`, glibc 2.39.

## Practical limits and evidence integrity

- This closes only SC-006 for this exact prebuilt v1.5.4 Linux AArch64 pair and frozen fixtures. It does not certify x86_64, other DuckDB versions, remote/spatial/region behavior, the budget repair's memory bounds, or all feature acceptance gates.
- Python was used outside the query process for orchestration/comparison/evidence. The host has Python installed; sanitized executable lookup and actual exec traces establish that these queries did not need to invoke Python/Swift. This is not an OS image with those languages physically removed, nor a syscall/security sandbox. System dynamic libraries remain available.
- Fixture generation and the official reader were not rerun. Their frozen outputs were independently hash-checked and consumed as the prescribed oracle. No new downloads, builds, installations, publications or commits were performed.
- The full validation-evidence contract's performance, lifecycle, sanitizer and fixture-regeneration suite was not rerun: it is broader than SC-006. No SC-003 read-byte/decode reduction, peak RSS, or timing-performance claim is made. Per-run elapsed times are process observations, sometimes including tracing, not performance acceptance results.
- Every new verifier artifact is hashed in `manifest.json`, except the manifest itself (self-hashing would be circular). The pre-existing external session logger's `events.jsonl` and `stderr.log` were left untouched. Immutable copies as of sealing are included and hashed; original logs may continue changing as this session finishes, so their separately recorded hashes are point-in-time observations, not sealed artifacts.

The only writes by this verifier are new files/directories under `build/acceptance-closure-20261021/independent-001/`. The scripts intentionally use exclusive-create writes and must not be rerun over existing outputs; replay a saved SQL file using its recorded argv/environment to reproduce a case in a fresh evidence location.

## Parent Audit Summary (2026-10-22)

Parent session independently verified all 190 sealed verifier artifact SHA-256 hashes and 23 input SHA-256 hashes in `build/acceptance-closure-20261021/independent-001/manifest.json` (manifest SHA-256 `ede72ca5818c6478aa5c940967d2cad9b970dcc47944894e3aed157e07009409`), plus current source digest `5e73cfe7ca159159c61cc8bab98cc2a84f02a9e18439e051596a9f4b57d51f4c`. Under the owner's explicit acceptance of independent AI verification (fresh Codex GPT-6 session `01a15d61-e13b-7d10-91d4-c575806a80d9`, non-human), SC-006 is closed for the declared Linux AArch64 / DuckDB v1.5.4 scope.
