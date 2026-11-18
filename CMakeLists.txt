# Mercury Engine
#
# One build system: CMake + Ninja. The engine, the editor, the tests and the
# third-party dependencies all build from this tree. Run
#
#   cmake --preset release
#   cmake --build --preset release
#
# from the repository root. Build outputs stay in build/<preset>/, which is
# ignored by git. See cmake/MercuryGame.cmake for building a game whose
# gameplay is C++.

cmake_minimum_required(VERSION 3.25)

# The engine's runtime floor is macOS 26.0: std::from_chars for floating point
# is only available from 26.0 in Apple's libc++, and the prebuilt MoltenVK /
# Vulkan loader / wxWidgets libraries the app bundles are built for 26.0 too.
# Set this before project() so every compiler invocation is stamped with it.
if (APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING
            "Minimum supported macOS version")
endif ()

project(MercuryEngine LANGUAGES C CXX ASM)

# The engine may be built as the top-level project or added to a game with
# add_subdirectory(); this stays the engine checkout either way.
set(MERCURY_ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

option(MERCURY_BUILD_EXAMPLES "Build the example games" ${PROJECT_IS_TOP_LEVEL})
option(MERCURY_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(MERCURY_BUILD_EDITOR "Build mercury_editor" ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# Everything lands in a few predictable folders, which keeps the editor's
# Build/Play and scripts/package_macos.pl independent of the generator.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
foreach (CONFIG Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${CONFIG}" CONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib")
endforeach ()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Keep library names stable between configurations. Assimp otherwise injects a
# "d" suffix into Debug builds, which would change the archives the editor and
# package script look for.
set(CMAKE_DEBUG_POSTFIX "" CACHE STRING "Suffix for Debug libraries" FORCE)

# ccache makes recompiling the engine, the editor and the dependencies cheap
# across build directories and branches. It is optional: without it CMake
# simply invokes the compiler directly.
find_program(CCACHE_PROGRAM ccache HINTS "/opt/homebrew/bin" "/usr/local/bin")
if (CCACHE_PROGRAM AND NOT DEFINED CMAKE_C_COMPILER_LAUNCHER)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING
            "C compiler launcher")
endif ()
if (CCACHE_PROGRAM AND NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING
            "C++ compiler launcher")
endif ()

if (APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif ()

find_package(Threads REQUIRED)
find_package(Vulkan REQUIRED)

# --------------------------------------------------------------------------
# Third-party dependencies
#
# Small libraries are compiled here directly so Ninja gets one incremental
# target per file. The larger dependencies that ship their own CMake project
# are added as subdirectories (glfw, assimp, googletest) or built in an
# isolated Ninja build tree (Filament, whose CMake project also defines an
# `assimp` target that would collide with ours).
# --------------------------------------------------------------------------

add_subdirectory(third_party)
include(cmake/MercuryGame.cmake)

# --------------------------------------------------------------------------
# Engine
# --------------------------------------------------------------------------

file(GLOB_RECURSE MERCURY_ENGINE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/mercury/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/mercury/*.c")
list(REMOVE_ITEM MERCURY_ENGINE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/mercury/player/main.cc")

add_library(mercury STATIC ${MERCURY_ENGINE_SOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/mercury/scripting/as/api_prelude.inc")

target_compile_features(mercury PUBLIC cxx_std_20)
target_include_directories(mercury PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/mercury"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_include_directories(mercury PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/assimp/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/assimp"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/assimp/contrib/zlib"
        "${CMAKE_CURRENT_BINARY_DIR}/third_party/assimp/contrib/zlib")
target_compile_definitions(mercury PUBLIC
        MERCURY_ENGINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        MERCURY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        MERCURY_BUILD_DIR="${CMAKE_BINARY_DIR}"
        MERCURY_BUILD_CONFIG="$<CONFIG>"
        MERCURY_MATERIAL_PATH="${CMAKE_BINARY_DIR}/materials"
        MERCURY_ASSET_PATH="${CMAKE_CURRENT_SOURCE_DIR}/examples/assets"
        MERCURY_UI_RESOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/mercury/ui/resources"
        MERCURY_GRAPHICS_VULKAN=1
        GLFW_INCLUDE_VULKAN=1
        GLM_FORCE_DEPTH_ZERO_TO_ONE=1)

target_link_libraries(mercury PUBLIC
        mercury_angelscript
        mercury_entt
        mercury_glm
        mercury_jolt
        mercury_meshoptimizer
        mercury_miniaudio
        mercury_stb
        mercury_nanosvg
        tinyxml2
        mercury_yoga
        assimp
        zlibstatic
        glfw
        mercury_filament
        Vulkan::Vulkan
        Threads::Threads)

if (APPLE)
    target_compile_options(mercury PRIVATE -Wno-deprecated-declarations)
    target_link_libraries(mercury PUBLIC
            "-framework AppKit"
            "-framework Cocoa"
            "-framework CoreFoundation"
            "-framework IOKit"
            "-framework Metal"
            "-framework QuartzCore"
            "-lobjc")
elseif (UNIX)
    target_link_libraries(mercury PUBLIC ${CMAKE_DL_LIBS})
endif ()

# Filament's Vulkan backend dlopen()s "libvulkan.1.dylib" by name. Linking the
# absolute loader is not enough on macOS: dyld would load it, but dlopen would
# still search the default paths. Add the loader's directory to the run path so
# the backend finds it.
if (APPLE AND Vulkan_LIBRARY)
    get_filename_component(MERCURY_VULKAN_LIBRARY_DIR "${Vulkan_LIBRARY}" DIRECTORY)
    target_link_options(mercury PUBLIC "-Wl,-rpath,${MERCURY_VULKAN_LIBRARY_DIR}")
endif ()

include(cmake/Rust.cmake)

# Link steps need Filament's archives; make a targeted build
# (`--target mercury_player`) build them even when mercury_materials is not in
# the requested target set.
add_dependencies(mercury filament_ext)

# Filament's material compiler (matc) and the engine's .filamat files. Both are
# build-time products; the player and editor read them from
# ${CMAKE_BINARY_DIR}/materials when running from the build tree.
file(GLOB MERCURY_MATERIAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/mercury/materials/*.mat")
set(MERCURY_MATERIAL_OUTPUT_DIR "${CMAKE_BINARY_DIR}/materials")
set(MERCURY_MATERIAL_OUTPUTS "")
foreach (MATERIAL ${MERCURY_MATERIAL_SOURCES})
    get_filename_component(MATERIAL_NAME "${MATERIAL}" NAME_WE)
    set(MATERIAL_OUTPUT "${MERCURY_MATERIAL_OUTPUT_DIR}/${MATERIAL_NAME}.filamat")
    add_custom_command(
            OUTPUT "${MATERIAL_OUTPUT}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${MERCURY_MATERIAL_OUTPUT_DIR}"
            COMMAND "${FILAMENT_MATC}" -a vulkan -p desktop -o "${MATERIAL_OUTPUT}" "${MATERIAL}"
            DEPENDS "${MATERIAL}" "${FILAMENT_MATC}"
            COMMENT "MATC ${MATERIAL_NAME}"
            VERBATIM)
    list(APPEND MERCURY_MATERIAL_OUTPUTS "${MATERIAL_OUTPUT}")
endforeach ()
add_custom_target(mercury_materials ALL DEPENDS ${MERCURY_MATERIAL_OUTPUTS})
add_dependencies(mercury_materials filament_ext)

# --------------------------------------------------------------------------
# Player and tools
# --------------------------------------------------------------------------

add_executable(mercury_player mercury/player/main.cc)
target_link_libraries(mercury_player PRIVATE mercury)

add_executable(mercury_model_info tools/model_info.cc)
target_link_libraries(mercury_model_info PRIVATE mercury)

# --------------------------------------------------------------------------
# Editor
# --------------------------------------------------------------------------

if (MERCURY_BUILD_EDITOR)
    find_package(wxWidgets REQUIRED COMPONENTS core base aui propgrid adv richtext stc)
    add_library(mercury_wx INTERFACE)
    target_include_directories(mercury_wx INTERFACE ${wxWidgets_INCLUDE_DIRS})
    target_compile_definitions(mercury_wx INTERFACE ${wxWidgets_DEFINITIONS})
    if (wxWidgets_CXX_FLAGS)
        separate_arguments(WX_CXX_FLAGS NATIVE_COMMAND "${wxWidgets_CXX_FLAGS}")
        target_compile_options(mercury_wx INTERFACE ${WX_CXX_FLAGS})
    endif ()
    target_link_libraries(mercury_wx INTERFACE ${wxWidgets_LIBRARIES})

    file(GLOB MERCURY_EDITOR_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cc")
    list(REMOVE_ITEM MERCURY_EDITOR_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/main.cc")

    add_library(mercury_editor_lib STATIC ${MERCURY_EDITOR_SOURCES})
    target_include_directories(mercury_editor_lib PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/editor")
    target_link_libraries(mercury_editor_lib PUBLIC mercury mercury_wx)

    add_executable(mercury_editor editor/main.cc)
    target_link_libraries(mercury_editor PRIVATE mercury_editor_lib)
endif ()

# --------------------------------------------------------------------------
# Examples
# --------------------------------------------------------------------------

if (MERCURY_BUILD_EXAMPLES)
    add_subdirectory(examples/cpp_hello)
endif ()

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------

if (MERCURY_BUILD_TESTS)
    enable_testing()
    include(GoogleTest)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)

    set(MERCURY_TEST_CACHE_DIR "${CMAKE_BINARY_DIR}/test_cache")
    set(MERCURY_TEST_TARGETS "")

    function(mercury_add_test TARGET)
        add_executable(${TARGET} ${ARGN})
        target_link_libraries(${TARGET} PRIVATE mercury GTest::gtest_main)
        target_compile_definitions(${TARGET} PRIVATE
                MERCURY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
                MERCURY_TEST_CACHE_DIR="${MERCURY_TEST_CACHE_DIR}")
        # Keep the plain build fast: tests are built by the `check` target.
        set_target_properties(${TARGET} PROPERTIES EXCLUDE_FROM_ALL TRUE)
        gtest_discover_tests(${TARGET} DISCOVERY_MODE PRE_TEST)
        set(MERCURY_TEST_TARGETS ${MERCURY_TEST_TARGETS} ${TARGET} PARENT_SCOPE)
    endfunction()

    mercury_add_test(mercury_test
            tests/gameplay_test.cc
            tests/hierarchy_test.cc
            tests/input_actions_test.cc
            tests/map_test.cc
            tests/model_cache_test.cc
            tests/model_preloader_test.cc
            tests/mpack_test.cc
            tests/mtext_test.cc
            tests/parallel_load_test.cc
            tests/physics_constraints_test.cc
            tests/primitives_test.cc
            tests/reflection_test.cc
            tests/rust_bridge_test.cc
            tests/script_behaviour_test.cc
            tests/script_runtime_test.cc
            tests/test_paths.h
            tests/test_scripts.h
            tests/ui_test.cc
            tests/user_path_test.cc)

    mercury_add_test(example_projects_test
            tests/example_projects_test.cc
            tests/test_paths.h)

    mercury_add_test(filament_smoke_test
            tests/filament_smoke_test.cc
            tests/test_paths.h)

    if (MERCURY_BUILD_EDITOR)
        add_executable(editor_smoke tests/editor_smoke.cc)
        target_link_libraries(editor_smoke PRIVATE mercury_editor_lib)
        set_target_properties(editor_smoke PROPERTIES EXCLUDE_FROM_ALL TRUE)
        set(MERCURY_TEST_TARGETS ${MERCURY_TEST_TARGETS} editor_smoke)

        mercury_add_test(editor_test
                tests/editor_templates_test.cc
                tests/test_paths.h)
        target_link_libraries(editor_test PRIVATE mercury_editor_lib)

        mercury_add_test(script_editor_test
                tests/script_editor_test.cc
                tests/test_paths.h)
        target_link_libraries(script_editor_test PRIVATE mercury_editor_lib)
    endif ()

    add_custom_target(check
            COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
            DEPENDS ${MERCURY_TEST_TARGETS}
            COMMENT "Running the Mercury tests"
            USES_TERMINAL)
    add_dependencies(check mercury_materials)
endif ()

# --------------------------------------------------------------------------
# Developer convenience
# --------------------------------------------------------------------------

# clangd reads compile_commands.json from the repository root. Keep the last
# configured build's database there (it is gitignored).
add_custom_target(compile_commands ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_BINARY_DIR}/compile_commands.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
        BYPRODUCTS "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
        COMMENT "Updating compile_commands.json"
        VERBATIM)
