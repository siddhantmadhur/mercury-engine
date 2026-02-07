cmake_minimum_required(VERSION 3.8)

project(MercuryEngine)
include_directories("third_party")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


set(SOURCES
    src/main.cc
    lib/assets/images.cc
)
add_executable(mercury ${SOURCES})

include_directories("lib")
#X11, Xi, Xcursor, GL, dl, pthread, m(?)


# Linux libraries
target_link_libraries(mercury "-lX11")
target_link_libraries(mercury "-lXi")
target_link_libraries(mercury "-lXcursor")
target_link_libraries(mercury "-lGL")
target_link_libraries(mercury "-ldl")
target_link_libraries(mercury "-lpthread")


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

target_link_libraries(mercury imgui)
# MacOS libraries
#target_link_libraries(mercury "-framework Cocoa")
#target_link_libraries(mercury "-framework Metal")
#target_link_libraries(mercury "-framework MetalKit")
#target_link_libraries(mercury "-framework QuartzCore")


