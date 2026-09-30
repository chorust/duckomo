cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME duckomo)
project(${TARGET_NAME} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to enforce")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

include_directories(src/include)

file(GLOB OM_C_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/om-file-format/c/src/*.c"
)

add_library(duckomo_om STATIC ${OM_C_SOURCES})
target_include_directories(duckomo_om PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/om-file-format/c/include"
)
set_target_properties(duckomo_om PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # Upstream generated bitpacking headers emit very large warning streams.
    target_compile_options(duckomo_om PRIVATE -w)
endif()

set(EXTENSION_SOURCES
    src/om_extension.cpp
    src/om/local_file.cpp
    src/om/metadata.cpp
    src/om/reader.cpp
    src/scan/batch.cpp
    src/scan/dimensions.cpp
    src/scan/projection.cpp
    src/scan/read_om.cpp
    src/scan/raw_scan.cpp
    src/scan/schema.cpp
    src/scan/spatial_filter.cpp
    src/scan/spatial_selection.cpp
    src/grid/domain_bbox.cpp
    src/grid/regular_grid.cpp
    src/grid/spatial_layout.cpp
    src/grid/domain_registry.cpp
)
build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})
set_target_properties(${TARGET_NAME}_extension ${TARGET_NAME}_loadable_extension PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # DuckDB's pinned archive is built as C++11.  Suppress C++17 GNU-unique
    # copies of its constexpr constants when extension TUs link that archive.
    target_compile_options(${TARGET_NAME}_extension PRIVATE -fno-gnu-unique)
    target_compile_options(${TARGET_NAME}_loadable_extension PRIVATE -fno-gnu-unique)
endif()
target_link_libraries(${TARGET_NAME}_extension duckomo_om)
target_link_libraries(${TARGET_NAME}_loadable_extension duckomo_om)

add_subdirectory(test)
