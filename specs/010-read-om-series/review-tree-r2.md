# DuckOMO whole-working-tree review — round 2

**Verdict: the five round-1 findings are closed within their stated scope; no new confirmed defect was found in this review. This is not an 008 PASS or 010 PASS, and not an unconditional approval to merge the entire dirty tree as-is.** The known acceptance gaps remain, and a product submission must apply the evidence/tooling scope policy. Those limitations are not new findings and are not a request to delete evidence or start another phase.

## Baseline and method

- Repository: `/home/blizhan/repo/github/duckomo`.
- HEAD: `80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9`.
- Comparison: **HEAD against the entire current working tree**, using `git diff HEAD`, plus `git ls-files --others --exclude-standard` and submodule status. No three-dot range was used. The index had no staged diff.
- Inventory before this report: 38 tracked files with textual changes, 5,089 nonignored untracked files, and the nested roadmap submodule's untracked `roadmap-config.yml`. No changed roadmap gitlink or tracked submodule diff was present.
- Host: `uname -m` returned **`aarch64`**. All executions below were on this linux_arm64 host.
- Both review lines were performed directly in this session. The available tools do not expose subagents; this is not represented as an independent parallel review. The code-review skill's comparison convention was overridden by the explicit HEAD-to-working-tree scope.
- **Standards line:** no new documented-standard violation found. `.specify/memory/constitution.md` remains an unfilled template; no applicable project `AGENTS.md`, `CONTRIBUTING.md`, or `CODING_STANDARDS.md` was found in the searched project scope. Ownership, exception cleanup, compatibility boundaries, and unnecessary duplication were inspected as review judgments, not invented formal rules. `git diff HEAD --check` passed.
- **Spec/behavior/docs-consistency line:** re-read current implementation and changed hunks, the 010 requirements and final reconciliation, the two targeted fix notes, current guides, community generation procedure, 009 checklist, and 008 status/evidence. No new actionable defect was established. Full acceptance remains incomplete.

Production review concentrated on the reader lifetime change, shared scanner/binder/schema/compatibility seams, series candidate planning and binding, structured missing-error handling, registration/build wiring, and associated native/SQL/Python checks. Evidence was reviewed through reports, indexes, selected manifests/identities, and size, rather than by line-reading 1.2G of historical output. The historical [round-1 report](review-tree.md) and [010-only review](review.md) were context, not substitutes for current-file inspection.

## Findings by feature

There are **zero new severity-ranked findings**. The table records the old finding's severity and its current disposition; these are not five open findings.

| Feature | Previous finding | Original severity | Round-2 disposition |
|---|---|---:|---|
| 008 | F008-01 — planned payload retention across synchronous calls | P1 | Closed: current-source retention probe passed; scope ownership and async buffer lifetime inspected |
| 010 | F010-01 — public guidance retained intermediate validation status | P2 | Closed: English/Chinese README and interface guide agree with scoped final evidence and Blocked gates |
| community | FCOM-01 — upgrade command did not update actual `repo.ref` | P2 | Closed: actual YAML fields now receive both substitutions; independently rendered and parsed |
| 009 | F009-01 — readiness checklist contradicted unresolved clarification | P2 | Closed: incomplete items unchecked; one clarification explicitly retained; Draft preserved |
| misc | FMISC-01 — no giant-evidence submission policy | P2 | Closed as a policy request: include/exclude and checksummed-archive policy exists; historical evidence preserved |

### 008 — lifetime fix and remaining status

**Locations inspected:** `src/include/duckomo/om_reader.hpp:276` (lookup entries and nested depth), `src/om/reader.cpp:352` (`PlanSelection` scope), `src/om/reader.cpp:783` (`DecodeSelectionImpl` scope), and `src/scan/read_om.cpp:3443` (scan-window scope).

`BeginPlannedCacheScope` clears lookup entries only at the outermost entry; `EndPlannedCacheScope` clears them when that owner's depth returns to zero. Standalone decoding therefore releases the planned directory on success and stack unwinding. The scan invocation holds an outer scope across nested planning/decoding, including the inline asynchronous-task `SYNCHRONOUS` return path. Plans and pending items retain their own shared payload references, so clearing lookup entries on return does not invalidate submitted reads. The recursive pending-output path is also nested rather than prematurely ending the outer scope. The single completed `data_cache` remains intentionally available for adjacent reuse.

The current public remote-path native test was freshly compiled together with the **current reader translation unit**, then linked using existing v2.0.0 static dependencies. This is stronger than simply repeating old probe values. The in-memory fixture advertises an HTTPS path to take the remote planner branch; it makes no network request.

Fresh observations after each public `DecodeSelection` call:

| Starting row | Planned entries | Retained payload bytes, including `data_cache` |
|---:|---:|---:|
| 0 | 0 | 136 |
| 7 | 0 | 496 |
| 14 | 0 | 136 |
| 21 | 0 | 144 |
| 28 | 0 | 144 |
| 35 | 0 | 152 |
| 42 | 0 | 152 |
| 49 | 0 | 152 |
| 56 | 0 | 152 |
| 63 | 0 | 152 |
| 70 | 0 | 447 |

Peak retained payload was **496 bytes**, below the fixture's **4,462-byte single-range bound**. All 11 calls returned with zero scope depth and no planned entries. Adjacent-selection reuse, injected second-read failure, injected interruption, and the existing reader capacity checks passed. These are newly observed values, not copied old measurements. No OOM, real remote RSS, or throughput conclusion follows from this small fixture.

The existing fix-build `remote_async_test` was separately rerun after confirming its binary SHA-256 and the relevant source hashes match the fix note. It passed async/sync/inline result equality, single-variable prefetch, multi-worker checks, error handling, cancellation/recovery, and inflight-budget checks. It is explicitly an existing binary rerun, not a fresh full scanner build. The inline check establishes result/lifecycle behavior; it does not directly measure per-batch retained bytes inside that branch.

No new failure caused by the nested scope was found. F008-01 is closed. See [the original fix evidence](../008-duckdb2-async-io/evidence/review-fix-f008-01-linux-arm64/fix-note.md).

**Known non-new limitations:** SC-006 remains **relaxed / will not meet the original ≥20% throughput gate**. Approximately 0% is not 20% PASS. `specs/008-duckdb2-async-io/tasks.md:126` and `:127` retain unchecked T042/T043; true cross-batch I/O/decode overlap remains unimplemented. The remaining SC-007/SC-008 matrix and other open tasks are not closed by this fix. Current spec, checklist, performance report, and final status consistently preserve those limits. Earlier benchmark request-count and performance results were not rerun on the lifetime-fixed tree in this review.

### 010 — series and shared seams

**F010-01 locations:** `README.md:19`, `README.md:26`, `README.md:175`, corresponding `README.en.md` paragraphs, and `docs/spec.md:5`, `:16`, `:122`.

The current guides correctly distinguish development-source series support from released extension identity. They describe executed linux_arm64 cases for v1.5.4/v1.5.5/v1.5.6/v2.0.0, keep S2 / SC-002 / SC-007 **Blocked**, limit real-object evidence to two adjacent IFS025 `temperature_2m` chunks at one domain point, and label N160 as synthetic. The DuckDB 2.0 wording is now scoped separately from the Feature 004 acceptance exclusion. These statements agree with the historical run report's authoritative final reconciliation and the later R1/R2 fix note. They are not evidence that the complete final matrix was rerun after F008-01.

**Historical R1/R2 remain fixed:** `src/scan/read_om_series.cpp:125` validates spatial declarations before entering the candidate-open loop. The shared validator at `src/scan/read_om.cpp:4131` resolves the declaration and checks declared spatial axes. At `read_om_series.cpp:143–157`, each candidate is bound and its signature validated before a slotless candidate is omitted. Initial missing errors still skip only when both typed category and initial-open stage match, and strict mode rethrows. This preserves strict presence and existing-object metadata obligations for slotless candidates.

I inspected these current paths, the planner's checked arithmetic/closed-window handling, the shared signature comparisons, logical union/projection adapters, and local/remote error-category additions. The SQL regressions and [R1/R2 fix evidence](evidence/review-fix-linux-arm64/fix-note.md) support the targeted repair. **I did not rerun the 68 R1/R2 SQL probes or their straces in this round**, and do not present their prior results as fresh execution. No current runtime defect was established that would reopen those historical findings.

The current Python series suites passed: 29 discovered unittest cases, plus the standalone registry and fixture-generator tests. Registry provenance/deterministic generation passed. Fixture tests use the deterministic stub writer for wrapper/oracle checks; their success is not a new real-OM-writer or four-engine acceptance run.

**Known non-new blockers:** S2 / SC-002 / SC-007 remain Blocked; FR-002/FR-007/FR-014 remain partially verified. Embedded-time-metadata conflicts, the full overflow/zero-access corpus, and the complete actual-object mismatch matrix still require the evidence already listed in the fix note. `tasks.md:176–178` correctly states that 42/42 checked tasks do not certify full acceptance. No extra finding is created merely because these known cases remain incomplete.

### community — upgrade procedure

**Locations:** `community/duckomo/description.yml.in:9` and `:23`; `docs/community-extensions.md:72–101`.

The placeholders now occupy actual `extension.version` and `repo.ref` fields. The documented Python procedure checks placeholder counts, substitutes both values, parses the rendered YAML, and asserts the resulting fields. I independently executed that rendering/parsing logic in memory with version `0.3.5-rc.2` and current HEAD. Parsed values were exactly:

```text
extension.version = 0.3.5-rc.2
repo.ref = 80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9
```

No generated descriptor was published or submitted. The `.in` template requires rendering before YAML parsing; the documented workflow now does so. No active script/test/build consumer of the template was found that depended on parsing its unrendered contents.

The published identity in `community/validation.json:9–21` remains `0.3.5-rc.1` / `blizhan` / `55d66f3fbd3199d7471b8cd569c43cc91a8cec7e`. Its artifact-hash TODO remains explicit. Current template fields other than the intentional placeholders retain the recorded publication scope. **No fresh upstream fetch, community install, signed artifact download, or multi-platform verification was performed.** The earlier live-alignment observation is historical evidence, not a new round-2 network check. FCOM-01 is closed; signed artifact identity remains an acknowledged evidence limitation.

### 009 — Draft readiness

**Locations:** `specs/009-point-site-nearest-sampling/checklists/requirements.md:16–17`, `:37`; `spec.md:7`, `:197`.

The spec still contains one unresolved legacy-metrics decision and remains Draft. Both “No [NEEDS CLARIFICATION] markers remain” and “Requirements are testable and unambiguous” are unchecked. The note explicitly identifies the legacy_v2/legacy_v3 choice and says the checklist cannot establish readiness for planning. F009-01 is closed. There is no 009 implementation or acceptance claim in this review, and no plan/tasks were generated.

### Misc — evidence, tooling, mixed changes

**Location:** [evidence/submission-scope.md](evidence/submission-scope.md).

The policy now specifies a small product-PR evidence set, hashes and rerun commands, representative failure/fix records, preserved Blocked outcomes, and a separate immutable archive with archive/checksum-manifest hashes and retrieval instructions. It explicitly says no archive has yet been created. This satisfies FMISC-01's policy request; it does not mean the future submission has already been packaged.

Fresh size-only inventory of `run-20260803-linux-arm64/`: **5,078 files, 1,181,103,584 bytes; `du -sh` reports 1.2G**, matching the fix note. No historical evidence file was deleted, moved, rewritten, compressed, or uploaded. Size agreement is not a full content-integrity audit.

The policy excludes `.pi/` and nested roadmap configuration from a product PR while preserving local files. `.specify/extensions/roadmap` has only untracked `roadmap-config.yml`; there is no changed parent gitlink. Agent extension/webhook behavior was not executed or reviewed as DuckOMO runtime behavior. No local secret file contents were read.

Mixed-hunk ownership still matters when preparing a submission:

- `src/scan/read_om.cpp` contains 010 binding/spatial/schema changes and 008 cache-window/reuse-accounting changes.
- `src/include/duckomo/om_reader.hpp` contains 010 error classification and 008 cache lifetime ownership.
- README changes combine series scope, install instructions, and dated usage examples; community and 009 are separate workstreams.
- CMake registrations and new series source/test/fixture files must accompany the 010 implementation. Retained evidence comes from mixed trees; extracting a feature onto HEAD would require verification of that actual extracted result.

These are submission boundaries, not new runtime defects or authority to stage/remove files.

## Verification commands and identities

Working directory for tests: `/home/blizhan/repo/github/duckomo`.

```sh
uname -m
git rev-parse HEAD
git status --short
git diff HEAD --stat
git diff HEAD --check
git diff --cached --stat
git ls-files --others --exclude-standard
git -C .specify/extensions/roadmap status --short
du -sh specs/010-read-om-series/evidence/run-20260803-linux-arm64
python3 -m unittest discover -s test/tools -p '*series*test.py'
python3 test/tools/series_registry_test.py
python3 test/tools/series_fixtures_test.py
/tmp/duckomo-r2-reader-capacity
build/official-matrix/v2.0.0/builds/76da44b31663a1775355935545f257c7536d02f8866c3af58233a985c206f293/release/test/native/remote_async_test
```

The current-source capacity binary was built without modifying production files or replacing build-tree artifacts. Reproduction recipe below uses the existing configured v2.0.0 build's compiler flags and static dependencies; it compiles both current TUs to `/tmp`, places the fresh reader object before the static archives, and substitutes the fresh capacity-test object:

```python
import json, pathlib, shlex, subprocess
b = pathlib.Path('build/official-matrix/v2.0.0/builds/'
    '76da44b31663a1775355935545f257c7536d02f8866c3af58233a985c206f293/release').resolve()
entries = json.loads((b / 'compile_commands.json').read_text())
for suffix, target, output in [
    ('/src/om/reader.cpp', 'duckomo_extension.dir', '/tmp/duckomo-r2-reader.o'),
    ('/test/native/reader_capacity_test.cpp', 'reader_capacity_test.dir', '/tmp/duckomo-r2-capacity.o'),
]:
    e = next(x for x in entries if x['file'].endswith(suffix) and target in x['command'])
    argv = shlex.split(e['command'])
    argv[argv.index('-o') + 1] = output
    subprocess.run(argv, cwd=e['directory'], check=True)
p = b / 'extension/duckomo/test/CMakeFiles/reader_capacity_test.dir/link.txt'
argv = shlex.split(p.read_text())
argv[argv.index('-o') + 1] = '/tmp/duckomo-r2-reader-capacity'
argv = ['/tmp/duckomo-r2-capacity.o'
        if x == 'CMakeFiles/reader_capacity_test.dir/native/reader_capacity_test.cpp.o'
        else x for x in argv]
argv.insert(1, '/tmp/duckomo-r2-reader.o')
subprocess.run(argv, cwd=b / 'extension/duckomo/test', check=True)
subprocess.run(['/tmp/duckomo-r2-reader-capacity'], check=True)
```

Compilation and link exited 0; the test exited 0. Compiler flags include `-march=armv8-a -mtune=generic`. Exact executed argv/cwd records are also in temporary `/tmp/duckomo-r2-build-commands.json`; these temporary files are not permanent repository evidence dependencies.

| Source or binary | SHA-256 observed in round 2 |
|---|---|
| `src/om/reader.cpp` | `4949a0ff7632aa595f7a94215af9009d65dae7208facf826486d725d1550de91` |
| `src/include/duckomo/om_reader.hpp` | `431d61fc552055097592f08938d52853c30545b5ec6516e5fc0fba7a40d76d04` |
| `src/scan/read_om.cpp` | `a49a7dae1e07975765c9f19e6929e8b9284f067b1b5604d26afdb2dbbcc9a741` |
| `test/native/reader_capacity_test.cpp` | `e44e977001c237c73f768797437a5399648b0d334d523fe73821d932738a918c` |
| Fresh `/tmp/duckomo-r2-reader-capacity` | `58c2a11e8a590f93ce8c76e766d9546b6231c34e279ba7db2f2fa48d1722f07a` |
| `test/native/remote_async_test.cpp` | `0bd5c1952dc3c0f2185129f4feb4470482dd0a1c17200f288480715c9f90c124` |
| Existing v2.0.0 `remote_async_test` rerun | `f428b35470d55b0e14bfcab0f9dd64165de0fb723de64618bd1500bb48332f47` |

Temporary logs: `/tmp/duckomo-r2-reader-capacity.log`, `/tmp/duckomo-r2-remote-async.log`, `/tmp/duckomo-r2-python.log`. All reported test invocations succeeded. The report embeds the important retention observations so conclusions do not depend on those temporary paths surviving.

## Limits and completion boundary

Not run: full rebuild, complete 008/010 acceptance, new four-pair series SQL/strace matrix, ASan/UBSan, real producer fetches, real HTTPFS performance/RSS benchmarks, community signed installation, external CI, other architectures, or a full secret/content audit of the giant evidence tree. Supporting static archives were reused; only the reader and capacity-test TUs were freshly compiled for the retained-bytes probe. Existing evidence is attributed to its recorded source/binary, not silently promoted to validation of every current dirty hunk.

**Counts:** new Standards findings 0; new Spec/behavior/docs-consistency findings 0; five previous findings closed. Historical 010 R1/R2 remain closed. Known 008/010 acceptance blockers and unperformed community artifact verification remain explicit.

Repository deliverable for this phase is only `specs/010-read-om-series/review-tree-r2.md`. The historical round-1 report is preserved. No production fix, task/checklist edit, commit, push, amend, rebase, reset, stash, evidence cleanup, subsequent implementation phase, or parent notification was performed.
