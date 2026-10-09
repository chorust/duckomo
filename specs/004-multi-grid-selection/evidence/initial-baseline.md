# 004 Initial Baseline

Recorded 2026-10-06 from the repository root before 004 source changes.

## Current local build

| Input | Identity |
|---|---|
| Repository | `main`, commit `4273a7dbe2b1897d83f77897ac71d99e382b0db1`; working tree already contains the untracked 004 planning files and an uninitialized `? .specify/extensions/roadmap` submodule entry |
| Platform | Linux 6.17.0-1029-nvidia, AArch64 |
| Build | Release, `build/release-vcpkg`, vcpkg triplet `arm64-linux` |
| Compiler / CMake | GCC 13.3.0 / CMake 3.28.3 |
| DuckDB | v1.5.4, `08e34c447bae34eaee3723cac61f2878b6bdf787` |
| OM C | `d8855e418e2231ae8439f0c7e840fa3f93b371e3` |
| extension-ci-tools | `b777c70d30942cca5bef62d6d4fa23a13362f398` |
| CLI SHA-256 | `c7649dca212e23511766bf6b865c39f6d3e7687b5e38b4ab949559a0eb70beba` |
| DuckOMO extension SHA-256 | `9525cbab840069aee7c7f16df1794d35bd720c7aa4bde52119f37e1d28d7517d` |
| httpfs extension SHA-256 | `1b0bea6729c9891ea788e304d69b1e98722d91de038ff523568af1d43092671c` |

Identity command run in this baseline capture:

```sh
build/release-vcpkg/duckdb -version
sha256sum build/release-vcpkg/duckdb \
  build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension \
  build/release-vcpkg/extension/httpfs/httpfs.duckdb_extension
```

Exit code: 0. No 004 grid sources or build artifacts existed at capture time.

## Existing local regression evidence

These are recorded historical results, not reruns against 004 changes.

| Phase | Command | Exit | Result / source |
|---|---|---:|---|
| 001 | `make test` | 0 | SQLLogicTests, native checks, and local release harness passed; [001 final evidence](../../001-local-om-scanner/evidence/final.md) |
| 001 | `scripts/validate.sh build/release` | 0 | Local fixture/hash and validation suite passed; same evidence file |
| 002 | `make test` | 0 | Spatial SQL/native gates passed on Linux AArch64; [002 final evidence](../../002-spatial-pushdown/evidence/final.md) |
| 002 | `scripts/validate.sh build/release` | 0 | Existing spatial release validation passed; same evidence file |
| 003 | `scripts/validate.sh build/release-vcpkg` | 0 | Local G0–G2 and local part of G4 passed after stale artifacts were rebuilt; [003 final evidence](../../../evidence/003-dimensions-remote-parallel/final.md) |
| 003 | `test/tools/duckomo_remote_validation ...` | not run | Controlled HTTP/S3 endpoints and server audit were not configured; G3/G5/G6 and full remote G4 remain open |
| 003 | Independent quickstart replay | not run | G7 remains open because no independent reviewer run is recorded |

`docs/issues/real-world-om-compatibility.md` remains the separate 1.5.5 record: eleven real OM v3 files were inspected, with full-value comparison for one sample. That scope does not establish 004 grid support.

## 003 dependency gate

The 003 record explicitly says its full G0–G7 release acceptance did not pass. In particular, remote protocol/body reconciliation, signed S3 revocation/recovery, cancellation and isolation, and independent reproduction are not evidence available to 004. These gates remain dependencies for 004 H6/H8/H9 and are not inferred from prior task checkboxes.
