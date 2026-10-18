LOAD '/home/blizhan/repo/github/duckomo/build/release/extension/duckomo/duckomo.duckdb_extension';
LOAD '/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension';
SET VARIABLE nearest_grid = {'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'};
SET VARIABLE nearest_axes = {
  'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},
  'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},
  'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}
};
COPY (
  SELECT region_id, sampling.axis_indices::VARCHAR AS axis_indices,
         member, valid_time, level, lat AS target_lat, lon AS target_lon,
         sampling.status AS geometry_status, sampling.source_lon, sampling.source_lat,
         sampling.source_point_index, sampling.distance_m,
         sampling.variables['small-separate/temperature'].status AS temperature_status,
         sampling.variables['small-separate/temperature'].source_count AS temperature_source_count,
         sampling.variables['small-separate/temperature'].valid_source_count AS temperature_valid_count,
         "small-separate/temperature" AS temperature,
         sampling.variables['small-separate/humidity'].status AS humidity_status,
         sampling.variables['small-separate/humidity'].source_count AS humidity_source_count,
         sampling.variables['small-separate/humidity'].valid_source_count AS humidity_valid_count,
         "small-separate/humidity" AS humidity,
         om_source.logical_index, om_source.point_index
  FROM read_om_regions(
    '/home/blizhan/repo/github/duckomo/test/data/sampling/small-separate.om',
    regions := [{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText(
      'POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}],
    region_crs := 'grid_lonlat', dimensions := ['member','time','level','lat','lon'],
    grid := getvariable('nearest_grid'), spatial_axes := ['lat','lon'],
    axes := getvariable('nearest_axes'), method := 'nearest', include_source := true
  )
  ORDER BY member, valid_time, level
) TO '/tmp/duckomo-us1-actual.csv' (HEADER, DELIMITER ',');
