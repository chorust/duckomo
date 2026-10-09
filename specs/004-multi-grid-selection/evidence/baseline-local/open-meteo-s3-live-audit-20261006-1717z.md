# Open-Meteo S3 live bucket audit

**Captured**: 2026-10-06 17:17 UTC (2026-10-07 01:17 Asia/Shanghai)

**Scope**: Anonymous, read-only S3 prefix listing, object HEAD, and small metadata GETs. This updates the bucket-presence evidence only; it does not complete T003, any Gaussian coordinate/value gate, signed-S3 authorization, or remote-benefit validation.

## Method

Requests used the public regional endpoint `https://openmeteo.s3.us-west-2.amazonaws.com/`:

- `GET /?list-type=2&delimiter=%2F&max-keys=1000&prefix=<candidate-prefix>` to inspect exact candidate prefixes.
- `HEAD /<key>` to record current object length, ETag, Last-Modified, and VersionId state.
- `GET` of small `meta.json` / `latest.json` files to compute SHA-256; no complete weather-data object was downloaded for this audit.
- `GET` with `Range: bytes=0-15` for selected `.om` objects to confirm the `OM\x03` header prefix. These 16-byte responses are format-presence evidence only, not full object hashes or official metadata validation.

The requests were made around 2026-10-06 17:17 UTC. S3 returned no VersionId for the listed objects; ETags are recorded as opaque server metadata and are not treated as content hashes.

## Gaussian-family and ensemble findings

The public bucket contains a real ECMWF IFS HRES forecast time-series OM v3 object:

| Object | Current S3 metadata | Format / interpretation |
|---|---|---|
| `data/ecmwf_ifs/temperature_2m/chunk_817.om` | 353,999,720 bytes; ETag `"8aacc014058aca39a7a1ae9800fbef58-43"`; Last-Modified `2025-04-03T01:11:58Z`; no VersionId | First 16 bytes `4f4d03c0ff7f00440600000000000000`; this is the already acquired HRES O1280 OM v3 supplement. Its prior manifest records full object SHA-256 `483dd0be2096d3e3fff6731509400f97591ebcfc237a941a37460f42a7a10e7f`, shape `[1,6599680,504]`, and chunks `[1,6,504]`. |
| `data/ecmwf_ifs/static/meta.json` | 658 bytes; ETag `"fb4897de46cf3c3784d2ed477a700bd9"`; Last-Modified `2026-10-06T12:31:56Z`; no VersionId; SHA-256 `25e49831b48e4ae0af2332ab940b0ebd11332dbf6e5c361ce578f48b666ce5ae` | WKT remark is `Reduced Gaussian Grid O1280 (ECMWF)`. This current metadata hash differs from the earlier 2026-10-06 acquisition snapshot, showing that the public metadata object is mutable. |
| `data/ecmwf_ifs/static/HSURF.om` | 2,482,560 bytes; ETag `"91286afefb256fd70a8e34b18fd4ddf7"`; Last-Modified `2025-10-21T09:36:01Z`; no VersionId | First 16 bytes `4f4d03c019fc00000000c019fc000000`; the previously acquired supplemental O1280 static field. |

The live listing also confirms that ensemble products are present in this bucket now:

| Prefix/object | Current S3 evidence | Grid metadata |
|---|---|---|
| `data/ecmwf_aifs025_ensemble/` | Prefix listing returned two child prefixes: `precipitation_probability/` and `static/`. The former had 25 objects (`chunk_1128.om` through `chunk_1152.om`); `chunk_1151.om` is 34,446,864 bytes, ETag `"4d02795ce5a62f7753d978e99f9dfde0-5"`, Last-Modified `2026-10-06T13:38:24Z`, no VersionId. Its first 16 bytes are `4f4d0300400b08070403040403141708`. | `data/ecmwf_aifs025_ensemble/static/meta.json`: 595 bytes, ETag `"a3575f245e61b76bb89d438526f9696f"`, Last-Modified `2026-10-06T13:40:03Z`, SHA-256 `0c4783863680058eeae234afa4c86d0bdf987adebb5874abe2b88d4da9cb5b1f`. Its WKT is WGS 84 geographic with world BBOX `[-90,-180,90,179.75]`. |
| `data_spatial/ecmwf_aifs025_ensemble/` | Root listing returned the `2026/` child plus `latest.json` and `in-progress.json`. The two JSON objects were byte-identical: 1,734 bytes, ETag `"56e031224915da9ca35e620700896116"`, Last-Modified `2026-10-06T12:58:07Z`, SHA-256 `00ed255f685802d91bd5d5dcb5b31556497fb81fb4aed4ed17c40d1b97512e56`. | `latest.json` names variable `precipitation_probability`, reference time `2026-10-06T06:00:00Z`, and the same WGS 84 world-BBOX metadata. This is not the 14,747-point N320 reduced-Gaussian Europe mapping. |

The exact candidate prefixes `data/ecmwf_aifs_europe_ensemble/` and `data_spatial/ecmwf_aifs_europe_ensemble/` each returned `KeyCount=0` and `IsTruncated=false`. The similarly named `ecmwf_aifs025_ensemble` products are real public ensemble products, but the available metadata identifies WGS 84 world coverage; it does not identify the N320 parent grid or the required regional local-to-parent point order.

## Seasonal Gaussian metadata

Two seasonal / extended-range model metadata objects are present under their actual prefixes. Neither is N160:

| Metadata object | Size / ETag / Last-Modified / SHA-256 | WKT grid remark |
|---|---|---|
| `data/ecmwf_seas5/static/meta.json` | 660 bytes / `"f25549b41f17a6910cc5177cb3431033"` / `2026-10-05T14:10:49Z` / `a474ec82a13e5e2f58316a03d3941ad7db98d8fd4fcdca9e77c4118b88bc57d9` | `Reduced Gaussian Grid O320 (ECMWF)` |
| `data/ecmwf_ec46/static/meta.json` | 658 bytes / `"4e1041db19f740a7d88bc1c870ebecd6"` / `2026-10-05T20:36:04Z` / `fd61d94941488e1685f55f89284f4064b44e3a4dfcba987764ca0c3ac6846df0` | `Reduced Gaussian Grid O320 (ECMWF)` |

Listing `data/ecmwf_seas5/` and `data/ecmwf_ec46/` returned only their `static/` child prefixes. These metadata objects do not provide a matching N160 OM v3 forecast sample. The available O320 static metadata also cannot be relabeled N160.

## Corrected interpretation and remaining evidence

The pinned Open Data README at commit `4fd52ad16c417c49bff45fab4bf175e5ea5760f2` has SHA-256 `85c2b2bee77119dfaa7b09ff96fac49bd06b8bb7cbad54e50537fbb7267c75a1` and says: “Climate, flood, satellite and ensemble models are not published on AWS due to their immense size.” The current bucket listing contradicts that blanket statement for `ecmwf_aifs025_ensemble`; preserve the README text as the pinned-source snapshot and use this dated S3 audit for current availability.

The existence of HRES O1280 and current AIFS ensemble products does not complete the Gaussian acceptance inputs. Required N160, full N320, and N320-region samples still have no fixed matching object in the audited public prefixes. O1280, O320, and the current WGS 84 AIFS product are distinct grid identities and cannot stand in for those inputs.

- Bucket objects are mutable and no VersionId was returned; the listed ETags are not content hashes.
- The AIFS chunk was checked only by HEAD and its first 16 range bytes. It was not downloaded, checked with the official OM C metadata API, or added as a fixed sample.
- Public anonymous access does not exercise signed-S3 authorization or service-side attempt accounting.
- Required-grid producer point order, independent coordinates, official full-value references, and selective-read benefit remain not-run.

## Follow-up live check: existing OM files only

**Captured**: 2026-10-08 00:49 UTC. This follow-up uses anonymous, read-only requests to the same regional endpoint. S3 ListObjectsV2 XML was parsed with its `http://s3.amazonaws.com/doc/2006-03-01/` namespace; unlike the earlier failed local parse attempt, the counts below are valid.

- Root delimiter listings returned 117 `data/` model prefixes and 80 `data_spatial/` prefixes, both without truncation.
- `data/ecmwf_aifs_europe_ensemble/` and `data_spatial/ecmwf_aifs_europe_ensemble/` each still return `KeyCount=0`, `IsTruncated=false`.
- Successful current `static/meta.json` reads for the relevant existing products identify `data/copernicus_era5/`, `copernicus_era5_ensemble/`, `copernicus_era5_land/`, `copernicus_era5_ocean/`, and ECMWF 0.25-degree/AIFS ensemble products as WGS 84 geographic grids; `data/ecmwf_ifs/` and `ecmwf_ifs_analysis_long_window/` identify O1280; `ecmwf_ec46/`, `ecmwf_ec46_ensemble_mean/`, `ecmwf_seas5/`, and `ecmwf_seas5_ensemble_mean/` identify O320.
- Existing weather OM objects are present for the O320 products: `data/ecmwf_ec46_ensemble_mean/temperature_2m/chunk_445.om` is 50,014,024 bytes (ETag `"d1ae937ab3abac8f25182d48bc54b69d-6"`, Last-Modified `2026-02-18T20:36:00Z`); `data/ecmwf_seas5_ensemble_mean/temperature_2m/chunk_113.om` is 77,716,536 bytes (ETag `"f54ef6da29560fbbc9ca1b8564776639-10"`, Last-Modified `2026-04-06T13:15:31Z`). A 16-byte ranged GET from each starts with `OM\x03`; neither object was downloaded in full.
- Current metadata snapshots are mutable: EC46 O320 ensemble-mean metadata SHA-256 is `a3c14d857014637f8b242cddc9ba8c2730a0ea56fc1672bc98b3b48db10a15a0`; SEAS5 O320 ensemble-mean metadata SHA-256 is `619705d137a3a12f96ab755dc781f7921e28477e14d878dcd0c818d7d9251b80`; current HRES O1280 metadata SHA-256 is `7520cf6e87fe5eb9cd4e541d5d19950f2095e97ef240da3a4ec47dfc6486aecc`; current ERA5 WGS 84 metadata SHA-256 is `bafb7cbc4cbd26524b4944d45a3f76d17b61d34f30ea4472b1627bac518c550f`.

**Conclusion for the requested scope**: no existing Open-Meteo OM object or matching metadata for N160, full N320, or the 14,747-point N320 regional mapping was found in the audited ECMWF/ERA5 prefixes. O320 is explicitly a different grid identity from N320 and cannot stand in for it. This check does not include conversion of any other source. Some weekly/monthly ECMWF prefixes do not publish a `static/meta.json`, so this result is about the metadata-backed relevant products and the exact candidate regional prefixes above; no full-bucket OM object decode was run.
