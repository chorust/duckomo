# DuckOMO httpfs companion patches

This directory contains the narrow adapter needed for DuckOMO's strict remote
range sessions. The source dependency is the MIT-licensed
[`duckdb-httpfs`](https://github.com/duckdb/duckdb-httpfs) checkout at commit
`c3f215ab360f04dc3d3d5305fa81849c0121f111`, paired with DuckDB `v1.5.4`
commit `08e34c447bae34eaee3723cac61f2878b6bdf787`.
The upstream checkout remains untouched; `scripts/stage-httpfs.sh` copies the
pinned tree into `build/httpfs-stage-v2/source`, copies in the shared ABI header,
checks every SHA-256 from `manifest.json`, and applies patches in ascending
`order`.

Patch revision `duckomo-httpfs-range-v2` is applied in this order:

1. `0001-om-range-session.patch` — range-session provider, strict response
   handling and response/body observer hooks.
2. `0002-om-capabilities.patch` — registers
   `httpfs_om_range_capabilities()` for ABI and build identity reporting.

`httpfs_om_range.hpp` is copied into the staged upstream include directory and
is also included by DuckOMO. Its ABI version, upstream commit and patch
revision are compile-time constants. The manifest records the source and
patch hashes; update those hashes whenever any source patch or shared header
changes.

The two extensions are a release pair. Remote `read_om` requires the matching
patched httpfs and DuckOMO artifacts built against the same DuckDB source and
shared header. Loading any upstream or differently patched httpfs must fail
remote capability negotiation. Local `read_om` and `read_om_raw` do not
require httpfs to be loaded. For a loadable build, load the companion `httpfs`
extension before `duckomo` as shown in the feature quickstart.

The patches are project changes under the repository's Apache-2.0 license;
the upstream source retains its MIT license and notices.
