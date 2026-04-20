#include "mesh.h"

#include <utility>

namespace Pequod {
Mesh::Mesh() {
  this->vertices_ = {};
  this->indices_ = {};
  this->scale = glm::vec3(1.0f);
}

Mesh::~Mesh() {}

void Mesh::SetVertices(std::vector<Vertex> vertices) {
  this->vertices_ = std::move(vertices);
}
std::vector<Vertex> Mesh::GetVertices() const { return this->vertices_; }

void Mesh::SetIndices(const std::vector<UINT> &indices) {
  this->indices_ = indices;
}

void Mesh::SetScale(glm::vec3 scale) { this->scale = scale; }

glm::vec3 Mesh::GetScale() const { return scale; }

uint32_t Mesh::GetIndicesID() const { return indices_id; }

std::vector<UINT> Mesh::GetIndices() const { return indices_; }
}  // namespace Pequod
