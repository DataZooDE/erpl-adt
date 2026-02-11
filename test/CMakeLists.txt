find_package(Catch2 3 CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Unit tests (offline, run in CI)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/adt/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/config/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/workflow/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/cli/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mcp/*.cpp"
)

if(TEST_SOURCES)
    add_executable(erpl_adt_tests ${TEST_SOURCES})
    target_link_libraries(erpl_adt_tests PRIVATE
        erpl_adt_lib
        Catch2::Catch2WithMain
    )
    if(MSVC)
        set_property(TARGET erpl_adt_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    include(Catch)
    catch_discover_tests(erpl_adt_tests)
else()
    message(STATUS "No test sources found — skipping test target")
endif()
