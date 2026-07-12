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

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

# --- vcpkg dependencies ---
find_package(httplib CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(argparse CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)

# --- Shared DataZoo telemetry library (git submodule) ---
# Built from third_party/posthog-telemetry. It expects a `duckdb_httplib_openssl`
# httplib (its DuckDB heritage); third_party/posthog_shim/httplib.hpp aliases our
# vcpkg cpp-httplib into that namespace. Linked PRIVATE — no consumer of
# erpl_adt_lib sees the duckdb:: telemetry API transitively.
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/posthog-telemetry/CMakeLists.txt")
    add_subdirectory(third_party/posthog-telemetry EXCLUDE_FROM_ALL)
    # The shim header must resolve for the library's quoted #include "httplib.hpp",
    # and it needs the real cpp-httplib on its include/link path.
    target_include_directories(posthog_telemetry PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/posthog_shim)
    target_link_libraries(posthog_telemetry PRIVATE httplib::httplib)
    # Third-party code: do not fail its build on our strict -Werror policy.
    if(NOT MSVC)
        target_compile_options(posthog_telemetry PRIVATE -w)
    endif()
    set(ERPL_ADT_HAVE_TELEMETRY TRUE)
else()
    message(WARNING
        "third_party/posthog-telemetry not found — telemetry compiled out. "
        "Run: git submodule update --init --recursive")
    set(ERPL_ADT_HAVE_TELEMETRY FALSE)
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
    target_link_libraries(erpl_adt_lib PUBLIC
        httplib::httplib
        tinyxml2::tinyxml2
        yaml-cpp::yaml-cpp
        argparse::argparse
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        ftxui::component
    )
    if(ERPL_ADT_HAVE_TELEMETRY)
        target_link_libraries(erpl_adt_lib PRIVATE posthog_telemetry)
        target_compile_definitions(erpl_adt_lib PRIVATE ERPL_ADT_TELEMETRY_ENABLED=1)
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
