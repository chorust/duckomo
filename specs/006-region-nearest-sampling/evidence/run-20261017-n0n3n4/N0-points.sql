-- Reproducible points-only N0 capture. Run from the repository root with the
-- matching DuckDB CLI/runtime and output files directed into this directory.
LOAD '/home/blizhan/repo/github/duckomo/build/release/extension/duckomo/duckomo.duckdb_extension';
LOAD '/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension';

CREATE TEMP TABLE region_shapes(region_id VARCHAR, geom GEOMETRY);
INSERT INTO region_shapes VALUES
    ('A', ST_GeomFromText('POLYGON((100 20,102 20,102 22,100 22,100 20))')),
    ('B', ST_GeomFromText('POLYGON((101 21,103 21,103 23,101 23,101 21))')),
    ('Hole', ST_GeomFromText('POLYGON((100 20,104 20,104 24,100 24,100 20),(101 21,103 21,103 23,101 23,101 21))')),
    ('Empty', ST_GeomFromText('POLYGON EMPTY')),
    ('Outside', ST_GeomFromText('POLYGON((110 30,111 30,111 31,110 31,110 30))')),
    ('Multi', ST_GeomFromText('MULTIPOLYGON(((100 20,101 20,101 21,100 21,100 20)),((103 23,104 23,104 24,103 24,103 23)))')),
    ('CopyA', ST_GeomFromText('POLYGON((100 20,102 20,102 22,100 22,100 20))')),
    ('Narrow', ST_GeomFromText('POLYGON((100 20,100.2 20,100.2 24,100 24,100 20))')),
    ('Gap', ST_GeomFromText('POLYGON((100.1 20.1,100.2 20.1,100.2 20.2,100.1 20.2,100.1 20.1))')),
    ('Corner', ST_GeomFromText('POLYGON((102 22,104 22,104 24,102 24,102 22))'));
SET VARIABLE region_input = (
    SELECT list({'id':region_id, 'wkb':ST_AsWKB(geom)} ORDER BY region_id)
    FROM region_shapes
);
SET VARIABLE demo_grid = {'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,
                          'dlat':1.0,'dlon':1.0,'order':'separate'};
SET VARIABLE demo_axes = {
    'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},
    'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}
};

COPY (
    SELECT region_id, om_source.logical_index AS logical_index,
           om_source.point_index AS point_index, om_source.axis_indices AS axis_indices,
           value, lat, lon, valid_time, member, om_source::VARCHAR AS om_source
    FROM read_om_regions(
        'test/data/regions/regions.om', regions:=getvariable('region_input'),
        region_crs:='grid_lonlat', dimensions:=['member','time','lat','lon'],
        grid:=getvariable('demo_grid'), spatial_axes:=['lat','lon'],
        axes:=getvariable('demo_axes'), include_source:=true)
    ORDER BY region_id, logical_index
) TO '/tmp/duckomo-sampling-current/n0n2n3n4/N0-points-rows.csv' (HEADER, DELIMITER ',');
COPY (
    SELECT metrics::VARCHAR AS metrics
    FROM duckomo_last_scan_metrics()
    ORDER BY scan_id DESC LIMIT 1
) TO '/tmp/duckomo-sampling-current/n0n2n3n4/N0-points-metrics.csv' (HEADER, DELIMITER ',');
