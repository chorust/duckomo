PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

EXT_NAME=duckomo
EXT_CONFIG=$(PROJ_DIR)extension_config.cmake
DUCKOMO_BUILD_DEVELOPER_TOOLS ?= $(if $(strip $(DUCKDB_PLATFORM)),OFF,ON)
EXT_FLAGS=-DBUILD_UNITTESTS=TRUE -DBUILD_SHELL=$(if $(filter 0,$(BUILD_SHELL)),FALSE,TRUE) -DNATIVE_ARCH=FALSE -DDUCKOMO_BUILD_DEVELOPER_TOOLS=$(DUCKOMO_BUILD_DEVELOPER_TOOLS)

# Build against the pinned DuckDB and extension-ci-tools submodule checkouts,
# independent of the directory from which make is invoked.
DUCKDB_SRCDIR := $(PROJ_DIR)duckdb/
# Community CI checks out its target version before building. Let DuckDB
# derive its version from that checkout. CI also supplies the selected tag,
# which handles source checkouts without tags; callers can explicitly override.
OVERRIDE_GIT_DESCRIBE ?= $(if $(filter v%,$(DUCKDB_GIT_VERSION)),$(DUCKDB_GIT_VERSION))
include $(PROJ_DIR)extension-ci-tools/makefiles/duckdb_extension.Makefile

# Version-matrix builds use isolated source/release paths and do not
# switch the shared DuckDB submodule used by the default v1.5.4 Make targets.
DUCKOMO_MATRIX ?= $(PROJ_DIR)test/data/grids/version-matrix.json
DUCKOMO_MATRIX_PAIR ?= v1.5.6
DUCKOMO_MATRIX_OUTPUT_ROOT ?= $(PROJ_DIR)build/official-matrix
DUCKOMO_SAMPLING_DUCKDB ?= $(PROJ_DIR)build/regions-runtime/v1.5.4/official/duckdb

.PHONY: matrix-release matrix-test
matrix-release:
	"$(PROJ_DIR)scripts/build-version.sh" --matrix "$(DUCKOMO_MATRIX)" \
		--pair "$(DUCKOMO_MATRIX_PAIR)" --output-root "$(DUCKOMO_MATRIX_OUTPUT_ROOT)"

matrix-test:
	"$(PROJ_DIR)scripts/validate.sh" --matrix "$(DUCKOMO_MATRIX)" \
		--pair "$(DUCKOMO_MATRIX_PAIR)" --output-root "$(DUCKOMO_MATRIX_OUTPUT_ROOT)"


# The shared makefile assigns quoted defaults intended for its own layout.
# These paths match this repository's DuckDB test runner and SQLLogicTests.
TEST_PATH := test/unittest
TESTS_BASE_DIRECTORY := test/sql/

.PHONY: test
test: release
	"$(PROJ_DIR)scripts/validate.sh" "$(PROJ_DIR)build/release" --local-only

.PHONY: grid-validation
grid-validation: release
	"$(PROJ_DIR)build/release/test/tools/duckomo_grid_validation" --self-check

.PHONY: region-validation sampling-validation
region-validation: release
	cmake --build "$(PROJ_DIR)build/release" --target duckomo_region_validation_bundle --parallel
	"$(PROJ_DIR)scripts/validate.sh" "$(PROJ_DIR)build/release" --local-only --regions

sampling-validation: release
	test -n "$(DUCKOMO_SPATIAL_EXTENSION)" || { echo 'Set DUCKOMO_SPATIAL_EXTENSION to the verified matching runtime package' >&2; exit 2; }
	spatial="$$(realpath "$(DUCKOMO_SPATIAL_EXTENSION)")"; \
	spatial_sha="$$(sha256sum "$$spatial" | awk '{print $$1}')"; \
	cmake -S "$(DUCKDB_SRCDIR)" -B "$(PROJ_DIR)build/release" \
		-DDUCKOMO_REGION_SANITIZER_SPATIAL_PATH="$$spatial" \
		-DDUCKOMO_REGION_SANITIZER_SPATIAL_SHA256="$$spatial_sha"
	cmake --build "$(PROJ_DIR)build/release" --target duckomo_sampling_validation_bundle --parallel
	python3 "$(PROJ_DIR)scripts/sampling_build.py" --build "$(PROJ_DIR)build/release" --sanitizers
	DUCKOMO_OFFICIAL_DUCKDB="$(DUCKOMO_SAMPLING_DUCKDB)" \
		"$(PROJ_DIR)scripts/validate.sh" "$(PROJ_DIR)build/release" --local-only --sampling --spatial "$(DUCKOMO_SPATIAL_EXTENSION)"

# Keep sanitizer builds and timings separate from the regular release tests.
.PHONY: sanitizer-test
sanitizer-test: release
	cmake --build "$(PROJ_DIR)build/release" --target duckomo_sanitizer_checks --parallel
