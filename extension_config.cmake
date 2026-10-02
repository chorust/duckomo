set(DUCKOMO_HTTPFS_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/build/httpfs-stage-v2/source")
if(NOT EXISTS "${DUCKOMO_HTTPFS_SOURCE_DIR}/src/httpfs_extension.cpp")
    message(FATAL_ERROR "Missing staged, pinned httpfs source. Run scripts/stage-httpfs.sh before configuring DuckDB.")
endif()

duckdb_extension_load(json)
duckdb_extension_load(parquet)
duckdb_extension_load(httpfs
    SOURCE_DIR ${DUCKOMO_HTTPFS_SOURCE_DIR}
    INCLUDE_DIR ${DUCKOMO_HTTPFS_SOURCE_DIR}/src/include
)

duckdb_extension_load(duckomo
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    TEST_DIR ${CMAKE_CURRENT_LIST_DIR}/test/sql
    LOAD_TESTS
)
