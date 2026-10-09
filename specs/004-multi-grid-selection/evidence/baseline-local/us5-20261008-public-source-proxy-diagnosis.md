# Public Open-Meteo OM remote source proxy diagnosis

**Recorded**: 2026-10-08 02:55 UTC  
**Scope**: Existing hash-pinned Open-Meteo `.om` objects only; no format conversion.

## Cause of the earlier strict-range initialization error

The baseline DuckDB CLI reported its `http_proxy` setting as `http://127.0.0.1:7890/`. A GDB `catch throw` backtrace for the failed remote `read_om` bind stopped in `duckdb::HTTPUtil::ParseHTTPProxyHost`, called by `HTTPParams::Initialize`, before HTTPFS issued a request. The parser removes the `http://` prefix and parses the remaining host and port; the trailing `/` made the port invalid. This explains the earlier generic DuckOMO error for both HTTPS and `s3://` without implicating the Open-Meteo endpoint or strict Range support.

Two minimal metadata/range open attempts then succeeded: one with all proxy environment variables removed, and one with `http_proxy` set to `http://127.0.0.1:7890` (no trailing slash). The public HTTPS endpoint's independent 206 check remains separately recorded in [the endpoint audit](us5-20261008-public-remote-source-attempts-5dac993/remote-endpoint-range-check.json).

## Full public source/info subchecks

The baseline build `5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d` completed the same validator through HTTPS and S3 after setting `HTTP_PROXY=http://127.0.0.1:7890`; S3 used region `us-west-2`. Both commands exited 0:

- [HTTPS run](us5-20261008-public-remote-source-corrected-proxy-5dac993-https/manifest.json)
- [S3 run](us5-20261008-public-remote-source-corrected-proxy-5dac993-s3/manifest.json) · raw validator JSON: [runner.stdout.json](us5-20261008-public-remote-source-corrected-proxy-5dac993-s3/runner.stdout.json)

The validator streamed all 2,843,101 spatial positions at time-axis index 0 from three existing projected OM v3 samples and compared them row-by-row with the local hash-pinned copies. HTTPS and S3 explicit/domain source positions match; source and `om_grid_info` value index/data/decode reads are zero. The closed 0.2° spatial subsets contain 15, 12, and 247 positions, respectively, and match the materialized full-plane baselines. URI-specific `object_id` values are retained separately; no cross-URI identity equality is required. Both remote object identities remain `content_verified=false` and `version_strength=unverifiable`.

The saved v4 metrics were also parsed: all 33 HTTPS and all 33 S3 source/info/query snapshots report zero value index/data bytes, requests, and decoded chunks.

The validator uses synthetic labels only to select time-axis index 0. These runs do not establish the OM axis-to-producer point-order mapping, compare weather values, or reconcile remote response body totals with server logs. They cover rotated GEM, regional GEM, and ALADIN projected samples only. Gaussian N160, full N320, and N320-region `.om` samples remain absent; complete H1/H2/H6/H7 and T070 therefore remain incomplete. O320/O1280 and converted non-OM formats were not used.
