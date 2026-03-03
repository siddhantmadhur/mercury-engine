#include <ecs/mesh.hh>

Mesh::Mesh(std::vector<vertex_t> init_vertices, std::vector<uint16_t> init_indices) {
    this->vertices = init_vertices;
    this->indices = init_indices;
}
Mesh::~Mesh() {}
