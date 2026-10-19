-- Run from repository root in a NEW DuckDB v1.5.5 process with -unsigned -bail.
-- Any failed assertion terminates execution; N7_FUNCTIONAL_PASS is printed last.
SELECT CASE WHEN version()='v1.5.5' THEN 'VERSION_PASS'
            ELSE error('Use DuckDB v1.5.5') END AS runtime_check;
SELECT CASE WHEN count(*)=0 THEN 'FRESH_PROCESS_PASS'
            ELSE error('duckomo/spatial already loaded; start a new process') END AS fresh_check
FROM duckdb_extensions() WHERE loaded AND extension_name IN ('duckomo','spatial');

LOAD 'build/official-matrix/v1.5.5/builds/8c86d97a161b1f2f5b88c7714fe5f36fd896d12c8b53acdc9b679a4fd88de8c9/release/extension/duckomo/duckomo.duckdb_extension';
LOAD 'build/regions-runtime/v1.5.5/official/spatial.duckdb_extension';
SELECT version() AS duckdb_version;
SELECT extension_name, extension_version, install_path, loaded
FROM duckdb_extensions() WHERE extension_name IN ('duckomo','spatial') ORDER BY extension_name;

SET VARIABLE n7_regions = [
 {'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((100.1 20.1,100.2 20.1,100.2 20.2,100.1 20.2,100.1 20.1))'))},
 {'id':'Empty','wkb':ST_AsWKB(ST_GeomFromText('POLYGON EMPTY'))},
 {'id':'Outside','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((110 30,111 30,111 31,110 31,110 30))'))}
];
SET VARIABLE n7_grid = {'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'separate'};
SET VARIABLE n7_axes = {
 'time':{'axis':'time','start':TIMESTAMP '2026-01-01','step':INTERVAL '1 hour'},
 'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}
};

CREATE TEMP TABLE n7_source AS
SELECT * FROM read_om('test/data/regions/regions.om',
 dimensions:= ['member','time','lat','lon'], grid:=getvariable('n7_grid'),
 spatial_axes:= ['lat','lon'], axes:=getvariable('n7_axes'), include_source:=true);
SELECT CASE WHEN count(*)=4 AND sum(value)=22 THEN 'RAW_RECTANGLE_PASS'
 ELSE error('ordinary grid rectangle differs') END AS check_result
FROM n7_source WHERE member=0 AND valid_time=TIMESTAMP '2026-01-01'
 AND lat BETWEEN 20 AND 21 AND lon BETWEEN 100 AND 101;

SELECT CASE WHEN count(*)=0 THEN 'POINTS_PASS' ELSE error('points must return zero rows') END AS check_result
FROM read_om_regions('test/data/regions/regions.om', regions:=getvariable('n7_regions'),
 region_crs:='grid_lonlat', dimensions:= ['member','time','lat','lon'], grid:=getvariable('n7_grid'),
 spatial_axes:= ['lat','lon'], axes:=getvariable('n7_axes'), method:='points');

CREATE TEMP TABLE n7_nearest AS
SELECT * FROM read_om_regions('test/data/regions/regions.om', regions:=getvariable('n7_regions'),
 region_crs:='grid_lonlat', dimensions:= ['member','time','lat','lon'], grid:=getvariable('n7_grid'),
 spatial_axes:= ['lat','lon'], axes:=getvariable('n7_axes'), method:='nearest', include_source:=true);
SELECT region_id,count(*) AS rows,min(sampling.status) AS status FROM n7_nearest GROUP BY region_id ORDER BY region_id;
SELECT CASE WHEN count(*)=18 AND count(*) FILTER (WHERE region_id='Gap' AND sampling.status='ok')=6
 AND count(*) FILTER (WHERE region_id='Empty' AND sampling.status='empty_geometry'
   AND lat IS NULL AND lon IS NULL AND value IS NULL AND om_source IS NULL)=6
 AND count(*) FILTER (WHERE region_id='Outside' AND sampling.status='outside_domain'
   AND lat IS NOT NULL AND lon IS NOT NULL AND sampling.source_point_index IS NULL
   AND value IS NULL AND om_source IS NULL)=6
 THEN 'CARDINALITY_STATES_PASS' ELSE error('nearest cardinality/states differ') END AS check_result FROM n7_nearest;
SELECT CASE WHEN count(*)=6 AND bool_and(sampling.source_lat=20 AND sampling.source_lon=100
 AND sampling.source_point_index=0 AND abs(lat-20.15)<1e-12 AND abs(lon-100.15)<1e-12
 AND sampling.variables['value'].status=CASE WHEN value IS NULL THEN 'source_null' ELSE 'ok' END)
 AND count(*) FILTER (WHERE value IS NULL)=2
 THEN 'SOURCE_AND_NULL_PASS' ELSE error('source/null propagation differs') END AS check_result
FROM n7_nearest WHERE region_id='Gap';

WITH sampled AS (
 SELECT member,valid_time,value,om_source.logical_index AS logical_index,om_source.point_index AS point_index
 FROM n7_nearest WHERE region_id='Gap'
), original AS (
 SELECT member,valid_time,value,om_source.logical_index AS logical_index,om_source.point_index AS point_index
 FROM n7_source WHERE lat=20 AND lon=100
), differences AS (
 (SELECT * FROM sampled EXCEPT ALL SELECT * FROM original)
 UNION ALL (SELECT * FROM original EXCEPT ALL SELECT * FROM sampled)
)
SELECT CASE WHEN count(*)=0 THEN 'EXACT_SOURCE_PARITY_PASS' ELSE error('source values/indices differ') END AS check_result FROM differences;

CREATE TEMP TABLE n7_near20 AS
SELECT * FROM read_om_regions('test/data/regions/regions.om', regions:=getvariable('n7_regions'),
 region_crs:='grid_lonlat', dimensions:= ['member','time','lat','lon'], grid:=getvariable('n7_grid'),
 spatial_axes:= ['lat','lon'], axes:=getvariable('n7_axes'), method:='nearest',max_distance_m:=20000,include_source:=true);
SELECT CASE WHEN count(*)=6 AND bool_and(sampling.status='beyond_max_distance' AND value IS NULL
 AND om_source IS NULL AND sampling.source_lat=20 AND sampling.source_lon=100 AND sampling.distance_m>20000)
 THEN 'DISTANCE_REJECT_PASS' ELSE error('distance rejection differs') END AS check_result
FROM n7_near20 WHERE region_id='Gap';
SELECT CASE WHEN count(*)=6 AND bool_and(sampling.status='ok') THEN 'DISTANCE_ACCEPT_PASS'
 ELSE error('distance acceptance differs') END AS check_result
FROM read_om_regions('test/data/regions/regions.om', regions:=getvariable('n7_regions'),
 region_crs:='grid_lonlat', dimensions:= ['member','time','lat','lon'], grid:=getvariable('n7_grid'),
 spatial_axes:= ['lat','lon'], axes:=getvariable('n7_axes'), method:='nearest',max_distance_m:=25000)
WHERE region_id='Gap';

SELECT region_id,member,valid_time,lat,lon,sampling.source_lat,sampling.source_lon,
 sampling.distance_m,value,sampling.variables['value'].status AS value_status,om_source.logical_index
FROM n7_nearest ORDER BY region_id,member,valid_time;
SELECT 'N7_FUNCTIONAL_PASS' AS acceptance;
-- Strict out_of_bounds/error with LIMIT 0 was independently checked and recorded
-- previously; it is intentionally separate because it must raise a Binder Error.
