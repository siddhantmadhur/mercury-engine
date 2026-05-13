#include "mesh.h"

#include <utility>

#include "debugger/debugger.h"

namespace Pequod {
Mesh::Mesh() {
  this->vertices_ = {};
  this->indices_ = {};
  this->scale = glm::vec3(1.0f);
}

Mesh::~Mesh() {}

void Mesh::SetVertices(std::vector<Vertex> vertices) {
  float lowest = 0.0;
  float highest = 0.0;
  for (auto &vertex : vertices) {
    PDebug::log("POS: {}", vertex.position.y);
    if (vertex.position.y < lowest) {
      lowest = vertex.position.y;
    }
    if (vertex.position.y > highest) {
      highest = vertex.position.y;
    }
  }
  this->height_ = highest - lowest;

  this->vertices_ = std::move(vertices);
}

std::vector<Vertex> Mesh::GetVertices() const { return this->vertices_; }

void Mesh::SetIndices(const std::vector<UINT> &indices) {
  this->indices_ = indices;
}

void Mesh::SetScale(glm::vec3 scale) { this->scale = scale; }

glm::vec3 Mesh::GetScale() const { return scale; }

uint32_t Mesh::GetIndicesID() const { return indices_id; }

float Mesh::GetHeight() { return this->height_ * this->scale.y; }

std::vector<UINT> Mesh::GetIndices() const { return indices_; }
}  // namespace Pequod
