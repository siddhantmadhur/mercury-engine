# A project whose gameplay is C++. Run it with:
#   ./build/release/bin/cpp_hello
# or from the editor: Play opens the target named in cpp_hello.mproj.

mercury_add_game(cpp_hello
        PROJECT "${CMAKE_CURRENT_SOURCE_DIR}")

# The editor loads this module while the project is open. Its target name is
# the project's [build] target ("cpp_hello") plus "_systems".
if (MERCURY_BUILD_EDITOR)
    mercury_add_editor_system(cpp_hello_systems
            PROJECT "${CMAKE_CURRENT_SOURCE_DIR}")
endif ()
