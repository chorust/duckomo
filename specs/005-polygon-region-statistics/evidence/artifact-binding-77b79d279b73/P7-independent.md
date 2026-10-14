# P7 independent verification — spec 005 current artifact

**Result: PASS for P7 under the 2026-10-13 bounded-window amendment.** This report is fresh evidence for the exact requested current artifact, independently executed by AI; no parent P5 or historical P7 result was used to infer success.

Verifier: AI model `codex/gpt-6.1-sol`, fresh delegated context, session `01a1392e-4090-75eb-b027-e92c18a9cc94`. I did not implement this change. Execution began `2026-10-14T06:55:14.810935+00:00` and completed `2026-10-14T06:57:33.580942+00:00` UTC; a subsequent mapping-identity probe also exited 0. Platform: Linux `aarch64`. Repository HEAD: `e69e19bee34235ef82508b449b307f30def11eb5` with current uncommitted changes.

Authority: repository read/validation, private `/tmp` scratch, and this bound report only. No implementation, test, spec, quickstart, roadmap, or tasks file was edited; no commit, stage operation, or child agent was created.

## Artifact and input identity

Exact DuckOMO artifact: `/home/blizhan/repo/github/duckomo/build/official-matrix/v1.5.4/builds/abc08d813e3b65de88306b9834d96029ba2b35c964c3d287a84b52662f48896d/release/extension/duckomo/duckomo.duckdb_extension`. The build manifest read was the sibling build-specific `build-manifest.json`; no release alias or older manifest was used. Source digest was independently recomputed by hashing sorted repository source/test/script/OM files according to the manifest algorithm: `77b79d279b735731300342630de0ac52a3b87b3d107dd60a3c668a128c8248db`, exactly matching the requested digest and this build manifest.

Official CLI/HTTPFS/spatial and compressed spatial hashes matched `test/data/regions/runtime-matrix.json`. Artifact hashes were rechecked after queries and remained unchanged. Actual version probe returned `v1.5.4`; spatial version was `28db190`; DuckOMO reported `e69e19b`.

The metadata function `duckdb_extensions()` reports DuckOMO install_path under the user extension directory even after explicit LOAD. To remove that ambiguity, a separate fresh CLI process loaded all three exact paths and `/proc/<pid>/maps` independently confirmed these actual mappings:

- `/home/blizhan/repo/github/duckomo/build/official-matrix/v1.5.4/builds/abc08d813e3b65de88306b9834d96029ba2b35c964c3d287a84b52662f48896d/release/extension/duckomo/duckomo.duckdb_extension`
- `/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.4/official/httpfs.duckdb_extension`
- `/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension`

| Input or binary | SHA-256 |
| --- | --- |
| test/data/regions/runtime-matrix.json | d6b3d9d9211c03d6b3c9efaa22036577eefe366fbf4788734bcec0d3912c27ff |
| test/data/regions/manifest.json | 14253140fa569b1b8ae7f69911a66103db16bc98651566eee8be7aebf7ff9a24 |
| test/data/regions/real-targets.json | 03c2bcc84b4a0081cbf636d0aace7347b5039eb45ad6bc6a132cf78d9988fd80 |
| test/data/regions/remote-windows.json | 832114aa533191b3db26f2a99d0e147aad24a7339c0afa7457c37a8646c7c4c7 |
| specs/005-polygon-region-statistics/quickstart.md | acd7c90cfe9e13ceabda701dac26b86c5fdb56481afa7f640cb01956c9e5cbb8 |
| specs/005-polygon-region-statistics/contracts/validation-evidence.md | 2782eda257828de1c8630801578a2dba03738f7012a1ac48d98c659a9c421a1d |
| specs/005-polygon-region-statistics/contracts/bounded-remote-validation-20261013.md | 44da81535f56e38759368a27b9c351a0dee194562fd17f3e884414acac279cff |
| build/official-matrix/v1.5.4/builds/abc08d813e3b65de88306b9834d96029ba2b35c964c3d287a84b52662f48896d/build-manifest.json | 36a4beef348394b2b676ee255f4de60abf8f97dd8529b832f39ec0591a26be4c |
| build/official-matrix/v1.5.4/builds/abc08d813e3b65de88306b9834d96029ba2b35c964c3d287a84b52662f48896d/release/extension/duckomo/duckomo.duckdb_extension | 171759d6a02e1e1924e2a68beaefeac03da208af066f6868bddc4d5163d59148 |
| build/regions-runtime/v1.5.4/official/duckdb | 513b573b5ad3a94ad2c0668b1ccefb5f281b6f7aa2a5e2ff057825473848e69a |
| build/regions-runtime/v1.5.4/official/httpfs.duckdb_extension | 6d30a487968cbe5553b7272fd5bad0e4485c4117db96e21fd1e5e2a225a5a538 |
| build/regions-runtime/v1.5.4/official/spatial.duckdb_extension | af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a |
| build/regions-runtime/v1.5.4/official/spatial.duckdb_extension.gz | 38eca92a690de960e64c749f1d8e5e0cd3d8a3f1c4af647d8d0fc66cb7c4c565 |
| test/data/regions/regions.om | 264fe0905d0681d0fedd08d60a43bf7974cbd161319df77e919f08b4cb582e36 |
| test/data/regions/regions.reference.csv | 98b8349ae035ef9fc14e4e7f37caf5daa2d44f7de2cad5fe3201534cf4be1b32 |
| test/data/regions/regions-membership.reference.csv | 5d2777324982402abfe6aad9f9dc1b8df4ec93c153dcfe45afe5f761108c59b4 |
| test/data/regions/regions-statistics.reference.csv | ec9b46a79a521b5951087a1b4b6df55df8a0ec3f65c744ba2f776bf9d02b3321 |
| build/regions-remote/005-completion-20261013/v1.5.4/run-manifest.json | 0e729c60d70f788ac70a4cd36dce8eac83e4105536d09348fc9ed1cbbbc7b281 |
| build/regions-remote/005-completion-20261013/v1.5.4/service-pids.json | 767cad2ae82796e54fe48571e4208dd197b112c7b5ed35327d7ef8101e292657 |
| build/regions-remote/005-completion-20261013/tls/loopback.crt | 99474ce367560059fa6aa7bb5e9636aa1fea7b2d1ea3ea5bad74946cd72caa30 |
| build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om | 0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd |
| build/evidence/spatial/reference/secondary_swell_wave_direction.csv | c5869e3890039ed88b684fb2916f6b74925c08243d36692fe906c255e7c4eed5 |
| build/evidence/spatial/reference/secondary_swell_wave_height.csv | 41446f0736f55a5ad9bc1878059cebd7b168a67501cf3a402224c069451c7a48 |
| build/evidence/spatial/reference/secondary_swell_wave_period.csv | e438f165d10dc7669ad2e6cc60797530453e32797c43a3745705261102dcf5fb |
| build/evidence/spatial/reference/swell_wave_direction.csv | f7ea32de7afc46b242715f08cbf7e71ca4723624e82f2956fec365b96c94b956 |
| build/evidence/spatial/reference/swell_wave_height.csv | e69cb9dd35f2834bf360cd74ff5796d2276a9ac894b6002e00077eb03e692993 |
| build/evidence/spatial/reference/swell_wave_period.csv | 9dc94349e8450e08465884a0adc1cf8a5da7f2d6a1c0c57d522765626776f4e1 |
| build/evidence/spatial/reference/tertiary_swell_wave_direction.csv | e1052e3024573087d74cc7231da3198f4ca6d6ef70322a83aca08e9275c425cc |
| build/evidence/spatial/reference/tertiary_swell_wave_height.csv | a978bc1ac061e4eaf5084c7c07543cf9edad268e80363f84322668867b224c8a |
| build/evidence/spatial/reference/tertiary_swell_wave_period.csv | 6403b60f88d31837c42ccdc928dcfe4bf5f1fc80aa407337316941a299fe81a0 |
| build/evidence/spatial/reference/wave_direction.csv | 4ef1d56c20de5739bdf8719aab09ad491dea9ccd5d7b3606fffe348067295a83 |
| build/evidence/spatial/reference/wave_height.csv | c5a0bd2bc2e0457ad1a3a0e4b1da877a655b2a8c37a654f8d25a3730c388e778 |
| build/evidence/spatial/reference/wave_period.csv | cebaca77f2a0c2c3131e6648de8e70ad9cdeb39c55f05452792e8690e2d85a9a |
| build/evidence/spatial/reference/wind_wave_direction.csv | 1b25c4905b056cefc8345e2f402a38ee5864e13302dc86182b50f272ca68e5ef |
| build/evidence/spatial/reference/wind_wave_height.csv | ab149d25f08b77714a06ce8030fc795315a7664dbd3600b9c566b70c1220c090 |
| build/evidence/spatial/reference/wind_wave_period.csv | dce257e8a4d70893cfa42e25c2dd78d4989cf9ae15870df4a7ba20ccbd122dc3 |

All 15 official ncep value-reference CSV hashes were rechecked before comparing every selected value. Frozen target object hash was checked both at its source path and the running HTTP server’s configured object mapping. The synthetic 150-value C API export matched the documented integer/NaN formula independently.

## Actual execution and command shapes

Private scratch: `/tmp/duckomo-p7-independent-yo_u7zrb`, created with mode `0700`; generated files used umask `077`. Output/stderr were captured privately. `run.env` permissions were verified private, parsed with `shlex` without execution, and only the three needed HTTPS/CA/HTTPFS fields were selected in memory. The env file, credentials, S3 setup SQL, signed URLs, and private stderr are not reproduced or archived here.

Every CLI subprocess used a freshly constructed environment dropping keys case-insensitively when their name ends in `_proxy`, and also `proxy`. Loopback control requests used `urllib.request.ProxyHandler({})`. CLI command for each independent process:

```text
/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.4/official/duckdb -unsigned -batch -bail :memory: < <private-scratch>/<label>.sql
```

Queries were completely consumed using `COPY (<query>) TO <private-scratch>/<result>.csv (HEADER, FORMAT CSV, NULL '__P7_NULL__')`. Metrics were copied immediately after the scan in the same process. No LIMIT was introduced.

| CLI process | Exit | stdin SQL SHA-256 |
| --- | --- | --- |
| identity-probes | 0 | 78f6a04f315284b504c5632af6a92872c7ec91cf4156f5931a0a5ba16ce87fc5 |
| local-quickstart | 0 | bacdd313f0221474933e1c191b598b941936da41d2b03c2d608df157e36eb08f |
| remote-baseline | 0 | 764a8b2c212c3cae1fe391545f6a286ecca805361fc28a38cf008fa1995af1f4 |
| remote-region | 0 | 474470d7d1e3945a2870f4d15d6971ead9ed1ee64090c72275704c53a2af0c9a |
| projection-only | 0 | a1522aa2170bf8dc05656751a6679a4d9f30d3eaff7923782b92c8c5546f981f |
| mapped-identity | 0 | 8b26d1d54b3cbac3a1d8675425b297c8ae02c19c2eead511c8dabead3f3b2076 |

Independent verifier commands: `python3 /tmp/duckomo-p7-independent-yo_u7zrb/verify.py` (exit 0), `python3 /tmp/duckomo-p7-independent-yo_u7zrb/finalize.py` (exit 0). The finalization checked ordered and multiset results, independent references, complete metrics and every isolated request pair. A separate Python-controlled `mapped-identity` CLI probe exited 0. `git diff --cached --name-only` exited 0 with empty output.

| Private verifier source | SHA-256 |
| --- | --- |
| verify.py | af4cc5f1d9256b6d6fe569e5d361e81875630ee5db765c0923d70a3a5e82a3d4 |
| finalize.py | 4164d8cc56464f500319c31bfc811bfe31f0cd1fac79a08108ea4769b524a9af |

## Local quickstart reproduction

The quickstart A/B/Empty definition, variable declarations, raw region scan, per-time/member aggregation, 18-combination left join, and full-materialization SQL baseline were executed from the current quickstart SQL. Local result checks:

- 108 raw rows, A=54 and B=54, Empty=0; 12 nonempty statistics groups and exactly 18 distinct filled region/time/member combinations.
- Raw rows exactly matched an independently enumerated integer-grid oracle (member × time × closed rectangle points), including logical source indices, coordinates, time, values, SQL NULLs and overlap membership; frozen membership CSV agreed.
- A/member0/00:00: 9 samples, 8 valid, mean 9.625, min 0, max 21, sum 77.
- Empty: six groups of 0 samples / 0 valid / NULL mean. A/B at 02:00: four groups of 9 samples / 0 valid / NULL numeric aggregates.
- Every aggregate matched independent arithmetic and the frozen statistics CSV. Both full-baseline EXCEPT ALL differences were 0.
- Spatial WKB round trip, validity, point and boundary covers probes succeeded; required function signatures were recorded.
- An additional fresh region-only `region_id,count(*)` projection returned A=54/B=54 with value index bytes=0, data bytes=0 and decoded chunks=0.

| region_id | valid_time | member | sample_count | valid_count | mean_value | min_value | max_value | sum_value |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A | 2026-01-01 00:00:00 | 0 | 9 | 8 | 9.625 | 0.0 | 21.0 | 77.0 |
| A | 2026-01-01 00:00:00 | 1 | 9 | 8 | 1009.625 | 1000.0 | 1021.0 | 8077.0 |
| A | 2026-01-01 01:00:00 | 0 | 9 | 8 | 109.625 | 100.0 | 121.0 | 877.0 |
| A | 2026-01-01 01:00:00 | 1 | 9 | 8 | 1109.625 | 1100.0 | 1121.0 | 8877.0 |
| A | 2026-01-01 02:00:00 | 0 | 9 | 0 | __P7_NULL__ | __P7_NULL__ | __P7_NULL__ | __P7_NULL__ |
| A | 2026-01-01 02:00:00 | 1 | 9 | 0 | __P7_NULL__ | __P7_NULL__ | __P7_NULL__ | __P7_NULL__ |
| B | 2026-01-01 00:00:00 | 0 | 9 | 8 | 22.0 | 11.0 | 33.0 | 176.0 |
| B | 2026-01-01 00:00:00 | 1 | 9 | 8 | 1022.0 | 1011.0 | 1033.0 | 8176.0 |
| B | 2026-01-01 01:00:00 | 0 | 9 | 8 | 122.0 | 111.0 | 133.0 | 976.0 |
| B | 2026-01-01 01:00:00 | 1 | 9 | 8 | 1122.0 | 1111.0 | 1133.0 | 8976.0 |
| B | 2026-01-01 02:00:00 | 0 | 9 | 0 | __P7_NULL__ | __P7_NULL__ | __P7_NULL__ | __P7_NULL__ |
| B | 2026-01-01 02:00:00 | 1 | 9 | 0 | __P7_NULL__ | __P7_NULL__ | __P7_NULL__ | __P7_NULL__ |

All 18 filled combinations:

| region_id | valid_time | member | sample_count | valid_count | mean_value |
| --- | --- | --- | --- | --- | --- |
| A | 2026-01-01 00:00:00 | 0 | 9 | 8 | 9.625 |
| A | 2026-01-01 00:00:00 | 1 | 9 | 8 | 1009.625 |
| A | 2026-01-01 01:00:00 | 0 | 9 | 8 | 109.625 |
| A | 2026-01-01 01:00:00 | 1 | 9 | 8 | 1109.625 |
| A | 2026-01-01 02:00:00 | 0 | 9 | 0 | __P7_NULL__ |
| A | 2026-01-01 02:00:00 | 1 | 9 | 0 | __P7_NULL__ |
| B | 2026-01-01 00:00:00 | 0 | 9 | 8 | 22.0 |
| B | 2026-01-01 00:00:00 | 1 | 9 | 8 | 1022.0 |
| B | 2026-01-01 01:00:00 | 0 | 9 | 8 | 122.0 |
| B | 2026-01-01 01:00:00 | 1 | 9 | 8 | 1122.0 |
| B | 2026-01-01 02:00:00 | 0 | 9 | 0 | __P7_NULL__ |
| B | 2026-01-01 02:00:00 | 1 | 9 | 0 | __P7_NULL__ |
| Empty | 2026-01-01 00:00:00 | 0 | 0 | 0 | __P7_NULL__ |
| Empty | 2026-01-01 00:00:00 | 1 | 0 | 0 | __P7_NULL__ |
| Empty | 2026-01-01 01:00:00 | 0 | 0 | 0 | __P7_NULL__ |
| Empty | 2026-01-01 01:00:00 | 1 | 0 | 0 | __P7_NULL__ |
| Empty | 2026-01-01 02:00:00 | 0 | 0 | 0 | __P7_NULL__ |
| Empty | 2026-01-01 02:00:00 | 1 | 0 | 0 | __P7_NULL__ |

The independently used membership/value formula was: index=(((member*3+time)*5+y)*5+x); lat=20+y; lon=100+x; value=1000*member+100*time+10*y+x, except time=2 or (x=2,y=2) => NULL. A covers x/y=0..2 and B x/y=1..3, including boundaries.

## Live fixture, TLS, cache and frozen remote SQL

Live registered HTTP PID `4028835` and audit-proxy PID `4028868` were alive and non-zombie. Their `/proc/<pid>/cmdline` identified repository `scripts/setup-remote-fixtures.py __http_server` and `__s3_proxy`. HTTP PID socket ownership matched both listeners `127.0.0.1:45461` and `127.0.0.1:43595` through `/proc/<pid>/fd` and `/proc/net/tcp`; HTTP/control/HTTPS endpoints were literal loopback and credential-free.

Control: `http://127.0.0.1:45461/__control`; HTTPS fixture base: `https://127.0.0.1:43595`. An independent Python SSL connection verified the CA chain and IP hostname and negotiated TLSv1.3. Peer certificate DER SHA-256: `3e467e2118fa1fc775c078188aab3e30c507dfa3fae637ddbf4ed57f6993ee5b`. The server certificate file and CA PEM hash matched the run manifest. The HTTPS SQL additionally enabled server certificate verification.

Each baseline/region process used the following public setup; no S3 secret/setup was needed:

```sql
LOAD '/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.4/official/httpfs.duckdb_extension';
LOAD '/home/blizhan/repo/github/duckomo/build/official-matrix/v1.5.4/builds/abc08d813e3b65de88306b9834d96029ba2b35c964c3d287a84b52662f48896d/release/extension/duckomo/duckomo.duckdb_extension';
LOAD '/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension';
SET ca_cert_file='/home/blizhan/repo/github/duckomo/build/regions-remote/005-completion-20261013/tls/loopback.crt';
SET enable_server_cert_verification=true;
SET enable_external_file_cache=false;
SET enable_http_metadata_cache=false;
SET enable_object_cache=false;
```

Before each query, this verifier POSTed a JSON body `{"state_key":"<unique-state>","state":"normal","object":"ncep_gfswave025.om"}` to the loopback control endpoint and required HTTP 204. Query URI used `?state_key=<unique-state>&audit=<unique-audit>`; no signing data was present.

| Scan | Unique audit ID | Unique state key | Control status |
| --- | --- | --- | --- |
| baseline | p7-independent-ebc91df61fff4d0d9187e81d49d9a4ab | p7-independent-ebc91df61fff4d0d9187e81d49d9a4ab-normal | 204 |
| region | p7-independent-382eecbb4a764fa0bc7a45522c1d898e | p7-independent-382eecbb4a764fa0bc7a45522c1d898e-normal | 204 |

Only the single exact frozen local object URI was substituted with that controlled HTTPS URI. The frozen materialized baseline, original polygon WKB, all 15 variables, source index projection, deterministic ORDER BY ALL, and window bounds `[-1.2000000000000002,-1.2000000000000002,1.2000000000000002,1.2000000000000002]` remained unchanged. Both substitutions were checked to occur exactly once; neither frozen query nor executed scan included LIMIT.

| Scan | Frozen SQL SHA-256 (UTF-8 string) | Executed query SHA-256 |
| --- | --- | --- |
| baseline | 24dcf8e7cd17bc6d0db0d65f640f6ecc7a47080dd9ad488d101cae8e40f6a1ab | c8aade8c82594ee863433729774356a86df838013c785c8b6076e80846818b3f |
| region | f04ffbb177854c8eed31da111e8eaaa28e84acc5398b7db66bcb2e0e254c917c | 67c4e56a551e8e06c7c2abc845cfedb55822c34ab3badc2067d6d6154525a8e5 |

Exact executed remote SQL wrappers (public HTTPS addresses and audit IDs only):

### baseline

```sql
COPY (WITH window_rows AS MATERIALIZED (SELECT secondary_swell_wave_direction, secondary_swell_wave_height, secondary_swell_wave_period, swell_wave_direction, swell_wave_height, swell_wave_period, tertiary_swell_wave_direction, tertiary_swell_wave_height, tertiary_swell_wave_period, wave_direction, wave_height, wave_period, wind_wave_direction, wind_wave_height, wind_wave_period, lat, lon, om_source FROM read_om('https://127.0.0.1:43595/ncep_gfswave025.om?state_key=p7-independent-ebc91df61fff4d0d9187e81d49d9a4ab-normal&audit=p7-independent-ebc91df61fff4d0d9187e81d49d9a4ab', domain := 'ncep_gfswave025', include_source := true) WHERE lon BETWEEN -1.2000000000000002 AND 1.2000000000000002 AND lat BETWEEN -1.2000000000000002 AND 1.2000000000000002) SELECT "secondary_swell_wave_direction","secondary_swell_wave_height","secondary_swell_wave_period","swell_wave_direction","swell_wave_height","swell_wave_period","tertiary_swell_wave_direction","tertiary_swell_wave_height","tertiary_swell_wave_period","wave_direction","wave_height","wave_period","wind_wave_direction","wind_wave_height","wind_wave_period","lat","lon",'ncep_gfswave025' AS region_id,om_source.logical_index AS source_index FROM window_rows WHERE ST_Covers(ST_GeomFromWKB(from_hex('010300000001000000050000009a9999999999d9bf9a9999999999d9bf9a9999999999d93f9a9999999999d9bf9a9999999999d93f9a9999999999d93f9a9999999999d9bf9a9999999999d93f9a9999999999d9bf9a9999999999d9bf')),ST_Point(lon,lat)) ORDER BY ALL) TO '/tmp/duckomo-p7-independent-yo_u7zrb/remote-baseline.csv' (HEADER, FORMAT CSV, NULL '__P7_NULL__');
COPY (SELECT query_id,scan_id,metrics::JSON AS metrics FROM duckomo_last_scan_metrics() ORDER BY query_id,scan_id) TO '/tmp/duckomo-p7-independent-yo_u7zrb/remote-baseline-metrics.csv' (HEADER, FORMAT CSV, NULL '__P7_NULL__');
```

### region

```sql
COPY (SELECT "secondary_swell_wave_direction","secondary_swell_wave_height","secondary_swell_wave_period","swell_wave_direction","swell_wave_height","swell_wave_period","tertiary_swell_wave_direction","tertiary_swell_wave_height","tertiary_swell_wave_period","wave_direction","wave_height","wave_period","wind_wave_direction","wind_wave_height","wind_wave_period","lat","lon",region_id,om_source.logical_index AS source_index FROM (SELECT secondary_swell_wave_direction, secondary_swell_wave_height, secondary_swell_wave_period, swell_wave_direction, swell_wave_height, swell_wave_period, tertiary_swell_wave_direction, tertiary_swell_wave_height, tertiary_swell_wave_period, wave_direction, wave_height, wave_period, wind_wave_direction, wind_wave_height, wind_wave_period, lat, lon, region_id, om_source FROM read_om_regions('https://127.0.0.1:43595/ncep_gfswave025.om?state_key=p7-independent-382eecbb4a764fa0bc7a45522c1d898e-normal&audit=p7-independent-382eecbb4a764fa0bc7a45522c1d898e', regions := [{'id': 'ncep_gfswave025', 'wkb': from_hex('010300000001000000050000009a9999999999d9bf9a9999999999d9bf9a9999999999d93f9a9999999999d9bf9a9999999999d93f9a9999999999d93f9a9999999999d9bf9a9999999999d93f9a9999999999d9bf9a9999999999d9bf')}], region_crs := 'grid_lonlat', domain := 'ncep_gfswave025', include_source := true) WHERE lon BETWEEN -1.2000000000000002 AND 1.2000000000000002 AND lat BETWEEN -1.2000000000000002 AND 1.2000000000000002) AS region_rows ORDER BY ALL) TO '/tmp/duckomo-p7-independent-yo_u7zrb/remote-region.csv' (HEADER, FORMAT CSV, NULL '__P7_NULL__');
COPY (SELECT query_id,scan_id,metrics::JSON AS metrics FROM duckomo_last_scan_metrics() ORDER BY query_id,scan_id) TO '/tmp/duckomo-p7-independent-yo_u7zrb/remote-region-metrics.csv' (HEADER, FORMAT CSV, NULL '__P7_NULL__');
```

## Complete comparison, terminal metrics and isolated audit

Baseline and region returned **9 rows each**, **19 columns**, and identical full CSV bytes. Ordered-row mismatches=0; baseline EXCEPT ALL region count=0; reverse count=0. Complete duplicate rows=0 and NULL cells=0 in this selected remote data. Equality comparison retained every row and field rather than deduplicating or sampling. All **135 values** (15 variables × 9 source positions) matched independently hashed official reference exports at exact Float32 representation. Source positions and coordinates also matched the independent 721×1440 quarter-degree grid formula.

Source indices: `517679, 517680, 517681, 519119, 519120, 519121, 520559, 520560, 520561`. For each source index i: lat=-90+floor(i/1440)*0.25; lon=-180+(i%1440)*0.25; all nine points lie within the unchanged polygon.

| Metric | Baseline | Region | Result |
| --- | --- | --- | --- |
| Value data bytes | 77580 | 25860 | strict decrease |
| Value index bytes | 12015 | 4005 | strict decrease |
| Decoded chunks | 135 | 45 | strict decrease |
| Service body bytes | 150469 | 79671 | strict decrease |
| Started / complete requests | 1107 / 1107 | 775 / 775 | all paired |
| Unpaired / unsuccessful requests | 0 / 0 | 0 / 0 | pass |

Both metric records reported `schema_version=4`, `outcome.status=success`, `scan_complete=true`, `terminal_published=true`; `reads.value_totals.decode_complete=true` and each of the 15 variable decode-complete flags was true. Region reduced value bytes and decoded chunks by 66.6667%; service body bytes fell by 47.0516%.

Audit counted only events carrying this verifier’s unique audit IDs. Every request ID had exactly one started and one complete event; method/range agreed, status was 200/206, and each ranged GET’s body length equaled its inclusive requested byte count. Baseline: one HEAD/1106 GETs; region: one HEAD/774 GETs. No incomplete or unsuccessful request was excluded to create a savings claim.

| Audit evidence | SHA-256 |
| --- | --- |
| baseline canonical filtered JSONL | 8bc23856652fb5095d51550ec84439959a90cf75374cc8139c1e786747891072 |
| region canonical filtered JSONL | 086608920f7dc240ac0b82fc1ea8c2f3dfb8d8e082ad149b554e4e7d045186fc |

Concurrent service log snapshot: `/home/blizhan/repo/github/duckomo/build/regions-remote/005-completion-20261013/fixture/logs/http.jsonl`; exact observed prefix length `4432020751` bytes; prefix SHA-256 `0086c933610cefcba140aafce8d79f1cd67e78f8a03693395b2d2990d742c83c`. Log was streamed with bounded memory and only this run’s two IDs parsed into retained private events; unrelated raw traffic was never printed or archived.

## Result and metrics hashes

SHA-256 values below bind exact private CSV bytes, including headers and NULL markers. Baseline and region result files are each 1,335 bytes.

| Result/metrics file | Bytes | SHA-256 |
| --- | --- | --- |
| extensions.csv | 173 | 184103ac0abb60e1c25b0493a70e3efed96045d1ed8c5d973dde75a16439748c |
| local-combinations.csv | 769 | 2056fa4292e6305247ead640fd7f18748650982fb748a7f6d7aa0e6d91b07d0b |
| local-extra.csv | 8 | eb80997d461176f7ef39fa2b2c69d75ebb90b120aed16b4055856f0b2fa4fcca |
| local-missing.csv | 10 | 8e79eaad94b3830b5c4617d4b9a0501300704eda5e787133d0f98a11c8f803e3 |
| local-raw.csv | 5112 | f651e0856fae07d95a8ae8540ae257f76e7214f07bbdc50e7f70d1272eba3647 |
| local-stats.csv | 829 | 290ecc875db74f65d3d2e0ec673a21bdad8c72826197c8f986a806de0a107aca |
| probes.csv | 23 | 8b32a53e6e804faf3ac349aee1d06c8066b9435d9918446de8a428849df630a6 |
| projection-count.csv | 22 | 20bc2d61f7b2dffc271dcbbd56e3161f90e8ef538e86c0841475922116ed4268 |
| projection-metrics.csv | 9782 | c0d8e374489661804f4d374c2b259f21eb53cacc6b2774d515e718d3004556a8 |
| remote-baseline-metrics.csv | 22119 | e992191685f06abefb199799f611d7c1fb834d15d661a65a413d553da60b346f |
| remote-baseline.csv | 1335 | 12ccf18583770352b9efce66eb3a199b1a9914b5c395eec760e0c786fe2d7ea1 |
| remote-region-metrics.csv | 22783 | 99546397fe340a301b6506c85ad1c598b120c3ef853273643a55d3f5b0f3eb87 |
| remote-region.csv | 1335 | 12ccf18583770352b9efce66eb3a199b1a9914b5c395eec760e0c786fe2d7ea1 |
| signatures.csv | 181 | 29b8c87b4763bc14b40a36b6e2946ca88cdece3a0fc00ca999d3be36814fb0da |
| version.csv | 15 | a883610de40e19810d9b272889b002a47daa067daecccf7e4f5f712861a748e9 |

## Residual limits and honest gate status

P7 local quickstart: pass. P7 independent bounded HTTPS baseline/region comparison: pass. P0–P6 and other versions/transports/targets: not run by this verifier. Historical 2026-10-13 evidence and parent validation were not treated as current P7 proof.

- P7 is independent AI verification, not human acceptance.
- Scope is the frozen bounded ncep_gfswave025 window and synthetic A/B/Empty quickstart on v1.5.4; this execution does not sign P0–P6, the other four remote grids, other DuckDB versions, or signed S3 correctness/savings.
- full_domain_scanned=false; full_domain_savings_claimed=false. The documented repeated small-read/coalescing performance limitation remains.
- Fresh processes and all prescribed client cache settings establish the required client cold condition; server OS/page cache was not purged and no cold-server throughput claim is made.
- HTTPFS transport counters remain unknown/NULL where reported; service body_bytes measure bytes sent by this controlled fixture, not client network receipt.
- Remote selected data contains zero NULL cells and zero duplicate complete rows; exact multiset/file comparison preserved both semantics, while synthetic input explicitly exercised NULLs and overlap/source multiplicity.
- Live HTTP and audit-proxy process identity were verified. The underlying S3 service image digest/version declaration was not independently re-attested in this P7 HTTPS-only execution.
- Raw CSV, metrics, filtered audits, scripts, and private stderr remain only in mode-0700 /tmp scratch; the report records their hashes. Scratch persistence is not guaranteed. The service log continues growing; its reported hash binds the exact byte prefix, not later appended traffic.

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "Independent exact-artifact P7 executed: local 108 raw/12 stats/18 combinations; HTTPS 9/9 exact rows and 135 official values; successful terminal metrics and isolated paired requests; all three required costs strictly decreased."
    }
  ],
  "changedFiles": [
    "/home/blizhan/repo/github/duckomo/build/evidence/regions/005-artifact-binding-77b79d279b73/P7-independent.md"
  ],
  "testsAddedOrUpdated": [],
  "commandsRun": [
    {
      "command": "python3 /tmp/duckomo-p7-independent-yo_u7zrb/verify.py",
      "result": "passed",
      "summary": "exit 0; artifact/runtime/source identity, independent local quickstart, fresh HTTPS queries"
    },
    {
      "command": "python3 /tmp/duckomo-p7-independent-yo_u7zrb/finalize.py",
      "result": "passed",
      "summary": "exit 0; complete equality, official references, isolated request audit and strict cost savings"
    },
    {
      "command": "fresh official DuckDB mapped-identity process",
      "result": "passed",
      "summary": "exit 0; /proc maps confirms exact DuckOMO/HTTPFS/spatial files"
    },
    {
      "command": "git diff --cached --name-only",
      "result": "passed",
      "summary": "exit 0; no staged files"
    }
  ],
  "validationOutput": [
    "Local 108 rows; 12 nonempty stats; all 18 combinations; SQL multiset differences 0/0.",
    "Remote baseline/region 9/9 rows, 19 columns, exact byte equality; 135 official Float32 values matched.",
    "Value data bytes 77580 -> 25860; decoded chunks 135 -> 45; service body bytes 150469 -> 79671.",
    "Audit started/complete 1107/1107 and 775/775, zero unsuccessful/unpaired; both scans success/complete."
  ],
  "residualRisks": [
    "P7 is independent AI verification, not human acceptance.",
    "Scope is the frozen bounded ncep_gfswave025 window and synthetic A/B/Empty quickstart on v1.5.4; this execution does not sign P0\u2013P6, the other four remote grids, other DuckDB versions, or signed S3 correctness/savings.",
    "full_domain_scanned=false; full_domain_savings_claimed=false. The documented repeated small-read/coalescing performance limitation remains.",
    "Fresh processes and all prescribed client cache settings establish the required client cold condition; server OS/page cache was not purged and no cold-server throughput claim is made.",
    "HTTPFS transport counters remain unknown/NULL where reported; service body_bytes measure bytes sent by this controlled fixture, not client network receipt.",
    "Remote selected data contains zero NULL cells and zero duplicate complete rows; exact multiset/file comparison preserved both semantics, while synthetic input explicitly exercised NULLs and overlap/source multiplicity.",
    "Live HTTP and audit-proxy process identity were verified. The underlying S3 service image digest/version declaration was not independently re-attested in this P7 HTTPS-only execution.",
    "Raw CSV, metrics, filtered audits, scripts, and private stderr remain only in mode-0700 /tmp scratch; the report records their hashes. Scratch persistence is not guaranteed. The service log continues growing; its reported hash binds the exact byte prefix, not later appended traffic."
  ],
  "noStagedFiles": true,
  "diffSummary": "Only the bound P7 Markdown report was written in the repository; private scratch contains verifier scripts and execution outputs.",
  "reviewFindings": [
    "no blockers for the scoped P7 verification"
  ],
  "manualNotes": "Fresh AI verifier codex/gpt-6.1-sol; did not implement change. Exact current artifact/source digest checked independently. Existing uncommitted repository changes were left untouched. No parent P5 or historical P7 result was used as acceptance evidence."
}
```
