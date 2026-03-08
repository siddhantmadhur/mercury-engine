#ifndef PEQUOD_SPRITEMAP_IMPL_HH_
#define PEQUOD_SPRITEMAP_IMPL_HH_

#include <string>
#include <stb/stb_image.h>
#include <sokol/sokol_gfx.h>

class SpriteMap {
public:
    SpriteMap(std::string, int unit_size);
    sg_image GetSokolImage();
private:
    stbi_uc* raw_data;
    int x, y, channels;
};

#endif
