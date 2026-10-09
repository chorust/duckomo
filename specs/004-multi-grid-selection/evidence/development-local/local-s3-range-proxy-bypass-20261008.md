# Local S3 Range proxy diagnosis — 2026-10-08

Scope: transport smoke only. The data object was the pinned Open-Meteo OM v3 static field `ecmwf_ifs_static_hsurf_reduced_gaussian_o1280`; it remains supplemental and this check does not establish O1280 grid mapping, producer point order, or spatial-read benefit.

The existing object was uploaded unchanged from its manifest-pinned local copy into a loopback SeaweedFS S3 fixture. The service image was pinned to `sha256:aacd79fc54acd838dc904a84d3107355ca2b11c8ad502a0a2e01550431158c1f`; its caller-reported source commit was `56fbc3deb5aa832c6ed38854b0c31ec4c8acad7f`. The object hash and byte count were rechecked by `setup-remote-fixtures.py` before upload. No credentials were written to this evidence.

Saved artifacts:

- [`run-manifest.json`](local-s3-range-proxy-bypass-20261008/run-manifest.json), SHA-256 `61e923bfbf343f24e9269fd16511b7a41f36f2bf934dbeb6489fc484a2525b0a`. The fixture records the service identity as caller-supplied and not independently verified.
- [`s3-range-audit.jsonl`](local-s3-range-proxy-bypass-20261008/s3-range-audit.jsonl), SHA-256 `4b6360884ce625f86998daf38005be3ce65312244703a0f391c85e68060ee0dc`. It contains only the 89 requests for this object, with no authorization headers or credentials.

Using the matching baseline 1.5.4 DuckDB, HTTPFS, and DuckOMO artifacts, this query returned one row with `value = -999.0` and exit code 0:

```sh
. build/grid-remote-fixtures/run.env
PAIR=build/grid-matrix/baseline-1.5.4/builds/5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d/release
SMOKE_SQL="LOAD '$DUCKOMO_HTTPFS'; LOAD '$PAIR/extension/duckomo/duckomo.duckdb_extension'; SELECT * FROM read_om('$DUCKOMO_S3_BASE/grids/ecmwf_ifs_static_hsurf_reduced_gaussian_o1280.om') LIMIT 1;"
env -u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY -u http_proxy -u https_proxy -u all_proxy \
  "$PAIR/duckdb" -unsigned -init "$DUCKOMO_S3_SETUP" -c "$SMOKE_SQL"
```

With inherited `HTTP_PROXY`/`HTTPS_PROXY`/`ALL_PROXY`, a loopback S3 query failed to establish a strict Range session before the local S3 audit proxy received a request. Setting only `NO_PROXY=127.0.0.1,localhost,::1` did not change that failure. Unsetting the proxy variables for the gate process made the query succeed. The successful proxy audit recorded 89 requests for this single-row query, all successful ranged responses; response bodies totaled 13,757 bytes. This is a narrow smoke result, not H5/H6 evidence: it did not compare all values across local/HTTP/HTTPS/S3, consume the object fully, or demonstrate a strict reduction against a local baseline.

The harness now scopes proxy-variable removal to fixture setups whose generated `run-manifest.json` declares loopback HTTP and S3 audit binds. External fixture endpoints retain the caller's proxy environment.
