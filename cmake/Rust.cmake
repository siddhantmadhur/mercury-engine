# Rust support (cxx bridges + the static library linked into the engine).
#
# The Rust crates are a normal Cargo workspace at the repository root. CMake
# drives `cargo build` in a persistent target directory (so incremental Rust
# builds work like the C++ ones) and runs cxxbridge to generate the C++ side of
# each #[cxx::bridge] module.

find_program(CARGO_EXECUTABLE cargo
        HINTS "$ENV{HOME}/.cargo/bin"
        REQUIRED
        DOC "Cargo, part of the Rust toolchain")
find_program(CXXBRIDGE_EXECUTABLE cxxbridge
        HINTS "$ENV{HOME}/.cargo/bin"
        REQUIRED
        DOC "cxxbridge-cmd; install with `cargo install cxxbridge-cmd --version 1.0.205`")

set(MERCURY_CARGO_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo")
set(MERCURY_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

# The version of the cxx crate must match the C++ header/source generator.
execute_process(
        COMMAND "${CARGO_EXECUTABLE}" metadata --format-version 1
        --manifest-path "${MERCURY_ENGINE_SOURCE_DIR}/Cargo.toml"
        --locked
        RESULT_VARIABLE CARGO_METADATA_RESULT
        OUTPUT_VARIABLE CARGO_METADATA
        ERROR_VARIABLE CARGO_METADATA_ERROR)
if (NOT CARGO_METADATA_RESULT EQUAL 0)
    message(FATAL_ERROR
            "cargo metadata failed:\n${CARGO_METADATA_ERROR}\n"
            "Run `cargo fetch --manifest-path ${MERCURY_ENGINE_SOURCE_DIR}/Cargo.toml` "
            "and configure again.")
endif ()

string(JSON CARGO_PACKAGE_COUNT LENGTH "${CARGO_METADATA}" packages)
math(EXPR CARGO_LAST_PACKAGE "${CARGO_PACKAGE_COUNT} - 1")
set(MERCURY_CXX_INCLUDE_DIR "")
foreach (INDEX RANGE ${CARGO_LAST_PACKAGE})
    string(JSON PACKAGE_NAME GET "${CARGO_METADATA}" packages ${INDEX} name)
    if (PACKAGE_NAME STREQUAL "cxx")
        string(JSON PACKAGE_MANIFEST GET "${CARGO_METADATA}" packages ${INDEX}
                manifest_path)
        get_filename_component(PACKAGE_DIR "${PACKAGE_MANIFEST}" DIRECTORY)
        set(MERCURY_CXX_INCLUDE_DIR "${PACKAGE_DIR}/include")
        break ()
    endif ()
endforeach ()
if (NOT MERCURY_CXX_INCLUDE_DIR)
    message(FATAL_ERROR "The cxx crate was not found in Cargo.lock")
endif ()

# cxxbridge and the engine's bindings use cxx's canonical `rust/cxx.h` include
# path. The crate ships the header as <crate>/include/cxx.h, so expose it under
# the expected prefix in the generated include tree.
file(MAKE_DIRECTORY "${MERCURY_GENERATED_DIR}/rust")
if (NOT EXISTS "${MERCURY_GENERATED_DIR}/rust/cxx.h")
    file(WRITE "${MERCURY_GENERATED_DIR}/rust/cxx.h"
            "#pragma once\n#include <cxx.h>\n")
endif ()

# --------------------------------------------------------------------------
# cxx bridges
# --------------------------------------------------------------------------

function(mercury_add_cxx_bridge NAME SOURCE)
    set(HEADER "${MERCURY_GENERATED_DIR}/mercury/${NAME}.rs.h")
    set(SOURCE_OUT "${MERCURY_GENERATED_DIR}/${NAME}.cc")
    add_custom_command(
            OUTPUT "${HEADER}" "${SOURCE_OUT}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "${MERCURY_GENERATED_DIR}/mercury/rust"
                    "${MERCURY_GENERATED_DIR}/mercury/input"
            COMMAND "${CXXBRIDGE_EXECUTABLE}" "${SOURCE}" --header -o "${HEADER}"
            COMMAND "${CXXBRIDGE_EXECUTABLE}" "${SOURCE}" -o "${SOURCE_OUT}"
            DEPENDS "${SOURCE}"
            COMMENT "cxxbridge ${NAME}"
            VERBATIM)
    set(MERCURY_CXX_BRIDGE_HEADERS ${MERCURY_CXX_BRIDGE_HEADERS} "${HEADER}"
            PARENT_SCOPE)
    set(MERCURY_CXX_BRIDGE_SOURCES ${MERCURY_CXX_BRIDGE_SOURCES} "${SOURCE_OUT}"
            PARENT_SCOPE)
endfunction()

mercury_add_cxx_bridge(rust/ffi
        "${MERCURY_ENGINE_SOURCE_DIR}/mercury/rust/ffi.rs")
mercury_add_cxx_bridge(input/input_actions_codegen
        "${MERCURY_ENGINE_SOURCE_DIR}/mercury/input/input_actions_codegen.rs")

target_sources(mercury PRIVATE ${MERCURY_CXX_BRIDGE_SOURCES})
target_include_directories(mercury PUBLIC
        "${MERCURY_GENERATED_DIR}"
        "${MERCURY_GENERATED_DIR}/mercury"
        "${MERCURY_CXX_INCLUDE_DIR}")

# --------------------------------------------------------------------------
# Cargo static library
# --------------------------------------------------------------------------

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(MERCURY_CARGO_PROFILE "debug")
    set(MERCURY_CARGO_PROFILE_FLAG "")
else ()
    set(MERCURY_CARGO_PROFILE "release")
    set(MERCURY_CARGO_PROFILE_FLAG "--release")
endif ()

set(MERCURY_RUST_STATICLIB "${CMAKE_BINARY_DIR}/lib/libmercury_rust.a")
file(GLOB_RECURSE MERCURY_RUST_SOURCES CONFIGURE_DEPENDS
        "${MERCURY_ENGINE_SOURCE_DIR}/mercury/*.rs")

add_custom_command(
        OUTPUT "${MERCURY_RUST_STATICLIB}"
        COMMAND "${CMAKE_COMMAND}" -E env
                "CARGO_TARGET_DIR=${MERCURY_CARGO_TARGET_DIR}"
                "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
                "${CARGO_EXECUTABLE}" build ${MERCURY_CARGO_PROFILE_FLAG}
                --manifest-path "${MERCURY_ENGINE_SOURCE_DIR}/Cargo.toml" --locked
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${MERCURY_CARGO_TARGET_DIR}/${MERCURY_CARGO_PROFILE}/libmercury_rust.a"
                "${MERCURY_RUST_STATICLIB}"
        DEPENDS ${MERCURY_RUST_SOURCES}
                "${MERCURY_ENGINE_SOURCE_DIR}/Cargo.toml"
                "${MERCURY_ENGINE_SOURCE_DIR}/Cargo.lock"
                "${MERCURY_ENGINE_SOURCE_DIR}/mercury/rust/Cargo.toml"
                "${MERCURY_ENGINE_SOURCE_DIR}/mercury/input/Cargo.toml"
        COMMENT "Building the Mercury Rust modules"
        VERBATIM)

add_custom_target(mercury_rust_build DEPENDS "${MERCURY_RUST_STATICLIB}")
add_library(mercury_rust STATIC IMPORTED)
set_target_properties(mercury_rust PROPERTIES
        IMPORTED_LOCATION "${MERCURY_RUST_STATICLIB}")
target_link_libraries(mercury PUBLIC mercury_rust)
add_dependencies(mercury mercury_rust_build)
