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
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(erpl_adt_lib PUBLIC -static-libgcc -static-libstdc++)
    elseif(MSVC)
        set_property(TARGET erpl_adt_lib PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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
