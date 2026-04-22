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
        pequod/assets/images.cc
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
        pequod/application/application.h
        pequod/application/application.cc
        pequod/application/d3d11_application.cc
        pequod/os/filesystem.h
        pequod/scene/scene.cc
        pequod/input/input_manager.cc
        pequod/input/input_manager.h
)


include_directories("pequod")
include_directories("editor")
#X11, Xi, Xcursor, GL, dl, pthread, m(?)

if (!WIN32)
    message(WARNING "Pequod does not support this platform")
endif ()

# link imgui

set(IMGUI_SRC "third_party/imgui")
add_library(imgui STATIC
        ${IMGUI_SRC}/imgui.cpp
        ${IMGUI_SRC}/imgui_widgets.cpp
        ${IMGUI_SRC}/imgui_draw.cpp
        ${IMGUI_SRC}/imgui_tables.cpp
        ${IMGUI_SRC}/imgui_demo.cpp
)

target_include_directories(imgui INTERFACE imgui)


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

target_compile_definitions(pequod PRIVATE
        PEQUOD_SHADER_PATH="${CMAKE_SOURCE_DIR}/pequod/shaders"
        PEQUOD_GRAPHICS_D3D11=1
)

target_link_libraries(pequod imgui glm::glm joltphysics tinyxml2 glfw)


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

#file(GLOB_RECURSE PONG_SRC "20-games-challenge/pong/*.cc")
#add_executable(pong ${PONG_SRC})
#target_link_libraries(pong pequod)

file(GLOB_RECURSE BREAKOUT_SRC "20-games-challenge/breakout/*.cc")
add_executable(breakout ${BREAKOUT_SRC})
if (APPLE)
    set_source_files_properties(20-games-challenge/breakout/main.cc PROPERTIES LANGUAGE OBJCXX)
endif ()
target_link_libraries(breakout pequod)

#file(GLOB_RECURSE SPACE_INVADERS_SRC "20-games-challenge/space_invaders/*.cc")
#add_executable(spaceinvaders ${SPACE_INVADERS_SRC})
#target_link_libraries(spaceinvaders pequod)
add_executable(pequod_test tests/main.cc)
add_executable(pong 20-games-challenge/pong/main.cc
        20-games-challenge/pong/pong_scene.cc
        20-games-challenge/pong/pong_scene.h
)
target_link_libraries(pong pequod)


