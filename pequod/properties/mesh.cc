#include "mesh.h"

#include <utility>

#include "debugger/debugger.h"

namespace Pequod {
Mesh::Mesh() {
  this->vertices_ = {};
  this->indices_ = {};
}

Mesh::~Mesh() {}

void Mesh::SetVertices(std::vector<Vertex> vertices) {
  this->vertices_ = std::move(vertices);
}

std::vector<Vertex> Mesh::GetVertices() const { return this->vertices_; }

void Mesh::SetIndices(const std::vector<UINT> &indices) {
  this->indices_ = indices;
}

uint32_t Mesh::GetIndicesID() const { return indices_id; }
void Mesh::SetAABB(glm::vec3 min, glm::vec3 max) {
  this->aabb_.min = min;
  this->aabb_.max = max;
}

mAABB Mesh::GetAABB() { return this->aabb_; }

float Mesh::GetHeight() {
  float local_height = (aabb_.max.y - aabb_.min.y);
  return local_height;
}

std::vector<UINT> Mesh::GetIndices() const { return indices_; }
}  // namespace Pequod
