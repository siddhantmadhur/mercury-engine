cmake_minimum_required(VERSION 3.30)

project(PequodEngine LANGUAGES CXX C)
# for windows only:
include_directories("C:\\VulkanSDK\\1.4.341.1\\Include")

include_directories("third_party")
include_directories("shaders")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(pequod
        pequod/ecs/ecs.hh
        pequod/ecs/ecs.cc
        pequod/physics_engine/physics_engine.cc
        pequod/physics_engine/physics_engine.h
        pequod/physics_engine/collision_shape.cc
        pequod/physics_engine/collision_shape.h
        pequod/ecs/texture.hh
        pequod/engine/engine.cc
        pequod/engine/world_scene.cc
        pequod/pobject/nodes/box2d.cc
        pequod/pobject/nodes/box2d.h
        pequod/pobject/manager.cc
        pequod/pobject/manager.h
        pequod/pobject/pobject.cc
        pequod/properties/property.cc
        pequod/assets/images.cc
        pequod/debugger/debugger.cc
        pequod/gameobjects/camera.cc
        pequod/properties/mesh.cc
        pequod/properties/position.cc
        pequod/rigidbody/rigidbody.cc
        pequod/serialization/serialization.cc
        pequod/serialization/serialization.h
)


include_directories("pequod")
include_directories("editor")
#X11, Xi, Xcursor, GL, dl, pthread, m(?)

if (APPLE)
# MacOS libraries
  target_link_libraries(pequod "-framework Cocoa")
  target_link_libraries(pequod "-framework Metal")
  target_link_libraries(pequod "-framework MetalKit")
  target_link_libraries(pequod "-framework QuartzCore")
  target_link_libraries(pequod "-framework OpenGL")
elseif (LINUX)
  target_link_libraries(pequod "-lX11")
  target_link_libraries(pequod "-lXi")
  target_link_libraries(pequod "-lXcursor")
  target_link_libraries(pequod "-ldl")
  target_link_libraries(pequod "-lpthread")
  target_link_libraries(pequod "-lm")
  target_link_libraries(pequod "-lvulkan")
  target_link_libraries(pequod "-lGL")
else()
  target_link_libraries(pequod "-ld3d11")
endif()

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

target_link_libraries(pequod imgui glm::glm joltphysics tinyxml2)


file(GLOB_RECURSE EDITOR_SOURCE "editor/*.cc" "editor/main.cc")
if (APPLE)
  set_source_files_properties(editor/main.cc PROPERTIES LANGUAGE OBJCXX)
endif()
add_executable(pequod_editor ${EDITOR_SOURCE}
        editor/main.cc
        "editor/panel/preview_scene.h"
        editor/editor_windows/preview_scene.cc
        editor/panel/object_tree.h
        editor/panel/object_tree.cc
        editor/panel/object_properties.h
        editor/panel/object_properties.cc
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
endif()
target_link_libraries(breakout pequod)

#file(GLOB_RECURSE SPACE_INVADERS_SRC "20-games-challenge/space_invaders/*.cc")
#add_executable(spaceinvaders ${SPACE_INVADERS_SRC})
#target_link_libraries(spaceinvaders pequod)
add_executable(pequod_test tests/main.cc)
add_executable(pong WIN32 20-games-challenge/pong/main.cc
        20-games-challenge/pong/scene.cc
        20-games-challenge/pong/scene.hh)
target_link_libraries(pong pequod)
