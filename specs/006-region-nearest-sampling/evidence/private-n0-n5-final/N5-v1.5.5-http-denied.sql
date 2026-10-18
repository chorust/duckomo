LOAD '/home/blizhan/repo/github/duckomo/build/official-matrix/v1.5.5/builds/8c86d97a161b1f2f5b88c7714fe5f36fd896d12c8b53acdc9b679a4fd88de8c9/release/extension/duckomo/duckomo.duckdb_extension';
LOAD '/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.5/official/spatial.duckdb_extension';
LOAD '/home/blizhan/repo/github/duckomo/build/regions-runtime/v1.5.5/official/httpfs.duckdb_extension';
SET ca_cert_file='/home/blizhan/repo/github/duckomo/build/regions-remote/005-completion-20261013/tls/loopback.crt';
-- Private S3 setup supplied via stdin.
SELECT sum(temperature_80m) FROM read_om('http://127.0.0.1:38829/real.om?state_key=n5-v1.5.5',domain:='ncep_gfs025',variable:='temperature_80m') WHERE lon=116.5 AND lat=40.0;