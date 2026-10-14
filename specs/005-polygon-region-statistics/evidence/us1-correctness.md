# US1 real-target correctness

Status: **P1 pass** on the current Linux AArch64 v1.5.4 build. The validator completed its synthetic membership checks and all five frozen real targets with independent coordinate references and fixed official OM C API value references.

| Target | Independent points | Expected/output pairs | Exact geometry matches | Result SHA-256 |
|---|---:|---:|---:|---|
| `ncep_gfswave025` | 9 | 9 | 9 | `e8e84729f9d4abd725daac60d6a7964e25c6889558a9fc0dea359a78a62b4c33` |
| `gem_rdps_10km` | 4 | 456 | 456 | `0ad3241886ad196f4f722f85198839753316fedd812c809a6e35cbb84146ff40` |
| `gem_regional` | 11 | 1,254 | 1,254 | `ce94f5916b92ca66834b27d03f532e7dc187136d5b21641b4820cd03ab036e89` |
| `aladin_central_europe_2km` | 6 | 720 | 720 | `a2f3059e22be85ae2b937b622d0bc404a6c131b507aad4cf5896a642accbff6c` |
| `ecmwf_ifs` O1280 | 3 | 3 | 3 | `7c4f94a8e6146aa1bc4fa4619f09d9e92a5029368931b0e644109d1469d35a75` |

All five output position multisets equal the independently selected positions, and their values equal the frozen official Float32 references, including NaN-to-NULL behavior. The three projected-grid output coordinates are within the frozen 0.0001-degree reference tolerance; membership was selected from the unexpanded frozen WKB bounds. Each region scan reports `match_count_complete=true` with the expected exact match count.

The teaching fixture contributes 438 region-record pairs and zero bidirectional SQL multiset differences. P0 dependency/binding checks and P2's 60 statistic groups also passed in the same validator run; the P2 statistics oracle SHA-256 is `ec9b46a79a521b5951087a1b4b6df55df8a0ec3f65c744ba2f776bf9d02b3321`.

The complete run is `build/evidence/region-validator-p1-scnQVfYX/`. Its top-level manifest SHA-256 is `ffe9a9a9b11e4ce483fd183822d8eeb8fd8c6fa71297d0f075b1136dfa90a62e`; the real-target report SHA-256 is `365cf47b758621cc17d469e4f3197cc07adc41cbf5aa5a7212da308b72d07eb1`. The report records each exact command, exit code, object/reference/WKB hashes, query metrics, result/oracle hashes, and per-target comparison result. Bulk outputs and per-scan metrics are retained in `build/evidence/region-validator-p1-scnQVfYX/P1-real/`.

The run covers P0–P2 on v1.5.4 only. It does not establish P3/P4 resource and lifecycle gates, P5 remote savings, P6 three-version coverage, or P7 independent reproduction.
