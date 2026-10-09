# Independent projected-grid reference revalidation

**Recorded:** 2026-10-07

**Scope:** Re-run the independent mathematical coordinate generator and the fixed official OM C full-array decoder for the three currently available projected producer samples, using the current `sample-manifest.json`. This refreshes reference provenance only; it does not establish DuckOMO coordinate/value agreement, producer-axis mapping, spatial-read benefit, or a complete H0/H1 gate.

The prior active reference manifests and import record were copied unchanged to [the pre-revalidation archive](reference-manifests-pre-revalidation-20261007/). The new exact commands, inputs, and artifact hashes are in [reference-revalidated-20261007.json](reference-revalidated-20261007.json). The current sample-manifest SHA-256 is `72af60d4769c2fc7b262864f1fb8e4f434e8a89ec463824f482c39103dc1939f`.

| Target | Sample | Coordinate rows | Independent coordinate CSV SHA-256 | Official decoded values | Value CSV SHA-256 |
|---|---|---:|---|---:|---|
| `rotated_v3` | `rotated_cmc_gem_rdps_10km_cape_chunk_4337` | 1,191,300 | `8d05866767e58173b20f6dd588b3f8766e81a8e85b9aa24b09fe17e2a496c57f` | 135,808,200 rows; 40,819,713 nulls | `cdcd6650b7d9acb6695886f7359ae2c8453e6b9ad972402a0699f35297be7f85` |
| `lambert_v3` | `lambert_chmi_aladin_central_europe_2km_cape_chunk_4135` | 881,361 | `dbc66a87803bb25e1a299ef63c8a0784f5006cd1999f953537b4fb81a0cf0762` | 105,763,320 rows; 0 nulls | `7fc25500b7c6387f2408caf04322a2955f15f89cd401700016f26c4d8355e4dd` |
| `stereographic_v3` | `stereographic_cmc_gem_rdps_cape_chunk_4337` | 770,440 | `90f3cad43a1ef85e5e1c58c40b3a51c1b24fad801c4c0f818211377a591e7f62` | 87,830,160 rows; 62,643,412 nulls | `ea70e1d945d885e2cc20159afb76db9e5a0c79effe45be2d50ddf018903ba467` |

All six regenerated CSV hashes equal the previously frozen outputs. The official decoder executable SHA-256 is `382588fa219cc154a7a372a8a523faa9d301676a5c7782e307147d17a92df926`; the official OM C source commit remains `d8855e418e2231ae8439f0c7e840fa3f93b371e3`.

The refreshed coordinate and value manifests now reference the current sample-manifest hash and remain `partial_not_run` for the three required Gaussian targets. `region-reference.json` remains empty because no independent GRIB or production-archive local point-order record is available. N160, full N320, and N320-region objects and their independent coordinate/value references remain missing.

## Official Gaussian source candidates checked

A read-only source lookup on 2026-10-07 found ECMWF documentation identifying ERA5 EDA as N160 and ERA5 HRES as N320 on the archived reduced-Gaussian grid ([ERA5 spatial reference](https://confluence.ecmwf.int/display/CKB/ERA5%3A+What+is+the+spatial+reference)). ECMWF also documents an operational AIFS ENS product on the N320 model grid ([AIFS Ensemble Set X](https://www.ecmwf.int/en/forecasts/dataset/set-x); [ECMWF open data](https://www.ecmwf.int/en/forecasts/datasets/open-data)). These are candidate GRIB/source archives for independent row and point-order evidence. No exact GRIB or OM v3 object was acquired or frozen in this lookup; these links do not close T004 or any Gaussian gate.

The public ECMWF forecast S3 mirror was also queried read-only for `20250701/06z/aifs-ens/`: its listing returned 250 keys with `IsTruncated=false`, all under `0p25`; the `20250701/06z/aifs-ens/n320/` prefix returned zero keys. This is a result for that exact mirror prefix/date, not proof that no native N320 archive exists elsewhere. The `0p25` files are not imported as N320 evidence.

Importer command:

```sh
python3 scripts/generate-grid-reference.py \
  --root "$PWD" \
  --sample-manifest "$PWD/test/data/grids/sample-manifest.json" \
  --records "$PWD/specs/004-multi-grid-selection/evidence/reference-revalidated-20261007.json" \
  --output-dir "$PWD/test/data/grids" \
  --force
```

`python3 test/tools/grid_reference_test.py` passed 6 checks, `python3 test/tools/grid_evidence_test.py` passed 9 checks, and `git diff --check` passed. The full H1 comparator has not been rerun here; previous coordinate/source-position comparison issues remain unresolved. T004 remains open.
