LOAD '/home/blizhan/repo/github/duckomo/build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension';
SET threads=1;
SET duckomo_max_threads=1;
SET duckomo_cache_enabled=true;
SET duckomo_cache_capacity=67108864;

CREATE TEMP TABLE work_small AS SELECT value AS value FROM read_om('/home/blizhan/repo/github/duckomo/test/data/grids/memory-small.om', dimensions := map(['value'], [['latitude_axis','longitude_axis']]), grid := {'version':1,'type':'rotated_latlon','numeric_policy':'float64_v1','earth':{'model':'sphere','radius_m':6371229.0},'layout':{'nx':256,'ny':256,'order':'separate'},'parameters':{'x0':-32.000000,'y0':-32.000000,'dx':0.250000,'dy':0.250000,'north_pole_latitude':90.0,'north_pole_longitude':0.0,'rotation':0.0}}, spatial_axes := ['latitude_axis','longitude_axis']) WHERE latitude BETWEEN -0.000001 AND 0.000001 AND longitude BETWEEN -0.000001 AND 0.000001;

COPY (SELECT (SELECT count(*)::UBIGINT FROM work_small) AS result_rows, (SELECT sum(value)::DOUBLE FROM work_small) AS result_sum, (SELECT min(value)::DOUBLE FROM work_small) AS result_min, (SELECT max(value)::DOUBLE FROM work_small) AS result_max, metrics::JSON AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us2-run-20261007-fix3/us2-local/work-peak-rotated_latlon/small.json' (FORMAT JSON, ARRAY false);

CREATE TEMP TABLE work_large AS SELECT value AS value FROM read_om('/home/blizhan/repo/github/duckomo/test/data/grids/memory-large.om', dimensions := map(['value'], [['latitude_axis','longitude_axis']]), grid := {'version':1,'type':'rotated_latlon','numeric_policy':'float64_v1','earth':{'model':'sphere','radius_m':6371229.0},'layout':{'nx':1024,'ny':1024,'order':'separate'},'parameters':{'x0':-64.000000,'y0':-64.000000,'dx':0.125000,'dy':0.125000,'north_pole_latitude':90.0,'north_pole_longitude':0.0,'rotation':0.0}}, spatial_axes := ['latitude_axis','longitude_axis']) WHERE latitude BETWEEN -0.000001 AND 0.000001 AND longitude BETWEEN -0.000001 AND 0.000001;

COPY (SELECT (SELECT count(*)::UBIGINT FROM work_large) AS result_rows, (SELECT sum(value)::DOUBLE FROM work_large) AS result_sum, (SELECT min(value)::DOUBLE FROM work_large) AS result_min, (SELECT max(value)::DOUBLE FROM work_large) AS result_max, metrics::JSON AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us2-run-20261007-fix3/us2-local/work-peak-rotated_latlon/large.json' (FORMAT JSON, ARRAY false);

