# Region validation integration

T005 and T042 are complete. The region input/geometry sources, three native tests, SQL files, fixture tool, and validator are registered in CMake. `duckomo_region_validation_bundle` builds the validator, fixture generator, and three native tests. The target and the full `duckomo_developer_tools` target both built successfully.

The full local integration command `scripts/validate.sh build/release --local-only --regions --region-gates P0,P2` completed with exit code 0. It ran the ordinary local SQL/native/Python checks in the normal no-spatial test processes, then verified the spatial package hash and ran P0/P2 through the region validator. The saved region output is `build/evidence/regions/local-v1.5.4-9D2zwEip/`.

The integration fails closed when a requested dependency is unavailable: `scripts/validate.sh build/release --local-only --regions --spatial /tmp/duckomo-missing-spatial.duckdb_extension` exited 2 with a missing-spatial diagnostic. The no-spatial isolation test independently passed while confirming ordinary `read_om`, `read_om_raw`, and `om_grid_info` remain available.

The complete region-validation target defaults to P0/P1/P2. It returns 2 while full P1 is `not-run` pending the five frozen real-target comparisons; synthetic success is not substituted for that gate.
