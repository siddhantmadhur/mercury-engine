#include "assets/images.hh"
#include <resources/spritemap.h>

#include <debugger/debugger.hh>

SpriteMap::SpriteMap(std::string path, int unit_size) {
    raw_data = stbi_load(
        path.c_str(), 
        &x, 
        &y, 
        &channels, 
        4
    );
    if (raw_data) {
        PDebug::info("loaded image...");
    } else {
        PDebug::error("could not load image...");
    }
}
