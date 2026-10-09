LOAD '/home/blizhan/repo/github/duckomo/build/grid-matrix/baseline-1.5.4/builds/15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8/release/extension/duckomo/duckomo.duckdb_extension';
SET threads = 1;
SET duckomo_max_threads = 1;
CREATE TEMP VIEW explicit_gem_rdps_10km AS
SELECT * FROM read_om(
    'build/s3-samples/openmeteo-v3/cmc-gem-rdps-10km-cape-chunk-4337.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]),
    grid := {
            'version': 1,
            'type': 'rotated_latlon',
            'numeric_policy': 'openmeteo_f32_v1',
            'earth': {
                'model': 'sphere',
                'radius_m': 6371229.0
            },
            'layout': {
                'nx': 1140,
                'ny': 1045,
                'order': 'separate'
            },
            'parameters': {
                'x0': -53.859,
                'y0': -48.806,
                'dx': 0.090298,
                'dy': 0.090298,
                'north_pole_latitude': 31.7583,
                'north_pole_longitude': 87.597,
                'rotation': 180
            }
        },
    spatial_axes := ['y', 'x'],
    include_source := true
);
CREATE TEMP VIEW domain_gem_rdps_10km AS
SELECT * FROM read_om(
    'build/s3-samples/openmeteo-v3/cmc-gem-rdps-10km-cape-chunk-4337.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]),
    domain := 'gem_rdps_10km',
    include_source := true
);
CREATE TEMP VIEW info_explicit_gem_rdps_10km AS SELECT grid_id, layout::VARCHAR AS layout, object_id, object_version, version_strength, content_verified FROM om_grid_info('build/s3-samples/openmeteo-v3/cmc-gem-rdps-10km-cape-chunk-4337.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]),
    grid := {
            'version': 1,
            'type': 'rotated_latlon',
            'numeric_policy': 'openmeteo_f32_v1',
            'earth': {
                'model': 'sphere',
                'radius_m': 6371229.0
            },
            'layout': {
                'nx': 1140,
                'ny': 1045,
                'order': 'separate'
            },
            'parameters': {
                'x0': -53.859,
                'y0': -48.806,
                'dx': 0.090298,
                'dy': 0.090298,
                'north_pole_latitude': 31.7583,
                'north_pole_longitude': 87.597,
                'rotation': 180
            }
        },
    spatial_axes := ['y', 'x']);
CREATE TEMP VIEW info_domain_gem_rdps_10km AS SELECT grid_id, layout::VARCHAR AS layout, object_id, object_version, version_strength, content_verified FROM om_grid_info('build/s3-samples/openmeteo-v3/cmc-gem-rdps-10km-cape-chunk-4337.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]),
    domain := 'gem_rdps_10km');
COPY (SELECT om_source.object_id, om_source.object_version, om_source.version_strength, om_source.content_verified, om_source.grid_id, om_source.layout_id, om_source.logical_index, om_source.point_index, om_source.parent_point_index, om_source.axis_indices, latitude, longitude FROM explicit_gem_rdps_10km LIMIT 4) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_rdps_10km.explicit-source.csv' (FORMAT CSV, HEADER true, NULL 'NULL');
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_rdps_10km.explicit-source-metrics.csv' (FORMAT CSV, HEADER true);
COPY (SELECT * FROM info_explicit_gem_rdps_10km) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_rdps_10km.explicit-info.csv' (FORMAT CSV, HEADER true, NULL 'NULL');
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_rdps_10km.explicit-info-metrics.csv' (FORMAT CSV, HEADER true);
COPY (SELECT om_source.object_id, om_source.object_version, om_source.version_strength, om_source.content_verified, om_source.grid_id, om_source.layout_id, om_source.logical_index, om_source.point_index, om_source.parent_point_index, om_source.axis_indices, latitude, longitude FROM domain_gem_rdps_10km LIMIT 4) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_rdps_10km.domain-source.csv' (FORMAT CSV, HEADER true, NULL 'NULL');
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_rdps_10km.domain-source-metrics.csv' (FORMAT CSV, HEADER true);
COPY (SELECT * FROM info_domain_gem_rdps_10km) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_rdps_10km.domain-info.csv' (FORMAT CSV, HEADER true, NULL 'NULL');
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_rdps_10km.domain-info-metrics.csv' (FORMAT CSV, HEADER true);
CREATE TEMP VIEW explicit_gem_regional AS
SELECT * FROM read_om(
    'build/s3-samples/openmeteo-v3/cmc-gem-rdps-cape-chunk-4337.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]),
    grid := {
            'version': 1,
            'type': 'stereographic',
            'numeric_policy': 'openmeteo_f32_v1',
            'earth': {
                'model': 'sphere',
                'radius_m': 6371229.0
            },
            'layout': {
                'nx': 935,
                'ny': 824,
                'order': 'separate'
            },
            'parameters': {
                'x0': -4878222.0,
                'y0': -7839461.0,
                'dx': 10717.9697265625,
                'dy': 10717.9658203125,
                'central_meridian': 249.0,
                'latitude_of_origin': 90.0,
                'scale_factor': 1.0
            }
        },
    spatial_axes := ['y', 'x'],
    include_source := true
);
CREATE TEMP VIEW domain_gem_regional AS
SELECT * FROM read_om(
    'build/s3-samples/openmeteo-v3/cmc-gem-rdps-cape-chunk-4337.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]),
    domain := 'gem_regional',
    include_source := true
);
CREATE TEMP VIEW info_explicit_gem_regional AS SELECT grid_id, layout::VARCHAR AS layout, object_id, object_version, version_strength, content_verified FROM om_grid_info('build/s3-samples/openmeteo-v3/cmc-gem-rdps-cape-chunk-4337.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]),
    grid := {
            'version': 1,
            'type': 'stereographic',
            'numeric_policy': 'openmeteo_f32_v1',
            'earth': {
                'model': 'sphere',
                'radius_m': 6371229.0
            },
            'layout': {
                'nx': 935,
                'ny': 824,
                'order': 'separate'
            },
            'parameters': {
                'x0': -4878222.0,
                'y0': -7839461.0,
                'dx': 10717.9697265625,
                'dy': 10717.9658203125,
                'central_meridian': 249.0,
                'latitude_of_origin': 90.0,
                'scale_factor': 1.0
            }
        },
    spatial_axes := ['y', 'x']);
CREATE TEMP VIEW info_domain_gem_regional AS SELECT grid_id, layout::VARCHAR AS layout, object_id, object_version, version_strength, content_verified FROM om_grid_info('build/s3-samples/openmeteo-v3/cmc-gem-rdps-cape-chunk-4337.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]),
    domain := 'gem_regional');
COPY (SELECT om_source.object_id, om_source.object_version, om_source.version_strength, om_source.content_verified, om_source.grid_id, om_source.layout_id, om_source.logical_index, om_source.point_index, om_source.parent_point_index, om_source.axis_indices, latitude, longitude FROM explicit_gem_regional LIMIT 4) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_regional.explicit-source.csv' (FORMAT CSV, HEADER true, NULL 'NULL');
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_regional.explicit-source-metrics.csv' (FORMAT CSV, HEADER true);
COPY (SELECT * FROM info_explicit_gem_regional) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_regional.explicit-info.csv' (FORMAT CSV, HEADER true, NULL 'NULL');
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_regional.explicit-info-metrics.csv' (FORMAT CSV, HEADER true);
COPY (SELECT om_source.object_id, om_source.object_version, om_source.version_strength, om_source.content_verified, om_source.grid_id, om_source.layout_id, om_source.logical_index, om_source.point_index, om_source.parent_point_index, om_source.axis_indices, latitude, longitude FROM domain_gem_regional LIMIT 4) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_regional.domain-source.csv' (FORMAT CSV, HEADER true, NULL 'NULL');
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_regional.domain-source-metrics.csv' (FORMAT CSV, HEADER true);
COPY (SELECT * FROM info_domain_gem_regional) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_regional.domain-info.csv' (FORMAT CSV, HEADER true, NULL 'NULL');
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/gem_regional.domain-info-metrics.csv' (FORMAT CSV, HEADER true);
CREATE TEMP VIEW explicit_aladin_central_europe_2km AS
SELECT * FROM read_om(
    'build/s3-samples/openmeteo-v3/chmi-aladin-ce-cape-chunk-4135.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]),
    grid := {
            'version': 1,
            'type': 'lambert_conformal_conic',
            'numeric_policy': 'openmeteo_f32_v1',
            'earth': {
                'model': 'sphere',
                'radius_m': 6371229.0
            },
            'layout': {
                'nx': 1053,
                'ny': 837,
                'order': 'separate'
            },
            'parameters': {
                'x0': -1364242.5,
                'y0': -717413.5,
                'dx': 2325.0,
                'dy': 2325.0,
                'central_meridian': 17.0,
                'latitude_of_origin': 46.244,
                'standard_parallel_1': 46.244,
                'standard_parallel_2': 46.244
            }
        },
    spatial_axes := ['y', 'x'],
    include_source := true
);
CREATE TEMP VIEW domain_aladin_central_europe_2km AS
SELECT * FROM read_om(
    'build/s3-samples/openmeteo-v3/chmi-aladin-ce-cape-chunk-4135.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]),
    domain := 'aladin_central_europe_2km',
    include_source := true
);
CREATE TEMP VIEW info_explicit_aladin_central_europe_2km AS SELECT grid_id, layout::VARCHAR AS layout, object_id, object_version, version_strength, content_verified FROM om_grid_info('build/s3-samples/openmeteo-v3/chmi-aladin-ce-cape-chunk-4135.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]),
    grid := {
            'version': 1,
            'type': 'lambert_conformal_conic',
            'numeric_policy': 'openmeteo_f32_v1',
            'earth': {
                'model': 'sphere',
                'radius_m': 6371229.0
            },
            'layout': {
                'nx': 1053,
                'ny': 837,
                'order': 'separate'
            },
            'parameters': {
                'x0': -1364242.5,
                'y0': -717413.5,
                'dx': 2325.0,
                'dy': 2325.0,
                'central_meridian': 17.0,
                'latitude_of_origin': 46.244,
                'standard_parallel_1': 46.244,
                'standard_parallel_2': 46.244
            }
        },
    spatial_axes := ['y', 'x']);
CREATE TEMP VIEW info_domain_aladin_central_europe_2km AS SELECT grid_id, layout::VARCHAR AS layout, object_id, object_version, version_strength, content_verified FROM om_grid_info('build/s3-samples/openmeteo-v3/chmi-aladin-ce-cape-chunk-4135.om',
    dimensions := map(['value'], [[
    'y',
    'x',
    'time'
]]),
    domain := 'aladin_central_europe_2km');
COPY (SELECT om_source.object_id, om_source.object_version, om_source.version_strength, om_source.content_verified, om_source.grid_id, om_source.layout_id, om_source.logical_index, om_source.point_index, om_source.parent_point_index, om_source.axis_indices, latitude, longitude FROM explicit_aladin_central_europe_2km LIMIT 4) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/aladin_central_europe_2km.explicit-source.csv' (FORMAT CSV, HEADER true, NULL 'NULL');
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/aladin_central_europe_2km.explicit-source-metrics.csv' (FORMAT CSV, HEADER true);
COPY (SELECT * FROM info_explicit_aladin_central_europe_2km) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/aladin_central_europe_2km.explicit-info.csv' (FORMAT CSV, HEADER true, NULL 'NULL');
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/aladin_central_europe_2km.explicit-info-metrics.csv' (FORMAT CSV, HEADER true);
COPY (SELECT om_source.object_id, om_source.object_version, om_source.version_strength, om_source.content_verified, om_source.grid_id, om_source.layout_id, om_source.logical_index, om_source.point_index, om_source.parent_point_index, om_source.axis_indices, latitude, longitude FROM domain_aladin_central_europe_2km LIMIT 4) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/aladin_central_europe_2km.domain-source.csv' (FORMAT CSV, HEADER true, NULL 'NULL');
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/aladin_central_europe_2km.domain-source-metrics.csv' (FORMAT CSV, HEADER true);
COPY (SELECT * FROM info_domain_aladin_central_europe_2km) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/aladin_central_europe_2km.domain-info.csv' (FORMAT CSV, HEADER true, NULL 'NULL');
COPY (SELECT metrics::VARCHAR AS metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1) TO '/home/blizhan/repo/github/duckomo/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-h3-public-source/h3-local/public-source/aladin_central_europe_2km.domain-info-metrics.csv' (FORMAT CSV, HEADER true);
