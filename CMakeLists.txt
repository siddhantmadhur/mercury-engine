cmake_minimum_required(VERSION 3.30)

project(PequodEngine )

set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "EditAndContinue")

# for windows only:
include_directories("C:\\VulkanSDK\\1.4.341.1\\Include")

include_directories("third_party")
include_directories("third_party/glfw/include")
include_directories("shaders")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS_DEBUG "/MDd /ZI /Ob0 /Od /RTC1")

add_library(pequod
        pequod/application/application.cc
        pequod/application/application.h
        pequod/debugger/debugger.cc
        pequod/debugger/debugger.hh
        pequod/application/d3d11_application.cc
        pequod/application/d3d11_application.h
        pequod/renderer/renderer.cc
        pequod/renderer/renderer.h
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

#glfw
add_subdirectory(third_party/glfw)

target_link_libraries(pequod imgui glm::glm joltphysics tinyxml2 glfw)


if (APPLE)
  set_source_files_properties(editor/main.cc PROPERTIES LANGUAGE OBJCXX)
endif()
add_executable(pequod_editor
        editor/main.cc
)
target_link_libraries(pequod_editor pequod)
set_target_properties(pequod PROPERTIES LINK_FLAGS "/INCREMENTAL")
set_target_properties(pequod_editor PROPERTIES LINK_FLAGS "/INCREMENTAL")

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
add_executable(pong 20-games-challenge/pong/main.cc
        20-games-challenge/pong/scene.cc
        20-games-challenge/pong/scene.hh)
target_link_libraries(pong pequod)
