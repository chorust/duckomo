PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

EXT_NAME=duckomo
EXT_CONFIG=$(PROJ_DIR)extension_config.cmake
EXT_FLAGS=-DBUILD_UNITTESTS=TRUE -DBUILD_SHELL=TRUE -DNATIVE_ARCH=FALSE
OVERRIDE_GIT_DESCRIBE=v1.5.4

# Build against the pinned DuckDB and extension-ci-tools submodule checkouts,
# independent of the directory from which make is invoked.
DUCKDB_SRCDIR := $(PROJ_DIR)duckdb/
include $(PROJ_DIR)extension-ci-tools/makefiles/duckdb_extension.Makefile

# The shared makefile assigns quoted defaults intended for its own layout.
# These paths match this repository's DuckDB test runner and SQLLogicTests.
TEST_PATH := test/unittest
TESTS_BASE_DIRECTORY := test/sql/

.PHONY: test
test: release
	./build/release/test/native/batch_test
	./build/release/test/native/raw_reader_test
	./build/release/test/native/lifecycle_test
	./build/release/test/native/schema_test
	./build/release/test/native/projection_evidence_test
	./build/release/test/tools/duckomo_validation --fixtures test/data --output build/evidence

# Keep sanitizer builds and timings separate from the regular release tests.
.PHONY: sanitizer-test
sanitizer-test: release
	cmake --build "$(PROJ_DIR)build/release" --target duckomo_sanitizer_checks --parallel
