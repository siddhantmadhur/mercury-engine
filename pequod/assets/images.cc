#include "images.hh"
#include <iostream>
#include <debugger/debugger.hh>

namespace Pequod {
    Image::Image(std::string pathname) {
        raw_data = stbi_load(
            pathname.c_str(), &x, &y, &channels, 4);
        if (raw_data) {
            PDebug::info("Loaded image...");
        } else {
            PDebug::error("Could not load image...");
        }
    }
}
