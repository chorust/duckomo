# Open-Meteo Public OM Remote Source Diagnostics

Status: **endpoint range check pass; DuckOMO remote source subcheck fail before scan**.

## Existing OM object examined

The Open-Meteo manifest sample `rotated_cmc_gem_rdps_10km_cape_chunk_4337` is an existing OM v3 object. Its pinned local copy hash remains `3db637197d27c65051e417b850f8c3656d1c5371ab78dcbd9fc05d0e440956f7`. No conversion or full remote download was performed.

The current public HTTPS endpoint returned HEAD `200` and `Content-Length: 68016448`. A single `Range: bytes=0-15` request returned `206`, `Content-Range: bytes 0-15/68016448`, and an `OM\x03` prefix. The current ETag and Last-Modified match the manifest snapshot; the multipart ETag is weak metadata, not a content hash. The exact headers and response bytes are recorded in [remote-endpoint-range-check.json](remote-endpoint-range-check.json).

## DuckOMO paired-build attempts

Using paired baseline build `5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d`, `grid_spatial_reference.py` failed before producing a scan for both the manifest HTTPS URL and `s3://openmeteo/...`. The S3 attempt was also repeated with region inferred as `us-west-2`; it failed at the same strict range-session initialization. Exact commands, exits, and logs are retained in:

- [HTTPS attempt](../us5-20261008-h1-h2-https-source-info-5dac993/manifest.json)
- [S3 attempt without explicit region](../us5-20261008-h1-h2-s3-source-info-5dac993/manifest.json)
- [S3 attempt with inferred us-west-2 region](../us5-20261008-h1-h2-s3-usw2-source-info-5dac993/manifest.json)

The server endpoint itself supports byte ranges, so these runs do not show that the OM object is unavailable. They show that the paired DuckOMO/HTTPFS remote session could not be established in this environment. No weather values or full OM object body were downloaded by these DuckOMO attempts.

T070 H6 public cross-URI evidence remains incomplete. Local H1/H2 public source evidence is recorded separately; object IDs are still URI-specific, and remote content equality is not claimed from ETag alone.
