//
// Created by smadhur on 4/10/2026.
//

#ifndef PEQUODENGINE_GLOBALS_H
#define PEQUODENGINE_GLOBALS_H

#include <DirectXMath.h>
#include <intsafe.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

using entity_id = uint16_t;
#define height_s (sapp_heightf() * (1.0f / ZOOM))
#define width_s (sapp_widthf() * (1.0f / ZOOM))
using PQ_FLOAT3 = DirectX::XMFLOAT3;
using PQ_MATRIX = DirectX::XMFLOAT4X4;

struct Vertex {
  PQ_FLOAT3 position;
  PQ_FLOAT3 color;
};

/**
 * @brief Describes how a singular object can be drawn
 *
 * Primitives are the minimum basic requirement for a renderer to draw
 * something on the screen. It will thus contain things like vertices,
 * indices, textures, uv's etc.
 */
struct Primitive {
  std::vector<Vertex> vertices_;
  std::vector<UINT> indices_;
  glm::vec3 scale_;
  glm::vec3 world_position_;
};

// Handles information on a per-model basis
struct VsModelBuffer {
  PQ_FLOAT3 scale;
  float opacity;
  PQ_MATRIX world_position;
};
#endif  // PEQUODENGINE_GLOBALS_H
