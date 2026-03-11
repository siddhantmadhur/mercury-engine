cmake_minimum_required(VERSION 3.30)

project(PequodEngine)
include_directories("third_party")
include_directories("shaders")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE ENGINE_SRC "pequod/*.cc")

add_library(pequod ${ENGINE_SRC})


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
else()
  target_link_libraries(pequod "-lX11")
  target_link_libraries(pequod "-lXi")
  target_link_libraries(pequod "-lXcursor")
  target_link_libraries(pequod "-ldl")
  target_link_libraries(pequod "-lpthread")
  target_link_libraries(pequod "-lm")
  target_link_libraries(pequod "-lvulkan")
  target_link_libraries(pequod "-lGL")
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


file(GLOB_RECURSE EDITOR_SOURCE "editor/*.cc")
add_executable(pequod_editor ${EDITOR_SOURCE})
target_link_libraries(pequod_editor pequod)

# CREATE NEW BINARIES USING THIS

#file(GLOB_RECURSE PONG_SRC "20-games-challenge/pong/*.cc")
#add_executable(pong ${PONG_SRC})
#target_link_libraries(pong pequod)

#file(GLOB_RECURSE BREAKOUT_SRC "20-games-challenge/breakout/*.cc")
#add_executable(breakout ${BREAKOUT_SRC})
#target_link_libraries(breakout pequod)

#file(GLOB_RECURSE SPACE_INVADERS_SRC "20-games-challenge/space_invaders/*.cc")
#add_executable(spaceinvaders ${SPACE_INVADERS_SRC})
#target_link_libraries(spaceinvaders pequod)
