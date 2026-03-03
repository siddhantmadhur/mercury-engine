#include "cube.hh"

#define WHITE 1.0f, 0.0f, 0.0f, 1.0f
/**
Shapes::Cube::Cube(float scale, bool use_texture) {
     #define SHORT_MAX 32767
    // #define SHORT_MAX 5
    float use_texture_f = (use_texture ? 1.0f : 0.0f);


    const vertex_t raw_vertices[24] = {
        { -scale, -scale, -scale,           0,          0, WHITE },
        {  scale, -scale, -scale,   SHORT_MAX,          0, WHITE },
        {  scale,  scale, -scale,   SHORT_MAX,  SHORT_MAX, WHITE },
        { -scale,  scale, -scale,           0,  SHORT_MAX, WHITE },

        { -scale, -scale,  scale,           0,          0, WHITE },
        {  scale, -scale,  scale,   SHORT_MAX,          0, WHITE },
        {  scale,  scale,  scale,   SHORT_MAX,  SHORT_MAX, WHITE },
        { -scale,  scale,  scale,           0,  SHORT_MAX, WHITE },

        { -scale, -scale, -scale,           0,          0, WHITE },
        { -scale,  scale, -scale,   SHORT_MAX,          0, WHITE },
        { -scale,  scale,  scale,   SHORT_MAX,  SHORT_MAX, WHITE },
        { -scale, -scale,  scale,           0,  SHORT_MAX, WHITE },

        {  scale, -scale, -scale,           0,          0, WHITE },
        {  scale,  scale, -scale,   SHORT_MAX,          0, WHITE },
        {  scale,  scale,  scale,   SHORT_MAX,  SHORT_MAX, WHITE },
        {  scale, -scale,  scale,           0,  SHORT_MAX, WHITE },

        { -scale, -scale, -scale,           0,          0, WHITE },
        { -scale, -scale,  scale,   SHORT_MAX,          0, WHITE },
        {  scale, -scale,  scale,   SHORT_MAX,  SHORT_MAX, WHITE },
        {  scale, -scale, -scale,           0,  SHORT_MAX, WHITE },

        { -scale,  scale, -scale,           0,          0, WHITE },
        { -scale,  scale,  scale,   SHORT_MAX,          0, WHITE },
        {  scale,  scale,  scale,   SHORT_MAX,  SHORT_MAX, WHITE },
        {  scale,  scale, -scale,           0,  SHORT_MAX, WHITE },

    };

    vertices = std::vector<vertex_t>(std::begin(raw_vertices), std::end(raw_vertices));

    uint16_t raw_indices[36] = {
        0, 1, 2,  0, 2, 3,
        6, 5, 4,  7, 6, 4,
        8, 9, 10,  8, 10, 11,
        14, 13, 12,  15, 14, 12,
        16, 17, 18,  16, 18, 19,
        22, 21, 20,  23, 22, 20
    };

    this->indices = std::vector<uint16_t>(std::begin(raw_indices), std::end(raw_indices));
}
**/
