# HTTPFS is a runtime dependency installed through DuckDB's official repository.
# DuckOMO builds and publishes only its own extension.
duckdb_extension_load(json)
duckdb_extension_load(parquet)
duckdb_extension_load(duckomo
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    TEST_DIR ${CMAKE_CURRENT_LIST_DIR}/test/sql
    LOAD_TESTS
)
