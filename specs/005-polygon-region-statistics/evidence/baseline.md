# Frozen real-region baseline

Status: `inputs_frozen_before_region_performance_measurement`. This records correctness inputs and comparable full/local SQL; it makes no region-query or performance claim.

| Grid | Object SHA-256 / bytes | Variable(s) | Axes / chunks | Polygon points | Coordinate/value baseline |
|---|---|---|---|---:|---|
| `ncep_gfswave025` | `0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd` / 5812040 | `secondary_swell_wave_direction, secondary_swell_wave_height, secondary_swell_wave_period, swell_wave_direction, swell_wave_height, swell_wave_period, tertiary_swell_wave_direction, tertiary_swell_wave_height, tertiary_swell_wave_period, wave_direction, wave_height, wave_period, wind_wave_direction, wind_wave_height, wind_wave_period` | `lat,lon` / `32,32` | 9 | coordinate `formula`; official value `15 per-variable CSVs; hashes in frozen manifest` |
| `gem_rdps_10km` | `3db637197d27c65051e417b850f8c3656d1c5371ab78dcbd9fc05d0e440956f7` / 68016448 | `value` | `latitude,longitude,time` / `1,26,114` | 4 | coordinate `be2598b5dd60b80248d58ee9f215732eafb8dd133e8305997cec90506e382552`; official value `cdcd6650b7d9acb6695886f7359ae2c8453e6b9ad972402a0699f35297be7f85` |
| `gem_regional` | `1d701b188c8c264532f0d8fa917400a66cf28aa1c8e53a7b40781a775333c7c1` / 23835720 | `value` | `latitude,longitude,time` / `1,26,114` | 11 | coordinate `d6556760901be1a17873825f680c3df7e4e51b11704042ae8967653c232d61f0`; official value `ea70e1d945d885e2cc20159afb76db9e5a0c79effe45be2d50ddf018903ba467` |
| `aladin_central_europe_2km` | `cef407b0aae43498485b8f6e25907c1ecf8a82c6cb8aa2185fe860f0c491132c` / 48727672 | `value` | `latitude,longitude,time` / `1,25,120` | 6 | coordinate `c9aebe8aa88a2ce11e3c7789be475b6051c90ebfeece7f2e5412bbfc2dbff719`; official value `7fc25500b7c6387f2408caf04322a2955f15f89cd401700016f26c4d8355e4dd` |
| `ecmwf_ifs` | `2e8279f8bd12052ba5be5a0bcba2293dc4316097e434d6691f9e6711249e1fd3` / 2482560 | `value` | `point` / `1,400` | 3 | coordinate `d568d8ac9b46d9a60e43a2ccf64ff44ca101be1d2f69d4ffe8c84de5915523da`; official value `083300d4593e326236b76dfb2d55485058fdbf360a27bfab6a04a64d01eab91a` |

## Frozen comparison SQL

Each target stores complete local and remote HTTPS/S3 `read_om` full-scan and `read_om_regions` queries where the source advertises both access paths, selected columns, region WKB, and the full/local comparison conditions in `test/data/regions/real-targets.json`. Run them with one thread and no SQL row limit; use identical output projection, retaining `om_source` to compare original positions. The region query additionally emits `region_id`.

## Baseline scope and limitations

- Missing frozen inputs: none. Every local object, coordinate artifact, and applicable official value reference is hash-checked; NCEP pins all 15 per-variable CSVs in the manifest.
- The three projected-grid coordinate references are pinned-producer Float32 exports with their frozen 0.0001-degree comparison tolerance; official value CSVs were produced by the fixed OM C API. This tolerance compares reported coordinates only and never expands polygon membership.
- The three time-series objects have a frozen raw time dimension but no semantic timestamp coordinate in their source metadata; comparisons retain original `logical_index` and do not infer `valid_time` labels.
- NCEP coordinates use the frozen domain formula and its official OM C API per-variable references.
- O1280 uses the existing 004 full-domain check (6,599,680 positions, zero value bit mismatches, zero recorded coordinate error) and pinned producer Float32 coordinate port. The selected coordinate subset is recounted from the retained `static-domain.csv` artifact and hash-checked against the original run manifest.
- A validator-development attempt exercised only the first two local region queries and failed comparison; no correctness or performance gate passed. The registered validator reruns all five frozen targets against the references.
