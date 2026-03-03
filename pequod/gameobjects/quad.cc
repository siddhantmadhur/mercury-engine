#include <gameobjects/quad.hh>

#define SHORT_MAX 32767
#define WHITE(c) 1.0f, 1.0f, 1.0f, 1.0f
#define SPREAD_COLOR(c) c.x, c.y, c.z, c.a

Quad::Quad(glm::vec2 position, glm::vec2 size, glm::vec4 color) {

    // #define SHORT_MAX 5
    vertex_t raw_vertices[4] = {
        {   size.x / 2.0f,  size.y / 2.0f, 0.0f,         0,         0, SPREAD_COLOR(color) }, // top right
        {   size.x / 2.0f, -size.y / 2.0f, 0.0f, SHORT_MAX,         0, SPREAD_COLOR(color) }, // bottom right
        {  -size.x / 2.0f, -size.y / 2.0f, 0.0f, SHORT_MAX, SHORT_MAX, SPREAD_COLOR(color) }, // bottom left
        {  -size.x / 2.0f,  size.y / 2.0f, 0.0f,         0, SHORT_MAX, SPREAD_COLOR(color) }, // top left
    };

    std::vector<vertex_t> vertices = std::vector<vertex_t>(std::begin(raw_vertices), std::end(raw_vertices));

    uint16_t raw_indices[6] = {
        0, 1, 3,
        1, 2, 3
    };

    std::vector<uint16_t> indices = std::vector<uint16_t>(std::begin(raw_indices), std::end(raw_indices));

    this->size = size;
    this->position = new Position(glm::vec3(position.x, position.y, 0));
    this->mesh = new Mesh(vertices, indices);
}

