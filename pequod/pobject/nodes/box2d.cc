//
// Created by smadhur on 4/1/26.
//

#include "box2d.h"

#include <properties/mesh.h>

#define SHORT_MAX 32767

namespace Pequod {
    Box2D::Box2D(glm::vec2 position, glm::vec2 size, glm::vec4 color) {
        this->name = "box2d";
        auto pos = Add<Position>();
        pos->Set(glm::vec3(position, 0.0f));

        auto mesh = Add<Mesh>();

        vertex_t raw_vertices[4] = {
            {1.0f, 1.0f, 0.0f, 0, 0, color.r, color.g, color.b, color.a},
            {1.0f, -1.0f, 0.0f, SHORT_MAX, 0, color.r, color.g, color.b, color.a},
            {-1.0f, -1.0f, 0.0f, SHORT_MAX, SHORT_MAX, color.r, color.g, color.b, color.a},
            {-1.0f, 1.0f, 0.0f, 0, SHORT_MAX, color.r, color.g, color.b, color.a},
        };
        mesh->SetVertices(std::vector<vertex_t>(std::begin(raw_vertices), std::end(raw_vertices)));

        uint16_t raw_indices[6] = {0, 1, 3, 1, 2, 3};
        mesh->SetIndicies(std::vector<uint16_t>(std::begin(raw_indices), std::end(raw_indices)));

        mesh->SetScale(glm::vec3(size.x, size.y, 1.0f));
    }
}
