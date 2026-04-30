cmake_minimum_required(VERSION 3.30)

project(PequodEngine LANGUAGES CXX C)

set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "EditAndContinue")

include_directories("third_party")
include_directories("shaders")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Backend selection. Defaults: D3D11 on Windows, VULKAN elsewhere.
set(PEQUOD_GRAPHICS_BACKEND "" CACHE STRING "Graphics backend: D3D11 or VULKAN")
if (NOT PEQUOD_GRAPHICS_BACKEND)
    if (WIN32)
        set(PEQUOD_GRAPHICS_BACKEND "D3D11")
    else ()
        set(PEQUOD_GRAPHICS_BACKEND "VULKAN")
    endif ()
endif ()
message(STATUS "Pequod graphics backend: ${PEQUOD_GRAPHICS_BACKEND}")

if (PEQUOD_GRAPHICS_BACKEND STREQUAL "VULKAN")
    set(PEQUOD_BACKEND_SOURCE pequod/application/vulkan_application.cc)
    set(IMGUI_BACKEND_SRC third_party/imgui/backends/imgui_impl_vulkan.cpp)
    find_package(Vulkan REQUIRED)
elseif (PEQUOD_GRAPHICS_BACKEND STREQUAL "D3D11")
    set(PEQUOD_BACKEND_SOURCE pequod/application/d3d11_application.cc)
    set(IMGUI_BACKEND_SRC third_party/imgui/backends/imgui_impl_dx11.cpp)
else ()
    message(FATAL_ERROR "Unknown PEQUOD_GRAPHICS_BACKEND: ${PEQUOD_GRAPHICS_BACKEND}")
endif ()

add_library(pequod
        pequod/physics_engine/physics_engine.cc
        pequod/physics_engine/physics_engine.h
        pequod/properties/collision_body.cc
        pequod/properties/collision_body.h
        pequod/pobject/nodes/box2d.cc
        pequod/pobject/nodes/box2d.h
        pequod/pobject/manager.cc
        pequod/pobject/manager.h
        pequod/pobject/pobject.cc
        pequod/properties/property.cc
        pequod/debugger/debugger.h
        pequod/properties/mesh.cc
        pequod/serialization/serialization.cc
        pequod/serialization/serialization.h
        pequod/properties/transform.h
        pequod/properties/transform.cc
        pequod/properties/transform.h
        pequod/physics_engine/shapes/box.cc
        pequod/physics_engine/shapes/box.h
        pequod/physics_engine/shapes/plane.cc
        pequod/physics_engine/shapes/plane.h
        pequod/application/application.h
        pequod/application/application.cc
        ${PEQUOD_BACKEND_SOURCE}
        pequod/os/filesystem.h
        pequod/scene/scene.cc
        pequod/input/input_manager.cc
        pequod/input/input_manager.h
        pequod/properties/texture2d.cc
        pequod/properties/texture2d.h
        pequod/pobject/nodes/camera2d.cc
        pequod/pobject/nodes/camera2d.h
        pequod/pobject/texture_atlas.cc
        pequod/pobject/texture_atlas.h
)


include_directories("pequod")
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
target_link_libraries(imgui glfw)
if (PEQUOD_GRAPHICS_BACKEND STREQUAL "VULKAN")
    target_link_libraries(imgui Vulkan::Vulkan)
endif ()


add_subdirectory(third_party/glm)


include_directories("third_party/glm")


# JOLT PHYSICS

file(GLOB_RECURSE JOLT_SOURCE "third_party/JoltPhysics/Jolt/*.cpp")


add_library(joltphysics STATIC ${JOLT_SOURCE})

include_directories("third_party/JoltPhysics")


# END OF JOLT PHYSICS

# TinyXML2

add_subdirectory(third_party/tinyxml2)
include_directories("third_party/tinyxml2")

# End of TinyXML2

include_directories("third_party/glfw/include")
add_subdirectory("third_party/glfw")

# Live++ :
include_directories("sdk/LivePP/API/x64/")

target_compile_definitions(pequod PUBLIC
        PEQUOD_SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/pequod/shaders"
        PEQUOD_ASSET_PATH="${CMAKE_CURRENT_SOURCE_DIR}/examples/assets"
)

if (PEQUOD_GRAPHICS_BACKEND STREQUAL "D3D11")
    target_compile_definitions(pequod PUBLIC PEQUOD_GRAPHICS_D3D11=1)
elseif (PEQUOD_GRAPHICS_BACKEND STREQUAL "VULKAN")
    target_compile_definitions(pequod PUBLIC PEQUOD_GRAPHICS_VULKAN=1)
endif ()

target_link_libraries(pequod imgui glm::glm joltphysics tinyxml2 glfw)

if (MSVC)
    message(STATUS "Using Visual Studio Compiler")
endif ()

if (PEQUOD_GRAPHICS_BACKEND STREQUAL "D3D11" AND WIN32 AND NOT MSVC)
    # MinGW (and any non-MSVC compiler) ignores `#pragma comment(lib, ...)`,
    # so the D3D11 system libs have to be linked explicitly.
    message(WARNING "Not using MSVC")
    target_link_libraries(pequod d3d11 dxgi d3dcompiler dxguid winmm)
    # The full DirectXMath headers (with `DirectXMath.h` casing the source
    # uses) are shipped as a separate package on mingw — the `directxmath.h`
    # in mingw-w64-headers is only a partial stub.
    find_package(directxmath CONFIG REQUIRED)
    target_link_libraries(pequod Microsoft::DirectXMath)
endif ()

if (PEQUOD_GRAPHICS_BACKEND STREQUAL "VULKAN")
    target_link_libraries(pequod Vulkan::Vulkan)

    # Compile HLSL -> SPIR-V via DXC at build time. -fvk-invert-y is
    # vertex-stage-only (DXC errors on PS), so it is appended per-job.
    find_program(DXC dxc REQUIRED)
    set(SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/pequod/shaders)
    set(SHADER_BIN ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${SHADER_BIN})
    set(DXC_COMMON_FLAGS -spirv
            -fvk-b-shift 0 0    # cbuffer bN -> binding N
            -fvk-t-shift 10 0   # texture tN -> binding N+10
            -fvk-s-shift 20 0)  # sampler sN -> binding N+20
    # job: src|profile|out|extra
    set(SHADER_JOBS
            "main.vs.hlsl|vs_6_0|main.vs.spv|-fvk-invert-y"
            "main.ps.hlsl|ps_6_0|main.ps.spv|"
            "textured.ps.hlsl|ps_6_0|textured.ps.spv|")
    set(SPV_OUTS "")
    foreach (JOB ${SHADER_JOBS})
        string(REPLACE "|" ";" JOB_PARTS "${JOB}")
        list(GET JOB_PARTS 0 SRC)
        list(GET JOB_PARTS 1 PROFILE)
        list(GET JOB_PARTS 2 OUT)
        list(GET JOB_PARTS 3 EXTRA)
        separate_arguments(EXTRA_LIST UNIX_COMMAND "${EXTRA}")
        add_custom_command(
                OUTPUT ${SHADER_BIN}/${OUT}
                COMMAND ${DXC} ${DXC_COMMON_FLAGS} ${EXTRA_LIST}
                -T ${PROFILE} -E Main
                ${SHADER_SRC}/${SRC} -Fo ${SHADER_BIN}/${OUT}
                DEPENDS ${SHADER_SRC}/${SRC}
                COMMENT "DXC ${SRC} -> ${OUT}")
        list(APPEND SPV_OUTS ${SHADER_BIN}/${OUT})
    endforeach ()
    add_custom_target(pequod_shaders DEPENDS ${SPV_OUTS})
    add_dependencies(pequod pequod_shaders)
    target_compile_definitions(pequod PUBLIC PEQUOD_SPIRV_PATH="${SHADER_BIN}")
endif ()


# The editor uses Sokol with a hardcoded D3D11 backend; only build it on
# Windows for now.
if (WIN32)
    file(GLOB_RECURSE EDITOR_SOURCE "editor/*.cc" "editor/main.cc")
    if (APPLE)
        set_source_files_properties(editor/main.cc PROPERTIES LANGUAGE OBJCXX)
    endif ()

    add_executable(pequod_editor ${EDITOR_SOURCE}
            editor/main.cc
            "editor/panel/preview_scene.h"
            editor/editor_windows/preview_scene.cc
            editor/panel/object_tree.h
            editor/panel/object_tree.cc
            editor/panel/object_properties.h
            editor/panel/object_properties.cc
            pequod/globals.h
            pequod/scene/scene.h
    )
    target_link_libraries(pequod_editor pequod)
endif ()

# CREATE NEW BINARIES USING THIS


add_executable(pequod_test tests/main.cc)
add_executable(pong examples/pong/main.cc
        examples/pong/pong_scene.cc
        examples/pong/pong_scene.h
)
target_link_libraries(pong pequod)


add_executable(example_breakout examples/breakout/main.cc
        examples/breakout/breakout_scene.cc
        examples/breakout/breakout_scene.h)
target_link_libraries(example_breakout pequod)

add_executable(example_space_invaders examples/space_invaders/main.cc
        examples/space_invaders/space_invaders.cc
        examples/space_invaders/space_invaders.h)
target_link_libraries(example_space_invaders pequod)
