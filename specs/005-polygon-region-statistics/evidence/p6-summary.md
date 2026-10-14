# 005 P6 version-matrix audit

**Result: `not-run` (2026-10-12).** P6 verified all three pinned runtime identities, rechecked the frozen inputs, build/runtime artifacts, P0–P4 command outputs, and structured resource reports. It reported no hash, source-identity, or evidence-integrity errors. Each version has passing local P0–P4 evidence; all three P5 gates are recorded as `not-run`, so P6 cannot pass until the controlled HTTP/HTTPS/S3 fixture run is complete.

| Version | Source identity | Adapter probes | extension version / install path | P0–P4 | P5 | spatial SHA-256 |
|---|---|---|---|---|---|---|
| v1.5.4 | verified | 5/5 pass | `28db190` / `""` | pass | not-run | `af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a` |
| v1.5.5 | verified | 5/5 pass | `eb1e57c` / `""` | pass | not-run | `b9129a7da7fde8eb7fb951d99a115cbf2e6fa34c8d41671086169174a1f5725d` |
| v1.5.6 | verified | 5/5 pass | `04270fe` / `""` | pass | not-run | `96b8d4b5d735e9ebe91da7badee4328dfbd07068ce4885d1d9b6de8c22370b14` |

All three DuckOMO builds used source snapshot SHA-256 `7713f740908a2ab595f64b127733eefe698469acf5033869dcaecc015686c05e`. Each build completed all 16 ASan/UBSan checks. Ordinary SQL/native/Python regression passed in each build, with `domain_reference_test` marked `not-run` because its two environment inputs were unavailable.

P6 machine report: `build/evidence/regions/p6-audit-20261012-final-r2/P6-summary.json` (SHA-256 `a4812f3bdc619228ae647682ce41baec98755f6a94a0ac225bd1d4af947f2d6d`). The enclosing P6 run manifest SHA-256 is `9bf5dfb476565a844399ff36a9259df2644b4df4f3962c9db124e5e310733d12`. The P0–P4 and P5 raw records and their hashes are summarized in [the version matrix](version-matrix.md).

P6 does not sign P7. Independent reproduction remains pending.
