LOAD 'build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension';

WITH domain_info AS (
    SELECT grid_id, (layout::JSON)::VARCHAR AS layout
    FROM om_grid_info(
        'build/s3-samples/openmeteo-v3/chmi-aladin-ce-cape-chunk-4135.om',
        dimensions := map(['value'], [['y', 'x', 'time']]),
        domain := 'aladin_central_europe_2km'
    )
), explicit_info AS (
    SELECT grid_id, (layout::JSON)::VARCHAR AS layout
    FROM om_grid_info(
        'build/s3-samples/openmeteo-v3/chmi-aladin-ce-cape-chunk-4135.om',
        dimensions := map(['value'], [['y', 'x', 'time']]),
        grid := {
            'version': 1,
            'type': 'lambert_conformal_conic',
            'numeric_policy': 'openmeteo_f32_v1',
            'earth': {'model': 'sphere', 'radius_m': 6371229.0},
            'layout': {'nx': 1053, 'ny': 837, 'order': 'separate'},
            'parameters': {
                'x0': -1364242.5, 'y0': -717413.5, 'dx': 2325.0, 'dy': 2325.0,
                'central_meridian': 17.0, 'latitude_of_origin': 46.244,
                'standard_parallel_1': 46.244, 'standard_parallel_2': 46.244
            }
        },
        spatial_axes := ['y', 'x']
    )
)
SELECT domain_info.grid_id = explicit_info.grid_id AS grid_identity_matches,
       domain_info.layout = explicit_info.layout AS layout_identity_matches
FROM domain_info CROSS JOIN explicit_info;
