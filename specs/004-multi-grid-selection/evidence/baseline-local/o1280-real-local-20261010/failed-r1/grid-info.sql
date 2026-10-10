LOAD '/home/blizhan/repo/github/duckomo/build/release/extension/duckomo/duckomo.duckdb_extension'; SET threads=1; SET preserve_insertion_order=true;
COPY (SELECT * FROM om_grid_info('/home/blizhan/repo/github/duckomo/build/s3-samples/openmeteo-v3/ecmwf-ifs-hsurf-o1280.om', domain := 'ecmwf_ifs')) TO '/home/blizhan/repo/github/duckomo/build/hres-o1280/validation-20261010-r1/grid-info.csv' (HEADER, NULL 'NULL');
COPY (SELECT * FROM duckomo_last_scan_metrics()) TO '/home/blizhan/repo/github/duckomo/build/hres-o1280/validation-20261010-r1/grid-info.metrics.csv' (HEADER);
