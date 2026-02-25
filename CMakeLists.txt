cmake_minimum_required(VERSION 3.8)


project(MercuryEngine)
include_directories("third_party")
include_directories("shaders")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE ENGINE_SRC "lib/*.cc")

#message(STATUS "SOURCES: ${LIB_SRC}")
#set(LIB_SRC
#    lib/assets/images.cc
#    lib/gameobjects/camera.cc
#    lib/engine/engine.cc
#)

file(GLOB_RECURSE GAME_SRC "src/*.cc")

#set(SOURCES
#    src/main.cc
#    src/scenes/demo_scene.cc
#)

add_executable(mercury ${ENGINE_SRC} ${GAME_SRC} )


include_directories("lib")
#X11, Xi, Xcursor, GL, dl, pthread, m(?)

if (APPLE)
# MacOS libraries
  target_link_libraries(mercury "-framework Cocoa")
  target_link_libraries(mercury "-framework Metal")
  target_link_libraries(mercury "-framework MetalKit")
  target_link_libraries(mercury "-framework QuartzCore")
  target_link_libraries(mercury "-framework OpenGL")
else()
  target_link_libraries(mercury "-lX11")
  target_link_libraries(mercury "-lXi")
  target_link_libraries(mercury "-lXcursor")
  target_link_libraries(mercury "-lGL")
  target_link_libraries(mercury "-ldl")
  target_link_libraries(mercury "-lpthread")
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

target_link_libraries(mercury imgui glm::glm)




