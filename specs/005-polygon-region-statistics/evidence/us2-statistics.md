# US2 statistics evidence

Status: **P2 passed for the frozen synthetic fixture** on DuckDB v1.5.4 / Linux AArch64.

`build/evidence/region-validator-YPVK1J/manifest.json` records `statistics_groups=60`, `statistics_mismatches=0`, and the statistics-oracle SHA-256 `ec9b46a79a521b5951087a1b4b6df55df8a0ec3f65c744ba2f776bf9d02b3321`. That hash matches `expected_group_statistics_sha256` in `test/data/regions/manifest.json` (manifest SHA-256 `ca1fb9682b9fcae9278d8e1516822e46cfbb88b3d591ec9a2b1d59ab8f5f3e9d`). The SQL test compares every region/time/member combination, including empty groups, exact counts and extrema, the frozen sum/mean tolerance, and NULL behavior.

For A/member 0/time 00:00, the test returns sample/valid/sum/mean/min/max `9/8/77/9.625/0/21`; time 02:00 returns `9/0/NULL/NULL`. Explicitly completed groups preserve `0/0/NULL` for Empty, Outside, and Gap. Separate SQL checks retain NaN-to-NULL and host ±Inf aggregation behavior, and repeated time labels remain distinguishable by source position before SQL grouping.

The quickstart SQL was executed in the rebuilt DuckDB v1.5.4 shell with the matching spatial package loaded by path. It produced 108 raw rows, 18 explicit region/time/member groups, and zero rows in each direction of the full-baseline `EXCEPT ALL` comparison. Captured SQL and output are in `build/evidence/regions/local-v1.5.4-9D2zwEip/quickstart/`; output SHA-256 is `a10f34c6d17094f22833b7bd0394c949338bad8f6424b217c6cd3260cacddee8`, SQL SHA-256 is `681892a0c6ff4012c766652efb5467f32051efcb821bbd21fac65e6ec0ed0688`.

This evidence covers the synthetic P2 acceptance. It does not claim real-target, remote, resource, lifecycle, version-matrix, or independent-review gates.
