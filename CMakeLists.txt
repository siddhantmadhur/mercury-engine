cmake_minimum_required(VERSION 3.30)

project(MercuryEngine LANGUAGES CXX C)

set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "EditAndContinue")

include_directories("third_party")
include_directories("shaders")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Vulkan is the only graphics backend, on every platform.
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
set(IMGUI_BACKEND_SRC third_party/imgui/backends/imgui_impl_vulkan.cpp)

add_library(mercury
        mercury/physics_engine/physics_engine.cc
        mercury/physics_engine/physics_engine.h
        mercury/pobject/manager.cc
        mercury/pobject/manager.h
        mercury/debugger/debugger.h
        mercury/properties/mesh.cc
        mercury/properties/transform.h
        mercury/properties/transform.cc
        mercury/properties/transform.h
        mercury/application/application.h
        mercury/application/application.cc
        mercury/application/vulkan_application.cc
        mercury/application/vulkan_application.h
        mercury/application/vulkan/vk_check.h
        mercury/application/vulkan/vk_context.cc
        mercury/application/vulkan/vk_context.h
        mercury/application/vulkan/vk_swapchain.cc
        mercury/application/vulkan/vk_swapchain.h
        mercury/application/vulkan/vk_buffer.cc
        mercury/application/vulkan/vk_buffer.h
        mercury/application/vulkan/vma_impl.cc
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
)


include_directories("mercury")
include_directories("editor")

# link imgui

file(GLOB IMGUI_SOURCES
        "${IMGUI_BACKEND_SRC}"
        "third_party/imgui/backends/imgui_impl_glfw.cpp"
        "third_party/imgui/backends/*.h"
        "third_party/imgui/*.cpp"
        "third_party/imgui/*.h"
)
add_library(imgui STATIC
        ${IMGUI_SOURCES}
)

target_include_directories(imgui PUBLIC "third_party/imgui")
target_link_libraries(imgui glfw Vulkan::Vulkan)


add_subdirectory(third_party/glm)


include_directories("third_party/glm")


# JOLT PHYSICS

file(GLOB_RECURSE JOLT_SOURCE "third_party/JoltPhysics/Jolt/*.cpp")


add_library(joltphysics STATIC ${JOLT_SOURCE})
target_link_libraries(joltphysics Threads::Threads)

include_directories("third_party/JoltPhysics")


# END OF JOLT PHYSICS

# TinyXML2
add_subdirectory(third_party/tinyxml2)
include_directories("third_party/tinyxml2")
# End of TinyXML2

include_directories("third_party/glfw/include")
add_subdirectory("third_party/glfw")


include_directories("third_party/assimp/include")
add_subdirectory("third_party/assimp")

include_directories("third_party/entt/single_include")
add_subdirectory("third_party/entt")

# Vulkan Memory Allocator (header-only; implemented in vma_impl.cc)
include_directories("third_party/VulkanMemoryAllocator/include")


target_compile_definitions(mercury PUBLIC
        MERCURY_SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/mercury/shaders"
        MERCURY_ASSET_PATH="${CMAKE_CURRENT_SOURCE_DIR}/examples/assets"
        MERCURY_GRAPHICS_VULKAN=1
        # Vulkan clip space has a [0, 1] depth range.
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if (WIN32)
    # Prevent <windows.h> from defining min/max macros, which collide with
    # std::numeric_limits<...>::max() inside EnTT's dense_hash_map/_set.
    target_compile_definitions(mercury PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif ()

target_link_libraries(mercury imgui glm::glm joltphysics tinyxml2 glfw assimp
        EnTT::EnTT Vulkan::Vulkan Threads::Threads)

if (MSVC)
    message(STATUS "Using Visual Studio Compiler")
endif ()

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


# CREATE NEW BINARIES USING THIS


add_executable(mercury_test tests/main.cc)
add_executable(pong examples/pong/main.cc
        examples/pong/pong_scene.cc
        examples/pong/pong_scene.h
)
target_link_libraries(pong mercury)


add_executable(example_breakout examples/breakout/main.cc
        examples/breakout/breakout_scene.cc
        examples/breakout/breakout_scene.h)
target_link_libraries(example_breakout mercury)

add_executable(example_space_invaders examples/space_invaders/main.cc
        examples/space_invaders/space_invaders.cc
        examples/space_invaders/space_invaders.h)
target_link_libraries(example_space_invaders mercury)

add_executable(mercury_editor editor/main.cc
        editor/ui_editor/scene.cc)
target_link_libraries(mercury_editor mercury)
target_compile_definitions(mercury_editor PRIVATE MERCURY_EDITOR_ASSET_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets")
