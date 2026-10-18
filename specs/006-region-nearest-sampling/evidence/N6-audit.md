# N6 current three-version evidence audit

**Result: pass for the registered N0–N5 scope.** The current-artifact validator completed N0–N5 with exit code 0; its separate N6 audit also completed with exit code 0. This does not close N7 or overall Feature 006 acceptance.

- Retained execution package: [private-n0-n5-final](private-n0-n5-final/final.md).
- Retained audit package: [private-n6-final](private-n6-final/final.md).
- Platform: Linux AArch64; DuckDB v1.5.4/v1.5.5/v1.5.6 with exact source commits and matching spatial/official HTTPFS packages.
- N0–N4: 59 recorded execution commands, including the v1.5.4 ASan/UBSan check.
- N5: 60 recorded queries across three versions, real/synthetic, local/HTTP/HTTPS/signed S3, direct source parity and denial/recovery.

## Audited invariants

The auditor verifies the inputs and command stream hashes; all three captured build identities against current source/artifacts; executed pass status for every N0–N5 gate; command exit codes, with explicit expected denial failures; every retained output and N5 SQL hash; complete N5 transport/query inventory per version; N5 extension identities; private archive retrieval evidence; and sanitized service-log hashes. Unknown HTTPFS network-body accounting is retained as unknown, not zero. Exact input/geometry identities, archive/authorization inventory and service image identity are recorded in the N5 result.

Negative tests in `test/tools/sampling_audit_test.py` reject absent/nonpassing gates, incomplete N5 inventory, extension identity mismatch, unverified archive retrieval, changed output, changed inputs and unsafe/public archive permissions. Existing evidence tests additionally cover stale source/replaced binaries and a source change during a build. Both test suites passed (2 audit tests containing multiple rejection scenarios and 15 evidence tests).

The N0 comparator requires exact result rows, logical work, data bytes/requests and decoded counts. Physical LUT index reuse and corresponding legacy physical totals may improve but must not exceed the frozen baseline. This documented scheduling boundary is not an allowance for changed semantics or higher read costs.

## Limitations

This is a source/contract-based audit of the registered finite scopes, not an exhaustive proof over every possible input. The private raw-object archive is outside the repository with owner-only permissions and a verified retrieval; an independent hardware backup is not established. The user provided authorization for internal validation/private retention and prohibited public distribution. Original GFS bytes, private setup SQL and credentials are absent from retained evidence. N7 remains separately partial as documented in N7-independent.md.
