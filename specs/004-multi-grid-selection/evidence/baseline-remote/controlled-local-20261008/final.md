# Controlled local remote environment — 2026-10-08

Scope: local Docker S3 service, HTTP/HTTPS fixture server, and signed-S3 audit
proxy. This run does not pass complete G3–G6 or H6.

The S3 server is the existing SeaweedFS 4.48 arm64 image pinned by image ID
`sha256:8c63bce99de77850e9189e54a93fce1ac9ba189c4995be3fb3927bcd39ae3ab9`.
`service-identity.json` records Docker image inspection and `weed version`.
Only the S3 port is published, on loopback. Test credentials and the TLS key
are held in the Git-ignored private directory; none are included here.

## Passed smoke checks

- Local, HTTP, HTTPS, and signed S3 return the same six `raw.om` values.
- HTTPS verifies the generated certificate using its explicit CA file.
- HTTP, HTTPS, and S3 each report 324 response-body bytes, exactly matching
  the independent fixture/proxy audit. All scans complete successfully.
- The remote validator now reads the embedded `legacy_v3` snapshot from
  published metrics v4. The exported SQL snapshot matches both that embedded
  snapshot and the QueryEnd metrics v3 sidecar. Original v3 exports remain
  accepted. This fixes the initial `profile is not schema v3` rejection.

## Failed complete attempt

The subsequent G3 attempt completed identical local and HTTP full scans of
the fixed real OM sample. Its S3 scan failed after receiving 21,975,694 body
bytes. The audit proxy reported repeated 502 responses for
`bytes=1461893-1462450`. The upstream/proxy failure's root cause has not been
established. G3 remains **fail**; G4–G6 were not reached, and complete H6
remains **not-run**. A smoke pass is not remote-benefit or protocol acceptance.

The raw failed-run server log was reset by the later isolated smoke run.
The failed QueryEnd sidecar is retained here; the observed 502/range is
recorded as a diagnostic observation, not a retained full server-byte audit.
Full result exports and local service data stay under
`build/grid-controlled-remote/`; compact smoke results, metrics, audit logs,
and artifact hashes are retained here.

## Reproduction and service lifecycle

The local environment is available through
`build/grid-controlled-remote/fixtures/run.env`.

```sh
python3 build/grid-controlled-remote/smoke.py
python3 build/grid-controlled-remote/run-g3.py NEW_EMPTY_OUTPUT_NAME
```

`NEW_EMPTY_OUTPUT_NAME` must not name an existing run. The generated scripts,
credentials, and local service data are local artifacts outside the commit;
setup uses `scripts/setup-remote-fixtures.py` with the image/endpoint recorded
in `service-identity.json`. Fixture objects are the hash-checked local OM
copies, not converted inputs.

To stop the fixture HTTP/HTTPS server and audit proxy:

```sh
python3 scripts/setup-remote-fixtures.py --stop \
  --output build/grid-controlled-remote/fixtures
```

The S3 container name is recorded in `service-identity.json`; stop that
container separately after completing the remote checks.

## Subsequent S3 502 repair

The 502 root cause was reproduced and fixed after the earlier attempts above.
The fixture proxy opened a fresh upstream connection per Range request,
exhausting the Docker forwarder's non-loopback temporary ports. It now reuses
at most 16 upstream sockets, discards closed idle sockets before reuse, and
counts generated error bodies without appending a second response after
forwarded headers. Seven subprocess proxy regressions pass.

The final cold signed-S3 real-file scan completes 226,418 requests with no
proxy errors; its 88,261,923-byte result matches the local/HTTP baselines
exactly, and client/server body totals both equal 196,376,420 bytes. Full
compressed before/after audits, metrics, root-cause experiments, test output,
and source/executable hashes are in [502-repair](502-repair/README.md).

The complete harness now progresses beyond the real S3 scan but fails the
HTTP ignore-Range fault byte audit (481 server-sent bytes vs 1 client-profile
byte). Header-stage rejection precedes the companion HTTPFS body counter.
That separate failed-protocol cost gap remains unvalidated; G3 stays **fail**,
G4–G6 and full H6 stay **not-run**, and T049 remains open.
