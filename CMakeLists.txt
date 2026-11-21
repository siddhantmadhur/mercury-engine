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

project(MercuryEngine VERSION 1.0.0 LANGUAGES C CXX ASM)

# One version source. The generated version.h is what every engine component,
# the extension ABI check and the SDK's package version use. A non-release
# build carries a -dev suffix; extension compatibility is compared on
# major.minor, so that suffix (and a patch release) never rejects a module.
set(MERCURY_VERSION_STRING "${PROJECT_VERSION}")
if (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    set(MERCURY_VERSION_STRING "${PROJECT_VERSION}-dev")
endif ()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/mercury")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in"
        "${CMAKE_BINARY_DIR}/generated/mercury/version.h" @ONLY)

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

# toml++ stays private: no public engine header includes it, so SDK installs
# and consumers never see it.
target_link_libraries(mercury PRIVATE mercury_tomlplusplus)

# JSON for the introspection API (mercury/introspection): same arrangement —
# built only inside the engine's implementation files, so the SDK surface
# keeps plain std::string results.
target_link_libraries(mercury PRIVATE nlohmann_json::nlohmann_json)

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
# Every consumer of the engine library runs with the .filamat files, so a
# targeted build (--target mercury_player, an editor or an example) must
# rebuild them when a .mat changes; the ALL target alone is not enough.
add_dependencies(mercury mercury_materials)

# --------------------------------------------------------------------------
# Player and tools
# --------------------------------------------------------------------------

add_executable(mercury_player mercury/player/main.cc)
target_link_libraries(mercury_player PRIVATE mercury)
mercury_link_rust(mercury_player)

add_executable(mercury_model_info tools/model_info.cc)
target_link_libraries(mercury_model_info PRIVATE mercury)
mercury_link_rust(mercury_model_info)

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

    # The same include paths and flags without the wx libraries. Editor system
    # modules use this: they resolve wx symbols against the running editor
    # instead of linking a second copy of wxWidgets.
    add_library(mercury_wx_headers INTERFACE)
    target_include_directories(mercury_wx_headers INTERFACE
            ${wxWidgets_INCLUDE_DIRS})
    target_compile_definitions(mercury_wx_headers INTERFACE
            ${wxWidgets_DEFINITIONS})
    if (wxWidgets_CXX_FLAGS)
        target_compile_options(mercury_wx_headers INTERFACE ${WX_CXX_FLAGS})
    endif ()

    file(GLOB MERCURY_EDITOR_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cc")
    list(REMOVE_ITEM MERCURY_EDITOR_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/main.cc")

    add_library(mercury_editor_lib STATIC ${MERCURY_EDITOR_SOURCES})
    target_include_directories(mercury_editor_lib PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/editor")
    target_link_libraries(mercury_editor_lib PUBLIC mercury mercury_wx)
    # lore_vcs.cc parses .lore/config.toml with toml++; the library is internal
    # (not installed), so the PRIVATE link keeps toml++ out of the SDK surface.
    target_link_libraries(mercury_editor_lib PRIVATE mercury_tomlplusplus)
    # The MCP transport bridge's generated C++ side; its Rust counterpart is
    # in the editor Rust archive (mercury_rust_editor).
    target_sources(mercury_editor_lib PRIVATE ${MERCURY_MCP_BRIDGE_SOURCES})
    # The MCP host: tool registry, settings plumbing and the .mcp.json writer.
    file(GLOB MERCURY_MCP_HOST_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/mcp/*.cc")
    target_sources(mercury_editor_lib PRIVATE ${MERCURY_MCP_HOST_SOURCES})
    # Tool handlers pass nlohmann::json values around; the library is internal
    # to the editor (not installed as a link dependency), like toml++ above.
    target_link_libraries(mercury_editor_lib PRIVATE nlohmann_json::nlohmann_json)

    # The MCP tool groups (editor/mcp/tools/<group>.cc). Each file registers its
    # tools with a static initializer (MERCURY_MCP_TOOL), which a static
    # archive drops when nothing references the file. So these are an OBJECT
    # library whose objects are linked straight into the editor and into every
    # editor-linked executable (mercury_link_mcp_tools), never through an
    # archive; the glob means a new tool file needs no CMake or registration
    # edit. Same include dirs and definitions as mercury_editor_lib, nlohmann
    # PRIVATE (the tool headers are not installed in the SDK).
    file(GLOB MERCURY_MCP_TOOL_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/mcp/tools/*.cc")
    add_library(mercury_mcp_tools OBJECT ${MERCURY_MCP_TOOL_SOURCES})
    target_include_directories(mercury_mcp_tools PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/editor")
    target_link_libraries(mercury_mcp_tools PRIVATE
            mercury mercury_wx nlohmann_json::nlohmann_json)

    # mercury_link_mcp_tools(<target>): add the tool objects to an executable
    # that links mercury_editor_lib. List them before the archives so the
    # symbols they use from mercury_editor_lib are still pulled in.
    function(mercury_link_mcp_tools TARGET)
        target_sources(${TARGET} PRIVATE $<TARGET_OBJECTS:mercury_mcp_tools>)
    endfunction()

    add_executable(mercury_editor editor/main.cc)
    target_link_libraries(mercury_editor PRIVATE mercury_editor_lib)
    mercury_link_mcp_tools(mercury_editor)
    # The editor is the only binary with the MCP transport (tokio + rmcp).
    mercury_link_rust(mercury_editor EDITOR)
    # Editor systems are modules loaded at run time; their engine, editor and
    # wx symbols resolve against this executable, so it exports them. Forcing
    # the archives in also exports symbols the editor itself does not call.
    if (APPLE)
        target_link_options(mercury_editor PRIVATE "-Wl,-export_dynamic")
        target_link_options(mercury_editor PRIVATE
                "LINKER:-force_load,$<TARGET_FILE:mercury_editor_lib>"
                "LINKER:-force_load,$<TARGET_FILE:mercury>")
    elseif (UNIX)
        target_link_options(mercury_editor PRIVATE "-rdynamic")
        target_link_options(mercury_editor PRIVATE
                "LINKER:--whole-archive"
                "$<TARGET_FILE:mercury_editor_lib>"
                "LINKER:--no-whole-archive")
    endif ()
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
    # The benchmark project holds the large models some tests import; they skip
    # when it is missing. The MERCURY_BENCHMARK_DIR environment variable
    # overrides this at run time.
    set(MERCURY_BENCHMARK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../benchmark" CACHE PATH
            "Folder of the benchmark project (models and maps) used by tests")
    set(MERCURY_TEST_TARGETS "")

    # mercury_add_test(<target> [EDITOR] <sources...>)
    #
    # EDITOR marks tests that link mercury_editor_lib and therefore need the
    # editor Rust archive (with the MCP transport) instead of the plain one.
    function(mercury_add_test TARGET)
        cmake_parse_arguments(TEST "EDITOR" "" "" ${ARGN})
        add_executable(${TARGET} ${TEST_UNPARSED_ARGUMENTS})
        target_link_libraries(${TARGET} PRIVATE mercury GTest::gtest_main)
        target_compile_definitions(${TARGET} PRIVATE
                MERCURY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
                MERCURY_TEST_CACHE_DIR="${MERCURY_TEST_CACHE_DIR}"
                MERCURY_BENCHMARK_DIR="${MERCURY_BENCHMARK_DIR}")
        # Keep the plain build fast: tests are built by the `check` target.
        set_target_properties(${TARGET} PROPERTIES EXCLUDE_FROM_ALL TRUE)
        gtest_discover_tests(${TARGET} DISCOVERY_MODE PRE_TEST)
        if (TEST_EDITOR)
            mercury_link_rust(${TARGET} EDITOR)
            mercury_link_mcp_tools(${TARGET})
        else ()
            mercury_link_rust(${TARGET})
        endif ()
        set(MERCURY_TEST_TARGETS ${MERCURY_TEST_TARGETS} ${TARGET} PARENT_SCOPE)
    endfunction()

    mercury_add_test(mercury_test
            tests/gameplay_test.cc
            tests/hierarchy_test.cc
            tests/image_load_test.cc
            tests/input_actions_test.cc
            tests/introspection_test.cc
            tests/map_test.cc
            tests/model_cache_test.cc
            tests/model_preloader_test.cc
            tests/mpack_test.cc
            tests/mtext_test.cc
            tests/obj_material_test.cc
            tests/parallel_load_test.cc
            tests/physics_constraints_test.cc
            tests/player_args_test.cc
            tests/primitives_test.cc
            tests/process_test.cc
            tests/project_settings_test.cc
            tests/reflection_test.cc
            tests/render_settings_test.cc
            tests/rust_bridge_test.cc
            tests/script_behaviour_test.cc
            tests/script_runtime_test.cc
            tests/sparse_set_test.cc
            tests/test_paths.h
            tests/test_scripts.h
            tests/ui_test.cc
            tests/user_path_test.cc
            tests/asset_importer_test.cc
            tests/component_serialization_test.cc
            tests/engine_sdk_dir_test.cc
            tests/event_bus_test.cc
            tests/system_registry_test.cc)
    # The introspection tests read the schema JSON back with nlohmann/json.
    target_link_libraries(mercury_test PRIVATE nlohmann_json::nlohmann_json)

    mercury_add_test(example_projects_test
            tests/example_projects_test.cc
            tests/test_paths.h)

    mercury_add_test(filament_smoke_test
            tests/filament_smoke_test.cc
            tests/test_paths.h)

    # The player running a whole example headlessly, the way the MCP server
    # drives games: load the project, compile its scripts, step 30 frames and
    # exit 0 (--headless --frames in mercury/player/player.cc).
    add_test(NAME mercury_player_headless_frames
            COMMAND mercury_player
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/roll_a_ball/roll_a_ball.mproj
            --headless --frames 30)

    if (MERCURY_BUILD_EDITOR)
        add_executable(editor_smoke tests/editor_smoke.cc)
        target_link_libraries(editor_smoke PRIVATE mercury_editor_lib)
        mercury_link_rust(editor_smoke EDITOR)
        mercury_link_mcp_tools(editor_smoke)
        set_target_properties(editor_smoke PROPERTIES EXCLUDE_FROM_ALL TRUE)
        set(MERCURY_TEST_TARGETS ${MERCURY_TEST_TARGETS} editor_smoke)

        # Unload-order regression: a module's custom inspector must be gone
        # before its library would be unloaded. Needs a real wx frame, so it
        # is a smoke executable like editor_smoke.
        add_executable(editor_extension_unload_smoke
                tests/editor_extension_unload_smoke.cc)
        target_link_libraries(editor_extension_unload_smoke
                PRIVATE mercury_editor_lib)
        target_compile_definitions(editor_extension_unload_smoke PRIVATE
                MERCURY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        set_target_properties(editor_extension_unload_smoke PROPERTIES
                EXCLUDE_FROM_ALL TRUE)
        set(MERCURY_TEST_TARGETS ${MERCURY_TEST_TARGETS}
                editor_extension_unload_smoke)
        mercury_link_rust(editor_extension_unload_smoke EDITOR)
        mercury_link_mcp_tools(editor_extension_unload_smoke)

        # The New Script dialog is modal; this smoke drives it with CallAfter
        # and creates all four kinds through EditorScene.
        add_executable(new_script_dialog_smoke
                tests/new_script_dialog_smoke.cc)
        target_link_libraries(new_script_dialog_smoke
                PRIVATE mercury_editor_lib)
        mercury_link_rust(new_script_dialog_smoke EDITOR)
        mercury_link_mcp_tools(new_script_dialog_smoke)
        set_target_properties(new_script_dialog_smoke PROPERTIES
                EXCLUDE_FROM_ALL TRUE)
        set(MERCURY_TEST_TARGETS ${MERCURY_TEST_TARGETS}
                new_script_dialog_smoke)

        # The preferences dialog is modal; this smoke flips to its MCP tab,
        # applies and cancels through EditorScene's MCP host.
        add_executable(mcp_prefs_dialog_smoke
                tests/mcp_prefs_dialog_smoke.cc)
        target_link_libraries(mcp_prefs_dialog_smoke
                PRIVATE mercury_editor_lib)
        mercury_link_rust(mcp_prefs_dialog_smoke EDITOR)
        mercury_link_mcp_tools(mcp_prefs_dialog_smoke)
        set_target_properties(mcp_prefs_dialog_smoke PROPERTIES
                EXCLUDE_FROM_ALL TRUE)
        set(MERCURY_TEST_TARGETS ${MERCURY_TEST_TARGETS}
                mcp_prefs_dialog_smoke)

        mercury_add_test(editor_test EDITOR
                tests/editor_templates_test.cc
                tests/lore_config_test.cc
                tests/project_scaffold_test.cc
                tests/test_paths.h)
        target_link_libraries(editor_test PRIVATE mercury_editor_lib)

        mercury_add_test(project_trust_test EDITOR
                tests/project_trust_test.cc)
        target_link_libraries(project_trust_test PRIVATE mercury_editor_lib)

        # The MCP host: registry dispatch, mode re-check, .mcp.json handling,
        # and the disabled-means-no-socket rule.
        mercury_add_test(mcp_host_test EDITOR
                tests/mcp_host_test.cc)
        target_link_libraries(mcp_host_test PRIVATE mercury_editor_lib)
        # The test includes <nlohmann/json.hpp> directly; mercury_editor_lib
        # links it PRIVATE, so the usage requirement does not propagate.
        target_link_libraries(mcp_host_test PRIVATE nlohmann_json::nlohmann_json)

        # The MCP tool groups' tests: every tests/mcp_tools_*_test.cc joins one
        # executable, so a new tool group needs no CMake edit. The glob is
        # re-run on every build (CONFIGURE_DEPENDS); with no matching file no
        # target is made (gtest discovery of an empty executable would fail).
        file(GLOB MCP_TOOLS_TESTS CONFIGURE_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/mcp_tools_*_test.cc")
        if (MCP_TOOLS_TESTS)
            mercury_add_test(mcp_tools_test EDITOR ${MCP_TOOLS_TESTS}
                    tests/mcp_tools_fixture.h)
            target_link_libraries(mcp_tools_test PRIVATE mercury_editor_lib
                    nlohmann_json::nlohmann_json)
        endif ()

        mercury_add_test(script_editor_test EDITOR
                tests/script_editor_test.cc
                tests/test_paths.h)
        target_link_libraries(script_editor_test PRIVATE mercury_editor_lib)

        # Tiny modules for editor_extension_test: one valid, one with a
        # deliberately wrong ABI. They call no editor symbols.
        function(mercury_add_test_extension TARGET)
            add_library(${TARGET} MODULE
                    tests/extensions/editor_test_extension.cc)
            target_compile_features(${TARGET} PRIVATE cxx_std_20)
            target_include_directories(${TARGET} PRIVATE
                    "${CMAKE_CURRENT_SOURCE_DIR}/editor"
                    $<TARGET_PROPERTY:mercury,INTERFACE_INCLUDE_DIRECTORIES>)
            target_compile_definitions(${TARGET} PRIVATE
                    $<TARGET_PROPERTY:mercury,INTERFACE_COMPILE_DEFINITIONS>)
            target_link_libraries(${TARGET} PRIVATE mercury_wx_headers)
            if (APPLE)
                target_link_options(${TARGET} PRIVATE
                        "-Wl,-undefined,dynamic_lookup")
            endif ()
            set_target_properties(${TARGET} PROPERTIES
                    PREFIX "" SUFFIX ".mercury-extension")
        endfunction()

        mercury_add_test_extension(editor_test_extension)
        mercury_add_test_extension(editor_test_extension_bad_abi)
        target_compile_definitions(editor_test_extension_bad_abi PRIVATE
                MERCURY_TEST_EXTENSION_ABI=999)

        mercury_add_test(editor_extension_test EDITOR
                tests/editor_extension_test.cc)
        target_link_libraries(editor_extension_test PRIVATE mercury_editor_lib)
        target_compile_definitions(editor_extension_test PRIVATE
                MERCURY_TEST_EXTENSION_PATH="$<TARGET_FILE:editor_test_extension>"
                MERCURY_TEST_EXTENSION_BAD_ABI_PATH="$<TARGET_FILE:editor_test_extension_bad_abi>")
        add_dependencies(editor_extension_test
                editor_test_extension editor_test_extension_bad_abi)
    endif ()

    add_custom_target(check
            COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
            DEPENDS ${MERCURY_TEST_TARGETS}
            COMMENT "Running the Mercury tests"
            USES_TERMINAL)
    add_dependencies(check mercury_materials)
    # The headless player CTest runs the whole mercury_player executable.
    add_dependencies(check mercury_player)

    # The mercury_mcp crate's Rust unit tests (transport start/stop, reply
    # plumbing). Same env handling as the CMake-driven cargo builds: a
    # dedicated target dir and no MACOSX_DEPLOYMENT_TARGET, which would break
    # proc-macro compilation (see cmake/Rust.cmake).
    add_test(NAME mercury_mcp_cargo_test
            COMMAND "${CMAKE_COMMAND}" -E env
                    "CARGO_TARGET_DIR=${CMAKE_BINARY_DIR}/cargo-test"
                    "--unset=MACOSX_DEPLOYMENT_TARGET"
                    "${CARGO_EXECUTABLE}" test -p mercury_mcp --locked
                    --manifest-path "${MERCURY_ENGINE_SOURCE_DIR}/Cargo.toml")
endif ()

# --------------------------------------------------------------------------
# SDK: headers, static archives and a relocatable CMake package
#
#   cmake --install <build> --prefix <prefix> --component sdk
#
# produces <prefix>/sdk/{include,lib,cmake}. scripts/package_macos.pl copies
# the same tree into <app>/Contents/Resources/sdk.
# --------------------------------------------------------------------------

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/mercury/"
        DESTINATION sdk/include/mercury
        COMPONENT sdk
        FILES_MATCHING PATTERN "*.h"
        # Internal: includes nlohmann/json (see the header's comment).
        PATTERN "introspection/variant_json.h" EXCLUDE)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/editor/"
        DESTINATION sdk/include/editor
        COMPONENT sdk
        FILES_MATCHING PATTERN "*.h"
        # The MCP registry's headers pass nlohmann::json values around and
        # tool_host.h is cxx-bridge-only; the SDK installs mcp/mcp_host.h,
        # which stays plain C++ (the pimpl keeps the Rust side in mcp_host.cc).
        PATTERN "mcp/tool_registry.h" EXCLUDE
        PATTERN "mcp/json_util.h" EXCLUDE
        PATTERN "mcp/tool_host.h" EXCLUDE
        # The tool groups' headers (scene_json.h includes nlohmann/json).
        PATTERN "mcp/tools" EXCLUDE)
# wxWidgets headers, so editor systems build without a wx development install.
# (The module still resolves wx symbols against the running editor.)
# wxWidgets_INCLUDE_DIRS only has the setup.h directory on macOS; the headers
# themselves come from the -I flags in wxWidgets_CXX_FLAGS.
if (MERCURY_BUILD_EDITOR)
    set(MERCURY_SDK_WX_INCLUDE_DIRS ${wxWidgets_INCLUDE_DIRS})
    foreach (FLAG ${WX_CXX_FLAGS})
        if (FLAG MATCHES "^-I(.+)$")
            list(APPEND MERCURY_SDK_WX_INCLUDE_DIRS "${CMAKE_MATCH_1}")
        endif ()
    endforeach ()
    list(REMOVE_DUPLICATES MERCURY_SDK_WX_INCLUDE_DIRS)
    foreach (WX_INCLUDE ${MERCURY_SDK_WX_INCLUDE_DIRS})
        if (EXISTS "${WX_INCLUDE}/wx/wx.h" OR EXISTS "${WX_INCLUDE}/wx/setup.h")
            # Homebrew's wx header directory is a symlink; resolve it so the
            # install rule copies the headers instead of failing on the link.
            file(REAL_PATH "${WX_INCLUDE}" WX_INCLUDE_REAL)
            install(DIRECTORY "${WX_INCLUDE_REAL}/"
                    DESTINATION sdk/include COMPONENT sdk
                    FILES_MATCHING PATTERN "*.h")
        endif ()
    endforeach ()
endif ()
install(DIRECTORY "${MERCURY_GENERATED_DIR}/"
        DESTINATION sdk/include
        COMPONENT sdk
        FILES_MATCHING PATTERN "*.h")
if (MERCURY_CXX_INCLUDE_DIR)
    install(FILES "${MERCURY_CXX_INCLUDE_DIR}/cxx.h"
            DESTINATION sdk/include COMPONENT sdk)
endif ()

# Only the third-party headers reachable from Mercury's public headers.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/include/"
        DESTINATION sdk/include COMPONENT sdk)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JoltPhysics/Jolt/"
        DESTINATION sdk/include/Jolt COMPONENT sdk)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/assimp/include/"
        DESTINATION sdk/include COMPONENT sdk)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/entt/src/entt/"
        DESTINATION sdk/include/entt COMPONENT sdk)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm/glm/"
        DESTINATION sdk/include/glm COMPONENT sdk)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/"
        DESTINATION sdk/include/stb COMPONENT sdk)
install(DIRECTORY
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/angelscript/sdk/angelscript/include/"
        DESTINATION sdk/include COMPONENT sdk)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/angelscript/sdk/add_on/"
        DESTINATION sdk/include COMPONENT sdk)
# Filament's headers (filament/, backend/, utils/, ...).
foreach (INCLUDE_DIR
        third_party/filament/filament/include
        third_party/filament/filament/backend/include
        third_party/filament/libs/utils/include
        third_party/filament/libs/filabridge/include
        third_party/filament/libs/filaflat/include
        third_party/filament/libs/geometry/include
        third_party/filament/libs/ibl/include
        third_party/filament/libs/image/include
        third_party/filament/libs/iblprefilter/include
        third_party/filament/libs/math/include
        third_party/filament/third_party/robin-map/include)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}/"
            DESTINATION sdk/include COMPONENT sdk)
endforeach ()

# Static archives. Filament builds in its own tree. The editor Rust archive
# (with the MCP transport) is never installed: SDK games link the plain one.
install(DIRECTORY "${CMAKE_BINARY_DIR}/lib/"
        DESTINATION sdk/lib COMPONENT sdk
        FILES_MATCHING PATTERN "*.a"
        PATTERN "libmercury_editor_lib.a" EXCLUDE
        PATTERN "libmercury_rust_editor.a" EXCLUDE
        PATTERN "libgtest.a" EXCLUDE
        PATTERN "libgtest_main.a" EXCLUDE
        PATTERN "libgmock.a" EXCLUDE
        PATTERN "libgmock_main.a" EXCLUDE)
if (EXISTS "${CMAKE_BINARY_DIR}/_deps/filament/out/lib")
    install(DIRECTORY "${CMAKE_BINARY_DIR}/_deps/filament/out/lib/"
            DESTINATION sdk/lib COMPONENT sdk
            FILES_MATCHING PATTERN "*.a")
endif ()

include(CMakePackageConfigHelpers)
set(MERCURY_SDK_CONFIG_DIR "${CMAKE_BINARY_DIR}/sdk-config")
# The in-tree link order; see cmake/MercuryConfig.cmake.in.
set(MERCURY_SDK_LIBRARY_NAMES
        libmercury.a
        libmercury_angelscript.a
        libmercury_jolt.a
        libmercury_meshoptimizer.a
        libmercury_miniaudio.a
        libtinyxml2.a
        libmercury_yoga.a
        libassimp.a
        libzlibstatic.a
        libglfw3.a
        libfilament.a
        libbackend.a
        libbluevk.a
        libfilaflat.a
        libfilabridge.a
        libfilament-iblprefilter.a
        libgeometry.a
        libibl.a
        libimage.a
        libsmol-v.a
        libutils.a
        libzstd.a
        libmercury_rust.a)
configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MercuryConfig.cmake.in"
        "${MERCURY_SDK_CONFIG_DIR}/MercuryConfig.cmake"
        INSTALL_DESTINATION sdk/cmake)
write_basic_package_version_file(
        "${MERCURY_SDK_CONFIG_DIR}/MercuryConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
install(FILES
        "${MERCURY_SDK_CONFIG_DIR}/MercuryConfig.cmake"
        "${MERCURY_SDK_CONFIG_DIR}/MercuryConfigVersion.cmake"
        DESTINATION sdk/cmake COMPONENT sdk)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MercuryGameSdk.cmake"
        DESTINATION sdk/cmake RENAME MercuryGame.cmake COMPONENT sdk)

# A development editor configures and builds each project's own tree the same
# way a packaged one does. Generate the package that points at this build tree
# so `find_package(Mercury CONFIG)` works before anything is installed.
set(MERCURY_DEV_SDK_DIR "${CMAKE_BINARY_DIR}/dev-sdk")
file(MAKE_DIRECTORY "${MERCURY_DEV_SDK_DIR}")
set(MERCURY_DEV_SDK_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/mercury"
        "${CMAKE_CURRENT_SOURCE_DIR}/editor"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JoltPhysics"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/assimp/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/entt/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/angelscript/sdk/angelscript/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/angelscript/sdk/add_on"
        "${MERCURY_GENERATED_DIR}"
        "${MERCURY_GENERATED_DIR}/mercury")
if (MERCURY_CXX_INCLUDE_DIR)
    list(APPEND MERCURY_DEV_SDK_INCLUDE_DIRS "${MERCURY_CXX_INCLUDE_DIR}")
endif ()
if (MERCURY_BUILD_EDITOR)
    list(APPEND MERCURY_DEV_SDK_INCLUDE_DIRS ${MERCURY_SDK_WX_INCLUDE_DIRS})
endif ()
foreach (INCLUDE_DIR
        third_party/filament/filament/include
        third_party/filament/filament/backend/include
        third_party/filament/libs/utils/include
        third_party/filament/libs/filabridge/include
        third_party/filament/libs/filaflat/include
        third_party/filament/libs/geometry/include
        third_party/filament/libs/ibl/include
        third_party/filament/libs/image/include
        third_party/filament/libs/iblprefilter/include
        third_party/filament/libs/math/include
        third_party/filament/third_party/robin-map/include)
    list(APPEND MERCURY_DEV_SDK_INCLUDE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}")
endforeach ()

# Archives that Filament builds in its own tree; everything else is in
# ${CMAKE_BINARY_DIR}/lib.
set(MERCURY_FILAMENT_SDK_ARCHIVES
        libfilament.a
        libbackend.a
        libbluevk.a
        libfilaflat.a
        libfilabridge.a
        libfilament-iblprefilter.a
        libgeometry.a
        libibl.a
        libimage.a
        libsmol-v.a
        libutils.a
        libzstd.a)
set(MERCURY_DEV_SDK_LIBRARIES "")
foreach (NAME ${MERCURY_SDK_LIBRARY_NAMES})
    if (NAME IN_LIST MERCURY_FILAMENT_SDK_ARCHIVES)
        set(ARCHIVE "${CMAKE_BINARY_DIR}/_deps/filament/out/lib/${NAME}")
    else ()
        set(ARCHIVE "${CMAKE_BINARY_DIR}/lib/${NAME}")
    endif ()
    list(APPEND MERCURY_DEV_SDK_LIBRARIES "${ARCHIVE}")
endforeach ()

file(WRITE "${MERCURY_DEV_SDK_DIR}/MercuryConfig.cmake"
        "# Generated for this engine build tree; do not edit.\n"
        "# The editor passes -DMercury_DIR=<this folder> to project builds.\n")
file(APPEND "${MERCURY_DEV_SDK_DIR}/MercuryConfig.cmake"
        "set(Mercury_INCLUDE_DIRS\n")
foreach (DIR ${MERCURY_DEV_SDK_INCLUDE_DIRS})
    file(APPEND "${MERCURY_DEV_SDK_DIR}/MercuryConfig.cmake" "  \"${DIR}\"\n")
endforeach ()
file(APPEND "${MERCURY_DEV_SDK_DIR}/MercuryConfig.cmake"
        ")\nset(Mercury_LIBRARIES\n")
foreach (LIB ${MERCURY_DEV_SDK_LIBRARIES})
    file(APPEND "${MERCURY_DEV_SDK_DIR}/MercuryConfig.cmake" "  \"${LIB}\"\n")
endforeach ()
file(APPEND "${MERCURY_DEV_SDK_DIR}/MercuryConfig.cmake"
        ")\n"
        "include(CMakeFindDependencyMacro)\n"
        "find_dependency(Threads)\n"
        "find_dependency(Vulkan)\n"
        "include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/MercuryGameSdk.cmake\")\n")

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
