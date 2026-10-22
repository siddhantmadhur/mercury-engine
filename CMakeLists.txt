cmake_minimum_required(VERSION 3.30)

project(MercuryEngine LANGUAGES CXX C)

# Examples, tests and the editor are only built when this is the top-level
# project, not when a game pulls the engine in with add_subdirectory().
option(MERCURY_BUILD_EXAMPLES "Build the example games" ${PROJECT_IS_TOP_LEVEL})
option(MERCURY_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(MERCURY_BUILD_EDITOR "Build mercury_editor" ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "EditAndContinue")

include_directories("third_party")
include_directories("shaders")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# No C++20 modules are used. Scanning for them only slows the build and adds
# GCC-only flags (-fmodules-ts, -fdeps-format) that break clangd.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (EMSCRIPTEN)
    set(MERCURY_GRAPHICS_WEBGPU ON)
    set(IMGUI_BACKEND_SRC third_party/imgui/backends/imgui_impl_wgpu.cpp)
    set(MERCURY_GRAPHICS_SOURCES
            mercury/application/webgpu_application.cc
            mercury/application/webgpu_application.h)
    add_compile_options(--use-port=emdawnwebgpu)
    add_link_options(--use-port=emdawnwebgpu -sUSE_GLFW=3 -sALLOW_MEMORY_GROWTH=1)
else ()
    set(MERCURY_GRAPHICS_VULKAN ON)
    find_package(Vulkan REQUIRED)
    find_package(Threads REQUIRED)
    set(IMGUI_BACKEND_SRC third_party/imgui/backends/imgui_impl_vulkan.cpp)
    set(MERCURY_GRAPHICS_SOURCES
            mercury/application/vulkan_application.cc
            mercury/application/vulkan_application.h
            mercury/application/vulkan/vk_check.h
            mercury/application/vulkan/vk_context.cc
            mercury/application/vulkan/vk_context.h
            mercury/application/vulkan/vk_swapchain.cc
            mercury/application/vulkan/vk_swapchain.h
            mercury/application/vulkan/vk_buffer.cc
            mercury/application/vulkan/vk_buffer.h
            mercury/application/vulkan/vma_impl.cc)
endif ()

add_library(mercury STATIC
        mercury/physics_engine/physics_engine.cc
        mercury/physics_engine/physics_engine.h
        mercury/pobject/manager.cc
        mercury/pobject/manager.h
        mercury/debugger/debugger.h
        mercury/properties/mesh.cc
        mercury/properties/primitives.cc
        mercury/properties/primitives.h
        mercury/properties/transform.h
        mercury/properties/transform.cc
        mercury/properties/transform.h
        mercury/application/application.h
        mercury/application/application.cc
        ${MERCURY_GRAPHICS_SOURCES}
        mercury/os/filesystem.h
        mercury/scene/scene.cc
        mercury/input/input_manager.cc
        mercury/input/input_manager.h
        mercury/properties/texture2d.cc
        mercury/properties/texture2d.h
        mercury/pobject/texture_atlas.cc
        mercury/pobject/texture_atlas.h
        mercury/properties/camera.cc
        mercury/properties/camera.h
        mercury/ui/ui_system.cc
        mercury/ui/ui_system.h
        mercury/ui/ui_element.cc
        mercury/ui/ui_element.h
        mercury/algorithms/sparse_set.h
        mercury/pipelines/renderer.cc
        mercury/pipelines/renderer.h
        mercury/math/vector.cc
        mercury/math/vector.h
        mercury/properties/mesh_instance.h
        mercury/properties/mesh_instance.cc
        mercury/serialization/mtext.cc
        mercury/serialization/mtext.h
        mercury/project/project.cc
        mercury/project/project.h
        mercury/project/map.cc
        mercury/project/map.h
        mercury/project/asset_database.cc
        mercury/project/asset_database.h
        mercury/scripting/reflection.cc
        mercury/scripting/reflection.h
        mercury/scripting/type_registry.cc
        mercury/scripting/type_registry.h
        mercury/scripting/behaviour.h
        mercury/components/components.h
        mercury/components/builtin_types.cc
        mercury/scene/map_scene.cc
        mercury/scene/map_scene.h
        mercury/player/player.cc
        mercury/player/player.h
        mercury/os/process.cc
        mercury/os/process.h
)


include_directories("mercury")

# link imgui

file(GLOB IMGUI_SOURCES
        "${IMGUI_BACKEND_SRC}"
        "third_party/imgui/backends/imgui_impl_glfw.cpp"
        "third_party/imgui/misc/cpp/imgui_stdlib.cpp"
        "third_party/imgui/backends/*.h"
        "third_party/imgui/*.cpp"
        "third_party/imgui/*.h"
)
add_library(imgui STATIC
        ${IMGUI_SOURCES}
)

target_include_directories(imgui PUBLIC "third_party/imgui")
if (EMSCRIPTEN)
    target_link_libraries(imgui)
else ()
    target_link_libraries(imgui glfw Vulkan::Vulkan)
endif ()


add_subdirectory(third_party/glm)


include_directories("third_party/glm")


# JOLT PHYSICS

file(GLOB_RECURSE JOLT_SOURCE "third_party/JoltPhysics/Jolt/*.cpp")


add_library(joltphysics STATIC ${JOLT_SOURCE})
if (NOT EMSCRIPTEN)
    target_link_libraries(joltphysics Threads::Threads)
endif ()

include_directories("third_party/JoltPhysics")


# END OF JOLT PHYSICS

# TinyXML2
add_subdirectory(third_party/tinyxml2)
include_directories("third_party/tinyxml2")
# End of TinyXML2

if (NOT EMSCRIPTEN)
    include_directories("third_party/glfw/include")
    add_subdirectory("third_party/glfw")
endif ()

set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL_PDB OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
include_directories("third_party/assimp/include")
add_subdirectory("third_party/assimp")

include_directories("third_party/entt/single_include")
add_subdirectory("third_party/entt")

# Vulkan Memory Allocator (header-only; implemented in vma_impl.cc)
include_directories("third_party/VulkanMemoryAllocator/include")


if (EMSCRIPTEN)
    target_compile_definitions(mercury PUBLIC
            MERCURY_SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/mercury/shaders"
            MERCURY_ASSET_PATH="${CMAKE_CURRENT_SOURCE_DIR}/examples/assets"
            MERCURY_GRAPHICS_WEBGPU=1
            GLM_FORCE_DEPTH_ZERO_TO_ONE
    )
    target_link_libraries(mercury imgui glm::glm joltphysics tinyxml2 assimp EnTT::EnTT)
else ()
    target_compile_definitions(mercury PUBLIC
            MERCURY_SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/mercury/shaders"
            MERCURY_ASSET_PATH="${CMAKE_CURRENT_SOURCE_DIR}/examples/assets"
            MERCURY_GRAPHICS_VULKAN=1
            GLM_FORCE_DEPTH_ZERO_TO_ONE
    )
    target_link_libraries(mercury imgui glm::glm joltphysics tinyxml2 glfw assimp
            EnTT::EnTT Vulkan::Vulkan Threads::Threads)
endif ()
# Games built outside this folder (see cmake/MercuryGame.cmake) need the same
# language level and include paths the engine uses.
target_compile_features(mercury PUBLIC cxx_std_20)
target_include_directories(mercury PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/mercury"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JoltPhysics"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/entt/single_include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/VulkanMemoryAllocator/include")

if (MSVC)
    message(STATUS "Using Visual Studio Compiler")
endif ()

if (NOT EMSCRIPTEN)
    # Compile HLSL -> SPIR-V via DXC at build time. The Y flip is done with a
    # negative viewport height at draw time, so no -fvk-invert-y here.
    find_program(DXC dxc
            HINTS $ENV{VULKAN_SDK}/bin ${CMAKE_CURRENT_SOURCE_DIR}/bin/dxc/bin
            REQUIRED)
    set(SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/mercury/shaders)
    set(SHADER_BIN ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${SHADER_BIN})
    set(DXC_COMMON_FLAGS -spirv
            -fvk-b-shift 0 0    # cbuffer bN -> binding N
            -fvk-t-shift 10 0   # texture tN -> binding N+10
            -fvk-s-shift 20 0   # sampler sN -> binding N+20
            $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Od>
            $<$<NOT:$<CONFIG:Debug>>:-O3>)
    # job: src|profile|out
    set(SHADER_JOBS
            "main.vs.hlsl|vs_6_0|main.vs.spv"
            "static.vs.hlsl|vs_6_0|static.vs.spv"
            "shadow.vs.hlsl|vs_6_0|shadow.vs.spv"
            "skybox.vs.hlsl|vs_6_0|skybox.vs.spv"
            "skybox.ps.hlsl|ps_6_0|skybox.ps.spv"
            "textured.ps.hlsl|ps_6_0|textured.ps.spv")
    set(SPV_OUTS "")
    foreach (JOB ${SHADER_JOBS})
        string(REPLACE "|" ";" JOB_PARTS "${JOB}")
        list(GET JOB_PARTS 0 SRC)
        list(GET JOB_PARTS 1 PROFILE)
        list(GET JOB_PARTS 2 OUT)
        add_custom_command(
                OUTPUT ${SHADER_BIN}/${OUT}
                COMMAND ${DXC} ${DXC_COMMON_FLAGS}
                -T ${PROFILE} -E Main
                ${SHADER_SRC}/${SRC} -Fo ${SHADER_BIN}/${OUT}
                DEPENDS ${SHADER_SRC}/${SRC}
                COMMENT "DXC ${SRC} -> ${OUT}"
                COMMAND_EXPAND_LISTS)
        list(APPEND SPV_OUTS ${SHADER_BIN}/${OUT})
    endforeach ()
    add_custom_target(mercury_shaders DEPENDS ${SPV_OUTS})
    add_dependencies(mercury mercury_shaders)
    target_compile_definitions(mercury PUBLIC MERCURY_SPIRV_PATH="${SHADER_BIN}")
endif ()


# CREATE NEW BINARIES USING THIS


# Unit tests (ctest --test-dir build)
if (MERCURY_BUILD_TESTS AND NOT EMSCRIPTEN)
enable_testing()
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)
add_executable(mercury_test
        tests/mtext_test.cc
        tests/map_test.cc
        tests/reflection_test.cc
        tests/primitives_test.cc)
target_link_libraries(mercury_test mercury GTest::gtest_main)
target_compile_definitions(mercury_test PRIVATE
        MERCURY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
include(GoogleTest)
gtest_discover_tests(mercury_test)
endif ()

# Games made of a .mproj project (maps, assets and C++ scripts)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MercuryGame.cmake)
if (MERCURY_BUILD_EXAMPLES)
    add_subdirectory(examples/pong)
    add_subdirectory(examples/roll_a_ball)
    add_subdirectory(examples/monkey_ball)

    # Older examples that still build their worlds in code
    if (NOT EMSCRIPTEN)
        add_executable(example_breakout examples/breakout/main.cc
                examples/breakout/breakout_scene.cc
                examples/breakout/breakout_scene.h)
        target_link_libraries(example_breakout mercury)

        add_executable(example_space_invaders examples/space_invaders/main.cc
                examples/space_invaders/space_invaders.cc
                examples/space_invaders/space_invaders.h)
        target_link_libraries(example_space_invaders mercury)
    endif ()
endif ()

if (MERCURY_BUILD_EDITOR AND NOT EMSCRIPTEN)
    # Everything but main(), so the smoke test can drive the same editor.
    add_library(mercury_editor_lib STATIC
            editor/editor_scene.cc
            editor/editor_scene.h
            editor/panels.cc
            editor/scene_view.cc
            editor/console.cc
            editor/console.h
            editor/editor_settings.cc
            editor/editor_settings.h
            editor/play_session.cc
            editor/play_session.h
            editor/templates.cc
            editor/templates.h)
    target_link_libraries(mercury_editor_lib PUBLIC mercury)
    target_include_directories(mercury_editor_lib PUBLIC editor)
    # New projects build against this checkout of the engine.
    target_compile_definitions(mercury_editor_lib PRIVATE
            MERCURY_ENGINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    add_executable(mercury_editor editor/main.cc)
    target_link_libraries(mercury_editor mercury_editor_lib)

    if (MERCURY_BUILD_TESTS)
        # Replays scripted input into the editor; see the file for the format.
        add_executable(mercury_editor_smoke tests/editor_smoke.cc)
        target_link_libraries(mercury_editor_smoke mercury_editor_lib)
    endif ()
endif ()
