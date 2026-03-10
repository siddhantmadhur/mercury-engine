/**
* window.h
* 
* Kind of a replacement for sokol_app.h
* The main purpose of it is to add multi-window support to the Pequod editor
**/

#ifndef PEQUOD_WINDOW_IMPL_HH_
#define PEQUOD_WINDOW_IMPL_HH_

#include <cstdint>
#include <string>

namespace Pequod {
class Window {
public:
    Window(
        std::string title, 
        uint16_t width, uint16_t height, 
        bool no_depth_buffer=true
    );
    int Start(); 
    void SwapBuffers();
    void OnEvent();
};
}

#endif
