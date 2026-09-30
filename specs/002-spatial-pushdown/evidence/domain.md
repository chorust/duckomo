# ncep_gfswave025 full-domain reference

Date: 2026-09-29
Environment: Linux AArch64, release DuckDB v1.5.4. This is correctness evidence for the real local sample. Linux x86_64 is deferred from the current scope; see [final status](final.md).

Pinned sample: `https://openmeteo.s3.amazonaws.com/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om`, 5,812,040 bytes, SHA-256 `0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd`. The source definition is pinned to open-meteo commit `34b9cea169395be9b4686f2b5b23eca26dfef7a2`. The local identity and all official-value reference hashes are in [domain-manifest.json](../../../test/data/domain-manifest.json).

The pinned Open-Meteo commit is the traceable grid-definition source; this record does not claim that the deployed producer which created the sample used that exact commit.

Command:

```sh
build/release/test/native/domain_reference_test \
  build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om \
  test/data/domain-manifest.json build/evidence/spatial/reference
```

Exit code: 0. The test verified the sample byte size and SHA-256, the fixed manifest identity, and each of the 15 official-reader CSV hashes before querying. It read the complete explicit grid (`ny=721`, `nx=1440`, `[lat,lon]`, origin `[-90,-180]`, step `[0.25,0.25]`) and compared 1,038,240 ordered logical positions. Every latitude/longitude matched the independent scalar formula within the predeclared absolute tolerance `1e-9` degrees. All 15 Float32 values matched the official C-reader references exactly; every reference NaN matched a SQL NULL at the same logical position. No rows were dropped or added.

The comparison helper computes coordinates directly from integer row/column arithmetic and does not call `RegularGrid` or `SpatialLayout`. It first validated the candidate mapping with explicit parameters; after that run passed, the same check was repeated using the registered `ncep_gfswave025` domain. Both modes matched the same 15 references at every position. This evidence validates the published mapping and does not measure spatial-filter savings.
