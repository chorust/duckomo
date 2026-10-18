-- Run from the repository root in a fresh DuckDB v1.5.5 session:
-- build/regions-runtime/v1.5.5/official/duckdb -unsigned
-- .read build/manual-acceptance-v1.5.5.sql
-- Expected: differences = 0 and acceptance = PASS.

LOAD 'build/official-matrix/v1.5.5/builds/8c86d97a161b1f2f5b88c7714fe5f36fd896d12c8b53acdc9b679a4fd88de8c9/release/extension/duckomo/duckomo.duckdb_extension';
LOAD 'build/regions-runtime/v1.5.5/official/spatial.duckdb_extension';

SET VARIABLE acceptance_regions = [
    {'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText(
        'POLYGON((100.1 20.1,100.2 20.1,100.2 20.2,100.1 20.2,100.1 20.1))'))}
];
SET VARIABLE acceptance_grid = {
    'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,
    'dlat':1.0,'dlon':1.0,'order':'separate'
};
SET VARIABLE acceptance_axes = {
    'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},
    'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}
};

CREATE OR REPLACE TEMP TABLE acceptance_sampled AS
SELECT member, valid_time, value,
       sampling.source_lat AS source_lat,
       sampling.source_lon AS source_lon,
       sampling.source_point_index AS source_point_index,
       om_source.logical_index AS source_logical_index,
       sampling.variables['value'].status AS value_status
FROM read_om_regions(
    'test/data/regions/regions.om',
    regions := getvariable('acceptance_regions'),
    region_crs := 'grid_lonlat',
    dimensions := ['member','time','lat','lon'],
    grid := getvariable('acceptance_grid'),
    spatial_axes := ['lat','lon'],
    axes := getvariable('acceptance_axes'),
    method := 'nearest', include_source := true
);

CREATE OR REPLACE TEMP TABLE acceptance_source AS
SELECT member, valid_time, value,
       om_source.point_index AS source_point_index,
       om_source.logical_index AS source_logical_index
FROM read_om(
    'test/data/regions/regions.om',
    dimensions := ['member','time','lat','lon'],
    grid := getvariable('acceptance_grid'),
    spatial_axes := ['lat','lon'],
    axes := getvariable('acceptance_axes'),
    include_source := true
)
WHERE lat = 20 AND lon = 100;

-- Inspect all six sampled values, NULL states and original source positions.
SELECT * FROM acceptance_sampled ORDER BY member, valid_time;

-- Bidirectional multiset comparison preserves NULLs and duplicate rows.
WITH differences AS (
    (SELECT member, valid_time, value, source_point_index, source_logical_index
     FROM acceptance_sampled
     EXCEPT ALL
     SELECT member, valid_time, value, source_point_index, source_logical_index
     FROM acceptance_source)
    UNION ALL
    (SELECT member, valid_time, value, source_point_index, source_logical_index
     FROM acceptance_source
     EXCEPT ALL
     SELECT member, valid_time, value, source_point_index, source_logical_index
     FROM acceptance_sampled)
), checks AS (
    SELECT
        (SELECT count(*) FROM differences) AS differences,
        (SELECT count(*) FROM acceptance_sampled) AS sampled_rows,
        (SELECT count(*) FROM acceptance_source) AS source_rows,
        (SELECT count(*) FROM acceptance_sampled
         WHERE source_lat IS DISTINCT FROM 20.0
            OR source_lon IS DISTINCT FROM 100.0
            OR source_point_index IS DISTINCT FROM 0::UBIGINT
            OR value_status IS DISTINCT FROM
                CASE WHEN value IS NULL THEN 'source_null' ELSE 'ok' END) AS invalid_source_or_status
)
SELECT *,
       CASE WHEN differences = 0 AND sampled_rows = 6 AND source_rows = 6
                      AND invalid_source_or_status = 0
            THEN 'PASS' ELSE 'FAIL' END AS acceptance
FROM checks;
