# Version matrix build and gate evidence

Recorded 2026-10-07 on Linux AArch64 with GCC 13.3.0, CMake 3.28.3, `arm64-linux`, and `libstdc++_cxx11abi=1_gcc=13.3.0_cxx17`.

Both pairs reached **build-verified**. This evidence covers isolated builds, paired extension loading, ABI identity checks, and the version compatibility SQL test. It does not complete H0–H8, establish remote grid support, or validate a released DuckDB 2.0 combination.

## Frozen inputs and staged HTTPFS

| Pair | DuckDB | HTTPFS | OM C | extension-ci-tools | Official overlays | DuckOMO patch set |
|---|---|---|---|---|---|---|
| `baseline-1.5.4` | `08e34c447bae34eaee3723cac61f2878b6bdf787` | `c3f215ab360f04dc3d3d5305fa81849c0121f111` | `d8855e418e2231ae8439f0c7e840fa3f93b371e3` | `b777c70d30942cca5bef62d6d4fa23a13362f398` | none | `0001` `c186b4eb394a3971e98b1b471ed3471e800f0efc672c1e5825742ee7e57da36c`; `0002` `5aae730e8c6ba505ae688fe3dffa59e7a5872525e8c374bfc1f360673b9057e8` |
| `prerelease-2.0-dev` | `7264a9f0e5b487358100f408826b8ae9e868b031` | `5e34903685e4d429cbb19b063406abdd8ce30591` | `d8855e418e2231ae8439f0c7e840fa3f93b371e3` | `9b1020499dc85966be3342541f98c8e8e4aada89` | order 1 `0002-collection-get-value.patch` `3833250e557ee83c0a85e8fa93a1d11c04f72ea3a62277c0d2e560661bd9b289`; order 2 `0003-duplicate-secret-option-error.patch` `7e8b3efe0247b26a7c2e5ab5e978fe53de1a6b22bf8d4be50091f98793d3ce16` | `0001` `28517fe12bf26597a0094fd2a0e676d557338cf097f69ab4a3e9d7f84aa6a009` |

Both pairs use ABI3 shared header SHA-256 `5b751573f5b24d03ff241e56916271906590f6d7c7569a172ed15160fef80181`. Their stage manifests show the baseline's two DuckOMO patches and the prerelease's two ordered official overlays followed once by its DuckOMO patch. See `build/grid-matrix/<pair>/builds/<build_id>/httpfs-stage/stage.json` for each applied hash and staged source tree hash.

The matrix source digest is `dbc22a26ed0fb44240ecc24412cd21886af961cfad2efc800a89b300c0c53110`. Mutable result fields in `test/data/grids/version-matrix.json` are excluded from this digest; each selected pair's frozen pins, patch declarations, platform, and options remain in the build ID input payload. The source digest is `dbc22a26ed0fb44240ecc24412cd21886af961cfad2efc800a89b300c0c53110`. The updated manifests were rebuilt from that source digest. After recording the result fields, `version_matrix.py resolve` returned the same build IDs for both pairs (exit 0) and the resolved input payload matched each build manifest.

Build options for each pair: `CMAKE_BUILD_TYPE=Release`, `BUILD_UNITTESTS=TRUE`, `BUILD_SHELL=TRUE`, `NATIVE_ARCH=FALSE`, `ENABLE_UNITTEST_CPP_TESTS=FALSE`, `ENABLE_EXTENSION_AUTOLOADING=FALSE`, `ENABLE_EXTENSION_AUTOINSTALL=FALSE`. Both used vcpkg commit `e06564091c10e0b042900800c9fd48aad7f00643` and the pinned `arm64-linux` installed dependency tree recorded in their manifests.

## Final builds

| Pair | Build ID | Runtime CLI version | Runtime source ID | Build manifest |
|---|---|---|---|---|
| `baseline-1.5.4` | `15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8` | `v1.5.4 (Variegata) 08e34c447b` | `08e34c447bae34eaee3723cac61f2878b6bdf787` | `build/grid-matrix/baseline-1.5.4/builds/15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8/build-manifest.json` |
| `prerelease-2.0-dev` | `9a7aff37d7cc7c910cc0d29fc08a0fb51d6f199f3115791731d71eb00630312b` | `v2.0.0-dev86261 (Development Version) 7264a9f0e5` | `7264a9f0e5b487358100f408826b8ae9e868b031` | `build/grid-matrix/prerelease-2.0-dev/builds/9a7aff37d7cc7c910cc0d29fc08a0fb51d6f199f3115791731d71eb00630312b/build-manifest.json` |

Runtime version commands exited 0:

```sh
/home/blizhan/repo/github/duckomo/build/grid-matrix/baseline-1.5.4/builds/15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8/release/duckdb -version
# v1.5.4 (Variegata) 08e34c447b

/home/blizhan/repo/github/duckomo/build/grid-matrix/prerelease-2.0-dev/builds/9a7aff37d7cc7c910cc0d29fc08a0fb51d6f199f3115791731d71eb00630312b/release/duckdb -version
# v2.0.0-dev86261 (Development Version) 7264a9f0e5
```

The prerelease checkout initially lacked the pinned commit parent needed to report its development counter. One history level was fetched and the same prerelease build ID was rebuilt; the final CLI reports `v2.0.0-dev86261`. The build manifests record the exact CMake configure and build argv, each with exit code 0. The three artifact hashes from the final manifests were independently recomputed after the gates:

| Pair | `duckdb` SHA-256 | `duckomo` SHA-256 | `httpfs` SHA-256 |
|---|---|---|---|
| `baseline-1.5.4` | `dea62fdc3792705921f8326719a6aedf9927f949a0209411cd3c7211f8972c09` | `8bed889b8f9cf3947aab3b7d94620e0ea6c0d4f4a22c5d26ffc4214411309c3e` | `fe0c1f37b42023e0d60ccf3ec4fc1803e7bf9474bc0c2710959de601c28f292e` |
| `prerelease-2.0-dev` | `d875b9d8c8791c7278fd475887736833d4d63af4c3dee937c031dedd6017de23` | `15e4175f3721d94e7be0095c1f96d7e07580f44ad806212f6e69586d828af277` | `b02a91514a307f64b301ccb4e92454636bd4dfa4ae841e2314559d2bbb69b6d1` |

## Build commands and exit codes

Commands ran from `/home/blizhan/repo/github/duckomo`; each matrix build command exited 0.

```sh
DUCKDB_COMMIT=08e34c447bae34eaee3723cac61f2878b6bdf787 \
DUCKOMO_VCPKG_ROOT=/tmp/duckomo-vcpkg \
DUCKOMO_BUILD_JOBS=4 \
python3 scripts/version_matrix.py build --root . \
  --matrix test/data/grids/version-matrix.json \
  --pair baseline-1.5.4 --jobs 4
# exit 0
```

```sh
DUCKDB_COMMIT=7264a9f0e5b487358100f408826b8ae9e868b031 \
DUCKOMO_VCPKG_ROOT=/tmp/duckomo-vcpkg \
DUCKOMO_BUILD_JOBS=4 \
python3 scripts/version_matrix.py build --root . \
  --matrix test/data/grids/version-matrix.json \
  --pair prerelease-2.0-dev --jobs 4
# exit 0
```

After writing the result fields, the matrix identity was recomputed with the required vcpkg root:

```sh
DUCKOMO_VCPKG_ROOT=/tmp/duckomo-vcpkg python3 scripts/version_matrix.py resolve \
  --root . --matrix test/data/grids/version-matrix.json --pair baseline-1.5.4
# exit 0; build_id 15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8

DUCKOMO_VCPKG_ROOT=/tmp/duckomo-vcpkg python3 scripts/version_matrix.py resolve \
  --root . --matrix test/data/grids/version-matrix.json --pair prerelease-2.0-dev
# exit 0; build_id 9a7aff37d7cc7c910cc0d29fc08a0fb51d6f199f3115791731d71eb00630312b
```

## Runtime gates

For each pair, the native targets were built with its pinned `DUCKDB_COMMIT`; each target build exited 0:

```sh
DUCKDB_COMMIT=08e34c447bae34eaee3723cac61f2878b6bdf787 cmake --build \
  build/grid-matrix/baseline-1.5.4/builds/15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8/release \
  --target grid_zero_io_test source_identity_test core_functions_loadable_extension httpfs_abi_test grid_version_compat_test duckomo_grid_validation --parallel 2
# exit 0

DUCKDB_COMMIT=7264a9f0e5b487358100f408826b8ae9e868b031 cmake --build \
  build/grid-matrix/prerelease-2.0-dev/builds/9a7aff37d7cc7c910cc0d29fc08a0fb51d6f199f3115791731d71eb00630312b/release \
  --target grid_zero_io_test source_identity_test core_functions_loadable_extension httpfs_abi_test grid_version_compat_test duckomo_grid_validation --parallel 2
# exit 0
```

All commands below also ran from the repository root. Each native test exited 0 for both pairs:

```sh
BASELINE_RELEASE=/home/blizhan/repo/github/duckomo/build/grid-matrix/baseline-1.5.4/builds/15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8/release
PRERELEASE_RELEASE=/home/blizhan/repo/github/duckomo/build/grid-matrix/prerelease-2.0-dev/builds/9a7aff37d7cc7c910cc0d29fc08a0fb51d6f199f3115791731d71eb00630312b/release

"$BASELINE_RELEASE/test/native/httpfs_abi_test"
httpfs_abi_test: static/loadable capability and local independence checks passed
exit 0

"$BASELINE_RELEASE/test/native/grid_version_compat_test"
grid_version_compat_test: exact paired identity checks passed
exit 0

"$PRERELEASE_RELEASE/test/native/httpfs_abi_test"
httpfs_abi_test: static/loadable capability and local independence checks passed
exit 0

"$PRERELEASE_RELEASE/test/native/grid_version_compat_test"
grid_version_compat_test: exact paired identity checks passed
exit 0
```

The SQL compatibility test command for each pair was:

```sh
BASELINE_RELEASE=/home/blizhan/repo/github/duckomo/build/grid-matrix/baseline-1.5.4/builds/15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8/release
PRERELEASE_RELEASE=/home/blizhan/repo/github/duckomo/build/grid-matrix/prerelease-2.0-dev/builds/9a7aff37d7cc7c910cc0d29fc08a0fb51d6f199f3115791731d71eb00630312b/release

"$BASELINE_RELEASE/test/unittest" --test-dir /home/blizhan/repo/github/duckomo \
  test/sql/grid_version_compat.test
# exit 0; All tests passed (9 assertions in 1 test case)

"$PRERELEASE_RELEASE/test/unittest" --test-dir /home/blizhan/repo/github/duckomo \
  test/sql/grid_version_compat.test
# exit 0; All tests passed (9 assertions in 1 test case)
```

It exited 0 for both builds and reported `All tests passed (9 assertions in 1 test case)`. The DuckDB 2.0 runner also printed deprecation notices for the test file's `__BUILD_DIRECTORY__` and `__WORKING_DIRECTORY__` placeholders; the test completed successfully.

The final CLI commands loaded the exact HTTPFS and DuckOMO loadable extensions from that pair's release directory, then queried the provider descriptor:

```sh
BASELINE_RELEASE=/home/blizhan/repo/github/duckomo/build/grid-matrix/baseline-1.5.4/builds/15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8/release
PRERELEASE_RELEASE=/home/blizhan/repo/github/duckomo/build/grid-matrix/prerelease-2.0-dev/builds/9a7aff37d7cc7c910cc0d29fc08a0fb51d6f199f3115791731d71eb00630312b/release

"$BASELINE_RELEASE/duckdb" -unsigned -csv -noheader -c \
  "LOAD '$BASELINE_RELEASE/extension/httpfs/httpfs.duckdb_extension'; \
   LOAD '$BASELINE_RELEASE/extension/duckomo/duckomo.duckdb_extension'; \
   SELECT * FROM httpfs_om_range_capabilities();"
# exit 0

"$PRERELEASE_RELEASE/duckdb" -unsigned -csv -noheader -c \
  "LOAD '$PRERELEASE_RELEASE/extension/httpfs/httpfs.duckdb_extension'; \
   LOAD '$PRERELEASE_RELEASE/extension/duckomo/duckomo.duckdb_extension'; \
   SELECT * FROM httpfs_om_range_capabilities();"
# exit 0
```

Each command exited 0. The capability rows included ABI 3, the matching full engine SHA, pair ID, build ID, HTTPFS SHA, patch revision/set hash, shared header hash, Linux AArch64 platform, C++ ABI, and all three required features. The exact row returned by each CLI was:

```text
baseline-1.5.4:
3,08e34c447bae34eaee3723cac61f2878b6bdf787,baseline-1.5.4,15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8,c3f215ab360f04dc3d3d5305fa81849c0121f111,duckomo-httpfs-range-v3-baseline-1.5.4,4c4ac9ab45686f0d94ac44893b62507b2e3ff788c83a1c83f367de8e7951d16d,5b751573f5b24d03ff241e56916271906590f6d7c7569a172ed15160fef80181,Linux AArch64;compiled=linux-aarch64,libstdc++_cxx11abi=1_gcc=13.3.0_cxx17;compiled=libstdcxx-20240904-dualabi-1,"exact-range,attempt-body-observer,provider-pair-identity"

prerelease-2.0-dev:
3,7264a9f0e5b487358100f408826b8ae9e868b031,prerelease-2.0-dev,9a7aff37d7cc7c910cc0d29fc08a0fb51d6f199f3115791731d71eb00630312b,5e34903685e4d429cbb19b063406abdd8ce30591,duckomo-httpfs-range-v3-prerelease-2.0-dev,fab205b2ec8007234834ecf0ca1db75c9f05fb7c797ba872141edd7d6bc72d07,5b751573f5b24d03ff241e56916271906590f6d7c7569a172ed15160fef80181,Linux AArch64;compiled=linux-aarch64,libstdc++_cxx11abi=1_gcc=13.3.0_cxx17;compiled=libstdcxx-20240904-dualabi-1,"exact-range,attempt-body-observer,provider-pair-identity"
```

The matrix entries remain `build-verified`; no grid behavior or remote protocol validation status was advanced.

## Final-source native and local H3/H7 checks

The final source digest `dbc22a26ed0fb44240ecc24412cd21886af961cfad2efc800a89b300c0c53110` includes a DuckDB 2.0 native-test startup fix: `grid_zero_io_test` and `source_identity_test` explicitly load the statically linked DuckOMO extension for API generation 2. Both frozen pairs were rebuilt under the resulting IDs shown above. The two native tests, `httpfs_abi_test`, `grid_version_compat_test`, `grid_version_compat.test`, `grid_zero_io.test`, `source_identity.test`, and `grid_info.test` all exited 0 for each pair.

The local H3/H7 synthetic subchecks also passed for both pairs. Baseline evidence is in `../baseline-local/us5-20261007-current-15f751-final/`; prerelease evidence is in `prerelease-local-20261007-9a7aff/`. Both evidence manifest audits passed. Each runner exited 2 with complete H3/H7 gates `not-run`: required real N-grid inputs, independent Gaussian producer point order, complete public-source/cross-URI checks, and controlled remote-service evidence remain unavailable. These local runs do not promote H0–H9 or producer support claims.

## 2026-10-08 failed-response accounting repair builds

The earlier build records above are historical. Current matrix build records use the final repaired companion patches and validator source. Source identity and artifact hashes were recomputed after native/SQL/load verification and matrix resolve; all pair command exits are 0. Full grid/remote acceptance is still incomplete.

| Pair | Current build ID | Runtime version | Native/SQL/load command exits |
|---|---|---|---|
| `baseline-1.5.4` | `1bc27ad01fdc1af3775975db8c5c19595e8323957bf04b059a706b3fbe33853b` | `v1.5.4 (Variegata) 08e34c447b` | 11 × 0 |
| `prerelease-2.0-dev` | `0f7c8170eee3c93906f3a0e6352890736b19b08f2b12941779d003555ab9724a` | `v2.0.0-dev86261 (Development Version) 7264a9f0e5` | 11 × 0 |

See [repair evidence](../003-dimensions-remote-parallel/20261008-body-accounting-repair/README.md) for the two build manifests/resolved inputs/artifact hashes, before/after accounting, Range/S3 XML retries, final CLI results, and actual G5/G6 scope. The paired build command for each pair was `DUCKOMO_VCPKG_ROOT=/tmp/duckomo-vcpkg DUCKOMO_BUILD_JOBS=8 scripts/build-version.sh --matrix test/data/grids/version-matrix.json --pair <pair>` (exit 0). Test target commands, source pins, and exits are in each `<pair>-final-verification.json`.
