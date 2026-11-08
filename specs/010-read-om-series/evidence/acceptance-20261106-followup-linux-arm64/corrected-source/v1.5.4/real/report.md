# read_om_series Real Producer Object Validation

- Feature: `010-read-om-series`
- Run status: **passed**
- Started: `2026-11-06T06:38:08Z`
- Mode: `real`

## S7 gate

- Status: **passed** — live read_om_series, manual read_om UNION ALL and frozen boundary rows matched

## Frozen object identities

- `data/ecmwf_ifs025/temperature_2m/chunk_1594.om`: **passed**; ETag `"3958affd1f109a35a6320048231668d4-8"`, size `66552160`, last modified `2026-10-09T01:03:49Z`
  - live HEAD: status `200`, ETag `"3958affd1f109a35a6320048231668d4-8"`, size `66552160`, last modified `2026-10-09T01:03:49Z`
- `data/ecmwf_ifs025/temperature_2m/chunk_1595.om`: **passed**; ETag `"2723deb82b74aee469f11cf7e948ec2d-9"`, size `67160304`, last modified `2026-10-22T01:44:24Z`
  - live HEAD: status `200`, ETag `"2723deb82b74aee469f11cf7e948ec2d-9"`, size `67160304`, last modified `2026-10-22T01:44:24Z`

## Provenance

- Model key: `ecmwf_ifs025`; producer commit `b06f4760fd1f997e5559bb380f64c5e496b4a509`
- Temporal registry SHA-256: `e3dc7e177cdf6c63b70a28933b8efa8769e81f7329d5f8f368173bb8d4ce4cf0`
- Live metadata SHA-256: `d1d9fa295ed8fd4b1acdb158df18122436225c11bb3a0bb0fa97eba378b8ac74`
- Domain: `ecmwf_ifs025`; point `{'lat': 0.0, 'lon': 0.0}`

## Oracle comparison

- Schema equal: `True`; SQL two-way EXCEPT ALL empty: `True`
- Series/manual multiset equal: `True`; series/frozen oracle equal: `True`; rows `2` vs `2` vs `2`
- Engine: `v1.5.4`
- Frozen rows: `[{"lat": 0.0, "lon": 0.0, "valid_time": "2026-10-08 21:00:00", "value": 25.950000762939453}, {"lat": 0.0, "lon": 0.0, "valid_time": "2026-10-09 00:00:00", "value": 25.600000381469727}]`

## Input retention

- The validator issues identity HEAD requests and uses DuckDB HTTPFS for the two selected object reads.
- No OM object bytes are written to the repository; evidence retains listing/metadata/HEAD snapshots, SQL and selected rows only.
- The result covers one registered IFS025 domain point and one adjacent-chunk boundary window.
