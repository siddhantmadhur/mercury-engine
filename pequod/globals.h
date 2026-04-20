//
// Created by smadhur on 4/10/2026.
//

#ifndef PEQUODENGINE_GLOBALS_H
#define PEQUODENGINE_GLOBALS_H

#include <DirectXMath.h>
#include <intsafe.h>

#include <cstdint>
#include <vector>

using entity_id = uint16_t;
#define height_s (sapp_heightf() * (1.0f / ZOOM))
#define width_s (sapp_widthf() * (1.0f / ZOOM))

#if PEQUOD_GRAPHICS_D3D11
#define PQ_FLOAT3 DirectX::XMFLOAT3
#endif

using Position = PQ_FLOAT3;
using Color = PQ_FLOAT3;

struct Vertex {
  Position position;
  Color color;
};

/**
 * @brief Describes how a singular object can be drawn
 *
 * Primitives are the minimum basic requirement for a renderer to draw
 * something on the screen. It will thus contain things like vertices,
 * indices, textures, uv's etc.
 */
struct Primitive {
  std::vector<Vertex> vertices;
  std::vector<UINT> indices;
};

#endif  // PEQUODENGINE_GLOBALS_H
