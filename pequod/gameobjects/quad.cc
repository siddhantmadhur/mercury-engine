#include <gameobjects/quad.hh>

#define SHORT_MAX 32767
#define WHITE 1.0f, 1.0f, 1.0f, 1.0f
#define SPREAD_COLOR(c) c.x, c.y, c.z, c.a

Shapes::Quad::Quad(glm::vec2 size, glm::vec2 position, glm::vec4 color) {

    // #define SHORT_MAX 5
    float use_texture_f = (isTexture() ? 1.0f : 0.0f);


    vertex_t raw_vertices[4] = {
        {   size.x,  size.y, 0.0f,         0,         0, SPREAD_COLOR(color) },
        {   size.x, -size.y, 0.0f, SHORT_MAX,         0, SPREAD_COLOR(color) },
        {  -size.x, -size.y, 0.0f, SHORT_MAX, SHORT_MAX, SPREAD_COLOR(color) },
        {  -size.x,  size.y, 0.0f,         0, SHORT_MAX, SPREAD_COLOR(color) },
    };

    vertices = std::vector<vertex_t>(std::begin(raw_vertices), std::end(raw_vertices));

    this->SetPosition(glm::vec3(position.x, position.y, 0.0f));

    uint16_t raw_indices[] = {
        0, 1, 3,
        1, 2, 3
    };

    indices = std::vector<uint16_t>(std::begin(raw_indices), std::end(raw_indices));

    this->size = size;
}

glm::vec2 Shapes::Quad::GetSize() {
  return this->size;
}
