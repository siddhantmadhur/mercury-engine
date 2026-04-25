//
// Created by smadhur on 4/1/26.
//

#include "box2d.h"

#include <properties/mesh.h>

#include "properties/transform.h"

#define SHORT_MAX 32767

namespace Pequod {
Box2D::Box2D(glm::vec2 position, glm::vec2 size, glm::vec4 color) {
  this->name = "box2d";
  auto pos = Add<Transform>();
  pos->SetPosition(glm::vec3(position, 0.0f));

  auto mesh = Add<Mesh>();
  PQ_FLOAT3 dx_color(color.r, color.g, color.b);
  Vertex raw_vertices[4] = {
      {PQ_FLOAT3{1.0f, 1.0f, 0.0f},   dx_color, PQ_FLOAT2{1.0f, 0.0f}},
      {PQ_FLOAT3{1.0f, -1.0f, 0.0f},  dx_color, PQ_FLOAT2{1.0f, 1.0f}},
      {PQ_FLOAT3{-1.0f, -1.0f, 0.0f}, dx_color, PQ_FLOAT2{0.0f, 1.0f}},
      {PQ_FLOAT3{-1.0f, 1.0f, 0.0f},  dx_color, PQ_FLOAT2{0.0f, 0.0f}},
  };
  mesh->SetVertices(
      std::vector<Vertex>(std::begin(raw_vertices), std::end(raw_vertices)));

  uint16_t raw_indices[6] = {0, 1, 3, 1, 2, 3};
  mesh->SetIndices(
      std::vector<UINT>(std::begin(raw_indices), std::end(raw_indices)));

  mesh->SetScale(glm::vec3(size.x, size.y, 1.0f));
}
}  // namespace Pequod
