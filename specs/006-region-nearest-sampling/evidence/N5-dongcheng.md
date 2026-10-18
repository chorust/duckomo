# N5 real Dongcheng and controlled remote evidence

**Gate status: partial; local real-data and controlled transport subchecks passed.** The complete N5 deliverable gate remains partial because redistribution terms for the upstream raw NOAA/NCEP object and a durable public archive path are unresolved. N1 and N2 pass for their declared profile scope; overall 006 acceptance remains incomplete because N5 is partial and N6 is unfinished; the recorded N7 local reproduction was executed by an implementation worker and does not satisfy the independent-executor requirement. This report does not change the archived validator run.

## Subsequent private-use authorization

The owner subsequently confirmed no public redistribution, permission for internal validation/private retention, and access for authorized later validators. See [N5-private-use-decision.md](N5-private-use-decision.md). The existing 112,068,504-byte object was rehashed and matches the frozen SHA-256. Public redistribution authorization is no longer a completion prerequisite for this privately scoped acceptance. N5 remains partial pending a documented private durable-acquisition/retrieval arrangement and a registered execution package against current artifacts. The public-archive/licensing blockers described in the historical capture below are superseded by this decision for private use only; they must not be interpreted as independently resolved public redistribution rights.

## Frozen inputs and provenance

### Dongcheng geometry

- Source: [geoBoundaries CHN ADM3 simplified GeoJSON](https://github.com/wmgeolab/geoBoundaries/raw/9469f09/releaseData/gbOpen/CHN/ADM3/geoBoundaries-CHN-ADM3_simplified.geojson), pinned to commit `9469f09592ced973a3448cf66b6100b741b64c0d`.
- Source archive SHA-256: `ea64fe24f193e319ce48895f229a5b3f46d2a60f3f3df67e6172ab037e4cc8fd8`.
- The pinned archive contains `shapeName=Dongcheng District`, `shapeID=62558664B68263371825694`, `shapeGroup=CHN`, `shapeType=ADM3`. Its polygon has 83 exterior coordinates including closure. The exported feature is one polygon; its bbox is `[116.37238629988792, 39.85655029985651, 116.44467660014504, 39.97264649963631]`.
- GeoJSON declares OGC CRS84 (longitude, latitude). geoBoundaries ADM3 metadata reports represented year 2017, source Lee Beryman / OpenStreetMap, and Open Data Commons Open Database License 1.0. See the [geoBoundaries metadata API](https://www.geoboundaries.org/api/current/gbOpen/CHN/ADM3/).
- Tracked one-feature export: [`dongcheng-geoboundaries-2017.geojson`](../../../test/data/sampling/geometries/dongcheng-geoboundaries-2017.geojson), 3,498 bytes, SHA-256 `5a3be71b77a44fcd2d0132e57345fd679155fa07b70de4a13b9df706cf98c508`. Compact selected feature SHA-256: `7deee811c16fb51df548b3bc73725e917a1102b2d9902044decae5c5df90690c`.
- The geometry is a simplified administrative boundary for represented year 2017; it does not claim current administrative accuracy. It is stored as data under ODbL 1.0 with attribution and share-alike obligations.

### GFS object

- Key: `data_spatial/ncep_gfs025/2026/10/14/0000Z/2026-10-14T0000.om`.
- Public URL: `https://openmeteo.s3.us-west-2.amazonaws.com/data_spatial/ncep_gfs025/2026/10/14/0000Z/2026-10-14T0000.om`.
- Anonymous HTTPS GET returned 200 on 2026-10-17, length 112,068,504 bytes. The downloaded copy and existing workspace copy have the same SHA-256: `676742c9fe81dd7af7472c7f41d3651d8bd3ff5bcaf1c47b965d415310472375`.
- HEAD metadata: ETag `72ea9265019f751a01d3fbb520d1a0f4-14`, Last-Modified `2026-10-14T03:40:30Z`, expiration `2026-10-22T00:00:00Z` (`DeleteSpatialDataOlderThan7Days`). No S3 VersionId was available.
- AWS Open Data Registry lists the Open-Meteo dataset as CC BY 4.0. The [Open-Meteo open-data README](https://github.com/open-meteo/open-data/blob/main/README.md) says `data_spatial/` files are retained seven days and identifies forecast sources including NOAA NCEP. Open-Meteo's [data license page](https://open-meteo.com/en/licence) lists NOAA NCEP forecasts with a separate unspecified upstream license. NOAA's [NCEP restricted-data policy](https://www.nco.ncep.noaa.gov/pmb/docs/restricted_data/) says restrictions on restricted inputs remain applicable after reformatting, but the page does not identify this GFS object or establish that it contains restricted input. The available sources do not resolve this object's redistribution rights. The 112 MB OM object remains outside tracked fixtures, and no archive copy is claimed.

The inventory is [`test/data/sampling/real-inputs.json`](../../../test/data/sampling/real-inputs.json). It records the still-open raw-data licensing and durable acquisition limitations.

## Real-data query result

For each runtime, the test used `domain := 'ncep_gfs025'`, the tracked Dongcheng feature, `variable := 'temperature_80m'`, `region_crs := 'grid_lonlat'`, and `include_source := true` for nearest. The points query returned zero rows. The nearest result was one `ok` row:

| Check | Observed |
| --- | --- |
| Representative target (lat, lon) | `(39.91557869989714, 116.40737101384086)` |
| Target is inside Dongcheng polygon | yes, verified by the supported spatial adapter used by the query |
| Nearest source (lon, lat) | `(116.5, 40.0)` |
| Distance | `12266.306480960757 m` |
| `source_point_index` | `749986` |
| `temperature_80m` | `13.45` (`FLOAT`) |
| Direct `read_om` at source coordinate | `13.45`, source `point_index=749986`, `logical_index=749986` |
| points row count | `0` |

The expected source coordinate, point index, variable and value match [`real-inputs.json`](../../../test/data/sampling/real-inputs.json).

## Runtime and transport matrix

Each runtime compared the full nearest semantic row columns: region, target coordinates, location and value status, source coordinate, distance, source point index, `FLOAT` value, logical index, original point index, grid ID and layout ID. All fields matched the local result across controlled HTTP, CA-verified HTTPS and signed S3. `om_source.object_id` differs by transport URI as expected and was not used as a value/location oracle.

| DuckDB | CLI SHA-256 | DuckOMO SHA-256 | spatial SHA-256 | HTTPFS SHA-256 |
| --- | --- | --- | --- | --- |
| v1.5.4 | `513b573b5ad3a94ad2c0668b1ccefb5f281b6f7aa2a5e2ff057825473848e69a` | `a9aec833be443d481a0ce583ec529eea6f58fd97aeab16c42149ac64b359e7fa` | `af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a` | `6d30a487968cbe5553b7272fd5bad0e4485c4117db96e21fd1e5e2a225a5a538` |
| v1.5.5 | `9882c99a9804407de82c0edb1816d7667733d37d771a98eb23ad5f6a8d37acb1` | `94d87241f57df140970b95bfa3e0c46476da76df4c3d8ba33ace3ba5bada6520` | `b9129a7da7fde8eb7fb951d99a115cbf2e6fa34c8d41671086169174a1f5725d` | `eba6e263e395a83966090f1f11ade63630b1b21422f0f2813858d179d42ea1e9` |
| v1.5.6 | `c0ac0b79e243ee312b3af29c34ac0b94e518aa44c4345036d5d96c1c042c9d48` | `75ecd0c5c273f6793e4c01ac38a058e720ec944b60b949600c31c88c91273824` | `96b8d4b5d735e9ebe91da7badee4328dfbd07068ce4885d1d9b6de8c22370b14` | `ae940d800b7f938a4848d0eebcffb28dda0bbed76987f11dc55faa6d1a86db0d` |

Per-runtime point and nearest CSVs are retained in [`n5-dongcheng/real`](n5-dongcheng/real). They include all four input sources. Semantic columns agree; `object_id` remains visible to show the distinct transport identities.

## Controlled synthetic differential and failure recovery

A separate fixed 5×5 `regions.om` fixture was run through local, HTTP, HTTPS and signed-S3 input for both `method='points'` and `method='nearest'`:

- Points returned zero rows for every source.
- Nearest returned 18 rows for every source (three regions × two members × three times), with six `ok`, six `empty_geometry`, and six `outside_domain` rows.
- Sorted semantic multisets were identical across all four sources after excluding URI-dependent `object_id` and the source label.
- CSV outputs are retained in [`n5-dongcheng/synthetic/v1.5.4`](n5-dongcheng/synthetic/v1.5.4).

The controlled HTTP fixture was switched to deny one object; the value query failed with a safe redacted remote-open error. The fixture was restored and the same normal query completed with sum `54912.0`. The private command output and service log hash were checked; no credentials or authorization headers were present in the JSONL log. The 005 services used a temporary S3 service identity supplied by that fixture (`version 30GB 4.48 56fbc3deb linux arm64`, declared digest `sha256:8c63bce99de77850e9189e54a93fce1ac9ba189c4995be3fb3927bcd39ae3ab9`); independent service-image identity was not re-established for this run. Sanitized service logs are retained as [`http-service.jsonl`](n5-dongcheng/http-service.jsonl) and [`s3-service.jsonl`](n5-dongcheng/s3-service.jsonl), SHA-256 `91a55732523b7aa5e4e0b09ca0a4375dc0f10b700cf5908511fb8b73d84439fd` and `85531ce97f823266321f80efc6042aaf074e71643ecffcaaf82543399e280afc`. They contain request/range/status/body-byte records only; credential markers were checked absent.

## Scope and residual status

Passed subchecks: real local points/nearest semantics and source-value comparison on the three pinned DuckDB versions; same real object via controlled HTTP, HTTPS and signed S3 on the three versions; four-source synthetic points/nearest differential; controlled HTTP denial and subsequent recovery.

N5 remains **partial** because the public spatial GFS object is temporary and raw-source redistribution terms remain unverified. Remote service byte counts are not presented as HTTPFS client transport metrics. This evidence does not change the N1/N2 scoped passes, complete N6, or satisfy N7's independent-executor requirement; overall 006 acceptance remains incomplete.
