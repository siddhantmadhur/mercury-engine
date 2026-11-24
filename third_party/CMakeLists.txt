# Third-party dependencies.
#
# Everything is built by this CMake project so Ninja sees every translation
# unit and can rebuild incrementally. The only dependency with a nested build
# tree is Filament: its CMake project defines an `assimp` target of its own,
# which collides with the full Assimp used by the engine.

# --------------------------------------------------------------------------
# Header-only dependencies
#
# They keep their checkout layout, so engine sources use the same includes:
#   #include "stb/stb_image.h", <glm/glm.hpp>, <entt/entt.hpp>, ...
# --------------------------------------------------------------------------

add_library(mercury_glm INTERFACE)
target_include_directories(mercury_glm INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/glm")

add_library(mercury_stb INTERFACE)
target_include_directories(mercury_stb INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(mercury_nanosvg INTERFACE)
target_include_directories(mercury_nanosvg INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(mercury_entt INTERFACE)
target_include_directories(mercury_entt INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/entt/src")

# TOML parsing for Mercury's text formats (.mproj/.mmap/.masset/...). Kept out
# of the public include surface: engine code includes <toml++/toml.hpp> from
# implementation files only. TOML_EXCEPTIONS=0 makes toml::parse return a
# parse_result instead of throwing, which maps onto mtext::ParseError.
add_library(mercury_tomlplusplus INTERFACE)
target_include_directories(mercury_tomlplusplus INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/tomlplusplus/include")
target_compile_definitions(mercury_tomlplusplus INTERFACE TOML_EXCEPTIONS=0)

# JSON support for tooling (the introspection API and MCP infrastructure).
# nlohmann/json is header-only and is linked only PRIVATE into the engine
# target — no public engine header includes it. Its own CMake
# project provides the nlohmann_json::nlohmann_json target; keep that same
# target available when the submodule is not checked out (CI sets
# GIT_SUBMODULE_STRATEGY: none for jobs that do not need dependencies).
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json/CMakeLists.txt")
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
    add_subdirectory(nlohmann_json EXCLUDE_FROM_ALL)
else ()
    add_library(nlohmann_json INTERFACE)
    add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
    target_include_directories(nlohmann_json INTERFACE
            "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json/include")
endif ()

# --------------------------------------------------------------------------
# AngelScript
# --------------------------------------------------------------------------

set(ANGELSCRIPT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/angelscript/sdk")
file(GLOB ANGELSCRIPT_SOURCES CONFIGURE_DEPENDS
        "${ANGELSCRIPT_ROOT}/angelscript/source/*.cpp"
        "${ANGELSCRIPT_ROOT}/add_on/scriptbuilder/*.cpp"
        "${ANGELSCRIPT_ROOT}/add_on/scriptarray/*.cpp"
        "${ANGELSCRIPT_ROOT}/add_on/scriptstdstring/*.cpp"
        "${ANGELSCRIPT_ROOT}/add_on/scriptmath/*.cpp"
        "${ANGELSCRIPT_ROOT}/add_on/scriptdictionary/*.cpp")
if (APPLE)
    # Apple's assembler needs the Mach-O flavoured source; the GCC/GAS file
    # uses ELF directives the macOS toolchain rejects. Both files are wrapped
    # in `#if defined(__aarch64__)`, so they assemble to nothing on other CPUs.
    list(APPEND ANGELSCRIPT_SOURCES
            "${ANGELSCRIPT_ROOT}/angelscript/source/as_callfunc_arm64_xcode.S")
elseif (UNIX)
    list(APPEND ANGELSCRIPT_SOURCES
            "${ANGELSCRIPT_ROOT}/angelscript/source/as_callfunc_arm64_gcc.S")
endif ()

add_library(mercury_angelscript STATIC ${ANGELSCRIPT_SOURCES})
target_include_directories(mercury_angelscript PUBLIC
        "${ANGELSCRIPT_ROOT}/angelscript/include"
        "${ANGELSCRIPT_ROOT}/add_on")
target_compile_options(mercury_angelscript PRIVATE -Wno-unused-parameter)
target_link_libraries(mercury_angelscript PUBLIC Threads::Threads)

# --------------------------------------------------------------------------
# Jolt Physics
# --------------------------------------------------------------------------

file(GLOB_RECURSE JOLT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/JoltPhysics/Jolt/*.cpp")
add_library(mercury_jolt STATIC ${JOLT_SOURCES})
target_include_directories(mercury_jolt PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/JoltPhysics")
# Jolt's version check aborts if the client disagrees about the object layer
# width, so publish the same value its CMake build uses.
target_compile_definitions(mercury_jolt PUBLIC JPH_OBJECT_LAYER_BITS=16)
target_link_libraries(mercury_jolt PUBLIC Threads::Threads)

# --------------------------------------------------------------------------
# Yoga (flexbox layout for the UI Toolkit)
# --------------------------------------------------------------------------

file(GLOB_RECURSE YOGA_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/yoga/yoga/*.cpp")
add_library(mercury_yoga STATIC ${YOGA_SOURCES})
target_include_directories(mercury_yoga PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/yoga")

# --------------------------------------------------------------------------
# Small C/C++ libraries
# --------------------------------------------------------------------------

file(GLOB MESHOPTIMIZER_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/meshoptimizer/src/*.cpp")
add_library(mercury_meshoptimizer STATIC ${MESHOPTIMIZER_SOURCES})
target_include_directories(mercury_meshoptimizer PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(mercury_miniaudio STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/miniaudio/miniaudio.c")
target_compile_definitions(mercury_miniaudio PRIVATE
        MA_NO_ENCODING
        MA_NO_GENERATION)
target_include_directories(mercury_miniaudio PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mercury_miniaudio PUBLIC Threads::Threads)
if (UNIX)
    target_link_libraries(mercury_miniaudio PUBLIC ${CMAKE_DL_LIBS} m)
endif ()

set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(tinyxml2 EXCLUDE_FROM_ALL)
add_library(mercury_tinyxml2 ALIAS tinyxml2)

# --------------------------------------------------------------------------
# glfw
# --------------------------------------------------------------------------

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw EXCLUDE_FROM_ALL)

# --------------------------------------------------------------------------
# Assimp
# --------------------------------------------------------------------------

set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL_PDB OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
add_subdirectory(assimp EXCLUDE_FROM_ALL)

if (APPLE AND TARGET zlibstatic)
    # Assimp's bundled zlib defines fdopen() NULL on macOS, which breaks the
    # SDK declaration.
    target_compile_definitions(zlibstatic PRIVATE fdopen=fdopen)
endif ()

# --------------------------------------------------------------------------
# Filament
#
# Built with its own CMake project in a nested Ninja tree. The build tree is
# part of the outer build directory, so incremental rebuilds work like any
# other target, and ccache is used for both trees. Only the targets the engine
# needs are built: the tools, samples and filamentapp (and its minimal copy of
# assimp) are skipped.
# --------------------------------------------------------------------------

include(ExternalProject)

set(FILAMENT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/filament")
set(FILAMENT_PREFIX "${CMAKE_BINARY_DIR}/_deps/filament")
set(FILAMENT_BUILD_DIR "${FILAMENT_PREFIX}/build")
set(FILAMENT_OUTPUT_DIR "${FILAMENT_PREFIX}/out")
set(FILAMENT_MATC "${FILAMENT_OUTPUT_DIR}/bin/matc")

set(FILAMENT_LIBRARY_NAMES
        filament
        backend
        bluevk
        filaflat
        filabridge
        filament-iblprefilter
        geometry
        ibl
        image
        smol-v
        utils
        zstd)

set(FILAMENT_LIBRARIES "")
foreach (LIBRARY ${FILAMENT_LIBRARY_NAMES})
    add_library(mercury_filament_${LIBRARY} STATIC IMPORTED)
    set_target_properties(mercury_filament_${LIBRARY} PROPERTIES
            IMPORTED_LOCATION "${FILAMENT_OUTPUT_DIR}/lib/lib${LIBRARY}.a")
    list(APPEND FILAMENT_LIBRARIES mercury_filament_${LIBRARY})
endforeach ()

set(FILAMENT_BUILD_BYPRODUCTS
        "${FILAMENT_MATC}"
        "${FILAMENT_OUTPUT_DIR}/lib/libfilament.a")
foreach (LIBRARY ${FILAMENT_LIBRARY_NAMES})
    if (NOT LIBRARY STREQUAL "filament")
        list(APPEND FILAMENT_BUILD_BYPRODUCTS
                "${FILAMENT_OUTPUT_DIR}/lib/lib${LIBRARY}.a")
    endif ()
endforeach ()

set(FILAMENT_C_COMPILER_LAUNCHER "")
set(FILAMENT_CXX_COMPILER_LAUNCHER "")
if (CCACHE_PROGRAM)
    set(FILAMENT_C_COMPILER_LAUNCHER "-DCMAKE_C_COMPILER_LAUNCHER=${CCACHE_PROGRAM}")
    set(FILAMENT_CXX_COMPILER_LAUNCHER "-DCMAKE_CXX_COMPILER_LAUNCHER=${CCACHE_PROGRAM}")
endif ()

set(FILAMENT_PLATFORM_ARGS "")
if (APPLE)
    list(APPEND FILAMENT_PLATFORM_ARGS
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    if (CMAKE_OSX_ARCHITECTURES)
        list(APPEND FILAMENT_PLATFORM_ARGS
                "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
    endif ()
endif ()

set(FILAMENT_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
if (NOT FILAMENT_BUILD_TYPE)
    set(FILAMENT_BUILD_TYPE "Release")
endif ()

ExternalProject_Add(filament_ext
        PREFIX "${FILAMENT_PREFIX}"
        SOURCE_DIR "${FILAMENT_SOURCE_DIR}"
        BINARY_DIR "${FILAMENT_BUILD_DIR}"
        CMAKE_GENERATOR Ninja
        CMAKE_ARGS
        "-DCMAKE_BUILD_TYPE=${FILAMENT_BUILD_TYPE}"
        "-DCMAKE_INSTALL_PREFIX=${FILAMENT_PREFIX}/install"
        "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=${FILAMENT_OUTPUT_DIR}/lib"
        "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${FILAMENT_OUTPUT_DIR}/lib"
        "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${FILAMENT_OUTPUT_DIR}/bin"
        "-DBUILD_SHARED_LIBS=OFF"
        "-DFILAMENT_BUILD_FILAMAT=ON"
        "-DFILAMENT_BUILD_TESTING=OFF"
        "-DFILAMENT_ENABLE_MATDBG=OFF"
        "-DFILAMENT_SKIP_SAMPLES=ON"
        "-DFILAMENT_SKIP_SDL2=ON"
        "-DFILAMENT_SUPPORTS_METAL=OFF"
        "-DFILAMENT_SUPPORTS_OPENGL=OFF"
        "-DFILAMENT_SUPPORTS_VULKAN=ON"
        ${FILAMENT_PLATFORM_ARGS}
        ${FILAMENT_C_COMPILER_LAUNCHER}
        ${FILAMENT_CXX_COMPILER_LAUNCHER}
        BUILD_COMMAND "${CMAKE_COMMAND}" --build "${FILAMENT_BUILD_DIR}"
        --parallel --target matc filament filament-iblprefilter geometry
        BUILD_BYPRODUCTS ${FILAMENT_BUILD_BYPRODUCTS}
        INSTALL_COMMAND ""
        UPDATE_COMMAND ""
        DOWNLOAD_COMMAND "")

# Filament's public headers live in its source tree. The engine only includes
# filament/, backend/, utils/, geometry/ and filament-iblprefilter/, but the
# others are cheap to expose and keep upgrades painless.
set(FILAMENT_INCLUDE_DIRS
        "${FILAMENT_SOURCE_DIR}/filament/include"
        "${FILAMENT_SOURCE_DIR}/filament/backend/include"
        "${FILAMENT_SOURCE_DIR}/libs/utils/include"
        "${FILAMENT_SOURCE_DIR}/libs/filabridge/include"
        "${FILAMENT_SOURCE_DIR}/libs/filaflat/include"
        "${FILAMENT_SOURCE_DIR}/libs/geometry/include"
        "${FILAMENT_SOURCE_DIR}/libs/ibl/include"
        "${FILAMENT_SOURCE_DIR}/libs/image/include"
        "${FILAMENT_SOURCE_DIR}/libs/iblprefilter/include"
        "${FILAMENT_SOURCE_DIR}/libs/math/include"
        "${FILAMENT_SOURCE_DIR}/third_party/robin-map/include")

add_library(mercury_filament INTERFACE)
target_include_directories(mercury_filament INTERFACE ${FILAMENT_INCLUDE_DIRS})
target_link_libraries(mercury_filament INTERFACE ${FILAMENT_LIBRARIES})
if (APPLE)
    target_link_libraries(mercury_filament INTERFACE
            "-framework CoreFoundation"
            "-framework Foundation")
elseif (UNIX)
    target_link_libraries(mercury_filament INTERFACE
            ${CMAKE_DL_LIBS} Threads::Threads)
endif ()
add_library(mercury::filament ALIAS mercury_filament)

# The material compiler is used by the top-level build.
set(FILAMENT_MATC "${FILAMENT_MATC}" PARENT_SCOPE)
