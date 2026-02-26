#include "cube.hh"

Shapes::Cube::Cube() {
    #define SHORT_MAX 32767
    const vertex_t raw_vertices[24] = {
        { -1.0f, -1.0f, -1.0f,      0,     0 },
        {  1.0f, -1.0f, -1.0f,  SHORT_MAX,     0 },
        {  1.0f,  1.0f, -1.0f,  SHORT_MAX, SHORT_MAX },
        { -1.0f,  1.0f, -1.0f,      0, SHORT_MAX },

        { -1.0f, -1.0f,  1.0f,      0,     0 },
        {  1.0f, -1.0f,  1.0f,  SHORT_MAX,     0 },
        {  1.0f,  1.0f,  1.0f,  SHORT_MAX, SHORT_MAX },
        { -1.0f,  1.0f,  1.0f,      0, SHORT_MAX },

        { -1.0f, -1.0f, -1.0f,      0,     0 },
        { -1.0f,  1.0f, -1.0f,  SHORT_MAX,     0 },
        { -1.0f,  1.0f,  1.0f,  SHORT_MAX, SHORT_MAX },
        { -1.0f, -1.0f,  1.0f,      0, SHORT_MAX },

        {  1.0f, -1.0f, -1.0f,      0,     0 },
        {  1.0f,  1.0f, -1.0f,  SHORT_MAX,     0 },
        {  1.0f,  1.0f,  1.0f,  SHORT_MAX, SHORT_MAX },
        {  1.0f, -1.0f,  1.0f,      0, SHORT_MAX },

        { -1.0f, -1.0f, -1.0f,      0,     0 },
        { -1.0f, -1.0f,  1.0f,  SHORT_MAX,     0 },
        {  1.0f, -1.0f,  1.0f,  SHORT_MAX, SHORT_MAX },
        {  1.0f, -1.0f, -1.0f,      0, SHORT_MAX },

        { -1.0f,  1.0f, -1.0f,      0,     0 },
        { -1.0f,  1.0f,  1.0f,  SHORT_MAX,     0 },
        {  1.0f,  1.0f,  1.0f,  SHORT_MAX, SHORT_MAX },
        {  1.0f,  1.0f, -1.0f,      0, SHORT_MAX },

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
