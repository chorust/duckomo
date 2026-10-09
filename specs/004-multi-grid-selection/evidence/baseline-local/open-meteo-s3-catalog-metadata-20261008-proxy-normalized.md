# Open-Meteo S3 catalog metadata sweep (proxy-normalized)

**Captured**: 2026-10-08 03:07 UTC  
**Scope**: Anonymous, read-only S3 root listings and small catalog JSON reads. No weather `.om` chunk was downloaded, and no other source format was converted.

## Reproduction

```sh
python3 scripts/audit-openmeteo-s3-catalog.py --workers 4 \
  --output-dir specs/004-multi-grid-selection/evidence/baseline-local/open-meteo-s3-catalog-metadata-20261008-proxy-normalized
```

The scanner removes a trailing `/` from proxy URLs in the process-local proxy map and records only the affected proxy scheme names, never proxy values. Scanner SHA-256: `d343409cfd2261286921adb7bd80f77422d8d6540607a3a6e23b01191e82e3b6`.

## Results

- Both root listings were complete: 117 `data/` model prefixes and 80 `data_spatial/` prefixes; neither was truncated.
- All 277 catalog metadata requests completed: 267 HTTP 200 and 10 HTTP 404, with zero network errors.
- No N160 or N320 grid-label metadata hits were found. Readable ECMWF metadata identified O320 and O1280 labels, which are distinct grids.
- Four exact European ensemble candidate prefixes all returned zero immediate objects and zero child prefixes: `data/` and `data_spatial/` for `ecmwf_aifs_europe_ensemble` and `ecmwf_aifs_europe_ensemble_mean`.
- The raw audit is [`audit.json`](open-meteo-s3-catalog-metadata-20261008-proxy-normalized/audit.json), SHA-256 `19ed7e0b2d4c4a337d722ad778687039f02bd5be862fb778d300be7b1a074214`.

## Interpretation

This is a complete catalog metadata sweep, not a recursive enumeration of every weather chunk in every model prefix. It found no metadata evidence for N160/N320 and the exact regional ensemble candidate prefixes are empty. Combined with [the live S3 object audit](open-meteo-s3-live-audit-20261006-1717z.md), which checked the relevant ECMWF/ERA5 object prefixes and found no N160, full N320, or N320-region `.om` object, the current Open-Meteo `.om` catalog does not provide the required Gaussian samples. O320/O1280 objects cannot substitute for them.

**Task effect**: T004 remains open because the required N160/N320/N320-region `.om` objects and independent point/value references are still absent.
