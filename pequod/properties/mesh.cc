#include "mesh.h"

namespace Pequod {
Mesh::Mesh() {
  this->vertices = {};
  this->indices = {};
  this->scale = glm::vec3(1.0f);
}

Mesh::~Mesh() {}

void Mesh::SetVertices(std::vector<vertex_t> vertices) {
  this->vertices = vertices;
}

void Mesh::SetIndicies(std::vector<uint16_t> indices) {
  this->indices = indices;
}

void Mesh::SetScale(glm::vec3 scale) { this->scale = scale; }

glm::vec3 Mesh::GetScale() { return scale; }

uint32_t Mesh::GetIndicesID() { return indices_id; }

const std::vector<uint16_t> Mesh::GetIndices() { return indices; }
}  // namespace Pequod
