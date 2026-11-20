# A project whose gameplay is C++. Run it with:
#   ./build/release/bin/cpp_hello
# or from the editor: Play opens the target named in cpp_hello.mproj.
#
# Inside the engine checkout the `mercury` target exists already. A project
# built outside it uses an installed SDK:
#   cmake -S . -B build -DMercury_DIR=<sdk>/cmake
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.25)
    project(cpp_hello LANGUAGES C CXX)
endif ()

if (NOT TARGET mercury)
    find_package(Mercury CONFIG REQUIRED)
endif ()

mercury_add_game(cpp_hello
        PROJECT "${CMAKE_CURRENT_SOURCE_DIR}")

# The editor loads this module while the project is open. Its target name is
# the project's [build] target ("cpp_hello") plus "_systems". Only an in-tree
# build with the editor available adds it here; an installed SDK project can
# call mercury_add_editor_system too.
if (TARGET mercury_wx_headers)
    mercury_add_editor_system(cpp_hello_systems
            PROJECT "${CMAKE_CURRENT_SOURCE_DIR}")
endif ()
