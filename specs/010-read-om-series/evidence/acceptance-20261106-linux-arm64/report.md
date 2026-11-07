# Feature 010 Current-Source Acceptance — Linux AArch64

**Run date:** 2026-11-06  
**Overall status:** **blocked; Feature 010 is not PASS** (SC-004, S3, S4, and full S7 remain incomplete).

**Source snapshot:** `e9194d1a0e0c16341e8bb1e6e8a953ee6e01c64ee39f2a0835b22d69e7f4c7b0`  
**Platform:** Linux AArch64 (`linux_arm64`)

This run replaces the earlier incomplete local S2 result for the current source snapshot. It does not upgrade the older 2026-08-03 evidence to current-source evidence. All four pair builds below use the same extension source digest and their pinned DuckDB commits and official CLI/HTTPFS artifacts.

## Current gate status

| Gate | Status | Observed result |
|---|---|---|
| S0 | **passed** | Four source-bound builds, exact requested extension loads, per-pair native binder checks, registry provenance/regeneration checks, and the recorded `ecmwf_ifs` registered-vs-explicit temporal equivalence case passed. |
| S1 | **passed** | Seven local schema and full-multiset scenarios passed on each pair. |
| S2 | **passed** | 46/46 local cases passed on each pair: 38 pure preflight cases with no candidate access and 8 metadata mismatch cases with the expected present candidate access. |
| S3 | **blocked** | Current-source controlled HTTP, verified HTTPS, and S3 missing/strict matrix was not run. |
| S4 | **blocked** | Current-source controlled transport fault and origin-audit matrix was not run. |
| S5 | **passed** | Synthetic N160 domain-vs-explicit results passed at worker caps 1/2/4 on each pair. |
| S6 | **passed** | Per-child scan identity and value-dependent/value-free I/O checks passed on each pair. |
| S7 | **blocked** | The current-source full existing-scanner regression workflow and real-producer acceptance run were not completed. The focused series cases below do not close this gate. |

The recorded success criteria are **SC-001 passed**, **SC-002 passed for the recorded acceptance matrix**, **SC-003 blocked**, **SC-004 blocked** (the explicit-equivalence query covered `ecmwf_ifs`, not every registered model), **SC-005 passed**, **SC-006 passed for Feature 010's per-child metrics contract**, and **SC-007 passed**. SC-007's current-source boundary, overflow, schema, cancellation, replacement/removal, and recovery checks ran on all four pinned pairs. These criterion results do not close S3, S4, or S7.

## Pair identities and results

All pairs used OM C commit `d8855e418e2231ae8439f0c7e840fa3f93b371e3` and extension-ci-tools commit `b777c70d30942cca5bef62d6d4fa23a13362f398`.

| Pair | DuckDB commit | Build ID | DuckOMO SHA-256 | Official CLI SHA-256 | Official HTTPFS SHA-256 | Local gates | Focused native suite |
|---|---|---|---|---|---|---|---|
| v1.5.4 | `08e34c447bae34eaee3723cac61f2878b6bdf787` | `b5202780d0553decc2ba30ce3873d73f056e8a71dbfa68d5fa8ed1f7f870ffb9` | `4281c1ad813caf9210b7890b749fb571bb42a6a07d5a5fa061f24f8590fba71d` | `513b573b5ad3a94ad2c0668b1ccefb5f281b6f7aa2a5e2ff057825473848e69a` | `6d30a487968cbe5553b7272fd5bad0e4485c4117db96e21fd1e5e2a225a5a538` | S1/S2/S5/S6 passed | 4/4 passed |
| v1.5.5 | `d8cdaa33fda8df955cc76ef58a280f68f4cd43fa` | `4bedfc80a9454f5ea60cbaf9ce3b07c04f4857003d4883cd530aa4eae5eb8acf` | `0e281b116cbeb0a34fcf40c1b9ef6591a7ecc89943b8af3d94203630d5457110` | `9882c99a9804407de82c0edb1816d7667733d37d771a98eb23ad5f6a8d37acb1` | `eba6e263e395a83966090f1f11ade63630b1b21422f0f2813858d179d42ea1e9` | S1/S2/S5/S6 passed | 4/4 passed |
| v1.5.6 | `069cc9f9b5be802405797faecc284961b07c70ef` | `d178c67316b6541d69520c8958cd44ea799089dac0b1cd1d0a0a3604b55e0ac7` | `343fb3eb3334edd9b5ab7928f4eb5036bc2e458aa8c25a93ba2171b7830d182e` | `c0ac0b79e243ee312b3af29c34ac0b94e518aa44c4345036d5d96c1c042c9d48` | `ae940d800b7f938a4848d0eebcffb28dda0bbed76987f11dc55faa6d1a86db0d` | S1/S2/S5/S6 passed | 4/4 passed |
| v2.0.0 | `d54cbbe3df17e8e5ce68312f483b4155836112aa` | `5cc4316e6939d4ddcfbb3630e50bea2541f47cbe5338527cd260c86bb0d58639` | `5b70255bb9c81964cf74e43e043900380f57e08f3e999d107ec2c13c2ce6d0ed` | `f4c681bcf41a5c43dcf9a354703fe5bded9e2265282d1915d6d99637cd1e1f29` | `53f7aea1a0448ea62ef9b63f95da37c5766070084f14f14110e275e76382e7b2` | S1/S2/S5/S6 passed | 4/4 passed |

Per-pair S1/S2/S5/S6 reports, commands, SQL, and access traces are under each pair's `final-local/` directory. The S2 SQL source SHA-256 was `c88be0cc9982cb1df11bd7da3d10a02b3ae4f6e87fb535b2be5b48c15f153fee`; the generated fixture manifest SHA-256 was `42fbc86a033736039d0cae8a540b9b21da1a6f29e573760afb9c84e4e096a8ab`.

## Test evidence

- Each pair's `final-local/` report records seven S1 oracle comparisons, all 46 S2 diagnostics and traces, and S5/S6 worker matrices. Every S2 preflight trace records no candidate object access; each metadata case records access to its expected candidate object(s).
- Each pair's native suite ran `series_plan_test`, `series_binding_test`, `series_error_test`, and `series_schema_test`. The error/lifecycle test covers bind and execution cancellation, post-bind removal/replacement, session isolation, and recovery. Logs are in the pair's `final-native/` directory; v1.5.4 uses `final-current-source-native/` to distinguish it from an earlier superseded build.
- Registered `ecmwf_ifs` temporal defaults matched explicit `chunk_time_length := 4, time_step := INTERVAL '1 hour'` with empty two-way EXCEPT ALL results on all four pairs. This is one model's equivalence case; the all-entry runtime equivalence criterion remains blocked. Results and exact SQL are in each pair's `registry-equivalence/` directory. The registry regeneration and provenance test passed; see `registry-check.log`.
- `test/sql/read_om_series_validation.test` passed all 69 assertions on the local release build. The native `reader_capacity_test` passed, including its injected interruption and retained-buffer bounds. `make release` completed successfully. These local release results are recorded in the [build and capacity fix note](../review-fix-20261106-linux-arm64/fix-note.md) and its logs under `evidence/review-fix-20261106-linux-arm64/`; they are not four-pair regressions.

## Remaining blocked scope

The current-source run did not execute controlled HTTP/HTTPS/S3 missing and fault matrices, the registered-vs-explicit SQL case for every model entry, or the real producer-object S7 acceptance run. It also did not run the complete `scripts/validate.sh --local-only` regression workflow across each pinned pair. Those gates remain blocked and are not inferred from local S1/S2/S5/S6 or focused native results.

Feature 008 was not evaluated for completion. Its SC-006 throughput gate remains **relaxed / won’t meet the original ≥20% target**, and Feature 008 T042/T043 remain unchecked. The reader-capacity test and release build are limited compatibility evidence; they do not establish Feature 008 PASS.
