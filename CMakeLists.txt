cmake_minimum_required(VERSION 3.30)

project(PequodEngine LANGUAGES CXX C)
# for windows only:

set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "EditAndContinue")

include_directories("third_party")
include_directories("shaders")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
        pequod/gameobjects/camera.cc
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
        pequod/application/d3d11_application.cc
        pequod/os/filesystem.h
        pequod/scene/scene.cc
        pequod/input/input_manager.cc
        pequod/input/input_manager.h
        pequod/properties/texture2d.cc
        pequod/properties/texture2d.h
)


include_directories("pequod")
include_directories("editor")
#X11, Xi, Xcursor, GL, dl, pthread, m(?)

if (NOT WIN32)
    message(WARNING "Pequod does not support this platform")
endif ()

# link imgui


file(GLOB IMGUI_SOURCES
        "third_party/imgui/backends/imgui_impl_dx11.cpp"
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
        PEQUOD_GRAPHICS_D3D11=1
)

target_link_libraries(pequod imgui glm::glm joltphysics tinyxml2 glfw)

# MinGW (and any non-MSVC compiler) ignores `#pragma comment(lib, ...)`,
# so the D3D11 system libs have to be linked explicitly.
if (WIN32 AND NOT MSVC)
    target_link_libraries(pequod d3d11 dxgi d3dcompiler dxguid winmm)
    # The full DirectXMath headers (with `DirectXMath.h` casing the source
    # uses) are shipped as a separate package on mingw — the `directxmath.h`
    # in mingw-w64-headers is only a partial stub.
    find_package(directxmath CONFIG REQUIRED)
    target_link_libraries(pequod Microsoft::DirectXMath)
endif ()


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
