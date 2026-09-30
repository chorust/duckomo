# Standalone developer tools and native checks for the duckomo extension.
# Sources are added by later implementation tasks, so keep configuring while
# they are absent and let CMake regenerate when one of the expected files is
# created.

set(DUCKOMO_TOOL_SOURCES
    duckomo_fixture_tool
    duckomo_validation
    duckomo_spatial_validation
)
set(DUCKOMO_NATIVE_TEST_SOURCES
    batch_test
    raw_reader_test
    lifecycle_test
    schema_test
    time_test
    projection_evidence_test
    regular_grid_test
    spatial_layout_test
    spatial_metrics_test
    spatial_callback_test
    spatial_selection_test
    spatial_io_test
    spatial_lifecycle_test
    domain_reference_test
)

set(DUCKOMO_SQLLOGICTEST_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/sql/raw.test"
    "${CMAKE_CURRENT_SOURCE_DIR}/sql/read_om.test"
    "${CMAKE_CURRENT_SOURCE_DIR}/sql/projection.test"
    "${CMAKE_CURRENT_SOURCE_DIR}/sql/spatial.test"
    "${CMAKE_CURRENT_SOURCE_DIR}/sql/spatial_pushdown.test"
    "${CMAKE_CURRENT_SOURCE_DIR}/sql/spatial_composition.test"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${DUCKOMO_SQLLOGICTEST_FILES}
)
set(DUCKOMO_EXISTING_SQLLOGICTEST_FILES)
foreach(sql_test IN LISTS DUCKOMO_SQLLOGICTEST_FILES)
    if(EXISTS "${sql_test}")
        list(APPEND DUCKOMO_EXISTING_SQLLOGICTEST_FILES "${sql_test}")
    endif()
endforeach()
add_custom_target(duckomo_sql_tests SOURCES ${DUCKOMO_EXISTING_SQLLOGICTEST_FILES})
# The root Makefile runs these files through DuckDB's SQLLogicTest runner.

function(duckomo_add_source_target target source output_dir)
    set(source_path "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source_path}")

    if(NOT EXISTS "${source_path}")
        message(STATUS "Skipping ${target}: ${source} will be added by a later task")
        return()
    endif()

    add_executable(${target} "${source_path}")
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test/${output_dir}"
    )
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src/include"
        "${PROJECT_SOURCE_DIR}/third_party/om-file-format/c/include"
    )
endfunction()

foreach(tool IN LISTS DUCKOMO_TOOL_SOURCES)
    duckomo_add_source_target(
        "${tool}"
        "tools/${tool}.cpp"
        tools
    )
endforeach()

if(TARGET duckomo_fixture_tool)
    target_link_libraries(duckomo_fixture_tool PRIVATE duckomo_om)
endif()

foreach(test IN LISTS DUCKOMO_NATIVE_TEST_SOURCES)
    duckomo_add_source_target(
        "${test}"
        "native/${test}.cpp"
        native
    )
    if(TARGET "${test}")
        if(test STREQUAL "domain_reference_test")
            target_include_directories("${test}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
        endif()
        if(test STREQUAL "spatial_selection_test")
            target_sources("${test}" PRIVATE
                "${PROJECT_SOURCE_DIR}/src/grid/regular_grid.cpp"
                "${PROJECT_SOURCE_DIR}/src/grid/spatial_layout.cpp"
                "${PROJECT_SOURCE_DIR}/src/scan/spatial_selection.cpp"
                "${PROJECT_SOURCE_DIR}/src/scan/batch.cpp"
                "${PROJECT_SOURCE_DIR}/duckdb/extension/loader/dummy_static_extension_loader.cpp"
            )
            target_link_libraries("${test}" PRIVATE duckdb_static duckomo_om)
        elseif(test STREQUAL "regular_grid_test" OR test STREQUAL "spatial_layout_test" OR
               test STREQUAL "spatial_metrics_test")
            # Pure value/layout tests only need the implementation under test.
            # Avoid pulling DuckDB's static extension loader (and its optional
            # CoreFunctions/Parquet symbols) into these standalone binaries.
            if(test STREQUAL "regular_grid_test")
                target_sources("${test}" PRIVATE
                    "${PROJECT_SOURCE_DIR}/src/grid/regular_grid.cpp"
                    "${PROJECT_SOURCE_DIR}/src/grid/domain_bbox.cpp"
                    "${PROJECT_SOURCE_DIR}/src/grid/domain_registry.cpp"
                )
            elseif(test STREQUAL "spatial_layout_test")
                target_sources("${test}" PRIVATE
                    "${PROJECT_SOURCE_DIR}/src/grid/regular_grid.cpp"
                    "${PROJECT_SOURCE_DIR}/src/grid/spatial_layout.cpp"
                    "${PROJECT_SOURCE_DIR}/duckdb/extension/loader/dummy_static_extension_loader.cpp"
                )
                target_link_libraries("${test}" PRIVATE duckdb_static)
            endif()
        else()
            target_link_libraries("${test}" PRIVATE duckdb_static duckomo_extension duckomo_om)
            target_link_libraries("${test}" PRIVATE duckdb_generated_extension_loader)
        endif()
    endif()
endforeach()

# Sanitizer runs compile separate, instrumented copies of the extension's C++
# translation units. Linking the regular duckomo_extension here would leave the
# code under test uninstrumented, even if the native executable itself used ASan.
if(UNIX AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(DUCKOMO_SANITIZER_COMPILE_OPTIONS
        -O1 -g -UNDEBUG -fno-omit-frame-pointer -fsanitize=address,undefined
    )
    set(DUCKOMO_SANITIZER_LINK_OPTIONS -fsanitize=address,undefined)

    set(DUCKOMO_SANITIZED_EXTENSION_SOURCES)
    foreach(source IN LISTS EXTENSION_SOURCES)
        list(APPEND DUCKOMO_SANITIZED_EXTENSION_SOURCES "${PROJECT_SOURCE_DIR}/${source}")
    endforeach()
    add_library(duckomo_extension_sanitized STATIC ${DUCKOMO_SANITIZED_EXTENSION_SOURCES})
    set_target_properties(duckomo_extension_sanitized PROPERTIES
        EXCLUDE_FROM_ALL TRUE
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    target_compile_options(duckomo_extension_sanitized PRIVATE ${DUCKOMO_SANITIZER_COMPILE_OPTIONS})
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(duckomo_extension_sanitized PRIVATE -fno-gnu-unique)
    endif()
    target_compile_definitions(duckomo_extension_sanitized PRIVATE
        "EXT_VERSION_DUCKOMO=\"${DUCKDB_EXTENSION_DUCKOMO_EXT_VERSION}\""
    )
    target_link_libraries(duckomo_extension_sanitized PUBLIC duckomo_om PRIVATE duckdb_static)

    # The lifecycle test intentionally exercises the loadable extension path.
    # Stage its sanitizer-built artifact in an isolated working tree matching
    # the test's relative extension and fixture paths; the release artifact
    # remains untouched for performance measurements.
    set(DUCKOMO_SANITIZER_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/sanitizer_runtime")
    file(MAKE_DIRECTORY "${DUCKOMO_SANITIZER_RUNTIME_DIR}/test")
    file(COPY "${PROJECT_SOURCE_DIR}/test/data" DESTINATION "${DUCKOMO_SANITIZER_RUNTIME_DIR}/test")

    set(DUCKDB_EXTENSION_DUCKOMO_SANITIZER_EXT_VERSION "${DUCKDB_EXTENSION_DUCKOMO_EXT_VERSION}")
    set(DUCKOMO_SAVED_LOCAL_EXTENSION_REPO "${LOCAL_EXTENSION_REPO}")
    set(DUCKOMO_SAVED_EXTENSION_TESTS_ONLY "${EXTENSION_TESTS_ONLY}")
    set(LOCAL_EXTENSION_REPO FALSE)
    set(EXTENSION_TESTS_ONLY TRUE)
    build_loadable_extension(duckomo_sanitizer " " ${DUCKOMO_SANITIZED_EXTENSION_SOURCES})
    set(LOCAL_EXTENSION_REPO "${DUCKOMO_SAVED_LOCAL_EXTENSION_REPO}")
    set(EXTENSION_TESTS_ONLY "${DUCKOMO_SAVED_EXTENSION_TESTS_ONLY}")
    set_target_properties(duckomo_sanitizer_loadable_extension PROPERTIES
        EXCLUDE_FROM_ALL TRUE
        OUTPUT_NAME duckomo
        LIBRARY_OUTPUT_DIRECTORY "${DUCKOMO_SANITIZER_RUNTIME_DIR}/build/release/extension/duckomo"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    target_compile_options(duckomo_sanitizer_loadable_extension PRIVATE ${DUCKOMO_SANITIZER_COMPILE_OPTIONS})
    target_link_options(duckomo_sanitizer_loadable_extension PRIVATE ${DUCKOMO_SANITIZER_LINK_OPTIONS})
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(duckomo_sanitizer_loadable_extension PRIVATE -fno-gnu-unique)
    endif()
    target_compile_definitions(duckomo_sanitizer_loadable_extension PRIVATE
        "EXT_VERSION_DUCKOMO=\"${DUCKDB_EXTENSION_DUCKOMO_EXT_VERSION}\""
    )
    target_link_libraries(duckomo_sanitizer_loadable_extension duckomo_om)

    enable_testing()
    set(DUCKOMO_SANITIZER_TEST_TARGETS)

    foreach(test batch_test raw_reader_test schema_test)
        set(test_target "${test}_sanitizer")
        duckomo_add_source_target("${test_target}" "native/${test}.cpp" "sanitizer/native")
        if(TARGET "${test_target}")
            set_target_properties("${test_target}" PROPERTIES EXCLUDE_FROM_ALL TRUE)
            target_compile_options("${test_target}" PRIVATE ${DUCKOMO_SANITIZER_COMPILE_OPTIONS})
            target_link_options("${test_target}" PRIVATE ${DUCKOMO_SANITIZER_LINK_OPTIONS})
            target_link_libraries("${test_target}" PRIVATE
                duckdb_static
                duckdb_generated_extension_loader
                duckomo_extension_sanitized
                duckomo_om
            )
            add_test(NAME "duckomo_sanitizer_${test}"
                COMMAND "$<TARGET_FILE:${test_target}>"
            )
            set_tests_properties("duckomo_sanitizer_${test}" PROPERTIES
                WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            )
            list(APPEND DUCKOMO_SANITIZER_TEST_TARGETS "${test_target}")
        endif()
    endforeach()

    duckomo_add_source_target(
        duckomo_lifecycle_sanitizer
        "native/lifecycle_test.cpp"
        "sanitizer/native"
    )
    if(TARGET duckomo_lifecycle_sanitizer)
        set_target_properties(duckomo_lifecycle_sanitizer PROPERTIES EXCLUDE_FROM_ALL TRUE)
        target_compile_options(duckomo_lifecycle_sanitizer PRIVATE ${DUCKOMO_SANITIZER_COMPILE_OPTIONS})
        target_link_options(duckomo_lifecycle_sanitizer PRIVATE ${DUCKOMO_SANITIZER_LINK_OPTIONS})
        target_link_libraries(duckomo_lifecycle_sanitizer PRIVATE
            duckdb_static
            duckdb_generated_extension_loader
            duckomo_om
        )
        add_test(NAME duckomo_sanitizer_lifecycle
            COMMAND "$<TARGET_FILE:duckomo_lifecycle_sanitizer>"
        )
        set_tests_properties(duckomo_sanitizer_lifecycle PROPERTIES
            WORKING_DIRECTORY "${DUCKOMO_SANITIZER_RUNTIME_DIR}"
        )
        list(APPEND DUCKOMO_SANITIZER_TEST_TARGETS duckomo_lifecycle_sanitizer)
    endif()

    duckomo_add_source_target(
        duckomo_spatial_selection_sanitizer
        "native/spatial_selection_test.cpp"
        "sanitizer/native"
    )
    if(TARGET duckomo_spatial_selection_sanitizer)
        set_target_properties(duckomo_spatial_selection_sanitizer PROPERTIES EXCLUDE_FROM_ALL TRUE)
        target_sources(duckomo_spatial_selection_sanitizer PRIVATE
            "${PROJECT_SOURCE_DIR}/src/grid/regular_grid.cpp"
            "${PROJECT_SOURCE_DIR}/src/grid/spatial_layout.cpp"
            "${PROJECT_SOURCE_DIR}/src/scan/spatial_selection.cpp"
            "${PROJECT_SOURCE_DIR}/src/scan/batch.cpp"
            "${PROJECT_SOURCE_DIR}/duckdb/extension/loader/dummy_static_extension_loader.cpp"
        )
        target_compile_options(duckomo_spatial_selection_sanitizer PRIVATE ${DUCKOMO_SANITIZER_COMPILE_OPTIONS})
        target_link_options(duckomo_spatial_selection_sanitizer PRIVATE ${DUCKOMO_SANITIZER_LINK_OPTIONS})
        target_link_libraries(duckomo_spatial_selection_sanitizer PRIVATE duckdb_static duckomo_om)
        add_test(NAME duckomo_sanitizer_spatial_selection
            COMMAND "$<TARGET_FILE:duckomo_spatial_selection_sanitizer>"
        )
        set_tests_properties(duckomo_sanitizer_spatial_selection PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        )
        list(APPEND DUCKOMO_SANITIZER_TEST_TARGETS duckomo_spatial_selection_sanitizer)
    endif()

    foreach(test spatial_io_test spatial_lifecycle_test)
        set(test_target "${test}_sanitizer")
        duckomo_add_source_target("${test_target}" "native/${test}.cpp" "sanitizer/native")
        if(TARGET "${test_target}")
            set_target_properties("${test_target}" PROPERTIES EXCLUDE_FROM_ALL TRUE)
            target_compile_options("${test_target}" PRIVATE ${DUCKOMO_SANITIZER_COMPILE_OPTIONS})
            target_link_options("${test_target}" PRIVATE ${DUCKOMO_SANITIZER_LINK_OPTIONS})
            target_link_libraries("${test_target}" PRIVATE duckdb_static duckdb_generated_extension_loader duckomo_om)
            add_test(NAME "duckomo_sanitizer_${test}"
                COMMAND "$<TARGET_FILE:${test_target}>"
            )
            set_tests_properties("duckomo_sanitizer_${test}" PROPERTIES
                WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                ENVIRONMENT "DUCKOMO_EXTENSION_PATH=${DUCKOMO_SANITIZER_RUNTIME_DIR}/build/release/extension/duckomo/duckomo.duckdb_extension"
            )
            list(APPEND DUCKOMO_SANITIZER_TEST_TARGETS "${test_target}")
        endif()
    endforeach()

    duckomo_add_source_target(
        duckomo_projection_evidence_sanitizer
        "native/projection_evidence_test.cpp"
        "sanitizer/native"
    )
    if(TARGET duckomo_projection_evidence_sanitizer)
        set_target_properties(duckomo_projection_evidence_sanitizer PROPERTIES EXCLUDE_FROM_ALL TRUE)
        target_compile_options(duckomo_projection_evidence_sanitizer PRIVATE ${DUCKOMO_SANITIZER_COMPILE_OPTIONS})
        target_link_options(duckomo_projection_evidence_sanitizer PRIVATE ${DUCKOMO_SANITIZER_LINK_OPTIONS})
        target_link_libraries(duckomo_projection_evidence_sanitizer PRIVATE
            duckdb_static duckdb_generated_extension_loader duckomo_extension_sanitized duckomo_om
        )
        add_test(NAME duckomo_sanitizer_projection_evidence
            COMMAND "$<TARGET_FILE:duckomo_projection_evidence_sanitizer>"
        )
        set_tests_properties(duckomo_sanitizer_projection_evidence PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            ENVIRONMENT "DUCKOMO_EXTENSION_PATH=${DUCKOMO_SANITIZER_RUNTIME_DIR}/build/release/extension/duckomo/duckomo.duckdb_extension"
        )
        list(APPEND DUCKOMO_SANITIZER_TEST_TARGETS duckomo_projection_evidence_sanitizer)
    endif()

    add_custom_target(duckomo_sanitizer_checks
        COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure -R "^duckomo_sanitizer_"
        DEPENDS ${DUCKOMO_SANITIZER_TEST_TARGETS} duckomo_sanitizer_loadable_extension
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        USES_TERMINAL
        COMMENT "Running instrumented native lifecycle, bounds, and corrupt-input checks"
    )
else()
    message(STATUS "Sanitizer checks are unavailable: they require a GNU or Clang compiler on a Unix platform")
endif()
