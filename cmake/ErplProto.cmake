# Optional BSL-1.1 dependency. CommonCryptoLib is supplied by the operator at
# runtime; neither the SAP NW RFC SDK nor SAP crypto binaries are bundled.
include(FetchContent)
set(ERPL_PROTO_REVISION "8e40272984ffd3d0d4bc0d38382c3eaec61ee3cd")
set(ERPL_PROTO_REPOSITORY "https://github.com/DataZooDE/erpl-proto.git")
set(ERPL_ADT_RUST_TOOLCHAIN "1.99.0" CACHE STRING "Pinned Rust toolchain for RFC support")
find_program(ERPL_ADT_CARGO cargo REQUIRED)
FetchContent_Declare(erpl_proto
    GIT_REPOSITORY "${ERPL_PROTO_REPOSITORY}"
    GIT_TAG "${ERPL_PROTO_REVISION}"
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(erpl_proto)

if(WIN32)
    set(_rust_target x86_64-pc-windows-msvc)
    set(_rust_library sapnwrfc.lib)
    set(_rust_flags "-Ctarget-feature=+crt-static")
elseif(APPLE)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        set(_rust_target x86_64-apple-darwin)
    else()
        set(_rust_target aarch64-apple-darwin)
    endif()
    set(_rust_library libsapnwrfc.a)
else()
    set(_rust_target x86_64-unknown-linux-gnu)
    set(_rust_library libsapnwrfc.a)
endif()
set(_rust_output "${CMAKE_BINARY_DIR}/erpl-proto-target/${_rust_target}/release/${_rust_library}")
file(GLOB_RECURSE _rust_sources CONFIGURE_DEPENDS "${erpl_proto_SOURCE_DIR}/crates/*.rs")
add_custom_command(OUTPUT "${_rust_output}"
    COMMAND "${CMAKE_COMMAND}" -E env
        "CARGO_TARGET_DIR=${CMAKE_BINARY_DIR}/erpl-proto-target"
        "RUSTFLAGS=${_rust_flags}"
        "${ERPL_ADT_CARGO}" "+${ERPL_ADT_RUST_TOOLCHAIN}" build --locked --release
        --manifest-path "${erpl_proto_SOURCE_DIR}/Cargo.toml"
        --target "${_rust_target}" -p erpl-proto-nwrfc --features snc
    DEPENDS ${_rust_sources} "${erpl_proto_SOURCE_DIR}/Cargo.lock"
        "${erpl_proto_SOURCE_DIR}/crates/erpl-proto-nwrfc/Cargo.toml"
    VERBATIM
)
add_custom_target(erpl_proto_build DEPENDS "${_rust_output}")
add_library(erpl_proto_nwrfc STATIC IMPORTED GLOBAL)
set_target_properties(erpl_proto_nwrfc PROPERTIES
    IMPORTED_LOCATION "${_rust_output}"
    INTERFACE_INCLUDE_DIRECTORIES "${erpl_proto_SOURCE_DIR}/crates/erpl-proto-nwrfc/include"
)
add_dependencies(erpl_proto_nwrfc erpl_proto_build)
target_link_libraries(erpl_adt_lib PRIVATE erpl_proto_nwrfc ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(erpl_adt_lib PRIVATE ws2_32 userenv bcrypt ntdll)
endif()
target_compile_definitions(erpl_adt_lib PUBLIC ERPL_ADT_WITH_RFC=1)
