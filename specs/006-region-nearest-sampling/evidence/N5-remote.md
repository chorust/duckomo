# N5 private-input real and controlled-transport validation

The owner authorized internal validation/private retention and authorized later access, with no public redistribution. See [private-use decision](N5-private-use-decision.md). The original GFS bytes remain untracked and were archived outside the repository under an owner-only content-addressed directory. A retrieval copy matches the frozen SHA-256; independent hardware backup is not claimed.

Current execution is registered in `scripts/sampling_remote.py`, invoked through `scripts/validate-sampling.py --gates N5`. Required inputs are the frozen real file and Dongcheng geometry, a private archive manifest, and a private loopback fixture environment. The module checks all identities before querying. The existing local SeaweedFS container image was independently inspected as `sha256:8c63bce99de77850e9189e54a93fce1ac9ba189c4995be3fb3927bcd39ae3ab9`, with its S3 port published only to 127.0.0.1. HTTP and HTTPS fixture servers and the S3 audit proxy also bind to loopback.

## Registered matrix

For each of DuckDB v1.5.4, v1.5.5 and v1.5.6:

- Real Dongcheng and the synthetic three-region/two-member/three-time fixture execute points and nearest on local, HTTP, CA-verified HTTPS and signed S3 sources.
- Points must return zero rows. Real nearest must return the frozen source `(lon,lat)=(116.5,40.0)`, original point index 749986 and geometry status ok. Synthetic nearest must retain all 18 results.
- Full sorted semantic rows, including original values, statuses, target/source coordinates, distance, axis indices, point/logical/grid/layout identity, match the corresponding local result and other runtime versions. URI-dependent object identity is retained in outputs but excluded from semantic equality.
- Real values are compared directly in SQL against the original `read_om` record in both EXCEPT ALL directions, not against a rounded reference presentation.
- HTTP denial must fail; restoring the fixture must allow the following valid read to succeed.

N5 records 60 commands, per-runtime CLI/extension/spatial/HTTPFS hashes, retained credential-free SQL, outputs, private archive/retrieval identity, verified service image and sanitized request logs. S3 credentials are supplied only through private process stdin; no credential-bearing setup SQL or raw error text is retained in the public-style evidence. Proxy environment variables are removed only for loopback fixture subprocesses. Service body counts remain server application evidence; HTTPFS network body is not claimed.

## Current package

The complete current N0–N5 run is `build/evidence/sampling/private-n0-n5-current/`. Consult `manifest.json` for terminal gate status, `inputs.json` for current source/build identities and the N5 gate object for command/output/query/log identities. This package supersedes historical subcheck-only evidence for the captured source snapshot. N6 is a separate audit of that finished package. N7 and overall Feature 006 acceptance are not implied.
