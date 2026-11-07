# read_om_series Local Validation

- Feature: `010-read-om-series`
- Run status: **blocked**
- Started: `2026-11-06T06:31:18Z`
- Platform: `Linux aarch64`
- Mode: `local`

## Gate mapping

- **S1: passed** — all local one/two/three-chunk, endpoint, negative-time, NULL and slotless oracles matched
- **S2: passed** — 38 no-access cases and 8 metadata-access cases audited individually
- **SC-004: passed** — all eleven registered model entries matched explicit equivalent temporal parameters
- **S5: passed** — synthetic N160 domain series matched an explicit-grid manual UNION ALL at 1/2/4 workers
- **S6: passed** — each executed child retained a distinct completed scan record with useful or zero value I/O as requested

## Extension load identity

- Status: **passed** — strace observed DuckDB opening the exact requested extension file
- Requested extension: `/home/blizhan/repo/github/duckomo/build/series-acceptance-sc004-corrected-20261106/v1.5.5/builds/ccfa7772a2ed17bc4c9366d95fdc46ecb4ff5d1ce38835e371446983aae40021/release/extension/duckomo/duckomo.duckdb_extension`
- Open trace: `/home/blizhan/repo/github/duckomo/specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.5/local/extension-load.strace`

## Local S1 scenarios

- `one_chunk`: **passed**, rows 12 vs 12, schema equal: `True`, SQL EXCEPT ALL empty: `True`
- `single_boundary_slot`: **passed**, rows 6 vs 6, schema equal: `True`, SQL EXCEPT ALL empty: `True`
- `unaligned_cross_boundary`: **passed**, rows 18 vs 18, schema equal: `True`, SQL EXCEPT ALL empty: `True`
- `three_chunks_and_null`: **passed**, rows 54 vs 54, schema equal: `True`, SQL EXCEPT ALL empty: `True`
- `alternate_start_end`: **passed**, rows 18 vs 18, schema equal: `True`, SQL EXCEPT ALL empty: `True`
- `negative_epoch`: **passed**, rows 12 vs 12, schema equal: `True`, SQL EXCEPT ALL empty: `True`
- `slotless_subsecond_window`: **passed**, rows 0 vs 0, schema equal: `True`, SQL EXCEPT ALL empty: `True`
- `sc004-registered-vs-explicit-ecmwf_ifs`: **passed**, rows None vs 3024, schema equal: `True`, SQL EXCEPT ALL empty: `None`
- `sc004-registered-vs-explicit-ecmwf_ifs025`: **passed**, rows None vs 624, schema equal: `True`, SQL EXCEPT ALL empty: `None`
- `sc004-registered-vs-explicit-ncep_gfs013`: **passed**, rows None vs 2886, schema equal: `True`, SQL EXCEPT ALL empty: `None`
- `sc004-registered-vs-explicit-ncep_gfs025`: **passed**, rows None vs 2886, schema equal: `True`, SQL EXCEPT ALL empty: `None`
- `sc004-registered-vs-explicit-dwd_icon`: **passed**, rows None vs 1518, schema equal: `True`, SQL EXCEPT ALL empty: `None`
- `sc004-registered-vs-explicit-dwd_icon_eu`: **passed**, rows None vs 1158, schema equal: `True`, SQL EXCEPT ALL empty: `None`
- `sc004-registered-vs-explicit-dwd_icon_d2`: **passed**, rows None vs 726, schema equal: `True`, SQL EXCEPT ALL empty: `None`
- `sc004-registered-vs-explicit-jma_gsm`: **passed**, rows None vs 660, schema equal: `True`, SQL EXCEPT ALL empty: `None`
- `sc004-registered-vs-explicit-cmc_gem_gdps`: **passed**, rows None vs 660, schema equal: `True`, SQL EXCEPT ALL empty: `None`
- `sc004-registered-vs-explicit-meteofrance_arpege_world025`: **passed**, rows None vs 1260, schema equal: `True`, SQL EXCEPT ALL empty: `None`
- `sc004-registered-vs-explicit-ukmo_global_deterministic_10km`: **passed**, rows None vs 1158, schema equal: `True`, SQL EXCEPT ALL empty: `None`

## Synthetic N160 S5/S6 worker matrix

- `s5-n160-domain-vs-explicit-worker-1`: **passed**, rows 3200 vs 3200, value scans `passed`, value-free COUNT scans `passed`; worker caps `True`/`True`, memory bounds `True`/`True`
- `s5-n160-domain-vs-explicit-worker-2`: **passed**, rows 3200 vs 3200, value scans `passed`, value-free COUNT scans `passed`; worker caps `True`/`True`, memory bounds `True`/`True`
- `s5-n160-domain-vs-explicit-worker-4`: **passed**, rows 3200 vs 3200, value scans `passed`, value-free COUNT scans `passed`; worker caps `True`/`True`, memory bounds `True`/`True`

## S2 per-case access audit

- Status: **passed** — 38 no-access cases and 8 metadata-access cases audited individually
- Preflight cases: `38`; metadata cases: `8`; passed/failed/blocked: `46/0/0`
- SQLLogicTest source SHA-256: `c88be0cc9982cb1df11bd7da3d10a02b3ae4f6e87fb535b2be5b48c15f153fee`
- `preflight_001`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_001.strace`
- `preflight_002`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_002.strace`
- `preflight_003`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_003.strace`
- `preflight_004`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_004.strace`
- `preflight_005`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_005.strace`
- `preflight_006`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_006.strace`
- `preflight_007`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_007.strace`
- `preflight_008`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_008.strace`
- `preflight_009`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_009.strace`
- `preflight_010`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_010.strace`
- `preflight_011`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_011.strace`
- `preflight_012`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_012.strace`
- `preflight_013`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_013.strace`
- `preflight_014`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_014.strace`
- `preflight_015`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_015.strace`
- `preflight_016`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_016.strace`
- `preflight_017`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_017.strace`
- `preflight_018`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_018.strace`
- `preflight_019`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_019.strace`
- `preflight_020`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_020.strace`
- `preflight_021`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_021.strace`
- `preflight_022`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_022.strace`
- `preflight_023`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_023.strace`
- `preflight_024`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_024.strace`
- `preflight_025`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_025.strace`
- `preflight_026`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_026.strace`
- `preflight_027`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_027.strace`
- `preflight_028`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_028.strace`
- `preflight_029`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_029.strace`
- `preflight_030`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_030.strace`
- `preflight_031`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_031.strace`
- `preflight_032`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_032.strace`
- `preflight_033`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_033.strace`
- `preflight_034`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_034.strace`
- `preflight_035`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_035.strace`
- `preflight_036`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_036.strace`
- `preflight_037`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_037.strace`
- `preflight_038`: **passed** — no candidate access; diagnostic matched `True`; trace `s2-corpus/traces/preflight_038.strace`
- `embedded_time_conflict_selected`: **passed** — opened chunk_0000.om; diagnostic matched `True`; trace `s2-corpus/traces/embedded_time_conflict_selected.strace`
- `embedded_time_conflict_slotless`: **passed** — opened chunk_0000.om; diagnostic matched `True`; trace `s2-corpus/traces/embedded_time_conflict_slotless.strace`
- `declared_type`: **passed** — opened chunk_0000.om; diagnostic matched `True`; trace `s2-corpus/traces/declared_type.strace`
- `dimension_order`: **passed** — opened chunk_0000.om; diagnostic matched `True`; trace `s2-corpus/traces/dimension_order.strace`
- `spatial_profile`: **passed** — opened chunk_0000.om; diagnostic matched `True`; trace `s2-corpus/traces/spatial_profile.strace`
- `grid_identity`: **passed** — opened chunk_0000.om; diagnostic matched `True`; trace `s2-corpus/traces/grid_identity.strace`
- `time_extent_across_chunks`: **passed** — opened chunk_0000.om, chunk_0001.om; diagnostic matched `True`; trace `s2-corpus/traces/time_extent_across_chunks.strace`
- `point_extent_across_chunks`: **passed** — opened chunk_0000.om, chunk_0001.om; diagnostic matched `True`; trace `s2-corpus/traces/point_extent_across_chunks.strace`

## Identity and outputs

- DuckDB CLI SHA-256: `9882c99a9804407de82c0edb1816d7667733d37d771a98eb23ad5f6a8d37acb1`
- Extension SHA-256: `2c981c7b1c372ec2f3f1e28212821c7359fcea53345fb427da41bfe027b78268`
- Fixture manifest SHA-256: `8b06774549ff9435537323ec2d59352d5fb508aa4448a854bf341c53e5129cd6`
- Full commands, outputs, query files, diagnostics and per-case access traces are retained beside this report.
- Controlled HTTP/HTTPS/S3 and real producer evidence remain separate gates and are not inferred from local S2 results.
