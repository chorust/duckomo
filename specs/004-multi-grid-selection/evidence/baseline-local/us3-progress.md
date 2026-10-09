# US3 progress: access configuration partitioning

Date: 2026-10-06

## Implemented

- Each remote bind resolves the path-matching DuckDB secret and current S3 authorization settings used by the supported HTTPFS configuration path.
- A fresh 256-bit salt from the Linux kernel CSPRNG for each connection keys an HMAC-SHA256 over length-delimited configuration fields to produce an opaque access fingerprint. The raw secret values are not retained in fingerprint state or emitted to diagnostics, metrics, or grid metadata. The fingerprint covers effective S3 credentials, endpoint, region, URL/SSL policy, requester-pays, version-pinning and compatibility settings, plus applicable HTTP credentials/proxy/headers and the URI query.
- The range cache removes entries for the same redacted object path when their access partition differs from the current fingerprint. Different objects keep their own cache entries.
- Every worker open recomputes the fingerprint and compares it with the bound session. A changed configuration rejects the open and requires a fresh bind. The existing HTTPFS open path still performs its fresh authorization/object probe and identity checks.
- The remote fixture setup tool accepts the grid manifest, verifies every acquired real OM v3 local copy before upload, streams large S3 fixture uploads, starts matching HTTP and HTTPS fixture endpoints, and writes per-request HTTP/HTTPS/S3 audit logs. It requires the upload bucket to match an explicitly authorized bucket, records caller-supplied S3 service version/digest as unverified claims, and creates credential SQL and run.env with mode `0600` from file creation.
- The v4 metrics path no longer retains a query-wide set of completed transport attempt IDs. Each remote handle now keeps only active attempt body counters (capped at 256); duplicate terminal callbacks are dropped and make transport evidence incomplete, and body callbacks for released attempts fail closed.
- The pinned baseline `0001-om-range-session.patch` now uses a single active attempt record, monotonically increasing attempt numbers, and immediate terminalization/release across retry, cancellation, and request failure paths. `git apply --check` passed against the pinned `third_party/duckdb-httpfs` checkout at `c3f215ab360f04dc3d3d5305fa81849c0121f111`; the manifest SHA-256 matches the updated patch. This is source-level patch applicability only; no httpfs stage/build or retry/cancellation regression was run.

## Not verified

- T043 fingerprint-change, partition invalidation, fresh authorization, and ABI3 mismatch tests have not been added or run.
- Signed S3 revocation/recovery and secret/endpoint/region switching have not been exercised against an authorized test bucket.
- The fixture setup script was not run; no S3 bucket was written and no controlled HTTP/HTTPS service was started. The supplied S3 service version/digest and TLS setup have not been independently checked against a live service.
- ABI3 provider pairing, 003 remote gates, and physical retry/body audit remain open; this implementation does not close those gates.
- T047 remains unchecked: the metrics history set, remote active-attempt consumer state, and baseline patch's per-request attempt storage have been bounded, but retry/cancellation regression coverage and a staged httpfs build are still missing. Late/duplicate behavior therefore has not been validated end to end.
- As of 2026-10-06 no build or test command had been run for the then-current changes. That historical status is updated only by the dated local evidence below, not by a remote support claim.

## 2026-10-08 local H5 refresh

- `reader_capacity_test` now also allocates a 64-byte `OmByteBuffer`, requests an impossible `size_t`-maximum replacement, catches allocation failure, and asserts the prior pointer, capacity, and size have already been released. `cmake --build build/grid-matrix/baseline-1.5.4/builds/15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8/release --target reader_capacity_test -j 2` exited 0; running the corresponding `release/test/native/reader_capacity_test` exited 0 (`reader capacity bounds passed`). This failure-path assertion detects retaining the old allocation when requesting its replacement; it does not measure allocator metadata or process RSS.
- The fresh H5 runner record is [`h5-local-refresh-20261008-t054-closed`](h5-local-refresh-20261008-t054-closed/). All five synthetic local checks passed and all 14 recorded commands exited 0; the evidence manifest audit passed. The full runner exited 2 with H5 `not-run`, and its manifest records no HTTP, HTTPS, or S3 inputs. Its reason now identifies the controlled baseline-remote inputs and server audit required by T055.
- The four work-peak comparisons used identical chunk shapes, grew spatial positions from 65,536 to 1,048,576 for the projected fixtures and from 256 to 3,968 for reduced Gaussian, and preserved result rows (1 for projected, 3 for reduced Gaussian). Work-peak ratios were 1.000223 for rotated, Lambert, and stereographic, and 1.000214 for reduced Gaussian.
- The separate [`H6 loopback stress evidence`](h6-loopback-protocol-final-20261008/h6-local/attempt-stress.json) records six synthetic Gaussian HTTP scans, 3,462 attempts total, 3,879,498 response-body bytes, query-owned peak 1,800,918 bytes against 2,584,092, and stable transport-control peak 38,404 bytes against 38,800. Its cancelled-query assertion confirms query-owned accounts are released at QueryEnd.

T054's implementation and local synthetic validation are complete: the H5 local runner executes the buffer replacement check, and the loopback stress plus interrupted-query case verify attempt bounds and QueryEnd account release. The complete H5 gate remains `not-run`; this does not provide controlled baseline-remote service evidence or server JSONL. T055 remains open for that service-backed H5/H6 run, and the producer-backed Gaussian and 003 prerequisites remain missing. No complete H5, H6, or remote support claim is made.
