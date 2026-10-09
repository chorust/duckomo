# S3 fixture proxy 502 repair — 2026-10-08

## Cause and reproduction

The original proxy opened a fresh upstream TCP connection for every downstream
Range request. Docker's userland forwarder opened a corresponding non-loopback
connection to the SeaweedFS container. Those sockets could not use the host's
loopback-only TIME_WAIT reuse (`net.ipv4.tcp_tw_reuse=2`). The available local
port range was 32768–60999 (28,232 ports).

A real signed-S3 OM scan reproduced the failure in 18.97 seconds after 28,264
requests; the safe proxy diagnostics reported `ConnectionResetError`, errno 104.
At the failure the container destination had 28,208 TIME_WAIT sockets. Its
container stayed running.

The isolated TCP experiment exercised the same Docker-published endpoint:
30,000 unsigned HEAD requests completed through one reused connection, while
new connections failed after 28,207 completed requests. HTTP 403 in this
experiment was expected: it tests TCP lifecycle, not authentication. The actual
signed-S3 scan below tests authorization and result parity.

## Repair and verification

`scripts/setup-remote-fixtures.py` now shares at most 16 upstream connections
across downstream clients and returns a socket to the pool only after the
response is consumed. Failed or closing sockets are discarded. Each request
retains its own Host and authorization headers. No proxy-level retry is added.
Generated 502 bodies are counted; a failed response whose headers were already
forwarded is closed without appending a second HTTP response. Diagnostic events
contain only exception class and errno, without exception text or auth headers.
Before reuse, the proxy detects a readable/closed idle socket and discards it
without sending the next request into a closed connection.

The seven subprocess integration regressions exercise separate downstream
connections with a finite upstream connection budget, concurrent clients,
GET/HEAD, reset/recovery, upstream close (both explicit and idle), and truncated responses. They reproduce
the connection exhaustion and accounting failures with the old implementation
and pass after the repair. See `regression.txt` and run:

```sh
python3 test/tools/remote_fixture_proxy_test.py
```

The repaired full scan consumed the hash-pinned 5,812,040-byte `real.om` object
through signed S3 with the extension cache disabled. All 227,178 requests passed.
The 88,261,923-byte CSV matches the successful local baseline exactly:
`9dd4deaeb523f00ea9416d85e537b2b5fa3f040b4936e88533442a385f07ab40`.
Client and full proxy audit both report 196,429,012 response-body bytes; scan
completion and transport accounting are complete. Full before/after server
logs are preserved in the ignored `build/community-preparation/audit/baseline-remote/controlled-local-20261008/502-repair/` directory as gzip files, with paths and hashes in the parent manifest. Executable and source hashes are in
`identities-initial.json`; the final source/validator hashes are in
`identities.json`. Result CSVs stay in ignored `build/grid-controlled-remote/`.

The first full harness run used the initial connection-pool revision. Its S3
result still matched the local baseline, but two idle-closed sockets produced
retryable 502 responses. The server audit included 54 bytes of generated error
bodies that the client profile omitted, so G3 correctly failed reconciliation.
That run is preserved as `initial-full-failure.*`; it is not acceptance evidence.
The idle-close regression failed on that revision and passes with the final
readability check. The full remote validation harness is rerun on the final
proxy separately. A repaired real-file
scan alone does not establish all G3–G6 or grid H6 acceptance conditions.

## Final proxy harness run

`python3 build/grid-controlled-remote/run-g3.py g3-fixed-final` used the final
proxy revision recorded in `identities.json`. The actual native exit was 1
(the local wrapper returns 0 and prints the native exit).

The complete real signed-S3 scan passed: 226,418 requests, zero proxy errors,
196,376,420 body bytes in both the client and server audit, and complete scan
and transport accounting. Its full CSV is identical to the local and HTTP
CSVs (the same hash recorded above). Different thread limits between the
initial standalone scan and the harness affect request/byte counts, not parity.
See `real-s3-final.*` for the retained final metrics and full audit.

All local/HTTP/S3 full and restricted performance-fixture queries passed before
the harness reached HTTP fault tests. It correctly rejected 403, 404, and no-HEAD
and passed each recovery check. The ignore-Range fault then failed the byte
audit: the server logged 481 sent body bytes, while the client profile counted
1 received body byte and claimed complete transport accounting.

The companion HTTPFS response callback rejects the invalid 200 response before
its body callback runs. Thus four 120-byte error bodies are logged as sent by
the fixture but absent from the client cost profile. This is a separate
failed-protocol accounting/validation gap; adding Content-Length to metrics
would not establish actual received bytes. The failing metrics and full audit
are retained as `remaining-ignore-range.*`.

Complete G3 stays **fail**, G4–G6 and full grid H6 stay **not-run**. This repair
does not complete spec 004 or promote any domain's remote validation status.
