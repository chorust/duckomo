# 004 implementation evidence summary

**Recorded:** 2026-10-08

**Status:** implementation in progress; no H0–H9 gate is fully verified.

**Scope:** evidence inventory and current support boundaries. This report does not promote implementation checkboxes, synthetic references, or partial sample checks into acceptance results.

The four-family real-input inventory task T003 is complete: the public bucket contains real OM v3 examples for rotated, stereographic, Lambert, and ECMWF IFS HRES reduced-Gaussian O1280. N160, full N320, and N320-region samples remain explicitly `not-run`; this task status does not pass their coordinate/value or remote gates.

## Gate status

| Gate | Status | Evidence and remaining condition |
|---|---|---|
| H0 — frozen inputs and regeneration | Partial | Frozen definitions/source and canonical vectors are present; prior local records report deterministic registry regeneration. Independent coordinate and official value references now cover three projected samples, but Gaussian producer inputs/point-order references and the full H0 gate remain incomplete. See [US1 record](baseline-local/us1.md) and the [reference import record](reference-import-pending.md). |
| H1 — coordinates, values, and layouts | Partial; full gate not-run | On matched baseline build `5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d`, the available rotated, Lambert, and stereographic samples passed full coordinate and official Float32/NaN value comparisons: 2,843,101 coordinate points within `1e-4°` (maximum `3.0517578125e-5°`) and 329,401,680 values exact by original `logical_index`. A separate source-only subcheck on the same build and sample-manifest hash streamed every spatial position for these samples at time-axis index 0, matched explicit/domain source rows, and recorded zero source/info value reads; its synthetic `valid_time` label and expected `[ny,nx,ntime]` profile do not independently prove OM axis-to-producer mapping or complete H1. Gaussian N160/N320/N320-region samples and references are missing. See [matched baseline H1 evidence](baseline-local/h1-20261008-5dac993/), the [all-position public source subcheck](baseline-local/us5-20261008-all-public-source-positions-5dac993-h3-local/h3-local/manifest.json), the [pinned Float32 refresh record](development-local/pinned-f32-coordinate-refresh-20261008.md), and [reference import record](reference-import-pending.md). |
| H2 — selection correctness | Not run as a complete gate | Selection implementation and local progress checks are recorded, but the complete frozen-input multiset and geometry suite is not available. See [US2 progress](baseline-local/us2-progress.md). |
| H3 — zero-value reads | Partial | Synthetic local coordinate/count checks and source/info zero-value-read checks have evidence; a matched-baseline source-only run also checks every spatial point in three projected samples at time-axis index 0. Full H3 remains not-run because required real-domain coverage is incomplete. See [US4 record](baseline-local/us4.md), the [all-position public source subcheck](baseline-local/us5-20261008-all-public-source-positions-5dac993-h3-local/h3-local/manifest.json), and the latest [US5 local record](baseline-local/us5.md). |
| H4 — parallelism and lifecycle | Not run as a complete gate | Bounded window scheduling is implemented; complete multi-worker, cancellation, failure, LIMIT, and recovery evidence is outstanding. See [US2 progress](baseline-local/us2-progress.md). |
| H5 — memory and cancellation | Partial local subchecks; complete gate not run | Four-grid synthetic ≥10× work-peak, reader-capacity, and preparation-cancellation checks passed. Six synthetic loopback Gaussian scans kept query-owned and transport-control peaks within bounds across 3,462 attempts; the cancelled query released tracked accounts at QueryEnd. The failed-replacement test verifies the old reader buffer is released before an impossible replacement allocation fails. Complete controlled remote H5 evidence remains open. See [US3 progress](baseline-local/us3-progress.md), [latest H5 local record](baseline-local/h5-local-refresh-20261008-t054-closed/), and [H6 loopback stress record](baseline-local/h6-loopback-protocol-final-20261008/h6-local/attempt-stress.json). |
| H6 — cross-source behavior and remote benefit | Partial local/loopback subchecks; complete gate not run | The 2026-10-08 synthetic interleaved local/loopback HTTP comparison matched 64 result rows including original source positions; v4 response body and independent loopback accounting both recorded 3,270 bytes, and both source metrics passed the QueryEnd release check. Six Gaussian-fixture stress scans covered 3,462 attempts with query-owned and transport-control peaks within their bounds. Weak-version reuse, equal-length strong-version replacement, short-body same-URI recovery, and no un-ranged GET fallback subchecks also passed. Controlled HTTPS/S3, signed authorization, required producer-backed Gaussian offsets, 003 prerequisites, and complete cold full/local comparisons remain open. See [2026-10-08 H6 loopback evidence](baseline-local/h6-loopback-protocol-final-20261008/) and [US3 progress](baseline-local/us3-progress.md). |
| H7 — spatial inputs | Partial local checks | Four synthetic grid types pass full source-position reconstruction and point/polygon relation multiset checks. Three hash-fixed projected producer samples pass explicit/domain identity, source/info zero-value-read checks, and a pinned Float32 coordinate comparison for the first four natural source positions (max errors: `1.52587890625e-5°`, `1.52587890625e-5°`, `5.841255187988281e-6°`). Complete OM axis-to-point mapping, full producer-backed relation checks, required Gaussian inputs, and public remote identity remain open. See [US5 local record](baseline-local/us5.md) and [2026-10-08 H7 evidence](baseline-local/us5-20261008-bounded-source-prefix/h7-local/manifest.json). |
| H8 — version matrix | Not run as a complete gate | Both isolated build manifests and matching artifact hashes exist, and targeted ABI/compatibility checks passed. The H8 identity/evidence summarizer now verifies both pair records and reports the missing complete same-input H0–H7 runs. HTTPFS protocol/cost gates also remain outstanding; matrix entries stay `build-verified`. See [version-matrix build evidence](version-matrix/builds.md), `test/data/grids/version-matrix.json`, and the latest [baseline](version-matrix/h8-20261007-v2-baseline/) and [prerelease](version-matrix/h8-20261007-v2-prerelease/) H8 records. |
| H9 — independent reproduction | Not run | No independent reviewer record exists. The implementer cannot substitute for that reviewer. |

## Domain and source coverage

| Domain or sample | Current evidence level | Boundary |
|---|---|---|
| `gem_rdps_10km` rotated grid | `coordinate-value-validated` | Matched baseline H1 coordinate/value comparison passed over the full projected sample using the expected producer axis profile; per the 2026-10-10 pinned-producer-reference decision the pinned Open-Meteo Swift source is the accepted coordinate/point-order reference (consistency with the published object, not native-GRIB equivalence). Remote-benefit and H9 checks remain open. |
| `gem_regional` stereographic grid | `coordinate-value-validated` | Same matched-baseline H1 comparison and the same pinned-producer-reference decision as `gem_rdps_10km`; remote-benefit and H9 checks remain open. |
| `aladin_central_europe_2km` Lambert domain | `coordinate-value-validated` | Same matched-baseline H1 comparison and the same pinned-producer-reference decision as `gem_rdps_10km`; remote-benefit and H9 checks remain open. |
| `lambert_formula_vector` | `definition-recorded` | Formula identity example only; it remains separate from the CHMI producer domain. |
| `n160` | `definition-recorded` | No matching real N160 OM v3 sample or independent source position/value reference acquired. |
| `n320` | `definition-recorded` | No full-grid N320 producer object/reference fixed. |
| `n320_ecmwf_aifs_europe_ensemble` | `definition-recorded` | 14,747-point mapping is source-derived; no real OM v3 regional object or independent producer point-order reference. |
| `ecmwf_ifs` O1280 (HSURF + chunk_817) | `coordinate-value-validated` per the 2026-10-10 pinned-producer-reference decision | 2026-10-10 local real-object checks: all 6,599,680 HSURF values match the official OM C decode, coordinates match an independent Python port of pinned producer arithmetic with maximum error 0, explicit/domain equivalence, spatial selection and 22×504 time values/NULL pass; see [o1280-real-local-20261010](baseline-local/o1280-real-local-20261010/final.md). Validates consistency with the published Open-Meteo objects, not ECMWF native GRIB latitudes; remote benefit and H9 remain open. O1280 does not close N160/N320/N-region gaps. |
| ECMWF HRES `temperature_2m/chunk_817.om` on O1280 | Real OM v3 time-series object; metadata checked, shape `[1,6599680,504]` | No coordinate/row mapping or value comparison; O1280 does not substitute for N160/N320/N-region. |

Detailed object hashes, shapes, chunks, acquisition commands, and S3 version limitations are in [sample acquisition](baseline-local/open-meteo-s3-sample-acquisition.md). Projected-grid coordinate/value reference hashes and commands are in the [reference import record](reference-import-pending.md); O1280 comparison details remain in [the supplemental comparison](baseline-local/o1280-supplemental-value-check.md).

## Requirement and success-criterion disposition

| Requirements | Current disposition |
|---|---|
| FR-001–007 | Implemented in part and covered by synthetic/local checks; real-family coordinate/value/layout acceptance is incomplete. |
| FR-008–014 | Selection, residual-filter, zero-I/O, bounded-window, and metrics paths are implemented in part; complete H2–H5 gates remain outstanding. |
| FR-015–019 | Not accepted. Same-content HTTP(S)/S3 behavior, strict remote cost reduction, server-side accounting, and complete 003 prerequisites remain outstanding. |
| FR-020–023 | Source/info interfaces and declared capability reporting are implemented; synthetic full-relation checks and partial projected-sample source/info checks exist, but producer-backed H7 and cross-URI acceptance remain incomplete. |
| FR-024–026 | Fixed 1.5.4 and DuckDB 2.0 prerelease builds plus targeted ABI/SQL compatibility checks are recorded at `build-verified`; complete matrix validation is absent. Formal 2.0 support remains a separate pending target. |
| FR-027 | Not met: required real Gaussian N160, N320, and N-region inputs/references are missing; required real coordinate/value gates are incomplete. |
| FR-028 | Registry inputs/generator and provenance levels exist; deterministic regeneration has prior local evidence, while complete source coverage remains incomplete. |
| FR-029 | Metrics v4 and query-end accounting are implemented; complete cross-gate evidence and cost audits are outstanding. |
| FR-030 | Documentation and quickstart are present, but complete frozen inputs, H0–H8 evidence, and independent H9 reproduction are absent. |

| Success criterion | Status |
|---|---|
| SC-001 | Partial; required real coordinate/value and N-grid coverage missing. |
| SC-002 | Partial; implementation/local examples exist, complete differential gate not run. |
| SC-003 | Not run; no per-family remote cold full/local service-audited savings evidence. |
| SC-004 | Partial; synthetic coordinate/count/source/info zero-I/O subcases recorded, complete real-domain H3 not run. |
| SC-005 | Not run as a complete criterion; full 1/2/4-worker lifecycle suite outstanding. |
| SC-006 | Partial; complete synthetic source reconstruction and relation checks pass, while all-family producer-backed reconstruction and relation checks remain outstanding. |
| SC-007 | Partial synthetic work-peak checks; complete real-input and remote memory/cancellation evidence remains outstanding. |
| SC-008 | Partial; registry regeneration evidence exists, but real per-family input/coverage evidence is incomplete. |
| SC-009 | Partial; paired matrix builds and targeted compatibility checks passed, while same-input H0–H8 runs and formal 2.0 remain pending. |
| SC-010 | Partial; synthetic rejection/failure checks exist, but full source, remote, permission, version, and cancellation suite is incomplete. |
| SC-011 | Not run; no independent reproduction record. |

## Version and dependency boundaries

- Baseline DuckDB v1.5.4 has an isolated matrix build manifest, matching artifact hashes, and passing targeted ABI/SQL checks; complete H0–H8 validation is absent.
- DuckDB `prerelease-2.0-dev` has a completed paired build and passing targeted ABI/SQL checks, recorded only as `build-verified`; it is not a validated grid or remote support claim.
- Formal DuckDB 2.0 release validation is pending and does not inherit prerelease status.
- Required real N-grid acquisition and the unfinished 003 remote gates remain dependencies for the corresponding H1/H6 claims.

## Earlier implementation continuation

That implementation continuation ran no build or test command. `test/data/grids/spatial-relations.json` was regenerated from `test/tools/grid_spatial_reference.py`; `test/native/range_cache_test.cpp` gained a regression case for invalidating stale authorization partitions while preserving the current partition and unrelated objects; `test/native/remote_session_test.cpp` gained 404 and non-identity encoding fault cases; and `test/sql/multi_grid.test` gained rejection cases for unknown domains and formula identity vectors used as producer domains. `test/tools/duckomo_grid_validation.cpp` now derives the run-level status and exit code from per-gate pass/fail/not-run results, fails closed on unknown statuses, and contains status aggregation self-check cases. `bash -n` passed separately for `scripts/validate.sh`, `scripts/build-version.sh`, and `scripts/stage-httpfs.sh`; Python AST parsing passed for the reference generator; and `git diff --check` passed.

No domain or gate is marked verified by this report. Remaining implementation and external-evidence work stays open in `tasks.md`.

## 2026-10-07 H6 input contract correction

`duckomo_grid_validation --server-log` now accepts the log directory emitted by `scripts/setup-remote-fixtures.py` and requires its `http.jsonl` and `s3.jsonl` audit files. The matching `duckomo_grid_validation` target rebuilt in `build/release-vcpkg`, and its `--self-check` exited 0. This fixes one H6 prerequisite check only; the H6 cross-source scan runner, T054 remote-attempt checks, and controlled-service execution remain incomplete. No remote service was started and no gate/support status changed.

`remote_session_test` now also compares local and loopback-HTTP results for the interleaved synthetic grid after a two-window longitude predicate and a `valid_time` intersection. The compared rows include grid/layout IDs, original logical/point/axis positions, coordinates, and both values; local and HTTP object IDs are checked independently. The `remote_session_test` target rebuilt and exited 0. This is a synthetic HTTP subcheck only; it does not validate producer axis order, HTTPS/S3, Gaussian region offsets, or H6 cost reduction.

The same native test now performs six full cold HTTP scans over a multi-chunk synthetic Gaussian object, totaling at least 256 instrumented transport attempts. Each v4 response-body count reconciles with loopback bytes; each query-owned and transport-control peak stays under its recorded bound, and the transport-control high-water mark remains stable across completed scans. The target rebuilt and passed. Reader replacement-buffer accounting and memory release after observer/handle cancellation remain unverified, so this does not pass H5.

Commands and exits for this continuation: `cmake --build build/release-vcpkg --target duckomo_grid_validation -j2` (0), `build/release-vcpkg/test/tools/duckomo_grid_validation --self-check` (0), `cmake --build build/release-vcpkg --target remote_session_test -j2` (0), `build/release-vcpkg/test/native/remote_session_test` (0), and targeted `git diff --check` (0).

## 2026-10-07 continuation

The public-bucket record confirms that ECMWF HRES is available as native OM v3 on an O1280 reduced Gaussian grid, including the `temperature_2m/chunk_817.om` forecast object. This remains supplemental Gaussian-family evidence: it does not replace the independently specified N160, N320, or N320 regional samples.

`test/native/remote_session_test.cpp` now exercises a change to an HTTPFS security setting across same-connection reads and requires fresh HEAD/one-byte authorization requests plus a new range-cache partition. `test/native/httpfs_abi_test.cpp` checks fail-closed descriptor mismatches for the baseline ABI2 pair and the ABI3 matrix identity, and its capability assertions now use the ABI-appropriate schema. `test/CMakeLists.txt` enables those ABI3 assertions in matrix builds. T043 is marked implemented; execution remains part of T055/T093. The H3 runner now schedules the existing source-identity native/SQL checks and `om_grid_info` SQL checks with exact argv, exit codes, input hashes, metrics hash, and output hashes; these are explicitly labeled synthetic/local evidence, so T070 and full H3 remain open.

Static whitespace checks passed for the then-edited files and `git diff --check` passed. No tests or builds were run in that earlier continuation. At that point T003/T004 and related sample-backed gates were open; subsequent entries below update T003's inventory status while keeping sample-backed gate status distinct.

## Additional continuation on 2026-10-07

`DUCKOMO_EVIDENCE_DIR=build/evidence-validate-20261007-continue8 scripts/validate.sh build/release-vcpkg` completed with exit code 0. It covered SQL/native regression, 15 ASan/UBSan checks, 47 fixture/reference hash checks, fixture regeneration, and the projection, dimensions, selection, and parallel harnesses. The output is in `build/evidence-validate-20261007-continue8/`.

US2 local synthetic subchecks now have a consolidated record in [baseline-local/us2.md](baseline-local/us2.md); T042 is complete. The complete H2/H4/H5 gates remain `not-run`. Registered-versus-explicit identity/layout checks now pass for the real rotated, stereographic, and CHMI Lambert samples, plus a synthetic N160 identity-only fixture. T017 is complete as SQL binding coverage; the fixture does not establish a real Gaussian producer mapping. T039 remains open because no acquired real sample has an independently validated source-position mapping suitable for a full/local spatial profile; the O1280 HRES row/point order remains unmapped.

T003's sample inventory is now marked complete based on the four real OM v3 grid-family examples, including the public ECMWF IFS HRES O1280 object. The manifest explicitly keeps N160, full N320, and N320-region coverage `not-run`; it does not assign the O1280 object an N-grid identity or claim spatial-read performance.

No H0–H9 gate or producer support level is promoted by these local results. At that point T004, the 003 remote prerequisites, signed-S3/server audit, paired matrix builds, and independent reproduction remained outstanding; the version-matrix continuation below records the later build-verified result only.

## Makefile verification on 2026-10-07

`make test DUCKOMO_HTTPFS_STAGE=/home/blizhan/repo/github/duckomo/build/httpfs-stage-make-test-20261007` completed with exit code 0. This used the baseline DuckDB v1.5.4 build (`duckdb --version`: `v1.5.4 (Variegata) 08e34c44`; DuckDB source commit `08e34c447bae34eaee3723cac61f2878b6bdf787`; extension-ci-tools commit `b777c70d30942cca5bef62d6d4fa23a13362f398`), CMake toolchain `/tmp/duckomo-vcpkg/scripts/buildsystems/vcpkg.cmake`, `arm64-linux` triplet, and vcpkg curl/OpenSSL libraries. The isolated HTTPFS stage records manifest SHA-256 `95304c7dd06f80b69d4d3c0297dfcd34a621fec0573b5a68c67d5dc19dbed316` and source-tree SHA-256 `7e44a1bcb2acdec4636b120b18c0d8fd98a9363ce354c4a11937d53e2e3e143c` in `build/httpfs-stage-make-test-20261007/stage.json`.

The run completed the SQLLogicTest suite (887 assertions across 16 cases), native regression checks, and all 15 sanitizer checks. Fixture regeneration and 47 fixture/reference/negative-asset SHA-256 validations passed. The projection harness passed all four scenarios. The dimensions/selection/parallel harness passed its available G0/G1, G2 full/restricted/empty/coordinate/count, and 1/2/4-thread result-consistency checks; full/local data bytes were 1,042,082 and 4,680, and decoded chunks were 4,264 and 8. Its measured two-worker median was slower than one worker, so no speedup claim is made. Machine-readable results are in `build/evidence-make-test-20261007/`.

The active build identity was CLI v1.5.4; SHA-256 values were DuckDB `8ab1aa11abd5441ef4b30be13717d6db3172ef6f018821de15504c1280b22103`, DuckOMO `a41636dfd2ad9138e49c5d7e9aa9871e018b71b12ab43b81754307e27ed40dec`, and HTTPFS `599684a30fc0c80fe366c52d84ad93a5c6aad687d2bc77ed2c6460c0218a8465`. The preceding `DUCKOMO_EVIDENCE_DIR=build/evidence-validate-20261007-continue8 scripts/validate.sh build/release-vcpkg` run also exited 0. Those local runs did not run the real-domain gate without `DUCKOMO_DOMAIN_FILE`, remote G3–G6 without provisioned S3, H0–H9, or independent reproduction. The later version-matrix continuation below adds isolated build-verified records, without completing the full gates or real-family claims.

## Remote protocol test additions on 2026-10-07

The loopback `httpfs_range_test` now also rejects forbidden/not-found/missing HEAD responses, 403/404 ranged GETs, non-identity encoding, redirects, and existing 200/short-body/version/412 cases. The short-body assertion reconciles all observed retry body bytes with the loopback server's sent-byte count. `build/release/test/native/httpfs_range_test` exited 0 after rebuilding; `duckomo_remote_validation --help`, the remote-fixture script help, Python AST parsing, and `git diff --check` passed. The remote fixture server can inject redirect and non-identity responses and the remote validation case list includes both. These changes validate the test implementation locally; service-backed S3 execution remains open under T055.

The remote harness now also runs signed-S3 secret, region, and endpoint switches within one query session: each changed authorization context must fail without cache hits, the original setup is restored, and the recovered result is compared with a local reference. The private setup SQL is not copied to evidence; an unreachable endpoint checks transport fail-closed behavior, while wrong secret/region require an HTTP 403. `httpfs_range_test` and `duckomo_remote_validation` both rebuilt successfully, and the loopback test exited 0. T044's protocol and cache scenario implementation is complete; full signed-S3 execution and service-side reconciliation remain T055 and were not run without provisioned service credentials.

## Sample and Lambert-domain continuation on 2026-10-07

T003 is checked as a completed four-family real OM v3 inventory. The Gaussian-family object is ECMWF IFS HRES O1280; the manifest preserves N160, full N320, and N320-region as `not-run`, so their H0/H1/H6 gates remain incomplete. T004 remains open for the independent coordinate, required full-value, and producer point-order references.

The CHMI sample is now associated with a distinct pinned production registry definition, `aladin_central_europe_2km`, derived from `Sources/App/Chmi/ChmiDomain.swift`. It is separate from `lambert_formula_vector`, which remains a non-bindable math vector. The registry check, generated sample-manifest comparison, fixture registry test, grid-identity native test, and `multi_grid.test` passed after this update. `multi_grid.test` reported 147 assertions.

The evidence query [aladin-domain-equivalence.sql](baseline-local/aladin-domain-equivalence.sql) returned `true,true` for registered-domain versus explicit version=1 `grid_id` and `layout` identity against the fixed CHMI sample. At this point it established one Lambert producer-domain equivalence example. Later checks added the projected samples and synthetic N160 SQL identity coverage; coordinate/value references, cold skip-block performance, and remote benefits remain `not-run`.

## Real GEM domain identity continuation on 2026-10-07

[`projected-domain-equivalence.sql`](baseline-local/projected-domain-equivalence.sql) returned `true,true` for both rotated GEM RDPS 10 km and GEM regional stereographic OM v3 samples. Combined with the CHMI Lambert result above, registered-versus-explicit grid and layout identities match for all three frozen projected producer samples. The later synthetic N160 SQL test completes binding identity coverage without relabeling the public ECMWF IFS HRES O1280 data as N160/N320; O1280 row lengths/point order remain unmapped. These checks validate binding identity and schema layout only, not coordinates, values, or spatial read benefit.


## N160 binding check on 2026-10-07

The synthetic N160 registered-domain versus explicit version=1 SQL identity/layout check passed as part of `build/release/test/unittest test/sql/multi_grid.test` (153 assertions, exit 0). `python3 test/tools/grid_registry_test.py` exited 0 after adding consistency checks against the frozen N160 rows. This completes T017's binding test coverage; it does not close T004/T028 or upgrade Gaussian support because the fixture is not an N160 producer object and has no independent coordinate or value oracle.

## Independent projected-grid references on 2026-10-07

`scripts/generate-grid-coordinate-reference.py` generated standalone double-precision spatial coordinates for the frozen rotated, CHMI Lambert, and stereographic definitions. The fixed official OM C API tool exported every root-array value for the corresponding three real objects. The references were revalidated against the current sample manifest; exact commands, source/object/tool hashes, output hashes, row counts, null counts, and tolerance limits are recorded in [the revalidation record](reference-revalidated-20261007.md) and the active manifests `test/data/grids/coordinate-reference.json` and `test/data/grids/value-reference-manifest.json`. The original import record is preserved in the pre-revalidation archive.

These artifacts are reference inputs, not a DuckOMO comparison or source-axis proof. OM array metadata does not embed axis names, so the pinned producer `[ny,nx,ntime]` profile remains an expected mapping. `region-reference.json` still has no producer point-order references; N160, full N320, and N320-region samples remain missing. T004, H0/H1, remote benefit, and all support levels remain open. No test or build command was run in this continuation.

## Version-matrix continuation on 2026-10-07

T077 and T079 are complete for the two frozen pairs. The isolated DuckDB 1.5.4 and DuckDB 2.0 prerelease builds report their pinned full source IDs, build IDs, and three artifact hashes in [the version-matrix build evidence](version-matrix/builds.md) and `test/data/grids/version-matrix.json`. Matching HTTPFS/DuckOMO loadable extensions reported ABI3 descriptors. `httpfs_abi_test`, `grid_version_compat_test`, and `grid_version_compat.test` passed for each pair. Matrix resolution after writing build results returned the same build IDs.

These results establish `build-verified` only. H8's paired identity subcheck now passes, but H8 remains `not-run` because neither pair has a complete H0–H7 run against the same frozen input set; full remote protocol, cost, authorization, cancellation, cache, and memory gates remain open. Formal DuckDB 2.0, the remaining N-grid references, 003 remote prerequisites, and independent H9 review remain pending.

## Formal DuckDB 2.0 availability check on 2026-10-07

The official DuckDB GitHub `releases/latest` endpoint returned stable tag `v1.5.6` (published 2026-09-28); the exact official `refs/tags/v2.0.0` lookup returned HTTP 404. T081 therefore remains `formal target/not-run`; the prerelease build pair does not satisfy that release condition. This check does not change either frozen matrix pair or promote H8.

## H0 schema and identity continuation on 2026-10-07

The sample manifest changed after the checked-in generated query file was last refreshed. The first baseline H0 attempt therefore failed the checked-in query regeneration comparison; it is retained only as a diagnostic in the ignored build directory. `scripts/generate-grid-registry.py` regenerated the header and `test/data/grids/sample-queries.sql`, and a second `--check` passed.

The baseline 1.5.4 CLI/extension H0 record at [matched schema audit](baseline-local/h0-query-regeneration-20261007-matched-schema/) now reports query generation and sample-view binding as `pass`, with 9 generated views. The binding audit includes 12 schema/source records: default output is unchanged when `include_source` is false, `om_source` is the exact trailing STRUCT when enabled, and explicit/domain `grid_id` and `layout` match for the three projected producer samples plus the synthetic N160 identity fixture. The evidence manifest audit passed. Overall H0 remains `not-run` with exit code 2 because independent Gaussian coordinates/values and required N-grid references are missing.

The same schema/source validator returned exit code 0 against both fixed matrix pairs; exact commands, binary hashes, schemas, and identity results are in [paired schema/source smoke evidence](version-matrix/schema-source-smoke-20261007.json). A separate `build/release-vcpkg` development pair returned mismatched N160 explicit/domain identities, so it is not used as matrix evidence. These schema checks do not validate producer axis order, Gaussian coordinates, values, or H8.

## US5 local source and spatial-input continuation on 2026-10-07

The matched baseline 1.5.4 run in [US5 local evidence](baseline-local/us5.md) passed the synthetic H3 zero-value-read, source identity, grid-info SQL, and 18-scenario v4 metrics checks. Its H7 synthetic subcheck matched full point/polygon relation multisets and source positions for rotated, Lambert, stereographic, and reduced-Gaussian fixtures, with zero value reads by `om_source` and `om_grid_info`. Three hash-fixed projected producer objects also passed explicit/domain source identity and zero-value-read smoke checks for their first four positions.

H3 and H7 remain `not-run` as complete gates. The public sample axis order still follows producer README metadata without an independent point-order reference; required N160/N320/N320-region and public remote/cross-URI evidence remain missing. The top-level run therefore exited 2, while its evidence manifest audit passed. T071 is complete for registration and local evidence; T070 remains open for the missing public source and complete producer-backed checks. No producer support level is upgraded.

## Final-source paired rebuild and local runtime checks on 2026-10-07

The final source digest is `dbc22a26ed0fb44240ecc24412cd21886af961cfad2efc800a89b300c0c53110`. It adds an explicit static DuckOMO load to the DuckDB 2.0 `grid_zero_io_test` and `source_identity_test` startup paths; without it, the prerelease native process could not find `read_om`. The fixed baseline and prerelease build IDs are `15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8` and `9a7aff37d7cc7c910cc0d29fc08a0fb51d6f199f3115791731d71eb00630312b`. Resolver payloads match both build manifests, and the three artifact hashes were independently recomputed. Exact values and capability descriptors are in [version-matrix build evidence](version-matrix/builds.md) and `test/data/grids/version-matrix.json`.

For both pairs, `grid_zero_io_test`, `source_identity_test`, `httpfs_abi_test`, `grid_version_compat_test`, `grid_version_compat.test`, `grid_zero_io.test`, `source_identity.test`, and `grid_info.test` passed. Paired local H3/H7 synthetic subchecks and evidence audits also passed; the complete gates remain `not-run` and each runner returned 2. Baseline evidence is at [the current US5 local run](baseline-local/us5-20261007-current-15f751-final/), and prerelease evidence is at [the paired prerelease local run](version-matrix/prerelease-local-20261007-9a7aff/).

Remaining unchecked work is T004, T039, T049–T055, T070, T080, T081, and T089. These items need missing independent Gaussian/N-grid references, the 003 remote prerequisites and controlled-service audit, complete H0–H8 gates, a released DuckDB 2.0 pair, or an independent H9 reviewer. Build verification and synthetic local checks do not close those gates.

## H8 identity-audit continuation on 2026-10-07

`test/tools/duckomo_grid_validation.cpp` now invokes `scripts/audit-grid-version-matrix.py` for H8. The auditor validates both fixed pair IDs, build IDs, pinned runtime source IDs, isolated build manifests, DuckDB/DuckOMO/HTTPFS artifact hashes, and the recorded ABI/compatibility command set. It then searches for per-pair run manifests requesting exactly H0–H7; acceptance requires all eight gates passed, a complete and identical frozen input-set hash, matching sample/matrix and binary hashes, and a passing evidence audit for each manifest.

The current baseline and prerelease identity subchecks passed. Both runner invocations returned exit code 2 with H8 `not-run`: the existing matching-pair evidence covers H3/H7 subchecks only, and no complete same-input H0–H7 run exists. Each H8 run's evidence manifest audit passed. Records are in [baseline H8 output](version-matrix/h8-20261007-v2-baseline/) and [prerelease H8 output](version-matrix/h8-20261007-v2-prerelease/). `python3 test/tools/grid_evidence_test.py` passed 9 checks; Python syntax checks and `git diff --check` passed; both fixed-pair `duckomo_grid_validation` targets rebuilt successfully.

T080 remains open until full paired H0–H7 and remote evidence exists. This audit does not change matrix status from `build-verified` or promote any grid, remote, prerelease, or formal-release support claim.

## H6 local/loopback runner continuation on 2026-10-07

`duckomo_grid_validation --cases H6` now runs the matching `remote_session_test` and saves structured partial evidence. The interleaved synthetic local/HTTP result files matched exactly across 64 rows, including grid/layout identity and logical/point/parent/axis positions. `object_id` was checked as present independently for each URI, without requiring cross-URI equality. The local and HTTP scans both published successful v4 metrics; HTTP reported 186 attempts and 3,724 response-body bytes, which matched the independent loopback server count. Exact metrics and CSV hashes are recorded in [the run directory](baseline-local/h6-loopback-20261007-final/h6-local/).

The same run completed six cold Gaussian-fixture scans with 577 transport attempts each (3,462 total). Every scan's 1,800,918-byte query-owned peak stayed below its 2,584,092-byte bound, and its stable 38,404-byte transport-control peak stayed below the 38,800-byte bound. The six server-byte totals reconciled with v4 metrics; the structured attempt record is in [`attempt-stress.json`](baseline-local/h6-loopback-20261007-final/h6-local/attempt-stress.json).

The H6 runner returned exit 2 with `H6=not-run`, and its evidence manifest audit passed. No controlled HTTPS/S3 service or objects were used. T051/T054 remain open for producer-backed and cancellation-lifetime coverage; T055 remains open for complete H5/H6 service execution and server JSONL evidence. `scripts/validate.sh` now exposes that runner when remote setup variables include HTTPS, accepting exit 2 only for a clean audited `not-run` result.

## Reference revalidation and refreshed H8 on 2026-10-07

The three available projected samples were regenerated from the current sample manifest. The independent coordinate CSVs and official OM C full-value CSVs all retained their prior hashes; the current sample-manifest SHA-256 is `72af60d4769c2fc7b262864f1fb8e4f434e8a89ec463824f482c39103dc1939f`. Exact inputs and artifact hashes are in [the reference revalidation record](reference-revalidated-20261007.md). The active reference manifests now identify this input. Gaussian N160/N320/N320-region inputs, producer point order, and region references remain missing, so T004 and full H0/H1 remain open.

The refreshed baseline and prerelease H8 runs each froze the same input-set SHA-256, `a0ce416ca05774ee9fc3572af1773106e0e7cf5b288ae59ca962fda057df6e77`; each frozen snapshot records sample-manifest SHA-256 `72af60d4769c2fc7b262864f1fb8e4f434e8a89ec463824f482c39103dc1939f`. Both pair identity checks and both evidence-manifest audits passed. Each runner returned exit code 2 with H8 `not-run`, because complete same-input H0–H7 evidence is still absent. See the [refreshed baseline record](version-matrix/h8-20261007-refreshed-baseline/) and [refreshed prerelease record](version-matrix/h8-20261007-refreshed-prerelease/). T080 remains open; the recorded identity checks do not establish matrix validation or support.

## H6 terminal-release assertion and loopback rerun on 2026-10-08

`test/tools/duckomo_grid_validation.cpp` now requires `memory.query_owned_released_at_terminal=true` in both successful local and HTTP v4 metrics before accepting the synthetic source-position comparison. The cancellation check requires the cancelled v4 metrics to report the same release state. The baseline 1.5.4 development build passed the grid-selection, zero-I/O, v4 metrics, session metrics, remote-session, and CLI self-checks.

The first rerun at [h6-loopback-20261008](baseline-local/h6-loopback-20261008/) matched all 64 interleaved local/HTTP rows, including original source positions. Both successful scans and the cancelled query reported terminal release; the cancellation status was `cancelled`. HTTP metrics and the independent loopback server both counted 3,270 response-body bytes. Six Gaussian-fixture scans recorded 3,462 attempts total (577 each); every scan's 1,800,918-byte query-owned peak was below its 2,584,092-byte bound, and the stable 38,404-byte transport-control peak was below its 38,800-byte bound.

The follow-up at [h6-loopback-protocol-final-20261008](baseline-local/h6-loopback-protocol-final-20261008/) additionally passed weak ETag cache-bypass checks across two scans, same-URI equal-length strong ETag replacement (new ordered rows match the replacement fixture and 122 HTTP body bytes reconcile with the loopback count), and same-URI recovery after a rejected short-body response (full rows match locally and 122 bytes cover the 120-byte object). The protocol evidence records zero un-ranged GET fallbacks. Its evidence manifest audit passed.

The runner returned exit code 2 with the full H6 gate `not-run`, because no controlled HTTPS/S3 inputs or server logs were provided and the producer-backed Gaussian/003 prerequisites remain unavailable. These local synthetic checks do not complete T051–T055 or H6/H5 acceptance.


## H7 pinned producer-coordinate prefix on 2026-10-08

The baseline H7 local subcheck now compares the first four natural source positions from each of three hash-pinned public projected OM v3 samples with the pinned producer Float32 coordinate reference. Rotated GEM and stereographic GEM maximum errors were `1.52587890625e-5°`; Lambert maximum error was `5.841255187988281e-6°`, all within the frozen `1e-4°` tolerance. Explicit/domain source identities and layouts matched, and source plus `om_grid_info` value index/data/decode reads were zero. Four synthetic grids still pass complete source-position and point/polygon relation checks. The evidence audit passed at [the new H7 run](baseline-local/us5-20261008-bounded-source-prefix/).

This remains a local subcheck: it does not independently prove the complete OM array-axis-to-producer-point mapping, cover the required Gaussian N-grid objects, or establish public remote/cross-URI identity. The requested H7 gate remains `not-run` and its harness returned 2. The first full-grid-sort attempt was interrupted and is documented separately; it is not acceptance evidence.

## H5 local refresh and reader replacement check on 2026-10-08

`reader_capacity_test` now verifies the release-before-replacement failure path for `OmByteBuffer`: after allocating 64 bytes, it requests a `size_t`-maximum replacement, observes allocation failure, and asserts the prior pointer, capacity, and size are cleared. The native target rebuilt and exited 0. The H5 local refresh at [h5-local-refresh-20261008-buffer-replacement](baseline-local/h5-local-refresh-20261008-buffer-replacement/) reports all five synthetic local checks and all 14 commands passed; its evidence audit passed. Four work-peak ratios are 1.000223 for rotated/Lambert/stereographic and 1.000214 for reduced Gaussian, with result rows unchanged at 1/1/1/3.

The full H5 runner at [h5-local-refresh-20261008-t054-closed](baseline-local/h5-local-refresh-20261008-t054-closed/) returned exit 2 with `H5=not-run`; its manifest records no HTTP, HTTPS, or S3 inputs. The loopback attempt stress and cancelled-query QueryEnd release assertion are recorded separately under H6. Together with the buffer replacement check, these complete T054's local implementation and synthetic validation. They do not provide a controlled baseline-remote H5 run or server JSONL; T055 and the complete H5 gate remain open.

## H1 and shared public-source evidence crosswalk on 2026-10-08

The matched H1 comparison and the H3 public-source run use the same baseline build `5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d` and sample-manifest SHA-256 `72af60d4769c2fc7b262864f1fb8e4f434e8a89ec463824f482c39103dc1939f`. The separate source-only run streamed 1,191,300 rotated, 770,440 stereographic, and 881,361 Lambert spatial positions at time-axis index 0. For each sample, explicit/domain source rows matched exactly, coordinates matched the pinned reference within `1e-4°`, and source plus `om_grid_info` value index/data/decode reads were zero. Its manifest and detailed output are at [the matched-baseline H3 source run](baseline-local/us5-20261008-all-public-source-positions-5dac993-h3-local/h3-local/manifest.json).

This provides supporting source-position and zero-value-read evidence for the same projected inputs used by H1. The check uses validator-declared synthetic `valid_time` labels and the expected `[ny,nx,ntime]` profile; it does not independently prove OM's axis mapping, cover other time positions, or complete H1/H2-specific selection and source-query checks. Full H1/H3 gates and T070 remain open.


## 2026-10-08 public HTTPS/S3 source/info recovery after proxy diagnosis

The earlier `could not establish a strict range session` errors were caused before network I/O by DuckDB's inherited `http_proxy` value ending in `/`; GDB located the throw in `HTTPUtil::ParseHTTPProxyHost`. With the same local proxy normalized to `http://127.0.0.1:7890`, full public source/info validator runs now pass for both HTTPS and S3 (`us-west-2`) on the fixed baseline pair. Across the three existing hash-pinned projected Open-Meteo OM v3 objects, 2,843,101 spatial positions match the local copies row-by-row; explicit/domain source identities agree; 0.2° subsets (15/12/247 positions) match their materialized full-plane baselines; source/info value index/data/decode reads are zero. Raw outputs and hashes are linked from [proxy diagnosis](baseline-local/us5-20261008-public-source-proxy-diagnosis.md).

This is a projected-grid source/info subcheck only. It selects time-axis index 0 with validator-declared synthetic labels, does not independently prove OM axis-to-producer point order, does not compare weather values or reconcile HTTP/S3 body totals with server logs, and does not cover Gaussian N160/full N320/N320-region objects. URI-specific object IDs remain distinct and unverifiable. Full H1/H2/H6/H7 and T070 remain `not-run`/incomplete; no O320/O1280 substitution or non-OM conversion was used.
