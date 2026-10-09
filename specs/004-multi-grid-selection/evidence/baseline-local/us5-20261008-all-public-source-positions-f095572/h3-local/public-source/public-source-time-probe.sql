LOAD '/home/blizhan/repo/github/duckomo/build/grid-matrix/baseline-1.5.4/builds/f09557296741f8f6493c5517473c8a38824f7a8dfaebd5e3cf51c1c47868a030/release/extension/duckomo/duckomo.duckdb_extension';
SET threads = 1;
SET duckomo_max_threads = 1;
CREATE TEMP VIEW time_probe_gem_rdps_10km AS SELECT valid_time FROM read_om('build/s3-samples/openmeteo-v3/cmc-gem-rdps-10km-cape-chunk-4337.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]));
COPY (SELECT valid_time FROM time_probe_gem_rdps_10km LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-all-public-source-positions-f095572/h3-local/public-source/gem_rdps_10km.first-valid-time.csv' (FORMAT CSV, HEADER true);
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-all-public-source-positions-f095572/h3-local/public-source/gem_rdps_10km.first-valid-time-metrics.csv' (FORMAT CSV, HEADER true);
CREATE TEMP VIEW time_probe_gem_regional AS SELECT valid_time FROM read_om('build/s3-samples/openmeteo-v3/cmc-gem-rdps-cape-chunk-4337.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]));
COPY (SELECT valid_time FROM time_probe_gem_regional LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-all-public-source-positions-f095572/h3-local/public-source/gem_regional.first-valid-time.csv' (FORMAT CSV, HEADER true);
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-all-public-source-positions-f095572/h3-local/public-source/gem_regional.first-valid-time-metrics.csv' (FORMAT CSV, HEADER true);
CREATE TEMP VIEW time_probe_aladin_central_europe_2km AS SELECT valid_time FROM read_om('build/s3-samples/openmeteo-v3/chmi-aladin-ce-cape-chunk-4135.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]));
COPY (SELECT valid_time FROM time_probe_aladin_central_europe_2km LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-all-public-source-positions-f095572/h3-local/public-source/aladin_central_europe_2km.first-valid-time.csv' (FORMAT CSV, HEADER true);
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-all-public-source-positions-f095572/h3-local/public-source/aladin_central_europe_2km.first-valid-time-metrics.csv' (FORMAT CSV, HEADER true);
