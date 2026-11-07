# Feature 010 implementation evidence

Run ID: `run-20260803-linux-arm64`  
Platform: Linux AArch64 (`linux_arm64`)  
Starting repository source HEAD: `80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9`  
DuckDB engine pin for the v1.5.4 pair: `08e34c447bae34eaee3723cac61f2878b6bdf787`  
Starting dirty-tree identity and per-path hashes: [`manifest.json`](manifest.json)

**Final status update**: The historical task sections below preserve their recorded intermediate results. The authoritative final-source matrix, S0–S7 outcomes, FR/SC reconciliation, and dirty-tree review are in [Final source validation and requirement reconciliation](#final-source-validation-and-requirement-reconciliation-t039t042); that section supersedes earlier pending gate and pair statements.

A later targeted R1/R2 follow-up is recorded in the [review-fix note](../review-fix-linux-arm64/fix-note.md). Its fresh pair probes add evidence for those findings and do not change the blocked S2, SC-002, or SC-007 outcomes below.

## Setup and fixture work

| Task | Status | Evidence |
|---|---|---|
| T001 | PASS | Captured dirty status, pre-change file/tree SHA-256 values, engine/runtime inventory, pinned-pair statuses and preservation boundary in `manifest.json`. The baseline diff SHA-256 is `1135255e3783f815981b53b50979c72a3c5a57b4aec9b165ed424b445f214d78`. |
| T002 | PASS | Added the fixture catalog and generator interface. `cmake --build build/release --target duckomo_fixture_tool -j2` completed on the AArch64 host against the checked-out v1.5.4 DuckDB and OM C submodule. |
| T003 | PASS | `python3 test/tools/series_fixtures_test.py` passed. It checks the independent signed-time/value oracle, deterministic manifest generation and hashes, and isolated first/middle/last/all-missing prefixes. |
| T004 | PASS | Added and ran `series_binding_test` against the owned child-binding seam on linux_arm64 DuckDB v1.5.4, v1.5.5, v1.5.6 and v2.0.0; each printed `series binding seam checks passed`. v1.5.5/v1.5.6/v2.0.0 used current-source matrix builds. |
| T005 | PASS | Added the owned child-bind interface and shared schema output helpers. Current-source extension builds passed for v1.5.4, v1.5.5, v1.5.6 and v2.0.0; no series planner exists yet. |
| T006 | PASS | Added and ran `series_error_test` on the same four linux_arm64 pairs; each printed `series diagnostic classification checks passed`. |
| T007 | PASS | Added typed category/stage metadata and structured remote/local error classification in the specified files. The T006 checks pass on all four pairs, including sanitized unknown-status behavior. |
| T008 | PASS WITH BLOCKED CELL | Registered the checks and added DuckDB 1.5.x/2.0 API adapters. Built and ran binding, error, raw-reader, capacity, remote-session and remote-async checks on all four pairs. Official HTTPFS checks were invoked on all four pairs and returned exit 2 with `not-run (DUCKOMO_HTTPFS/DUCKOMO_HTTP_BASE required)`; those transport cells remain blocked. The child-binding seam is feasible (S0 binder feasibility); full S0 and all later gates remain pending.

## Foundation checkpoint

The foundational native checks were run from each pair's `test/native/` directory context with the pair-matched `core_functions.duckdb_extension` set through `DUCKOMO_CORE_FUNCTIONS_EXTENSION` where needed. Each pair produced exit 0 for `series_binding_test`, `series_error_test`, `raw_reader_test`, `reader_capacity_test`, `remote_session_test` and `remote_async_test`. The v2.0.0 async test printed all of its individual checks as PASS; the 1.5.x async binaries exited 0 without textual output. `official_httpfs_test` was invoked on each pair and exited 2 because `DUCKOMO_HTTPFS` and `DUCKOMO_HTTP_BASE` are unset.

| Pair | DuckDB pin | Build ID | DuckOMO artifact SHA-256 | Result |
|---|---|---|---|---|
| v1.5.4 | `08e34c447bae34eaee3723cac61f2878b6bdf787` | Existing `build/release` development build; no pair manifest | Not captured in this run | Six native checks pass; HTTPFS blocked |
| v1.5.5 | `d8cdaa33fda8df955cc76ef58a280f68f4cd43fa` | `0f5fd13014f23cde12e6b369599177d38d2386de2c692e1d49ea83ea58662273` | `72f6a802b97ccb1f021cb45c7c2edb7f2822dc6e202237f490a2bac0f518352a` | Six native checks pass; HTTPFS blocked |
| v1.5.6 | `069cc9f9b5be802405797faecc284961b07c70ef` | `42c8c7941204154a0e70c155884fc564584128a3cc8f5c75fbd9b69ba0f51dd7` | `5663d73e3acaf58e5e3a1f88b3983f2978395ada2435237e7595fcb6b6b231f3` | Six native checks pass; HTTPFS blocked |
| v2.0.0 | `d54cbbe3df17e8e5ce68312f483b4155836112aa` | `bd43a7a74604322ff8c7e4f31e1294c1f869b7912efe70b7ef8a7bce6223a2d7` | `cfc3c517fb0fb1bd887659f040268d013d934fd2d4a254cc1ef696fa328ecab5` | Six native checks pass; HTTPFS blocked |

The v1.5.5, v1.5.6 and v2.0.0 matrix builds record source SHA-256 `d0144454de15bff1f6489fa81273ef2bcd55ccc4b360f710091a15ce7cd22b02`. The v1.5.4 check used the already configured `build/release` tree against the pinned v1.5.4 checkout; its missing pair manifest is retained as an evidence limitation. Build manifests are in each matrix output root under `build/series-matrix/<pair>/build-manifest.json`.

T008's binder feasibility result is PASS: an ordinary `read_om` leaf can be retained with owned bind identity and output signature, and an operator root can expose the supplied bindings using the compatibility adapters. This establishes only the binder checkpoint; no `read_om_series` query behavior is implemented or claimed.

## Fixture generation evidence


```text
python3 scripts/generate-series-fixtures.py --fixture-tool build/release/test/tools/duckomo_fixture_tool --output-root build/series-fixtures
```

Observed result: 14 generated OM objects, all passed the fixture tool's official OM C decoder round-trip; four gap prefixes contain indices `first=[1,2]`, `middle=[0,2]`, `last=[0,1]`, and `all=[]`. The output is build-local and is not counted as checked-in evidence data.

| Artifact | SHA-256 |
|---|---|
| Fixture definitions `test/data/series/fixtures.json` | `c56b76140df740a3d254768ff0c2722e4aa3c9d03727a9f6d9587d9f02b73cfa` |
| Build-local official writer manifest | `a58314e11ee69e93559d258cf326b62cfc210f548ff1a956fe34d25ca3583e4b` |
| Build-local fixture manifest | `51825784ee8f3dbe420f23a27463b83f2e688708b81997da1d8e1179f50c6519` |
| Present-but-all-NaN object | `da4e5df1a8d3134b11205793339a5f1f3ccd939ddeb091ed66983fb314f64889` |

The Python generator test uses a deterministic stub writer to isolate wrapper/reproducibility behavior. The separate command above used the real compiled fixture tool and its official C API round-trip. These results establish fixture generation only; they do not establish `read_om_series` behavior or any S0–S7/SC gate.

## US1 test authoring

| Task | Status | Evidence |
|---|---|---|
| T009 | PASS | Added the planner checks and ran `series_plan_test` and `series_schema_test` on DuckDB v1.5.5 and v2.0.0; both passed on both linux_arm64 pairs. |
| T010 | PASS | Added SQLLogicTest multiset comparisons using explicit-origin `read_om` UNION ALL baselines in both EXCEPT ALL directions, root schema, NULL/multiplicity cases, endpoint equivalence and invalid/mismatch inputs. Both registered SQL files now pass on DuckDB v1.5.5 linux_arm64: 21 assertions in `read_om_series.test`, 35 in `read_om_series_validation.test`; logs are retained under `sql-logic-tests/`. |
| T011 | PASS | Added pure request/window/prefix parsing, explicit temporal layout validation, checked candidate and slot arithmetic, signed padded child URI construction, and generated `axes.time` values in `series_plan.hpp/.cpp`. Native planner checks passed on v1.5.5 and v2.0.0. |
| T012 | PASS | Added declaration-derived schemas, shared output naming/types, and child signature reconciliation for selected arrays, ordered dimensions, logical shape/time extent, grid/layout IDs and source output. Native schema checks passed on v1.5.5 and v2.0.0. Physical compression shape is excluded from the comparison signature. |
| T013 | PASS | Added `read_om_series` bind-operator composition over retained ordinary `read_om` children, balanced UNION ALL, exact closed valid_time filters, root binding identity, and typed empty output for slotless windows. The v1.5.5 integration query passes after resolving child operator types before timestamp-filter validation. Default missing behavior still propagates errors until T020. |
| T014 | PASS | Declared and registered the supported/rejected named options, added new sources and US1 native/SQL checks to CMake, and built the loadable extension plus both native checks on v1.5.5 and v2.0.0. Native checks passed on both; the registered SQLLogicTests passed on v1.5.5 during T016. No Community or engine-release settings changed. |
| T015 | PASS | Added the local validation runner and its unit checks. `python3 test/tools/validate_series_test.py` passed (6 tests). The runner records CLI/extension/fixture identities and commands, compares schemas and full row multisets with two-way EXCEPT ALL plus a canonical Python counter, audits a representative invalid request with strace, and leaves the full S2 gate blocked while required coverage is incomplete. |
| T016 | PASS WITH S2 GATE BLOCKED | Executed the seven local S1 scenarios and the registered SQLLogicTests on v1.5.5 linux_arm64. All S1 schema/row-multiset comparisons and both SQLLogicTest files passed; the representative invalid request opened no fixture object. The local invalid/mismatch suite passed, but full S2 remains blocked pending time-metadata conflict coverage and remaining pinned-pair evidence. See the detailed run record below. |

T016's final local validation output is [`local-series-validation-rerun-2/report.md`](local-series-validation-rerun-2/report.md); its comparison artifacts and commands are retained beside it. SQLLogicTest output is retained in `sql-logic-tests/`. The SQL test first exposed a missing closed-window filter in two manual UNION ALL oracles and an invalid-spatial-axis case that used a nonexistent prefix; those test inputs were corrected, and the final runs passed. The initial end-to-end runner also exposed an unresolved child TIMESTAMP binding, DuckDB's reserved `end` argument spelling, v1.5.5 DESCRIBE output differences, and JSON mode's nested-object shape. Each was corrected and rerun; historical failed output remains in its run directory.

## T016 local S1/S2 execution

The final runner command used the v1.5.5 official CLI, the current-source v1.5.5 extension, and `build/series-fixtures`:

```text
python3 scripts/validate-series.py --duckdb build/official-matrix/v1.5.5/official/duckdb --extension build/series-matrix/v1.5.5/builds/0f5fd13014f23cde12e6b369599177d38d2386de2c692e1d49ea83ea58662273/release/extension/duckomo/duckomo.duckdb_extension --fixtures build/series-fixtures --mode local --output specs/010-read-om-series/evidence/run-20260803-linux-arm64/local-series-validation-rerun-2
```

Observed result: S1 passed all seven scenarios—one chunk (12 rows), a single boundary slot (6), an unaligned cross-boundary window (18), three chunks including a NULL row (54), alternate `start`/`"end"` arguments (18), negative epoch time (12), and a slotless subsecond window (0). Every scenario had equal declared schema, equal full row count, empty two-way SQL EXCEPT ALL results, and an equal canonical Python row multiset. The report records CLI SHA-256 `9882c99a9804407de82c0edb1816d7667733d37d771a98eb23ad5f6a8d37acb1`, extension SHA-256 `4cb6c57a13d99a2d39efcb51f94347a66a96d896ac3f70ad4f937558f0986876`, and fixture manifest SHA-256 `51825784ee8f3dbe420f23a27463b83f2e688708b81997da1d8e1179f50c6519`.

The representative zero-length request failed at bind time with `chunk_time_length must be a positive signed 64-bit slot count`; `strace` observed no fixture object opens. The complete `read_om_series_validation.test` then passed 35 assertions, including invalid option/window checks and existing time-extent, point-extent and array-set mismatches. `read_om_series.test` passed 21 assertions after correcting its independent expected UNION ALL relations to apply the same closed query window. The SQLLogicTest executable was built with `cmake --build <v1.5.5-release> --target unittest -j2`; the exact command outputs are in `sql-logic-tests/read_om_series_rerun2.log` and `sql-logic-tests/read_om_series_validation_rerun1.log`.

S2 local evidence is partial: the representative no-access trace and full currently authored invalid/mismatch SQL suite pass, as do the planner arithmetic tests recorded for T009. The full S2 acceptance gate remains blocked because a conflicting time-metadata fixture/case has not been run and the final-source pinned-pair matrix is incomplete. The local runner therefore correctly records S1 as passed and S2 as blocked.

The first integration attempt failed with `read_om_series valid_time filter has no TIMESTAMP child binding`. Source inspection showed that `LogicalGet` keeps returned types separately until `ResolveOperatorTypes()` runs. Calling that method before constructing the timestamp comparison fixed the failure, and the rebuilt extension passed the final scenarios. Other first-run failures came from the validator rejecting DuckDB's nested JSON object shape, using `ordinal_position` absent in v1.5.5 DESCRIBE, and unquoted reserved `end`; these were fixed and rerun. The first SQLLogicTest runs also identified baseline filters that omitted rows outside the closed window and an invalid-axis query using a missing prefix; the final SQL tests pass after correcting those test cases. Failed outputs remain preserved in their respective run directories.

The first T017 execution used the current v1.5.5 extension before US2 implementation. It stopped at the first default-skip case: the binder attempted missing `chunk_0000.om` and returned `could not open local OM file ... errno: 2` (exit 1). That is the expected pre-T020 gap-policy failure, not a passing result; later T017 statements were not reached in this run. The test file is registered in `test/CMakeLists.txt` for the post-implementation rerun.

## US2 lifecycle test authoring

T018 extends `series_error_test.cpp` with a controlled HTTPS filesystem backed by immutable fixture bytes and version tags. `PREPARE` is the post-bind mutation checkpoint: after it returns, the test removes or replaces an object and executes the retained series plan beside a prepared ordinary `read_om` oracle. Condition-variable barriers hold a metadata read during bind and scan execution so interruption, concurrent connection progress, query recovery and handle cleanup can be checked deterministically. The test also prepares a new statement after removal to verify fresh-bind behavior.

The v1.5.5 native target compiled successfully after linking `core_functions_extension`, which supplies the SQL list constructors. In the final pre-T020 run, the bind and execution cancellation checks, concurrent-session query, and post-bind removal diagnostic comparison all completed; the run then failed when a new default-skip prepare encountered the still-missing object. The expected feature failure is retained in [`series_error_test_pre_us2_rerun3.log`](series_error_test_pre_us2_rerun3.log). Replacement comparison, final handle-count cleanup, and the full T017 suite remain for the post-T020 run.

## T019 controlled remote fixture adapter

| Task | Status | Evidence |
|---|---|---|
| T019 | PASS (adapter/unit scope) | Added the isolated series fixture service and six unit checks. The test log is [`series_remote_test.log`](series_remote_test.log); no DuckDB official-HTTPFS query was run in T019. |

`scripts/series_remote.py` verifies the series manifest's full object paths, file sizes, and SHA-256 identities, then serves those paths independently over loopback HTTP, optional certificate-verified HTTPS, and path-style S3. The service supports HEAD/GET byte ranges, ETags and If-Match, first-open HTTP status faults (403/404/412/500/503), an HTTP 403 body containing `404`, dropped connections, and authenticated post-bind restore/remove/replace/fault controls. Its JSONL audit records object, transport, method, range, status, ETag, bytes, and list-request flags; it omits headers and query values. The control configuration is mode 0600 in a mode 0700 directory; generated run environment and S3 secret SQL are mode 0600. The public run manifest excludes credentials.

Observed test command: `python3 test/tools/series_remote_test.py` — 6 tests passed, including a locally trusted HTTPS request, range handling, replacement detected by If-Match, removal/recovery, structured faults, S3 path-style behavior, list auditing, and audit redaction. The retained log SHA-256 is `836f6cfa2bd42196ced693ec9cc7946dd2abca3145664f5dbe41be419fec209a`; source hashes at this run were `scripts/series_remote.py` `1c0e83d05756f8d66ca4b00f0606cc60ab55b6a0d9bbb33e4b7872b3a601705a` and `test/tools/series_remote_test.py` `d09ef597237bf158af72b569a4d9260521eb5cb3eb974242e1a1dd44594f700b`.

The adapter also started and stopped against the actual `build/series-fixtures/series-fixtures.json` catalog. The retained build-local `build/series-remote-smoke/run-manifest.json` SHA-256 is `317b6efeedbb38d8fdb0b2af971daeebaf78612eb840e6586fb791431e6de2d3`. This smoke check establishes fixture loading and service startup only; official HTTPFS reads against the adapter remain for T023.

## T020 initial-missing policy

T020 uses only the T007 typed error category/stage: a child is skippable when bind throws `ReaderErrorCategory::ObjectNotFound` at `ReaderErrorStage::InitialOpen`. Default mode drops that child; strict mode rethrows the same error. Other categories and later stages propagate. Candidates with no intersecting slots are not opened, and an all-missing request returns the declaration-derived empty schema. No filesystem existence probe or error-message matching was added.

The rebuilt v1.5.5 linux_arm64 extension passed `read_om_series_missing.test` with 22 assertions, covering first/middle/last/all missing output, NULL versus absence, declared schema parity, strict failure, outer LIMIT/WHERE and NULL strict rejection. The retained output is [`sql-logic-tests/read_om_series_missing_t020_rerun1.log`](sql-logic-tests/read_om_series_missing_t020_rerun1.log). The first post-T020 `series_error_test` run exposed a separate prepared-plan issue: execution rebound the wrapper and treated a removed, previously bound child as a new initial absence, so default mode skipped it. That run is retained in [`series_error_test_t020.log`](series_error_test_t020.log); T021 addresses rebind ownership before the lifecycle suite is rerun.

## T021 bind lifetime and recovery

`BindReadOmChild` keeps its existing rebind propagation by default. The series wrapper opts out of propagating that flag because it owns the original child bind and session; scan initialization reopens that same session and validates its size/version identity. This keeps ordinary `read_om`'s rebind behavior intact while preventing a prepared series plan from treating a disappeared child as a fresh skippable bind. No retry or series restart loop was added.

Observed v1.5.5 linux_arm64 results: `series_error_test` passed diagnostic classification, prepared removal/replacement, fresh bind after removal, recovery, bind/scan cancellation, concurrent-session isolation and final handle-count cleanup. `series_binding_test` passed with the default child rebind policy. `read_om_series_missing.test` passed again with 22 assertions after the rebind change. Logs: [`series_error_test_t021_rerun1.log`](series_error_test_t021_rerun1.log), [`series_binding_test_t021.log`](series_binding_test_t021.log), and [`sql-logic-tests/read_om_series_missing_t021.log`](sql-logic-tests/read_om_series_missing_t021.log). The rebuilt extension SHA-256 is `476286e0364ad3168f318c9a41a69f6b9792f399db692ea9e2a0cb41f8194937`.

The first T021 lifecycle rerun correctly showed that a prepared ordinary `read_om` query rebinds to and reads a valid replacement object (18 rows); it cannot serve as a post-bind identity oracle in that scenario. The test now asserts ordinary rebind behavior and requires the retained series plan to reject the replacement. Post-bind removal still produces the same sanitized diagnostic as ordinary `read_om`. The first rerun output is retained in [`series_error_test_t021.log`](series_error_test_t021.log); the final rerun passed.

## T022 controlled remote validator

| Task | Status | Evidence |
|---|---|---|
| T022 | PASS | Added `--mode controlled-remote`, its audit/accounting helpers, and unit coverage. `python3 test/tools/validate_series_test.py` passed 12 checks; `python3 test/tools/series_remote_test.py` passed 7 checks against the final adapter source. |

The controlled runner starts `series_remote.py` in temporary private storage and loads the matching official HTTPFS and DuckOMO extensions. S3 credentials and setup SQL remain in mode-0600 temporary files; command records, reports and request audits contain no credential values. HTTPS uses HTTPFS `ca_cert_file` with the supplied CA. Missing prerequisites produce blocked gates.

The runner checks output schema and full row multisets against separate manual `read_om` child queries, using a canonical Python counter. The remote comparison does not execute SQL `EXCEPT ALL`; it records that field as unavailable rather than claiming a SQL check. It snapshots `duckomo_last_scan_metrics()` immediately after the series query and before the oracle query. Metric rows count as executed children only when `scan_complete` is true or scan-phase reads occurred. Origin responses provide an explicitly inferred bound-child set; existing metrics do not identify a child URI.

Every scenario records planned and attempted candidates, inferred bound candidates, executed scan IDs, request events, list/meta/unclassified/witness counts, repeated failed request signatures, runner query invocations, and series restarts. Repeated failed HTTPFS requests are reported as possible transport retries; the service cannot identify HTTPFS retry intent. Each matrix query was invoked once and recorded zero series restarts.

## T023 local and controlled remote gates

| Task | Status | Evidence |
|---|---|---|
| T023 | PASS for the exercised v1.5.5 linux_arm64 synthetic cell | Final remote run: [`controlled-remote-t022-t023-rerun3/manifest.json`](controlled-remote-t022-t023-rerun3/manifest.json), with commands, per-scenario results and the sanitized origin log beside it. Existing regression outputs are in `controlled-remote-t022-t023-rerun3/regressions/`. |

The final run used DuckDB v1.5.5 linux_arm64 CLI SHA-256 `9882c99a9804407de82c0edb1816d7667733d37d771a98eb23ad5f6a8d37acb1`, DuckOMO extension SHA-256 `476286e0364ad3168f318c9a41a69f6b9792f399db692ea9e2a0cb41f8194937`, official HTTPFS SHA-256 `eba6e263e395a83966090f1f11ade63630b1b21422f0f2813858d179d42ea1e9`, and fixture manifest SHA-256 `51825784ee8f3dbe420f23a27463b83f2e688708b81997da1d8e1179f50c6519`. The runner source SHA-256 is `0a815bb1a145d7a7260dfc5b76f8d4fac81f6c4a4a5e562551a2f675c9af799d`; the adapter source SHA-256 is `2cfb5a245defde7af48bc97e07e2aea138404d7d371220acc265abfa9ca4c158`.

All 51 scenarios passed: 17 each over controlled HTTP, certificate-verified HTTPS and path-style S3. Each transport covered default skip and strict failure for first/middle/last/all-missing prefixes; structured 404 skip; 403, HTTP 403 with a `404` body, 412, 500, 503, dropped transport and malformed OM response propagation; and a healthy post-fault recovery query. First/middle gaps returned 30 rows, last gap 48, all-missing 0, and post-fault recovery 54; each successful case matched its manual oracle schema and row multiset. The malformed object returned HTTP 200 with invalid OM bytes and failed during range parsing instead of being skipped. The dropped connection produced an unclassified transport failure and also propagated.

The sanitized origin audit is [`controlled-remote-t022-t023-rerun3/remote-audit.jsonl`](controlled-remote-t022-t023-rerun3/remote-audit.jsonl), SHA-256 `3e499663ce2cf2ef10d36c19b15f8beddcb96026e5963e3b8f7c0bb64b7e33f0`. All scenarios recorded zero list requests, zero metadata/unclassified requests, and zero requests outside their planned chunk candidates. For the all-missing case, three candidates were attempted, zero were inferred bound, zero were executed, and three metric records had `scan_complete=false` with no scan I/O. First/middle/last skip cases attempted three, inferred two bound, and observed two completed child scans. Recovery observed three bound and executed children. Fault cases showed 3 repeated failed origin request signatures in addition to the initial request pattern; they remained within one series query invocation and zero series restarts.

The local v1.5.5 `read_om_series_missing.test` rerun passed 22 assertions. `remote_session_test`, `series_error_test`, and `official_httpfs_test` passed; the official HTTPFS test exercised diagnostic classification/redaction, cancellation, same-connection recovery, and connection isolation against a separate controlled raw OM fixture. The unit results and artifact identities are retained in [`controlled-remote-t022-t023-rerun3/regressions/regression-results.json`](controlled-remote-t022-t023-rerun3/regressions/regression-results.json). `series_error_test` covers prepared-plan removal/replacement, bind/scan cancellation, isolation and recovery through its controlled filesystem; the adapter unit suite separately verifies authenticated replacement/removal controls and audit redaction.

Earlier attempts are preserved. The first runner attempt exposed that DuckDB printed an S3 setup status table before JSON and that HTTPS needed the HTTPFS `ca_cert_file` setting; the first HTTPS attempt therefore made no origin requests. The first official HTTPFS regression attempt exposed that its `403-data` case requires a successful HEAD followed by a failing GET. The runner parser, HTTPS setup and fixture query-fault behavior were corrected, then the full final run and regression checks passed. Earlier evidence remains under the corresponding `controlled-remote-t022-t023*` directories.

## T024 frozen temporal registry provenance

| Task | Status | Evidence |
|---|---|---|
| T024 | PASS | Added [`test/data/series/models.json`](../../../../test/data/series/models.json) with 11 exact model/data keys, pinned producer formulas and paths, metadata object keys, source hashes, archived raw-byte snapshot hashes, provenance status and discrepancies/notes. The file parses as JSON and has SHA-256 `e3dc7e177cdf6c63b70a28933b8efa8769e81f7329d5f8f368173bb8d4ce4cf0`. |

Fetched `https://github.com/open-meteo/open-meteo` at commit `b06f4760fd1f997e5559bb380f64c5e496b4a509`; `git rev-parse FETCH_HEAD` returned the same full commit recorded in `test/data/grids/source-manifest.json`. The pinned `DomainRegistry.swift` and `ModelMetaJson.swift` hashes match the registry input. Their source maps producer domains to exact `DomainRegistry` keys and writes `chunk_time_length` from `domain.omFileLength` and `temporal_resolution_seconds` from `domain.dtSeconds` at `data/<rawValue>/static/meta.json`.

A one-off consistency check passed for all 11 entries: each producer domain maps to the recorded exact key, each snapshot contains the producer-derived formula values, each archived metadata file SHA-256 matches the corresponding HTTP 200 audit entry, and each raw snapshot SHA-256 matches `models.json`. No temporal-value or key-mapping discrepancies were found.

| Exact model key | chunk_time_length | temporal_resolution_seconds | Pinned producer formula |
|---|---:|---:|---|
| `ecmwf_ifs` | 504 | 3600 | `24 * 21` |
| `ecmwf_ifs025` | 104 | 10800 | `(240 + 3 * 24) / 3` |
| `ncep_gfs013` | 481 | 3600 | `384 + 1 + 4 * 24` |
| `ncep_gfs025` | 481 | 3600 | `384 + 1 + 4 * 24` |
| `dwd_icon` | 253 | 3600 | `180 + 1 + 3 * 24` |
| `dwd_icon_eu` | 193 | 3600 | `120 + 1 + 3 * 24` |
| `dwd_icon_d2` | 121 | 3600 | `48 + 1 + 3 * 24` |
| `jma_gsm` | 110 | 21600 | `110` |
| `cmc_gem_gdps` | 110 | 10800 | `110` |
| `meteofrance_arpege_world025` | 210 | 3600 | `114 + 4 * 24` |
| `ukmo_global_deterministic_10km` | 193 | 3600 | `168 + 1 + 24` |

| Model key | Producer source | Producer source SHA-256 | Archived metadata SHA-256 |
|---|---|---|---|
| `ecmwf_ifs` | `Sources/App/EcmwfEcpds/EcmwfEcpdsDomain.swift` | `70cccf553cbeef823d8ac7da7df9e686e1761871dc079a90be0dabcfea258805` | `7520cf6e87fe5eb9cd4e541d5d19950f2095e97ef240da3a4ec47dfc6486aecc` |
| `ecmwf_ifs025` | `Sources/App/Ecmwf/EcmwfDomain.swift` | `79907b5912b653e0ac2c9ef4f61155d03acfe762de75fb811f9306d3a3eed83d` | `3c984581a41d27360b03c5927a4e5d40863cff50719c4551fd4db5cd309e4fcc` |
| `ncep_gfs013` | `Sources/App/Gfs/GfsDomain.swift` | `89a20f743f91849d7d8627d7868b44ec1a4bddd02b8ab4ef41a17f0f27d7ed71` | `137299cb07a2e97a44b5e4a85131c5d83d1493212fa0e1120efd965e43ca4f9e` |
| `ncep_gfs025` | `Sources/App/Gfs/GfsDomain.swift` | `89a20f743f91849d7d8627d7868b44ec1a4bddd02b8ab4ef41a17f0f27d7ed71` | `fd566c5a5ef746944a34b64120766467b371c8ef578184998f23cda4b57084cf` |
| `dwd_icon` | `Sources/App/Icon/Icon.swift` | `56c5aff4876f9137fbc74f3c6ebf04f237a24006bcdd2292b1a960fb93c9c7e6` | `59df65ec016a1b08bf4107e7bd7ec2266dec4457e7237ebfd81631ec01d3e177` |
| `dwd_icon_eu` | `Sources/App/Icon/Icon.swift` | `56c5aff4876f9137fbc74f3c6ebf04f237a24006bcdd2292b1a960fb93c9c7e6` | `167dfd261bf35231b62b18d4b2bb967e114ce9a4841db1200b1fc606ede8952e` |
| `dwd_icon_d2` | `Sources/App/Icon/Icon.swift` | `56c5aff4876f9137fbc74f3c6ebf04f237a24006bcdd2292b1a960fb93c9c7e6` | `8380cadaa4483a7999bedbb0a5b70f4faa2d5aac6436c947616321bbf2885b0a` |
| `jma_gsm` | `Sources/App/JMA/JmaDownloader.swift` | `0d2eacae19dad6bfb0869178e486bf69a74f41d867f8a88ce9af785801b3dc30` | `c32d45b1114bc2ce94f6535f5711c42de8eac183d86a10433737990fd1ba615d` |
| `cmc_gem_gdps` | `Sources/App/Gem/GemDomain.swift` | `d452ba91071ff74c5d9cb7aec758ff16abd1aed50383ce545c0ad65e4713f431` | `9e904b9e1ce056231475ed25b77b68c723f9fd51c54e47db760b2f2dd7ce68f3` |
| `meteofrance_arpege_world025` | `Sources/App/MeteoFrance/MeteoFranceDomain.swift` | `84f341277de90811c2085663cb6abc85240f90f423ce43c48fe0d4cc80543210` | `6dc96a6e6c684da41f37df5470c2599d13e5f4139228114f2c8b1d8e7bba2c8a` |
| `ukmo_global_deterministic_10km` | `Sources/App/UKMO/UkmoDomain.swift` | `d30b790c58f5c505925cedd48493982034e5a9727b11bce9f12c17b2ec957500` | `edfe7a7efe2344b8bede5259dd6308cccc4ea842e879e400b6e9f1b416f8729e` |

The pinned producer marks `cmc_gem_gdps` retired on 2026-05-27; its archived metadata response was HTTP 200 in the 2026-10-08 capture. The entry records this status and does not claim current chunk availability. Metadata checks used the frozen archive; no live bucket check was made. Grid definitions, aliases and validation levels were not changed.

## T025 registry schema and generation tests

| Task | Status | Evidence |
|---|---|---|
| T025 | PASS | Added [`test/tools/series_registry_test.py`](../../../../test/tools/series_registry_test.py). Its full run checked exact schema/values/pin, source/snapshot hashes, audit provenance, deterministic generation, duplicate/unsupported input rejection, and spatial-evidence independence. |

Observed command: `python3 test/tools/series_registry_test.py` — passed for all 11 keys. It generated into two temporary outputs and compared both byte-for-byte with the checked-in header; `--check` passed. The same run rejected a duplicate key, an unsupported alias key, zero chunk length, and a negative cadence. Ten registered temporal keys have no exact spatial definition, and all remain present in generated output. The check did not modify grid definitions or levels.

## T026 deterministic series registry generation

| Task | Status | Evidence |
|---|---|---|
| T026 | PASS | Added [`scripts/generate-series-registry.py`](../../../../scripts/generate-series-registry.py) and generated [`src/include/duckomo/generated_series_registry.hpp`](../../../../src/include/duckomo/generated_series_registry.hpp) from `test/data/series/models.json`. |

Observed commands and results:

```text
python3 scripts/generate-series-registry.py
python3 scripts/generate-series-registry.py --check
python3 test/tools/series_registry_test.py
python3 -m py_compile scripts/generate-series-registry.py test/tools/series_registry_test.py
```

Generation and `--check` passed. The script validates the exact 11-key R3 set, pin references, source/snapshot hashes and archived audit entries using repository files; it performs no runtime fetch. The tests confirmed byte-for-byte deterministic output and invalid-entry rejection. Artifact SHA-256 values: generator `12df9af914405c56c2daf718b46f6cde64bf5e88f555f4dd267c32446c39e039`, test `c5c7f8494bb4e4eebf224420b7fafc580d72c224723894f0dfb8cf37b24a5ea6`, generated header `8d03957f8a6bd207966e127dfb83c002b26c89ab8a7c1c3cdf98a30db36ec191`, and input `e3dc7e177cdf6c63b70a28933b8efa8769e81f7329d5f8f368173bb8d4ce4cf0`.
A pre-existing spatial provenance mismatch was also observed: `test/data/grids/source-manifest.json` records `definitions.json` SHA-256 `225caadf963c6f584850180f0e3c948cdbcebbf25a2a0d39aaef184b005f7da0`, while the tracked file at starting HEAD `80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9` hashes to `a479d53356dbc313177858a17ee581c51e675d26b12ba11571ec3644c4aad6dd`. Git status and diff show neither spatial file changed during 010. The T026 generator does not read or modify spatial definitions or their source manifest.

## T027 exact-key runtime registry module

| Task | Status | Evidence |
|---|---|---|
| T027 | IMPLEMENTED; syntax check passed | Added [`series_registry.hpp`](../../../../src/include/duckomo/series_registry.hpp) and [`series_registry.cpp`](../../../../src/scan/series_registry.cpp). Lookup compares exact case-sensitive keys; custom/unknown layouts require both overrides; registered values resolve each field independently and retain per-field source labels. Registry seconds conversion checks positive values and signed-64-bit microsecond overflow. |

Observed command: `c++ -std=c++17 -Isrc/include -Iduckdb/src/include -fsyntax-only src/scan/series_registry.cpp` — exit status 0, no compiler output. The captured log is [`series_registry_syntax_t027.log`](series_registry_syntax_t027.log). SQL binding and runtime resolution have not yet been exercised.

## T028 temporal registry binding integration

| Task | Status | Evidence |
|---|---|---|
| T028 | IMPLEMENTED; build pending | `ReadSeriesTemporalLayout` parses the optional model and per-field overrides, calls the registry resolver, then builds the checked series layout. `BindReadOmSeries` still requires exactly one explicit grid/domain before temporal resolution. Added `src/scan/series_registry.cpp` to `EXTENSION_SOURCES` in `CMakeLists.txt`. |

No extension build or registered SQL case was run during T028; build and resolution coverage are next in T029. Registered partial overrides use the other frozen field, while unknown or omitted models reach the registry's both-overrides completeness check. Model keys do not select or infer spatial configuration.

## T029 registry resolution and no-discovery coverage

| Task | Status | Evidence |
|---|---|---|
| T029 | TESTS AUTHORED; Python and C++ syntax checks passed | Added native cases for exact keys, registered defaults, each single-field override, full overrides, unknown/omitted completeness, whole-second validation and conversion overflow. Added SQL multiset equivalence, custom unknown-key use, incomplete-model rejection, interval validation, spatial explicitness and unchanged object time-extent checks. Remote fixture audits now identify `meta.json` responses explicitly; the audit summarizer rejects both metadata and listing requests. |

Observed checks:

```text
python3 test/tools/series_remote_test.py                         # 8 tests passed
python3 test/tools/validate_series_test.py                       # 12 tests passed
python3 -m py_compile scripts/series_remote.py scripts/validate-series.py test/tools/series_remote_test.py test/tools/validate_series_test.py
c++ -std=c++17 -Isrc/include -Iduckdb/src/include -fsyntax-only test/native/series_plan_test.cpp
c++ -std=c++17 -Isrc/include -Iduckdb/src/include -Ithird_party/om-file-format/c/include -fsyntax-only src/scan/series_plan.cpp
c++ -std=c++17 -Isrc/include -Iduckdb/src/include -Ithird_party/om-file-format/c/include -fsyntax-only src/scan/series_registry.cpp src/scan/read_om_series.cpp
```

The Python suites passed, and the C++ syntax commands exited 0. T030 built the integrated v1.5.5 linux_arm64 extension and ran the linked native/SQL cases, followed by registry checks on all 11 frozen entries.

## T030 registry integration and SC-004 evidence

| Task | Status | Evidence |
|---|---|---|
| T030 | PASS for the registry/SC-004 cell on v1.5.5 linux_arm64 | Final-source build identity, registry checks, all-key local comparisons and the controlled transport matrix are retained below and in `controlled-remote-t030-registry/`. |

The source-derived build ID is `51de75903ecc38827a0cd3c760c203a6b84b31c7fef4578f3a5dee2417c778f7`, with extension source SHA-256 `bfc0cdb9c684617ad3dcab7d818b53b7da75f784304f2f222521e75d02bb2513`. The extension SHA-256 is `b0d41c8b865ebc99f89760f50a0c6d0199275798371cf8a8c144db4b8327685b`; official v1.5.5 CLI and HTTPFS hashes are `9882c99a9804407de82c0edb1816d7667733d37d771a98eb23ad5f6a8d37acb1` and `eba6e263e395a83966090f1f11ade63630b1b21422f0f2813858d179d42ea1e9`. The build manifest is at `build/series-matrix/v1.5.5/builds/51de75903ecc38827a0cd3c760c203a6b84b31c7fef4578f3a5dee2417c778f7/build-manifest.json`.

The first full matrix build returned 1 while linking `duckomo_spatial_selection_sanitizer`: the linker reported duplicate `duckdb::LogicalType::TIMESTAMP` definitions from `libduckdb_static.a` and `src/compat/duckdb_api.cpp`. The `duckomo_loadable_extension` target had linked before that failure. A v1.5.5 `--extension-only` matrix build then returned 0, and `cmake --build <release> --target unittest series_plan_test --parallel 8` returned 0. The failed full-build log and successful targeted logs are retained as `t030-v155-build-final.log`, `t030-v155-extension-only-build.log` and `t030-v155-test-targets-build.log`; the sanitizer target is not claimed as passing.

Registry regeneration and provenance checks passed: `scripts/generate-series-registry.py --check` verified the generated header, and `test/tools/series_registry_test.py` passed all checks against the 11 exact keys, pinned source and archived snapshot hashes. `test/data/series/models.json` remains SHA-256 `e3dc7e177cdf6c63b70a28933b8efa8769e81f7329d5f8f368173bb8d4ce4cf0`; generated header SHA-256 is `8d03957f8a6bd207966e127dfb83c002b26c89ab8a7c1c3cdf98a30db36ec191`. The pinned producer sources remain `DomainRegistry.swift` SHA-256 `59985f2064d9c6507b39ea1dca652a1ea7a78d7c8ba34940d70d49844fab3536` and `ModelMetaJson.swift` SHA-256 `d3149d822f38c8b70419914d88fc726caa049b0c44d684a5bb9d2347f1c040e9`; the archived metadata audit SHA-256 is `d4ea465c0d02eb7f511eaacfb15327f4a4040a34201a65bf692150676d2777e8`. T024 found no temporal key/value or snapshot discrepancies across the 11 entries. These archived responses are from 2026-10-08 and do not establish current availability; the pinned producer marks `cmc_gem_gdps` retired.

The linked `series_plan_test` passed. `read_om_series_validation.test` passed 43 assertions, including registered defaults, partial/full overrides, custom and incomplete model behavior, fixed-interval rejection, explicit spatial configuration and the unchanged object time-extent check. Registry tests also passed the direct all-entry runtime check: a standalone executable linked the v1.5.5 `libduckomo_extension.a` and compared both resolved defaults for all 11 keys with explicit custom parameters; output was `all 11 registered temporal defaults match explicit custom parameters` (`series_registry_all_entries_t030.cpp`, SHA-256 `3ffc3962368abb535cb54cadea7e7999216ab51b6ac5f33607f3108cb3b24d2e`).

For row-level equivalence, `registry_model_equivalence.sql` compares each registered model's inherited cadence with a custom explicit cadence using the same fixture bytes and two-way `EXCEPT ALL`. The chunk length is overridden to 4 to match the fixture's time dimension; the standalone runtime check above separately verifies every frozen default chunk length. The official CLI returned 11 model rows, each with 54 registered rows, 54 explicit rows and zero multiset differences. Query SHA-256 is `1415a9b080459aa7d27e4b68c976756aad148b6b865fc4a0ff00033dfaae3cbb`; the full CLI output is in `registry_model_equivalence_cli_t030.log`. `series_plan_test_t030.log` and `read_om_series_validation_t030.log` retain the linked test results. The Python remote and validator suites passed 8 and 13 tests, respectively.

The controlled remote rerun used the same extension and matching official HTTPFS across HTTP, certificate-verified HTTPS and path-style S3. All 54 scenarios passed (18 per transport). Each transport's registered partial-override case returned 54 rows and matched its independent manual `read_om` row multiset. Across all scenarios, the sanitized audit recorded zero list, metadata (`meta.json`), unclassified or unplanned witness requests. The audit contains 1,251 events and has SHA-256 `ba830da8c6831983f10dcc9a3af4862caab56671720b8115a3cf03cc717533a7`; the run manifest is `controlled-remote-t030-registry/manifest.json` (SHA-256 `24334af46c7f15a9ef7385c4ed8e73d280843f99e7f5c02e0ba292ee7e00c602`). The remote oracle compares schema and complete row multisets in Python; it does not claim SQL `EXCEPT ALL` ran remotely.

SC-004 is **passed** for the frozen registry on the exercised v1.5.5 linux_arm64 source: provenance is reproducible for every initial entry, both defaults resolve to their explicit equivalents for all 11 models, local row equivalence covers every registered cadence, and the controlled origin audit observed no registry-discovery/list or metadata request. The S0 registry/provenance subgate passed. The broader S0 gate still awaits final-source binder/build evidence on the remaining pinned pairs in T039. No live producer data or real-object validation was performed here.

## US4 evidence: T031–T037

| Task | Status | Observed evidence |
|---|---|---|
| T031 | PASS | `test/sql/read_om_series_pushdown.test` passes 37 assertions in [`read_om_series_pushdown_t031_rerun1.log`](sql-logic-tests/read_om_series_pushdown_t031_rerun1.log). It covers combined spatial/time/value/residual filtering, source identity, projection and cardinality, per-child query/scan IDs, value-dependent versus value-free COUNT, thread settings 1/2/4, and `WHERE FALSE` bind records versus executed scans. |
| T032 | PASS WITH FIXTURE LIMITATION | Added three synthetic N160 children to `test/data/series/fixtures.json`; the generated set now contains 17 OM objects. The children share logical shape `[1,138346,4]` and vary physical chunk shapes `[1,2048,4]`, `[1,1024,2]`, and `[1,4096,1]`. All use `FPX_XOR2D`; an attempted PFOR fixture failed the official round-trip check at row 1272. No N160 producer object was found, so the fixtures remain synthetic only. |
| T033 | PASS | Registered SQL comparisons passed with 37 pushdown assertions and 21 base series assertions. The logical series plan retains ordinary `read_om` children and propagates filters/projections through the existing callbacks; no decoder or ordinary physical read path was changed for this story. |
| T034 | PASS | Local and remote matrices retained distinct per-child scan IDs and completed outcomes. The wrapper adds no series-level metrics record or new metrics JSON schema. The optimizer-eliminated `WHERE FALSE` case retains bind-time records with `scan_complete=false` and no scan-phase/value I/O. |
| T035 | PASS | The validator now builds the same N160 domain-versus-explicit query for local and remote URI prefixes, checks rows/counts, per-child value I/O, worker caps, query-owned memory bounds, and remote object audits. `python3 -m py_compile scripts/validate-series.py test/tools/validate_series_test.py` passed; all 16 `validate_series_test.py` checks passed. |
| T036 | PASS FOR THE EXERCISED SYNTHETIC CELL | Local run [`us4-local-official-rerun3/report.md`](us4-local-official-rerun3/report.md) records S1/S5/S6 passed and S2 blocked. Controlled run [`us4-controlled-remote-rerun2/report.md`](us4-controlled-remote-rerun2/report.md) records S3/S4/S5/S6 passed across HTTP, certificate-verified HTTPS and path-style S3; 21 scenarios passed per transport. All nine remote N160 transport/worker cases returned 3,200 rows against 3,200 manual rows and value-free COUNT 691,730 against 691,730. Two child scans completed per query with distinct scan IDs. Value-dependent scans recorded value index/data reads and decodes; every value-free count child recorded zero value index/data requests and zero decoded chunks. All nine N160 audits recorded zero list, metadata/unclassified and witness requests. |
| T037 | PASS FOR THE EXERCISED CELL | The official v1.5.5 native [`series_binding_test_t037.log`](us4-local-official-rerun3/series_binding_test_t037.log), [`series_error_test_t037.log`](us4-local-official-rerun3/series_error_test_t037.log), and [`reader_capacity_test_t037.log`](us4-local-official-rerun3/reader_capacity_test_t037.log) passed. They cover owned child binding, series bind/execution cancellation and recovery, and ordinary reader capacity-bound growth (the capacity test reported rectangular batch decoded chunks `35 -> 5`). The local and remote N160 matrices also observed per-child active workers within the configured 1/2/4 caps and query-owned peak memory at or below each recorded upper bound. |

The matching EXPLAIN output is [`n160-count-explain.txt`](us4-local-official-rerun3/n160-count-explain.txt), generated by [`n160-count-explain.sql`](us4-local-official-rerun3/queries/n160-count-explain.sql) with the v1.5.5 official CLI and the exact loadable extension. In the series plan, both `READ_OM` leaves project only `valid_time`; the series cardinality projection supplies constant `42` to `count_star()`. The identical manual `read_om` UNION ALL baseline projects `value` and `valid_time`. The result agrees with the metrics: series COUNT avoids value index/data I/O and decoding, while the value-returning query reads and decodes values. The plan and row comparisons are synthetic N160 evidence only.

A preliminary unregistered COUNT metrics probe expected zero new records and observed two bind-time child records. The failure log remains as [`series_count_metrics_probe.log`](series_count_metrics_probe.log); the leftover unregistered test-tree file was removed during T038 because its assertion duplicates the registered pushdown coverage. The registered pushdown test snapshots existing metrics and checks optimizer-eliminated bind records separately; its final 37-assertion run passed. An earlier pushdown log with the stale expectation is retained for history and is not the final result.

The latest local and remote runs used DuckDB v1.5.5 official CLI SHA-256 `9882c99a9804407de82c0edb1816d7667733d37d771a98eb23ad5f6a8d37acb1`, DuckOMO extension SHA-256 `cab083012dbfbf2c01295bc6814af3d97466ef10f1f586282946ae225fcbee15`, official HTTPFS SHA-256 `eba6e263e395a83966090f1f11ade63630b1b21422f0f2813858d179d42ea1e9`, and fixture manifest SHA-256 `7656a90f4e2b3b1bd8eb6faced3e06e4fc0339c52f9dd83ccde986987435e72c`. Both validator runs used `strace` to confirm that DuckDB opened the requested extension file. The controlled remote manifest is SHA-256 `24e869cb45c2ecc5aadc51a561c3ed851c8ad743f8b6ade2b473586bdcc120a6`; its sanitized request audit is SHA-256 `c24b410ac1b3981a7ba58b07f6a4f8641bfa16da7506aab31f3c28eab3c46979`.

## T038 documentation

| Task | Status | Observed result |
|---|---|---|
| T038 | COMPLETE | Updated `README.md` and `README.en.md` with the development-only directory-prefix query, registered partial override, `value AS temperature` naming, complete dimensions/spatial requirements, skip/strict and missing-versus-NULL behavior, ordering and exclusions. Updated `docs/spec.md` with the series signature and contract boundaries, current validation scope, and links to Feature 010 artifacts. Added the temporal planner, retained ordinary child binds, balanced UNION ALL, cardinality projection and per-child metrics to `docs/architecture.md`. Reworked `quickstart.md` to use current tools and evidence, added registered/custom override SQL examples, and recorded the incomplete gates. No release or Community configuration was changed. Removed the stale unregistered `test/sql/series_count_metrics_probe.test`; its observed failure log is preserved. Documentation content was reviewed against `contracts/sql.md` and the retained local/remote reports; no runtime check was run for T038. |

## Final source validation and requirement reconciliation (T039–T042)

The historical task sections above retain their intermediate observations. Final-source evidence uses extension source SHA-256 `4974560d43df2a1abcc67cb24755f57b9da36df37a92df3d0f2d34fae3aec516`; the four build IDs, DuckDB CLI/HTTPFS/DuckOMO artifact hashes, commands and exits are recorded in [`manifest.json`](manifest.json) under `final_validation`.

| Pair | Final-source outcomes |
|---|---|
| v1.5.4 | Build passed; 14/14 sanitizer checks passed; local regression workflow exited 0; local series S1/S5/S6 passed and S2 is blocked; controlled remote passed 63 scenarios; native official HTTPFS passed; real S7 passed. |
| v1.5.5 | Build passed; 14/14 sanitizer checks passed; local regression workflow exited 0; local series S1/S5/S6 passed and S2 is blocked; controlled remote passed 63 scenarios; native official HTTPFS passed; real S7 passed. |
| v1.5.6 | Build passed; 14/14 sanitizer checks passed; local regression workflow exited 0; local series S1/S5/S6 passed and S2 is blocked; controlled remote passed 63 scenarios; native official HTTPFS passed; real S7 passed. |
| v2.0.0 | Build passed; 14/14 sanitizer checks passed; local regression workflow exited 0 using the engine compatibility matrix; local series S1/S5/S6 passed and S2 is blocked; controlled remote passed 63 scenarios; native official HTTPFS passed; real S7 passed. |

The per-pair build logs are `t040/build-final-<pair>.log`; local regression logs are `t040/workflow-local-final-<pair>.log`; local series reports are [`t040/local-final-v1.5.4/report.md`](t040/local-final-v1.5.4/report.md) through `local-final-v2.0.0/report.md`; controlled reports are [`t040/controlled-final-v1.5.4/report.md`](t040/controlled-final-v1.5.4/report.md) through `controlled-final-v2.0.0/report.md`; native HTTPFS logs and isolated audit snapshots are `t040/official-httpfs-native-v1.5.4.*` through `official-httpfs-native-v2.0.0.*`; real reports are [`t040/real-v1.5.4/report.md`](t040/real-v1.5.4/report.md) through `real-v2.0.0/report.md`.

The standalone local series runner exits 2 because it correctly reports S2 blocked; its reports show S1, S5 and S6 passed on all four pairs. S2's zero-length no-access preflight passed, but the complete invalid-input, schema-mismatch, overflow and time-metadata-conflict corpus remains unrun. The controlled remote runner exits 0 for each pair and covers 21 scenarios on each of HTTP, certificate-verified HTTPS and path-style S3. The N160 fixtures remain synthetic and do not establish a matching producer N160 object.

The dedicated native `official_httpfs_test` passed for all four pairs using an isolated loopback fixture server. A broader default `scripts/validate.sh` attempt is also retained: its first run lacked `DUCKOMO_HTTP_BASE`, and its retry reached the separate broad remote validator but stopped before cases because `https_base`, `s3_base`, `s3_setup`, `server_log` and `https_ca_cert` were not supplied. The task-specific `--local-only` regressions and controlled remote tests passed; the pre-existing Feature 008 service and configuration were left unchanged.

The finalized quickstart was executed on v1.5.5 with the exact loadable extension confirmed by `strace`. The cross-boundary schema matched, both `EXCEPT ALL` differences were empty, and the output contained six rows at each of 03:00, 04:00 and 05:00. The registered-default and custom-layout examples each returned the same 18 rows. Evidence is in [`t040/quickstart-final-v1.5.5/cross-boundary.json`](t040/quickstart-final-v1.5.5/cross-boundary.json) and `t040/quickstart-final-v1.5.5/examples.json`.

All four final-source S7 runs passed. Each live HEAD matched the frozen identities for adjacent `ecmwf_ifs025/temperature_2m` chunks 1594 and 1595. The series result, a fresh manual `read_om` UNION ALL and the frozen oracle matched in schema and both boundary rows: `2026-10-08 21:00:00` → `25.950000762939453`, and `2026-10-09 00:00:00` → `25.600000381469727`. The multipart ETags are object identities, not content hashes; no OM object bytes were retained. This covers one domain point and one boundary window only.

### Acceptance gates

| Gate | Final status | Evidence and limit |
|---|---|---|
| S0 | **Passed** | All four final-source builds, binder/native checks, registry provenance checks and sanitizer targets passed; pair artifact identities are in `manifest.json`. |
| S1 | **Passed** | Seven local independent-oracle scenarios passed on each pair, including boundary, negative epoch, NULL and slotless window cases. |
| S2 | **Blocked** | The zero-length rejection had a zero-object-access trace. The complete invalid-option, overflow, schema-mismatch and time-metadata-conflict corpus is incomplete. |
| S3 | **Passed** | First/middle/last/all-missing skip and strict behavior passed across all three controlled transports on each pair. |
| S4 | **Passed** | Structured faults, corruption, post-bind changes, recovery, cancellation, redaction and connection isolation passed in controlled/native checks. |
| S5 | **Passed** | Local and remote regular/N160 cases matched independent manual UNION ALL references at worker caps 1/2/4; N160 remains synthetic. |
| S6 | **Passed** | Executed children retained distinct scan identities; value-free COUNT scans had zero value index/data requests and zero decoded chunks. |
| S7 | **Passed** | Existing local scanner/native/sanitizer regressions passed on all pairs, and final-source real-object S7 passed on all pairs. |

### Functional requirement reconciliation

| Requirement | Status | Observed result |
|---|---|---|
| FR-001 | **Passed** | Separate directory-prefix function exercised on local and controlled HTTP/HTTPS/S3 inputs; ordinary `read_om` remains the oracle. |
| FR-002 | **Partial** | Closed endpoint forms and exercised valid windows matched; the full pure-argument invalid-input matrix remains part of blocked S2. |
| FR-003 | **Passed** | All 11 exact model keys and provenance were checked; explicit overrides matched registered defaults without runtime metadata/list requests. |
| FR-004 | **Passed** | Chunk numbering, closed time filters and adjacent boundaries matched manual reads, including the real two-chunk boundary. |
| FR-005 | **Passed** | Automatic time axes and rejection of caller-supplied `axes` passed the registered validation tests. |
| FR-006 | **Passed** | Queries used complete dimensions and explicit grid/domain declarations; the model registry did not infer spatial configuration. |
| FR-007 | **Partial** | Valid homogeneous chunks and varying physical compression blocks passed; the full schema/time-metadata mismatch corpus remains incomplete. |
| FR-008 | **Passed** | Default skip and strict policy passed first/middle/last/all-missing cases across all controlled transports. |
| FR-009 | **Passed** | Other faults, post-bind disappearance/replacement, cancellation, redaction and recovery propagated as required in the exercised checks. |
| FR-010 | **Passed** | UNION ALL row multiplicity and NULL behavior matched independent full-row oracles. |
| FR-011 | **Passed** | Spatial/time/value/residual filtering, projection, worker limits and inherited per-child behavior matched manual reads. |
| FR-012 | **Passed** | Typed empty/all-missing and slotless schemas matched declared/present schemas in the exercised cases. |
| FR-013 | **Passed** | Per-child scan metrics remained distinct; missing attempts were not represented as successful scans. |
| FR-014 | **Partial** | Planning rejects the representative invalid length before object access and applies expansion checks; full overflow/count-bound evidence is part of blocked S2. |
| FR-015 | **Passed** | Unsupported selectors and invalid declarations were covered by the passing registered validation SQL tests. |
| FR-016 | **Passed** | SQL/native, controlled remote, registry, regression and sanitizer outcomes are retained with actual commands and artifacts; the separate broad remote validator limitation is recorded above. |
| FR-017 | **Passed** | Acceptance remained on `linux_arm64`; no 007/008/009 implementation or Community packaging expansion was made. |

### Success-criterion reconciliation

| Criterion | Final status | Evidence and limit |
|---|---|---|
| SC-001 | **Passed** | Single-, two- and three-chunk schemas and row multisets matched on all four pairs. |
| SC-002 | **Blocked** | Endpoint behavior passed for exercised inputs, but full pre-access invalid-input and metadata-dependent mismatch coverage is incomplete. |
| SC-003 | **Passed** | Tested missing policies and specified fault classes passed across HTTP, verified HTTPS and path-style S3. |
| SC-004 | **Passed** | Frozen provenance and all 11 registered-versus-explicit temporal defaults passed; audits observed no registry metadata/list discovery. |
| SC-005 | **Passed** | Filter/projection and value-free counts matched at 1/2/4 workers; every executed value-free child recorded zero value I/O and decode. |
| SC-006 | **Passed** | Child metrics have distinct identities and distinguish bind attempts from executed scans. |
| SC-007 | **Blocked** | Four-pair compatibility, cancellation and recovery checks passed, but S2 overflow/schema/time-metadata mismatch cases remain incomplete. |

### Dirty-tree and scope review

The baseline manifest covered 155 pre-existing files across 43 status entries. At final review, 146 file hashes were unchanged, none were missing, and the nine changed baseline files were expected Feature 010 edits to README text, the two shared reader seams, and Feature 010 specification artifacts. The 15 pre-existing Feature 008/community paths, both Feature 009 draft paths, and the untouched `src/om/reader.cpp` / `test/native/remote_async_test.cpp` hashes still match baseline. The intentionally shared `src/include/duckomo/om_reader.hpp` and `src/scan/read_om.cpp` changes are limited to the Feature 010 seams described above. No current dirty path falls outside the baseline set or the reviewed Feature 010 file scope. Detailed hashes and scope checks are in [`t040/preservation-review.json`](t040/preservation-review.json). No stash, reset, commit, push, amend or rebase was performed.


