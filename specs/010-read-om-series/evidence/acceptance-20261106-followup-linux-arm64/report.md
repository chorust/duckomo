# Feature 010 Current-Source Acceptance — Follow-up

**Run date:** 2026-11-06  
**Overall status:** **PASS for the declared Linux AArch64 four-pair matrix**  
**Platform:** Linux AArch64 (`linux_arm64`)  
**Git HEAD:** `80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9`  
**Extension source SHA-256:** `8f1f2624fc64951aa42e35625f90c7008ff18672c4e114000dfada4c7e6bef6e`  
**Temporal registry SHA-256:** `e3dc7e177cdf6c63b70a28933b8efa8769e81f7329d5f8f368173bb8d4ce4cf0`

This follow-up completes the gates that were blocked in the earlier [2026-11-06 acceptance report](../acceptance-20261106-linux-arm64/report.md). That report and the 2026-08-03 historical evidence remain unchanged. The corrected-source builds, run manifests, commands, SQL, traces, and logs are retained beneath `corrected-source/`; the consolidated identities and result map are in [manifest.json](manifest.json).

## Gate results

| Gate | Status | Evidence |
|---|---|---|
| S0 | **passed** | Four isolated pinned builds; registry provenance and regeneration; binder/native coverage; 14/14 sanitizer checks per pair. |
| S1 | **passed** | Seven local oracle comparisons per pair, including boundaries, negative time, NULL and slotless windows. |
| S2 | **passed** | 46/46 access-audited cases per pair: 38 pure preflight cases with no candidate access and 8 metadata mismatch cases opening only the expected candidate objects. |
| S3 | **passed** | First/middle/last/all-missing skip and strict cases, including `LIMIT 0` and `WHERE false`, across HTTP, verified HTTPS and S3 on every pair. |
| S4 | **passed** | Injected non-absence faults failed rather than being silently skipped. Under official HTTPFS, HTTP(S) 403/412 surfaced as unknown-status `[transport]` errors. The controlled `corrupt` case served HTTP 200 with `not-an-om-object` and failed at range-protocol validation; it did not exercise OM metadata parsing or decode failure. Post-bind mutation, recovery, cancellation, redaction and connection isolation passed. |
| S5 | **passed** | Local and remote N160 domain/explicit comparisons passed at worker caps 1/2/4. |
| S6 | **passed** | Executed child scans retained distinct existing metrics identities; value-free scans recorded zero value I/O and decodes. |
| S7 | **passed** | Full `scripts/validate.sh --local-only` exit 0 on all pairs; official HTTPFS native regression exit 0 on all pairs; real-object validation passed on all pairs. |

All 117 controlled-remote scenarios passed for each pair. This includes 33 SC-004 model/transport cells per pair (all eleven frozen models over HTTP, verified HTTPS and S3), with zero listing, metadata or witness requests in those audits. Each pair's overall controlled-remote manifest reports S3, S4, S5, S6 and SC-004 passed.

All seven success criteria pass for the recorded scope: SC-001, SC-002, SC-003, SC-004, SC-005, SC-006 and SC-007. Feature 010 acceptance is **PASS on Linux AArch64 for the four pinned pairs below**. This does not establish another architecture or broad real-producer data support.

## Pair identities

All builds use the same extension source digest and the frozen OM C and extension-ci-tools commits. CLI, HTTPFS and DuckOMO artifact identities are recorded here and in each pair's build and runtime manifests.

| Pair | DuckDB commit | Build ID | DuckOMO SHA-256 | CLI SHA-256 | HTTPFS SHA-256 |
|---|---|---|---|---|---|
| v1.5.4 | `08e34c447bae34eaee3723cac61f2878b6bdf787` | `19b0d53c9b42e73c00d210f4d2b99736566fad20b0ce582673d8552f325c804d` | `703df1cc7a7c493bcf8e2fc4e3c37480f482081f476808e3dbcd14d0142701b0` | `513b573b5ad3a94ad2c0668b1ccefb5f281b6f7aa2a5e2ff057825473848e69a` | `6d30a487968cbe5553b7272fd5bad0e4485c4117db96e21fd1e5e2a225a5a538` |
| v1.5.5 | `d8cdaa33fda8df955cc76ef58a280f68f4cd43fa` | `ccfa7772a2ed17bc4c9366d95fdc46ecb4ff5d1ce38835e371446983aae40021` | `2c981c7b1c372ec2f3f1e28212821c7359fcea53345fb427da41bfe027b78268` | `9882c99a9804407de82c0edb1816d7667733d37d771a98eb23ad5f6a8d37acb1` | `eba6e263e395a83966090f1f11ade63630b1b21422f0f2813858d179d42ea1e9` |
| v1.5.6 | `069cc9f9b5be802405797faecc284961b07c70ef` | `da4b62d463745dafef6d0c113d5235798c460be8e856776cc29fd1af782de54a` | `50ceb367f019195d03b5287d189e8db66bc8abdf12f7ce65612a0cb5c2509add` | `c0ac0b79e243ee312b3af29c34ac0b94e518aa44c4345036d5d96c1c042c9d48` | `ae940d800b7f938a4848d0eebcffb28dda0bbed76987f11dc55faa6d1a86db0d` |
| v2.0.0 | `d54cbbe3df17e8e5ce68312f483b4155836112aa` | `7202d65f2f8bf615349bbd87f64d2979c9ee65d6731b8dded234bdb448e91556` | `0af98d6150b38a88817a151d318da706c83a26f397e0067a87327c269005e5e0` | `f4c681bcf41a5c43dcf9a354703fe5bded9e2265282d1915d6d99637cd1e1f29` | `53f7aea1a0448ea62ef9b63f95da37c5766070084f14f14110e275e76382e7b2` |

The common OM C commit is `d8855e418e2231ae8439f0c7e840fa3f93b371e3`; the common extension-ci-tools commit is `b777c70d30942cca5bef62d6d4fa23a13362f398`. Each pair's isolated build ran 14 sanitizer checks with zero failures.

## SC-004 provenance and runtime equivalence

`test/data/series/models.json` contains eleven exact model keys with pinned producer-source formulas and archived metadata provenance. The official writer generated and round-tripped one object per registry entry. For each model on each transport, the registered call was compared with that same model using explicit equivalent `chunk_time_length` and `time_step`; schema, bounds, counts and two-way `EXCEPT ALL` matched. Origin audits recorded no prefix listing, `static/meta.json` request, or unrelated witness request.

Local S1/S2/SC-004/S5/S6 reports are under each authoritative `corrected-source/<pair>/local/` directory (v2.0.0: `corrected-source/v2.0.0/local-corrected/`), as recorded in `manifest.json`. The outer pair directories retain earlier attempts and are not the authoritative local results. Controlled results and sanitized request audits are under each pair's `controlled-remote/` directory.

## S7 and real producer result

The full `scripts/validate.sh --local-only` workflow exited 0 on all four pairs. It ran the SQLLogicTests, available native regressions and Python tool checks against the matching pair build. The separate official HTTPFS native diagnostic/cancellation/recovery/isolation test also exited 0 for every pair; its result files and sanitized loopback request audit are under `official-httpfs-fixture/` and each pair's `official-httpfs/` directory.

The real-object runner passed on all four pairs. It rechecked live HTTPS identities for the two adjacent public IFS025 `temperature_2m` chunks and compared `read_om_series` with both a manual `read_om` union and the frozen two-row boundary oracle. Both rows, schema, and full multiset matched. The source objects themselves were not retained. This real-data evidence covers only the two adjacent `temperature_2m` objects, one point and one boundary window; the other ten registered model entries have provenance plus synthetic runtime equivalence only, and N160 is synthetic.

## Intermediate attempts and final reruns

The first post-bind replacement assertion expected HTTP 412, but the `v1.5.4/controlled-remote` run was produced from pre-correction source digest `98b40a9b76f0a63be35165f85187c6f714287f45505fc2d5c5b716109d460cd3`, not the corrected-source digest `8f1f2624fc64951aa42e35625f90c7008ff18672c4e114000dfada4c7e6bef6e`. That validator expectation failed: the fixture returned HTTP 200 with a changed ETag, and the query reported `object identity changed`. The injected HTTP 412 fault remained a separate passing case. Between the two recorded build-manifest times, the only hashed source inputs with newer modification times were `scripts/validate-series.py` and `test/tools/validate_series_test.py`; the corrected validator checks the observed HTTP 200 replacement response, changed ETag and identity diagnostic, and its unit test covers the changed/unchanged ETag cases. The retained build manifests record aggregate source digests rather than per-file hashes, so they do not provide a line-by-line source diff.

The initial v1.5.4 S2 run at the outer `v1.5.4/local/` path also remains pre-correction evidence: 38/46 cases passed, while all 8 metadata-access cases failed the audit with `observed candidate-object access differs from the case contract`. Its fixture manifest came from `build/series-fixtures-sc004-20261106/`, while those SQL corpus cases resolve candidate paths under canonical `build/series-fixtures/`. The authoritative corrected-source rerun uses the canonical fixture root and passed all 46 cases. These retained failures were not deleted.

An initial v2.0.0 local S2 attempt used a fixture root different from the canonical paths in the SQL corpus. Its access trace therefore failed the fixture-identity check despite observing the expected path. The rerun used `build/series-fixtures`, the path named by the SQL corpus, and all 46 S2 cases passed. Both runs remain available for audit.

## Preservation and limits

The protected `run-20260803-linux-arm64/` evidence still contains 5,078 files totaling 1,181,103,584 bytes, matching its recorded inventory. Builds used isolated source/build roots under `build/series-acceptance-sc004-corrected-20261106/`; no shared DuckDB submodule checkout was switched. No stash, reset, commit or push was performed. The current tracked dirty-diff identity and source digest are recorded in `manifest.json`.

Feature 008 was not evaluated for acceptance and is **not PASS**. SC-006 remains relaxed / will not meet the original ≥20% target; T042/T043 remain unchecked. `read_om_series` is development-source-only and is not a released feature identity. This run makes no x86_64 or multi-architecture claim, no broader production-grid support claim, and no general claim for producer models, variables, locations or time windows beyond the evidence described above.
