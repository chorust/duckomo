# T070 H1/H2 Public Open-Meteo Source Subcheck

Status: **partial subcheck pass**. T070 and the complete H1/H2 gates remain open.

## Scope and command

Using baseline build `5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d`, the public-source validator inspected the three hash-pinned Open-Meteo OM v3 projected samples already in the manifest. No source conversion was performed.

Command exit code: `0`; elapsed time: `76,379.109 ms`. The exact argument vector and input/output hashes are in [manifest.json](manifest.json); per-query SQL, CSV, and metrics are in [public-source/](public-source/).

## Results

| Existing OM sample | Full spatial positions at time-axis index 0 | Maximum coordinate error | Bounded spatial matches |
|---|---:|---:|---:|
| `gem_rdps_10km` | 1,191,300 | `1.52587890625e-5°` | 15 |
| `gem_regional` | 770,440 | `3.0517578125e-5°` | 12 |
| `aladin_central_europe_2km` | 881,361 | `1.52587890625e-5°` | 247 |

For all three samples, the explicit and domain forms matched source identity row by row across the streamed full spatial plane and produced identical selected spatial positions. The first four natural positions also matched in the SQL identity join. Source, `om_grid_info`, full-plane source, and spatial-source queries reported zero value index/data reads and zero value decodes. The bounded selections matched the corresponding full-scan source baseline.

## Limits

The validator uses `ntime`-derived synthetic `valid_time` labels only to select array-axis index zero; these labels do not claim to be producer times. The OM array-axis-to-producer point-order mapping still lacks independent proof. This run does not cover Gaussian N160/N320 objects, public cross-URI/remote source identity, or a complete H1/H2/H6 gate. The current Open-Meteo S3 inventory contains no matching N160, full N320, or N320-region OM object; O320/O1280 evidence is not a substitute.
