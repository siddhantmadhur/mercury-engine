#include <gameobjects/quad.hh>

#define SHORT_MAX 32767
#define WHITE 1.0f, 1.0f, 1.0f, 1.0f

Shapes::Quad::Quad(glm::vec2 size, glm::vec2 position, glm::vec4 color) {

    // #define SHORT_MAX 5
    float use_texture_f = (isTexture() ? 1.0f : 0.0f);


    vertex_t raw_vertices[4] = {
        { (size.x), (size.y), 0.0f,           0,          0, WHITE, use_texture_f },
        {  (size.x),-(size.y), 0.0f,   SHORT_MAX,          0, WHITE, use_texture_f },
        {  -size.x,  -size.y, 0.0f,   SHORT_MAX,  SHORT_MAX, WHITE, use_texture_f },
        { -size.x,  size.y, 0.0f,           0,  SHORT_MAX, WHITE, use_texture_f },
    };

    for (int i = 0; i < 4; i++) {
        raw_vertices[i].x += position.x;
        raw_vertices[i].y += position.y;
    }

    vertices = std::vector<vertex_t>(std::begin(raw_vertices), std::end(raw_vertices));


    uint16_t raw_indices[] = {
        0, 1, 3,
        1, 2, 3
    };

    indices = std::vector<uint16_t>(std::begin(raw_indices), std::end(raw_indices));
}
