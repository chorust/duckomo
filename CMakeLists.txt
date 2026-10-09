cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME duckomo)
project(${TARGET_NAME} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to enforce")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

include_directories(src/include)

# Matrix builds pass pair-specific identities from the frozen input manifest.
# Generate the interface in the active build tree so isolated pairs never
# rewrite shared source files when producing their identity headers.
foreach(identity_field DUCKOMO_BUILD_PAIR_ID DUCKOMO_BUILD_ID DUCKOMO_DUCKDB_COMMIT
		DUCKOMO_OM_COMMIT DUCKOMO_EXTENSION_CI_TOOLS_COMMIT
		DUCKOMO_MATRIX_PLATFORM DUCKOMO_CXX_ABI)
	if(NOT DEFINED ${identity_field})
		set(${identity_field} "")
	endif()
endforeach()
set(DUCKOMO_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${DUCKOMO_GENERATED_INCLUDE_DIR}/duckomo")
include_directories("${DUCKOMO_GENERATED_INCLUDE_DIR}")
configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/src/include/duckomo/build_identity.hpp.in"
	"${DUCKOMO_GENERATED_INCLUDE_DIR}/duckomo/build_identity.hpp"
	@ONLY
)

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
    src/compat/duckdb_api.cpp
    src/om/local_file.cpp
    src/om/metadata.cpp
    src/om/reader.cpp
    src/om/remote_file.cpp
    src/scan/batch.cpp
    src/scan/dimensions.cpp
    src/scan/axis_filter.cpp
    src/scan/axis_selection.cpp
    src/scan/projection.cpp
    src/scan/read_om.cpp
	src/scan/grid_info.cpp
	src/scan/grid_selection.cpp
	src/scan/semantic_axes.cpp
    src/scan/raw_scan.cpp
    src/scan/schema.cpp
    src/scan/spatial_filter.cpp
    src/scan/spatial_selection.cpp
    src/grid/domain_bbox.cpp
    src/grid/grid_definition.cpp
    src/grid/grid_identity.cpp
    src/grid/projected_grid.cpp
    src/grid/gaussian_grid.cpp
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
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Preserve explicitly staged Float32 source arithmetic in coordinate kernels.
    set_source_files_properties(
        src/grid/projected_grid.cpp
        src/grid/gaussian_grid.cpp
        PROPERTIES COMPILE_OPTIONS "-ffp-contract=off"
    )
endif()
target_link_libraries(${TARGET_NAME}_extension duckomo_om)
target_link_libraries(${TARGET_NAME}_loadable_extension duckomo_om)
target_include_directories(${TARGET_NAME}_extension PUBLIC "${DUCKOMO_GENERATED_INCLUDE_DIR}")
target_include_directories(${TARGET_NAME}_loadable_extension PRIVATE "${DUCKOMO_GENERATED_INCLUDE_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-grid-reference.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-grid-coordinate-reference.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compare-grid-sample-references.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/test/tools/grid_spatial_reference.py"
)

option(DUCKOMO_BUILD_DEVELOPER_TOOLS "Build DuckOMO native checks and fixture tools" OFF)
if(DUCKOMO_BUILD_DEVELOPER_TOOLS)
    add_subdirectory(test)
endif()
