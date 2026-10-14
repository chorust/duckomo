# P7 independent quickstart review — 2026-10-13

## First attempt — prior Codex CLI, not-run

Verifier: **Codex CLI AI verifier (GPT-6)**, fresh context; I did not participate in implementation. This is AI verification, not human acceptance.

Final conclusion: **NOT-RUN**. Required live HTTPS fixture identity could not be verified: the registered HTTP server PID has no `/proc` entry and `ps` reports no such process. Per the task's explicit stop rule for missing required fixtures, I stopped before any query or control request. No replacement fixture or frozen input was introduced. Prior P5/P6 summaries were not used as execution evidence.

Environment: `Linux-6.17.0-1029-nvidia-aarch64-with-glibc2.39`; machine `aarch64`; UTC `2026-10-13T07:51:38.605147+00:00`. Intended runtime v1.5.4; version SQL not run.

## Fixture identity failure

Non-private manifest fields: http_control `http://127.0.0.1:45461/__control`, http_base `http://127.0.0.1:45461`, https_base `https://127.0.0.1:43595`. All endpoints use loopback 127.0.0.1. Service registration: HTTP PID `4028835`.

Exact identity commands and output:

```text
python3 - <<'PY'
from pathlib import Path
print(Path('/proc/4028835/cmdline').read_bytes().replace(b'\0',b' ').decode())
PY
exit: 1
FileNotFoundError: /proc/4028835/cmdline

ps -p 4028835 -o pid=,args=
exit: 1
stdout: (empty)
stderr: (empty)
```

The surrounding earlier shell read also ran `sed -n '300,323p' scripts/setup-remote-fixtures.py` successfully; its combined shell exit was 0, which does not turn the failed Python PID probe into a success. No private stderr was archived.

## Input and artifact SHA-256

Computed independently with Python `hashlib.sha256(Path(path).read_bytes()).hexdigest()`. MATCH means equality with the frozen manifest value.

| Path | SHA-256 | Check |
|---|---|---|
| `specs/005-polygon-region-statistics/quickstart.md` | `63183b811dd74de600494b69a2f3193c25fac9a13481615bce5ae26a3554ada9` | governing input |
| `specs/005-polygon-region-statistics/evidence/p7-preflight-audit-20261013.md` | `b5bf2fef0e6191f3a0ca78ab8b355004369a85ef497bdabdc71c7510a98d3255` | governing input |
| `test/data/regions/manifest.json` | `14253140fa569b1b8ae7f69911a66103db16bc98651566eee8be7aebf7ff9a24` | governing input |
| `test/data/regions/real-targets.json` | `03c2bcc84b4a0081cbf636d0aace7347b5039eb45ad6bc6a132cf78d9988fd80` | governing input |
| `test/data/regions/remote-windows.json` | `832114aa533191b3db26f2a99d0e147aad24a7339c0afa7457c37a8646c7c4c7` | governing input |
| `test/data/regions/runtime-matrix.json` | `d6b3d9d9211c03d6b3c9efaa22036577eefe366fbf4788734bcec0d3912c27ff` | governing input |
| `build/official-matrix/v1.5.4/build-manifest.json` | `48bd4f6ffad3c985445a645fe9cccec976d4cc353ff4fff5eba3d9e85bcbc24e` | governing input |
| `test/data/regions/regions.om` | `264fe0905d0681d0fedd08d60a43bf7974cbd161319df77e919f08b4cb582e36` | MATCH |
| `test/data/regions/regions-membership.reference.csv` | `5d2777324982402abfe6aad9f9dc1b8df4ec93c153dcfe45afe5f761108c59b4` | MATCH |
| `test/data/regions/regions-statistics.reference.csv` | `ec9b46a79a521b5951087a1b4b6df55df8a0ec3f65c744ba2f776bf9d02b3321` | MATCH |
| `/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.4/official/duckdb` | `513b573b5ad3a94ad2c0668b1ccefb5f281b6f7aa2a5e2ff057825473848e69a` | MATCH |
| `/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.4/official/httpfs.duckdb_extension` | `6d30a487968cbe5553b7272fd5bad0e4485c4117db96e21fd1e5e2a225a5a538` | MATCH |
| `/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension` | `af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a` | MATCH |
| `/home/blizhan/repo/github/duckomo/build/official-matrix/v1.5.4/builds/3384f3825144abfe16e1487558583e511a5964577f9f2ced211e3e097dc794dc/release/extension/duckomo/duckomo.duckdb_extension` | `9cec658ed0fbb32e6101b01d75754dffaccdc24f966826e1f94abb85a2fe0658` | MATCH |

## Execution and comparison accounting

| Required execution | Status | Actual evidence |
|---|---|---|
| Teaching raw multi-region query | not-run | 0 queries; no output hash; 108 expected rows not verified |
| Membership multiset comparison | not-run | 0 actual rows compared; no missing/extra counts available |
| Per-time/member statistics oracle | not-run | 0 of 12 A/B groups compared |
| Empty-vs-all-NULL checks | not-run | 0 of 18 completed combinations checked |
| NCEP fixed-window HTTPS baseline | not-run | no CLI process launched |
| NCEP fixed-window HTTPS region | not-run | no CLI process launched |
| Full ordered 9-row comparison | not-run | 0 rows compared; NULLs, multiplicity, region_id/source_index unverified |
| Terminal scan metrics | not-run | status, scan_complete, value_data_bytes, decoded_chunks unavailable |
| Per-query service-sent bytes | not-run | no independent audit IDs or request pairs exist |

Control POST requests: **0**; audit started/complete events attributable to this run: **0**. No service byte measurement exists; zero requests must not be interpreted as a zero-byte scan. Strict decreases in value bytes, decoded chunks and service-sent bytes are **unverified**. No scan SQL was executed, so no query commands, query exit codes or result hashes exist.

Commands additionally executed: governing-input `cat` reads (exit 0); non-private JSON inspection with Python (exit 0); `rg`/`head` inspection of audit code and CSV headers followed by certificate filename search (combined exit 1, no certificate filename match); report-generation Python (exit 0 if this report exists). No run.env, AWS credential, S3 setup SQL or signature URL was read or recorded.

## Scope and residual issue

Only this report was written in the repository. No implementation, tests, quickstart, roadmap, tasks, other evidence or P7/T050 checkboxes were modified; T050 was not started. No full-domain savings or human acceptance is claimed. Required follow-up is an independently executed P7 against a live, identity-verifiable authorized fixture; this report cannot satisfy the P7 pass gate.

`git diff --cached --name-only`: exit 0; output (empty). No staged files: True.

## Second attempt — fresh-context independent verification (2026-10-13)

Verifier identity: **AI verifier, GPT-6, fresh context**. This is AI verification, not human acceptance. I independently repeated the live fixture probes before any execution; this attempt supersedes neither the first-attempt history above nor its explicit not-run status.

### Live fixture and TLS identity

The fixture registration file `build/regions-remote/005-completion-20261013/fixture/service-pids.json` lists HTTP PID `4028835` and S3 audit proxy PID `4028868`. Both were live in this runtime. Their `/proc/<pid>/cmdline` values began with `/home/blizhan/miniconda3/bin/python3 /home/blizhan/repo/github/duckomo/scripts/setup-remote-fixtures.py __http_server` and the matching `__s3_proxy`, respectively; the HTTP command used `--fixtures /home/blizhan/repo/github/duckomo/test/data`, the registered readiness/log paths, and the loopback TLS certificate/key. The manifest reports `http_base=http://127.0.0.1:45461`, `http_control=http://127.0.0.1:45461/__control`, and `https_base=https://127.0.0.1:43595`; all are loopback. `http_tls_ca_sha256` and `http_tls_certificate_sha256` both equal `99474ce367560059fa6aa7bb5e9636aa1fea7b2d1ea3ea5bad74946cd72caa30`, matching the local cert file. `openssl s_client -connect 127.0.0.1:43595 -servername localhost -tls1_3 -CAfile build/regions-remote/005-completion-20261013/tls/loopback.crt </dev/null` exited 0 and reported `Protocol: TLSv1.3`, `Verification: OK`, and `Verify return code: 0 (ok)`.

The selected runtime reported `v1.5.4 (Variegata) 08e34c447b`. The preflight audit, quickstart, manifest, membership oracle, statistics oracle, real-target manifest, remote-windows manifest, runtime matrix, build manifest, synthetic OM, official CLI, HTTPFS, spatial, and DuckOMO hashes were independently recomputed and matched the values in the first-attempt inventory above. The TLS certificate hash is recorded above. The HTTP log hash at preflight was `8f6d5449e746faf03fc968b435abc44e07e5edc17244f497c2241a78a4de41f6`; it changes as the live fixture handles requests.

### Local teaching validation — pass

A private mode-0700 `/tmp` directory held SQL, outputs, and helper material. No temporary output was placed in the repository. The matched official CLI was invoked with `-unsigned -csv -noheader`, an init file loading the exact-hash DuckOMO and spatial extensions, and the quickstart three-region setup, `read_om_regions` raw projection, grouped statistics, and 18-combination left-joined grid queries. The CLI exit code was 0. The process used `date_diff('hour', TIMESTAMP '2026-01-01 00:00:00', valid_time)` to expose the frozen oracle's time index while preserving the quickstart time axis.

- Raw output: 108 rows total, all for A/B (54 each); Empty contributes 0. CSV SHA-256: `4dafc42ffd52c09639efe642eda878e18a873ee0d30f3de04cfa2837665811fb`.
- A/B multiset comparison against the frozen membership CSV used `region_id, logical_index, member, time_index, lat, lon, value`; numeric formatting was normalized while multiplicity was retained. Expected 108, actual 108, missing 0, extra 0.
- A/B grouped-statistics comparison against the statistics oracle: 12 actual and 12 expected groups; exact row differences 0. Statistics CSV SHA-256: `fc002c3aa4235dc9a79dc4c8ad016de9fda17e14039867d47fa4d76bfe317091`. A/member0/time0 was 9 samples, 8 valid, sum 77, average 9.625, min 0, max 21. A/member0/time2 was 9 samples, 0 valid, and NULL sum/average/min/max.
- Completed grid: 18 rows; CSV SHA-256 `454d14e517f9887eb721ccfb9c49dc2075a42cf934a3da0e1fd4ffd39e99b735`. All six Empty rows were 0 samples/0 valid/NULL mean. All four A/B time-index-2 groups were 9 samples/0 valid/NULL mean, distinct from Empty. The grid is generated from the quickstart's three teaching regions; Outside and Gap are not members of this three-region input. Their frozen 18-combination zero-sample expectations remain represented in the independent full statistics oracle, not falsely reported as queried here.

### Remote fixed-window attempt — incomplete; P7 not-run

I selected `ncep_gfswave025`, expected row count 9, and window bounds `[-1.2000000000000002, -1.2000000000000002, 1.2000000000000002, 1.2000000000000002]` from `test/data/regions/remote-windows.json`. The frozen baseline and region SQL were each to receive exactly one local-object URI substitution to `https://127.0.0.1:43595/ncep_gfswave025.om`, with only `state_key` and `audit` query parameters appended. The baseline state control POST returned HTTP 204 for state key `2bb0c549-92c0-4e8a-b76d-b8db5c21e9a2`; its unique audit ID was `4f8bd77d-bf61-4ebe-a2e2-9d59a754d89f`. The official CLI process was fresh, loaded the hash-matched HTTPFS, DuckOMO and spatial artifacts, and was configured before scan with the CA file, server certificate verification, and all three required caches disabled. Its exit code was 1. DuckOMO emitted a sanitized `[configuration]` remote-open failure. The private CLI stderr was not copied into this report.

The failed scan did not produce a complete baseline output or terminal scan metric. Filtering `fixture/logs/http.jsonl` by this attempt's unique audit ID found 0 started requests and 0 complete requests, so there are no paired request IDs, successful requests, or service body bytes to report. No region control POST or region query was run after the baseline failed. Consequently the two full ordered 9-row outputs, their hashes and equality, terminal success/complete metrics, value-data and decoded-chunk savings, and region-versus-baseline service-byte reduction are all **not-run**. No P5 summary was used as substitute evidence.

The failed CLI argv shape was the official v1.5.4 CLI with `-unsigned -csv -noheader -init <private init file> -f <private SQL file>`, wrapped by a private executable that sourced the authorized v1.5.4 `run.env` without printing it. Exit code: 1. The preceding control POST exited successfully with HTTP 204. A prior remote probe before private environment loading also exited 1; it was not counted as a scan and its separate control returned 204. No credentials, `run.env` contents, S3 setup SQL, signed URL, access token, or raw CLI stderr are recorded here.

**Second-attempt result:** local teaching checks **PASS**; remote P7 **NOT-RUN / incomplete** because the baseline scan could not open the controlled HTTPS object. Overall independent P7 **NOT-RUN**. The first attempt above remains preserved as a distinct not-run record. No implementation, tests, quickstart, roadmap, task checkboxes, T050, or other repository evidence was changed by this attempt.

## Third attempt — fresh-context independent verification (2026-10-13)

Verifier identity: **AI verifier, GPT-6, fresh context**. I was not involved in implementation. This is AI verification and makes no human acceptance claim. The first-attempt not-run and second-attempt local-pass/remote-not-run records above are preserved unchanged.

### Fixture, TLS, frozen inputs, and artifacts

Independently checked registered HTTP PID `4028835` and S3 audit proxy PID `4028868`: both had live `/proc/<pid>` entries, and each command line identified the repository `scripts/setup-remote-fixtures.py` with its expected `__http_server` or `__s3_proxy` role. The registered endpoints were loopback only: `http://127.0.0.1:45461`, `http://127.0.0.1:45461/__control`, and `https://127.0.0.1:43595`. The fixture CA/certificate SHA-256 and local TLS certificate SHA-256 were all `99474ce367560059fa6aa7bb5e9636aa1fea7b2d1ea3ea5bad74946cd72caa30`. A separate TLS peer probe negotiated TLSv1.3 and successfully verified the `localhost` certificate against the fixture CA.

The following governing inputs were hashed for this attempt; hashes match the frozen values in the earlier inventory where present:

| Input | SHA-256 |
|---|---|
| `specs/005-polygon-region-statistics/quickstart.md` | `63183b811dd74de600494b69a2f3193c25fac9a13481615bce5ae26a3554ada9` |
| `specs/005-polygon-region-statistics/evidence/p7-preflight-audit-20261013.md` | `b5bf2fef0e6191f3a0ca78ab8b355004369a85ef497bdabdc71c7510a98d3255` |
| `test/data/regions/manifest.json` | `14253140fa569b1b8ae7f69911a66103db16bc98651566eee8be7aebf7ff9a24` |
| `test/data/regions/real-targets.json` | `03c2bcc84b4a0081cbf636d0aace7347b5039eb45ad6bc6a132cf78d9988fd80` |
| `test/data/regions/remote-windows.json` | `832114aa533191b3db26f2a99d0e147aad24a7339c0afa7457c37a8646c7c4c7` |
| `test/data/regions/runtime-matrix.json` | `d6b3d9d9211c03d6b3c9efaa22036577eefe366fbf4788734bcec0d3912c27ff` |
| `build/official-matrix/v1.5.4/build-manifest.json` | `48bd4f6ffad3c985445a645fe9cccec976d4cc353ff4fff5eba3d9e85bcbc24e` |
| `test/data/regions/regions.om` | `264fe0905d0681d0fedd08d60a43bf7974cbd161319df77e919f08b4cb582e36` |
| `test/data/regions/regions-membership.reference.csv` | `5d2777324982402abfe6aad9f9dc1b8df4ec93c153dcfe45afe5f761108c59b4` |
| `test/data/regions/regions-statistics.reference.csv` | `ec9b46a79a521b5951087a1b4b6df55df8a0ec3f65c744ba2f776bf9d02b3321` |
| `test/data/regions/regions.reference.csv` | `98b8349ae035ef9fc14e4e7f37caf5daa2d44f7de2cad5fe3201534cf4be1b32` |
| Official DuckDB v1.5.4 CLI | `513b573b5ad3a94ad2c0668b1ccefb5f281b6f7aa2a5e2ff057825473848e69a` |
| Official HTTPFS extension | `6d30a487968cbe5553b7272fd5bad0e4485c4117db96e21fd1e5e2a225a5a538` |
| Official spatial extension | `af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a` |
| DuckOMO v1.5.4 extension | `9cec658ed0fbb32e6101b01d75754dffaccdc24f966826e1f94abb85a2fe0658` |
| Fixture TLS certificate | `99474ce367560059fa6aa7bb5e9636aa1fea7b2d1ea3ea5bad74946cd72caa30` |

Official CLI version output was `v1.5.4 (Variegata) 08e34c447b`. All query output, SQL, metrics, and private stderr files were confined to a mode-0700 `/tmp` directory. No `run.env` contents were read or emitted; only its variable names were inspected privately to locate the configured TLS CA and matched official extension. No credentials, S3 setup SQL, signed URLs, or raw/private stderr are included here.

### Local teaching checks — pass

The official CLI argv shape was `duckdb -unsigned -csv -noheader -f <private local SQL>`; exit code **0**. Its private stderr was empty. The SQL followed quickstart's three-region A/B/Empty fixture, raw region query, grouped statistics, and full 18-combination grid.

- Raw query: 108 rows. CSV SHA-256 `bc78d04350bc7d4445bb423c905debffd9b384a49baebe2390f577b76cd0b00e`.
- A/B raw results were compared to the membership oracle with numeric normalization and multiplicity retained: 108 expected, 108 actual, 0 missing, 0 extra.
- A/B statistics: 12 expected and 12 actual groups, 0 missing and 0 extra. CSV SHA-256 `4075011d96a5f979ac70dc19046a6253d214cd41c007833300205debd8c951b3`.
- Full grid: 18 combinations, CSV SHA-256 `0112bf7fbe47fd05e1ebb59e091f2fa9dada50e90c9d77383612b8c03be51a1c`. All six Empty combinations had 0 samples/0 valid/NULL mean. Four A/B time-index-2 groups had 9 samples/0 valid/NULL mean, preserving the distinction between empty and populated all-NULL groups.

### Remote fixed-window comparison — pass

Selected the exact `ncep_gfswave025` baseline and region SQL and bounds from `test/data/regions/remote-windows.json`: `[-1.2000000000000002, -1.2000000000000002, 1.2000000000000002, 1.2000000000000002]`, expected 9 rows. Each SQL contained exactly one frozen local URI; that URI alone was replaced by the manifest `https_base` plus `/ncep_gfswave025.om`, with unique state/audit query parameters appended. No frozen bounds or SQL predicates were changed. Before each scan, its unique state control POST returned HTTP 204.

Each scan used a separate official DuckDB process with argv shape `duckdb -unsigned -csv -noheader -f <private query SQL>` and exit code **0**. Each process loaded the matched HTTPFS, DuckOMO, and spatial artifacts, set the configured CA certificate and `enable_server_cert_verification=true`, then disabled external-file, HTTP-metadata, and object caches before the scan. For **each** DuckDB subprocess the environment removed exactly these present variable names, case-insensitively: `ALL_PROXY`, `HTTPS_PROXY`, `HTTP_PROXY`, `all_proxy`, `https_proxy`, `http_proxy`. No environment values were emitted.

Both full results contained exactly 9 rows; baseline and region row files had identical SHA-256 `81f034a7c79f82bfc5f6790d3f4dcbffd9b384a49baebe2390f577b76cd0b00e` (baseline) and `81f034a7c79f82bfc5f6790d3f4dcbffd9b384a49baebe2390f577b76cd0b00e` (region). Ordered row-by-row comparison including CSV NULL representation, duplicates, `region_id`, and `source_index`: 9 vs 9, 0 differences. Unique audit IDs were `p7third-baseline-979c8917b4d2450682a61bcd3b69b1ac` and `p7third-region-23d4131fef344b02a662653462990aff`; the corresponding state keys were `p7third-baseline-71c613d2` and `p7third-region-71c613d2`.

Each scan published exactly one terminal metric. Baseline and region both reported `status=success`, `scan_complete=true`, `terminal_published=true`; value decode completeness was true. Baseline value data bytes/chunks were **77,580 / 135**; region was **25,860 / 45**. Per-query HTTP audit was filtered only by that query's unique audit ID; each request ID had one started and one complete event, with no missing pairs or failed responses:

| Query | Started | Complete | Failed | Successful body bytes |
|---|---:|---:|---:|---:|
| Baseline | 1,107 | 1,107 | 0 | 150,469 |
| Region | 775 | 775 | 0 | 79,671 |

Region required strictly fewer value data bytes, decoded chunks, and fixture response body bytes. The successful scan metrics CSV SHA-256 values were `8d5a4a5e4a42c1cc7ee49fb477c6596ea86672d62f9f0ba6ff7998e68ae36212` (baseline) and `73587fcdff271d56598399958f8f7b629423f372754b0807360c22cdf0dcb6da` (region).

**Third-attempt result:** local teaching checks **PASS**; remote fixed-window checks **PASS**. This attempt supplies successful independent P7 execution evidence for these checks. It does not claim human acceptance or full-domain savings. The first and second attempt histories remain unchanged above. Only this report was modified in the repository; no implementation, test, quickstart, roadmap, task checkbox, T050, or other evidence file was changed.
