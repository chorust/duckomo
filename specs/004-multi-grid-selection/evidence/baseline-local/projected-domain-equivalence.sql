LOAD 'build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension';

WITH
rotated_domain AS (
    SELECT grid_id, layout::VARCHAR AS layout
    FROM om_grid_info(
        'build/s3-samples/openmeteo-v3/cmc-gem-rdps-10km-cape-chunk-4337.om',
        dimensions := map(['value'], [['y', 'x', 'time']]),
        domain := 'gem_rdps_10km'
    )
),
rotated_explicit AS (
    SELECT grid_id, layout::VARCHAR AS layout
    FROM om_grid_info(
        'build/s3-samples/openmeteo-v3/cmc-gem-rdps-10km-cape-chunk-4337.om',
        dimensions := map(['value'], [['y', 'x', 'time']]),
        grid := {
            'version': 1,
            'type': 'rotated_latlon',
            'numeric_policy': 'openmeteo_f32_v1',
            'earth': {'model': 'sphere', 'radius_m': 6371229.0},
            'layout': {'nx': 1140, 'ny': 1045, 'order': 'separate'},
            'parameters': {
                'x0': -53.859, 'y0': -48.806, 'dx': 0.090298, 'dy': 0.090298,
                'north_pole_latitude': 31.7583, 'north_pole_longitude': 87.597,
                'rotation': 180.0
            }
        },
        spatial_axes := ['y', 'x']
    )
),
stereographic_domain AS (
    SELECT grid_id, layout::VARCHAR AS layout
    FROM om_grid_info(
        'build/s3-samples/openmeteo-v3/cmc-gem-rdps-cape-chunk-4337.om',
        dimensions := map(['value'], [['y', 'x', 'time']]),
        domain := 'gem_regional'
    )
),
stereographic_explicit AS (
    SELECT grid_id, layout::VARCHAR AS layout
    FROM om_grid_info(
        'build/s3-samples/openmeteo-v3/cmc-gem-rdps-cape-chunk-4337.om',
        dimensions := map(['value'], [['y', 'x', 'time']]),
        grid := {
            'version': 1,
            'type': 'stereographic',
            'numeric_policy': 'openmeteo_f32_v1',
            'earth': {'model': 'sphere', 'radius_m': 6371229.0},
            'layout': {'nx': 935, 'ny': 824, 'order': 'separate'},
            'parameters': {
                'x0': -4878222.0, 'y0': -7839461.0,
                'dx': 10717.9697265625, 'dy': 10717.9658203125,
                'central_meridian': 249.0, 'latitude_of_origin': 90.0,
                'scale_factor': 1.0
            }
        },
        spatial_axes := ['y', 'x']
    )
)
SELECT 'rotated_latlon' AS grid_family,
       domain.grid_id = explicit.grid_id AS grid_identity_matches,
       domain.layout = explicit.layout AS layout_identity_matches
FROM rotated_domain AS domain CROSS JOIN rotated_explicit AS explicit
UNION ALL
SELECT 'stereographic',
       domain.grid_id = explicit.grid_id,
       domain.layout = explicit.layout
FROM stereographic_domain AS domain CROSS JOIN stereographic_explicit AS explicit;
