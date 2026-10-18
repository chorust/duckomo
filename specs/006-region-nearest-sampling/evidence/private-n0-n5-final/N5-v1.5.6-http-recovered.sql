LOAD '/home/blizhan/repo/github/duckomo/build/official-matrix/v1.5.6/builds/cf0e88531eb34e8a357a8926a2d54e841245a6ffb6833ca783d15e9216620125/release/extension/duckomo/duckomo.duckdb_extension';
LOAD '/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.6/official/spatial.duckdb_extension';
LOAD '/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.6/official/httpfs.duckdb_extension';
SET ca_cert_file='/home/blizhan/repo/github/duckomo/build/regions-remote/005-completion-20261013/tls/loopback.crt';
-- Private S3 setup supplied via stdin.
SELECT sum(temperature_80m) FROM read_om('http://127.0.0.1:38829/real.om?state_key=n5-v1.5.6',domain:='ncep_gfs025',variable:='temperature_80m') WHERE lon=116.5 AND lat=40.0;