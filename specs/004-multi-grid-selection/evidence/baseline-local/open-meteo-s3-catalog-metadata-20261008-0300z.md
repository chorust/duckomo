# Open-Meteo S3 catalog metadata sweep

**Captured**: 2026-10-08 03:00 UTC  
**Scope**: Anonymous, read-only root prefix listings and small catalog JSON reads. No weather `.om` object was downloaded; no conversion of another source format was considered.

## Results

- The bucket root listings returned 117 `data/` model prefixes and 80 `data_spatial/` prefixes; both listings were complete (`IsTruncated=false`).
- The scan attempted 277 metadata reads: 127 returned HTTP 200, 10 returned HTTP 404, and 140 failed with `URLError`.
- All 140 errors were `data_spatial/*/{latest,in-progress}.json` requests. The scan found no N160/N320 metadata-label hits in the responses it could read.
- The collector exited with status 2 because the metadata sweep was incomplete.
- The raw response summary is in [`audit.json`](open-meteo-s3-catalog-metadata-20261008/audit.json), SHA-256 `d4ea465c0d02eb7f511eaacfb15327f4a4040a34201a65bf692150676d2777e8`.

## Interpretation

This metadata sweep does not enumerate weather chunk keys and is not proof that a matching `.om` object exists or does not exist. The 140 failed requests also leave the spatial latest/in-progress metadata partially unread. Use the dated exact-prefix and object checks in [the live S3 audit](open-meteo-s3-live-audit-20261006-1717z.md) as the direct object-presence evidence: its 2026-10-08 follow-up found no existing Open-Meteo `.om` object or matching metadata for N160, full N320, or the 14,747-point N320 regional mapping; the candidate regional prefixes were empty. O320 and O1280 objects are different grid identities.

**Task effect**: T004 remains open. The sweep adds no qualifying N160/N320 `.om` sample and cannot complete the independent coordinate, regional point-order, or official-value references.
