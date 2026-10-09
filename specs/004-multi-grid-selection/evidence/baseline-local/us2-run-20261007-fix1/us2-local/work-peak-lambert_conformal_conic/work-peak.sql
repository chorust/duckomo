LOAD '/home/blizhan/repo/github/duckomo/build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension';
SET threads=1;
SET duckomo_max_threads=1;
SET duckomo_cache_enabled=true;
SET duckomo_cache_capacity=67108864;

CREATE TEMP TABLE work_small AS SELECT value FROM read_om('/home/blizhan/repo/github/duckomo/test/data/grids/memory-small.om', dimensions := map(['value'], [['latitude_axis','longitude_axis']]), grid := {'version':1,'type':'lambert_conformal_conic','numeric_policy':'float64_v1','earth':{'model':'sphere','radius_m':6371229.0},'layout':{'nx':256,'ny':256,'order':'separate'},'parameters':{'x0':-128000.000000,'y0':-128000.000000,'dx':1000.0,'dy':1000.0,'central_meridian':10.0,'latitude_of_origin':46.244,'standard_parallel_1':40.0,'standard_parallel_2':50.0}}, spatial_axes := ['latitude_axis','longitude_axis']) WHERE latitude BETWEEN 45.5 AND 47.0 AND longitude BETWEEN 9.5 AND 10.5;

COPY (SELECT (SELECT count(*)::UBIGINT FROM work_small) AS result_rows, (SELECT sum(value)::DOUBLE FROM work_small) AS result_sum, (SELECT min(value)::DOUBLE FROM work_small) AS result_min, (SELECT max(value)::DOUBLE FROM work_small) AS result_max, metrics::JSON AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us2-run-20261007-fix1/us2-local/work-peak-lambert_conformal_conic/small.json' (FORMAT JSON, ARRAY false);

CREATE TEMP TABLE work_large AS SELECT value FROM read_om('/home/blizhan/repo/github/duckomo/test/data/grids/memory-large.om', dimensions := map(['value'], [['latitude_axis','longitude_axis']]), grid := {'version':1,'type':'lambert_conformal_conic','numeric_policy':'float64_v1','earth':{'model':'sphere','radius_m':6371229.0},'layout':{'nx':1024,'ny':1024,'order':'separate'},'parameters':{'x0':-512000.000000,'y0':-512000.000000,'dx':1000.0,'dy':1000.0,'central_meridian':10.0,'latitude_of_origin':46.244,'standard_parallel_1':40.0,'standard_parallel_2':50.0}}, spatial_axes := ['latitude_axis','longitude_axis']) WHERE latitude BETWEEN 45.5 AND 47.0 AND longitude BETWEEN 9.5 AND 10.5;

COPY (SELECT (SELECT count(*)::UBIGINT FROM work_large) AS result_rows, (SELECT sum(value)::DOUBLE FROM work_large) AS result_sum, (SELECT min(value)::DOUBLE FROM work_large) AS result_min, (SELECT max(value)::DOUBLE FROM work_large) AS result_max, metrics::JSON AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us2-run-20261007-fix1/us2-local/work-peak-lambert_conformal_conic/large.json' (FORMAT JSON, ARRAY false);

