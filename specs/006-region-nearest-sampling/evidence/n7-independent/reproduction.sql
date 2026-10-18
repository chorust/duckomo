LOAD '/home/blizhan/repo/github/duckomo/build/release/extension/duckomo/duckomo.duckdb_extension';
LOAD '/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension';

SET VARIABLE demo_regions = [
  {'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText(
    'POLYGON((100.1 20.1,100.2 20.1,100.2 20.2,100.1 20.2,100.1 20.1))'))},
  {'id':'Empty','wkb':ST_AsWKB(ST_GeomFromText('POLYGON EMPTY'))},
  {'id':'Outside','wkb':ST_AsWKB(ST_GeomFromText(
    'POLYGON((110 30,111 30,111 31,110 31,110 30))'))}
];
SET VARIABLE demo_grid = {'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,
                          'dlat':1.0,'dlon':1.0,'order':'separate'};
SET VARIABLE demo_axes = {
  'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},
  'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}
};

SELECT lat,lon,value
FROM read_om('test/data/regions/regions.om',
  dimensions := ['member','time','lat','lon'], grid := getvariable('demo_grid'),
  spatial_axes := ['lat','lon'], axes := getvariable('demo_axes'))
WHERE member=0 AND valid_time=TIMESTAMP '2026-01-01 00:00:00'
  AND lat BETWEEN 20 AND 21 AND lon BETWEEN 100 AND 101;

SELECT count(*) AS source_records
FROM read_om_regions('test/data/regions/regions.om',
  regions := getvariable('demo_regions'), region_crs := 'grid_lonlat',
  dimensions := ['member','time','lat','lon'], grid := getvariable('demo_grid'),
  spatial_axes := ['lat','lon'], axes := getvariable('demo_axes'), method := 'points');

CREATE TEMP TABLE nearest_results AS
SELECT * FROM read_om_regions('test/data/regions/regions.om',
  regions := getvariable('demo_regions'), region_crs := 'grid_lonlat',
  dimensions := ['member','time','lat','lon'], grid := getvariable('demo_grid'),
  spatial_axes := ['lat','lon'], axes := getvariable('demo_axes'),
  method := 'nearest', include_source := true);

SELECT region_id,count(*) AS result_rows,
       min(sampling.status) AS location_status
FROM nearest_results GROUP BY region_id ORDER BY region_id;

SELECT region_id,lat,lon,sampling.source_lat,sampling.source_lon,
       sampling.distance_m,sampling.variables['value'].status AS value_status,
       value,om_source.point_index
FROM nearest_results
WHERE member=0 AND valid_time=TIMESTAMP '2026-01-01 00:00:00'
ORDER BY region_id;

SELECT n.region_id,n.lat AS target_lat,n.lon AS target_lon,
       n.sampling.source_lat AS nearest_source_lat,
       n.sampling.source_lon AS nearest_source_lon,
       n.value AS nearest_value,n.om_source.point_index AS nearest_point_index,
       d.value AS direct_value,d.om_source.point_index AS direct_point_index
FROM nearest_results n
JOIN read_om('test/data/regions/regions.om',
  dimensions := ['member','time','lat','lon'], grid := getvariable('demo_grid'),
  spatial_axes := ['lat','lon'], axes := getvariable('demo_axes'), include_source := true) d
  ON d.lat=n.sampling.source_lat AND d.lon=n.sampling.source_lon
 AND d.member=n.member AND d.valid_time=n.valid_time
WHERE n.region_id='Gap' AND n.member=0
  AND n.valid_time=TIMESTAMP '2026-01-01 00:00:00';
