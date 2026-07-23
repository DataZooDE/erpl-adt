cmake_minimum_required(VERSION 3.21)
project(erpl-adt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Version injection ---
# CI passes -DERPL_ADT_VERSION=X.Y.Z; fall back to PROJECT_VERSION, then "dev".
if(NOT DEFINED ERPL_ADT_VERSION)
    set(ERPL_ADT_VERSION "${PROJECT_VERSION}")
endif()
if(NOT ERPL_ADT_VERSION OR ERPL_ADT_VERSION STREQUAL "")
    set(ERPL_ADT_VERSION "dev")
endif()
configure_file(
    "${CMAKE_SOURCE_DIR}/include/erpl_adt/core/version.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/erpl_adt/core/version.hpp"
    @ONLY
)

# --- vcpkg dependencies ---
find_package(httplib CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(argparse CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)

# --- Embedded DuckDB (git submodule, built statically from source) ---
# vcpkg's duckdb port tracks upstream releases with a real lag (1.4.4 was
# the newest available there as of this writing) and only ships a static
# library for Linux — macOS/Windows official prebuilt archives are dynamic-
# only, incompatible with this project's fully-static distribution model.
# Building from source keeps static linking on every platform (DuckDB's own
# CMakeLists.txt already sets CMAKE_MSVC_RUNTIME_LIBRARY to match this
# project's /MT convention, so no CRT-mismatch override is needed here,
# unlike posthog_telemetry/webui_resources below) while tracking whatever
# tag we pin. Produces the same non-namespaced `duckdb`/`duckdb_static`
# targets vcpkg's port did — no changes needed to the target_link_libraries
# calls below.
set(BUILD_SHELL OFF CACHE BOOL "" FORCE)
set(BUILD_UNITTESTS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/duckdb EXCLUDE_FROM_ALL)

# Compiler warnings — deliberately set AFTER the DuckDB subdirectory above:
# add_compile_options() is directory-scoped and inherited by every
# subdirectory added AFTER the call, so setting this any earlier would
# apply our warnings-as-errors policy to DuckDB's own (much larger, not
# ours to keep warning-clean) codebase too.
if(MSVC)
    add_compile_options(/W4 /WX)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

# --- Shared DataZoo telemetry library (git submodule) ---
# Built from third_party/posthog-telemetry. It expects a `duckdb_httplib_openssl`
# httplib (its DuckDB heritage); third_party/posthog_shim/httplib.hpp aliases our
# vcpkg cpp-httplib into that namespace. Linked PRIVATE — no consumer of
# erpl_adt_lib sees the duckdb:: telemetry API transitively.
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/posthog-telemetry/CMakeLists.txt")
    # The submodule's cmake_minimum_required(3.14) predates CMP0091, which would
    # leave the MSVC_RUNTIME_LIBRARY abstraction disabled for its target (so our
    # /MT override below would be ignored). Force the policy NEW for that scope.
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
    add_subdirectory(third_party/posthog-telemetry EXCLUDE_FROM_ALL)
    # The shim header must resolve for the library's quoted #include "httplib.hpp",
    # and it needs the real cpp-httplib on its include/link path.
    target_include_directories(posthog_telemetry PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/posthog_shim)
    target_link_libraries(posthog_telemetry PRIVATE httplib::httplib)
    # Third-party code: do not fail its build on our strict warnings-as-errors
    # policy (e.g. httplib's CPPHTTPLIB_OPENSSL_SUPPORT macro redefinition on MSVC).
    if(MSVC)
        target_compile_options(posthog_telemetry PRIVATE /w /WX-)
        # Match erpl-adt's static CRT (/MT). The submodule's CMake doesn't set
        # this, so it would default to /MD and fail linking with LNK2038.
        set_property(TARGET posthog_telemetry PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_compile_options(posthog_telemetry PRIVATE -w)
    endif()
    set(ERPL_ADT_HAVE_TELEMETRY TRUE)
else()
    message(WARNING
        "third_party/posthog-telemetry not found — telemetry compiled out. "
        "Run: git submodule update --init --recursive")
    set(ERPL_ADT_HAVE_TELEMETRY FALSE)
endif()

# --- Embedded web UI (Flutter web build baked into the binary via CMakeRC) ---
# Optional: the C++ build must not require the Flutter/Dart toolchain. If
# `make webui` (flutter build web) hasn't been run, WEBUI_WEB_DIR won't
# contain index.html and we skip the resource library entirely — `erpl-adt
# catalog webui` still builds, it just serves an instructional message
# instead of the app. See ERPL_ADT_HAVE_WEBUI below.
set(WEBUI_WEB_DIR "${CMAKE_SOURCE_DIR}/flutter/erpl_catalog_kit/example/build/web")
if(EXISTS "${WEBUI_WEB_DIR}/index.html")
    # CMakeRC.cmake's own cmake_minimum_required(3.12) predates CMP0091 — same
    # issue as the posthog-telemetry submodule above — so force the policy NEW
    # here too, otherwise the generated webui_resources target ignores our /MT
    # override below and defaults to /MD, failing to link with LNK2038.
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
    include(${CMAKE_SOURCE_DIR}/third_party/cmrc/CMakeRC.cmake)
    file(GLOB_RECURSE WEBUI_WEB_FILES CONFIGURE_DEPENDS "${WEBUI_WEB_DIR}/*")
    cmrc_add_resource_library(webui_resources
        ALIAS webui::rc
        NAMESPACE webui
        WHENCE "${WEBUI_WEB_DIR}"
        ${WEBUI_WEB_FILES}
    )
    if(MSVC)
        # Match erpl-adt's static CRT (/MT) — CMakeRC's generated target
        # doesn't set this, so it defaults to /MD and fails linking with
        # LNK2038 (RuntimeLibrary mismatch), same fix as posthog_telemetry.
        set_property(TARGET webui_resources PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set(ERPL_ADT_HAVE_WEBUI TRUE)
    message(STATUS "Web UI: embedding ${WEBUI_WEB_DIR} into the binary")
else()
    set(ERPL_ADT_HAVE_WEBUI FALSE)
    message(STATUS "Web UI: no build output at ${WEBUI_WEB_DIR} — run 'make webui' to embed it. "
                    "'erpl-adt catalog webui' will build but serve an instructional message instead of the app.")
endif()

# --- Static library (everything except main.cpp) ---
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(FILTER LIB_SOURCES EXCLUDE REGEX "src/main\\.cpp$")

if(LIB_SOURCES)
    add_library(erpl_adt_lib STATIC ${LIB_SOURCES})
    target_include_directories(erpl_adt_lib PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
    )
    # DuckDB's own CMakeLists.txt exposes its headers via a directory-scoped
    # include_directories(src/include) call, not a target-scoped
    # target_include_directories(duckdb_static PUBLIC ...) — that only
    # reaches targets defined within its own subdirectory tree, not us.
    # SYSTEM (not a plain PUBLIC include) matters here, not just style:
    # duckdb.hpp is header-only in large part (e.g. VectorBuffer::Cast's
    # DynamicCastCheck<T> template), so its code gets compiled as part of
    # whichever of our own translation units includes it — without SYSTEM
    # (-isystem, not -I) our own -Werror policy would fail on warnings
    # inside DuckDB's own headers, not just our code.
    target_include_directories(erpl_adt_lib SYSTEM PUBLIC
        ${CMAKE_SOURCE_DIR}/third_party/duckdb/src/include
    )
    target_link_libraries(erpl_adt_lib PUBLIC
        httplib::httplib
        tinyxml2::tinyxml2
        yaml-cpp::yaml-cpp
        argparse::argparse
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        ftxui::component
        $<IF:$<TARGET_EXISTS:duckdb>,duckdb,duckdb_static>
    )
    if(ERPL_ADT_HAVE_TELEMETRY)
        target_link_libraries(erpl_adt_lib PRIVATE posthog_telemetry)
        target_compile_definitions(erpl_adt_lib PRIVATE ERPL_ADT_TELEMETRY_ENABLED=1)
    endif()
    if(ERPL_ADT_HAVE_WEBUI)
        target_link_libraries(erpl_adt_lib PRIVATE webui::rc)
        # PUBLIC (not PRIVATE like ERPL_ADT_TELEMETRY_ENABLED above): the test
        # target needs to know at compile time which serving behavior to
        # assert on (real embedded app vs. instructional fallback).
        target_compile_definitions(erpl_adt_lib PUBLIC ERPL_ADT_HAVE_WEBUI=1)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(erpl_adt_lib PUBLIC -static-libgcc -static-libstdc++)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_link_libraries(erpl_adt_lib PUBLIC
            "-framework IOKit"
            "-framework CoreFoundation")
    elseif(MSVC)
        set_property(TARGET erpl_adt_lib PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_link_libraries(erpl_adt_lib PUBLIC iphlpapi)
    endif()
else()
    # No source files yet — INTERFACE library so dependents still get includes/deps
    add_library(erpl_adt_lib INTERFACE)
    target_include_directories(erpl_adt_lib INTERFACE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
    )
    target_link_libraries(erpl_adt_lib INTERFACE
        httplib::httplib
        tinyxml2::tinyxml2
        yaml-cpp::yaml-cpp
        argparse::argparse
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        ftxui::component
        $<IF:$<TARGET_EXISTS:duckdb>,duckdb,duckdb_static>
    )
endif()

# --- Executable ---
add_executable(erpl-adt src/main.cpp)
target_link_libraries(erpl-adt PRIVATE erpl_adt_lib)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(erpl-adt PRIVATE -static-libgcc -static-libstdc++)
elseif(MSVC)
    set_property(TARGET erpl-adt PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# --- Tests ---
enable_testing()
add_subdirectory(test)
