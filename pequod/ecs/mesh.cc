#include <ecs/mesh.hh>

Mesh::Mesh(std::vector<vertex_t> init_vertices, std::vector<uint16_t> init_indices, glm::vec3 init_scale) {
    this->vertices = init_vertices;
    this->indices = init_indices;
    this->scale = init_scale;
}
Mesh::~Mesh() {}
