#include "images.hh"
#include <iostream>
#include <debugger/debugger.hh>

Image::Image(std::string pathname) {
    raw_data = stbi_load(
        pathname.c_str(), &x, &y, &channels, 4)  ;
    if (raw_data) {
        PDebug::info("loaded image...");
    } else {
        PDebug::error("could not load image...");
    }
}
