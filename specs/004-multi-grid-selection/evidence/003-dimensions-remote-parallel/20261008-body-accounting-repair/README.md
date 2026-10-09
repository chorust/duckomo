# Failed HTTP response body accounting repair — 2026-10-08

## Root cause

Strict HTTPFS validation rejected invalid response headers before the content callback. Four rejected 120-byte bodies were sent by the controlled server but omitted from the received-byte profile. The baseline repro failed its audit with **1 client byte versus 481 server bytes**, while claiming complete accounting.

## Repair

The ABI2 companion and both ABI3 pair patches now defer small response rejections while counting and discarding actual callback bytes. Rejected bytes never enter the OM destination buffer. Retries retain their original budget and each attempt contributes its actual received bytes. Bodies over 64 KiB, malformed declared lengths, and unknown-length bodies crossing the streaming bound are stopped and mark accounting incomplete. No cost is inferred from Content-Length.

The prerelease CURL and httplib backends deliver error headers/content through the observer before buffering. Small bounded S3 XML errors remain available to native authentication, timeout, and region retry logic. The strict HEAD path rejects unsuccessful fresh HEAD results instead of falling back to GET. Native tests cover the two prerelease backends, partial reads, rejected-buffer integrity, all retry bodies, declared/unknown large 200/503 responses, and a real S3 RequestTimeout XML response followed by successful retry.

## Validator repairs found during verification

- Required JSON fields accept false and zero while still rejecting missing/null fields.
- The eviction query scans every time chunk of the pinned performance fixture, forcing both HTTP and S3 caches over 16 KiB.
- Replacement isolation compares physical/body costs with a fresh-cache run; query-local metadata hits remain valid. Recovery must reduce bytes.
- Sessions with intentional failures accept the shell's retained exit status of 1; the signed configuration session continues after expected errors.
- Secret attribute replacement accepts both keyword and assignment forms and skips quoted contents; it never prints credential values.
- Cancellation cleanup handles a shell that has already closed its input, avoiding SIGPIPE and preserving the QueryEnd sidecar.

## Scope and remaining gate

The diagnostic full run passed G3 source/result/body parity and G4 repeated local/HTTP/S3 performance. Its G5 run exposed the validator issues above. After their repairs, eviction, equal-length replacement, revocation/recovery, and invalid-secret denial passed. The controlled SeaweedFS environment did not reject the deliberately invalid signing region, so the full G5 configuration gate remains **fail**. Its profile reported success instead of the required failure; this is retained in `diagnostic-g5/`. Region-denial/cache isolation is not established by this run.

An independently executed diagnostic G6 suffix passed concurrent profiles, cancellation state/body reconciliation, and recovery after cancellation. It has `failed_protocol_query_profiles=0` because it deliberately runs only G6; it does not replace the combined G3–G6 gate. See `diagnostic-g6/`.

Final matched-build identities, native/SQL results, and CLI fault results are recorded alongside this file after verification. T049, full grid H6, and unfinished independent/Gaussian sample and release matrix work remain open. No domain support status is promoted. Large CSVs and credential SQL stay outside retained evidence.

## Final matched artifacts and verification

| Pair | Build ID | Original ignore-Range client/server | Paired native/SQL/load checks | Signed S3 raw client/server |
|---|---|---|---|---|
| `baseline-1.5.4` | `1bc27ad01fdc1af3775975db8c5c19595e8323957bf04b059a706b3fbe33853b` | 481/481, expected query failure | 11 command exits 0 | 324/324, query exit 0 |
| `prerelease-2.0-dev` | `0f7c8170eee3c93906f3a0e6352890736b19b08f2b12941779d003555ab9724a` | 481/481, expected query failure | 11 command exits 0 | 324/324, query exit 0 |

`e2e-final.json` records 18 failed CLI protocol queries and two successful signed-S3 queries. All received/sent byte audits match; the two S3 result hashes are identical. Each pair also passed the range/ABI/zero-I/O/source/version native tests, four SQLLogicTests, and paired capability/source-id load. The prerelease Range test exercises CURL and httplib, including bounded error bodies and the S3 XML retry. Matrix unit tests: 9 pass; proxy regressions: 7 pass; `git diff --check`: pass. Mutable matrix results resolve to the same final build identities.

Final baseline artifacts additionally passed the isolated G6 runner (native exit 0). Their isolated G5 runner still exits 1 at invalid signing-region rejection, after the preceding cache, replacement, revocation/recovery, and secret-change checks passed. These final runs are retained in `g6-final-matched-baseline/` and `g5-final-matched-baseline/`. The earlier G3/G4 full-run evidence under `diagnostic-before-eviction-fix/` used an earlier build with the same repaired baseline HTTPFS implementation; it is diagnostic scope, not a new full combined acceptance run.
