//
// Created by smadhur on 4/10/2026.
//

#ifndef PEQUOD_ENGINE_VERSION
#define PEQUOD_ENGINE_VERSION "dev"
#include "entt/entt.hpp"
#endif

#ifndef PEQUODENGINE_GLOBALS_H
#define PEQUODENGINE_GLOBALS_H

#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>
#include <vector>
#include <entt/entt.hpp>
#include <memory>

#ifdef PEQUOD_GRAPHICS_D3D11
#include <DirectXMath.h>
#include <intsafe.h>
#endif

using entity_id = uint16_t;
using kEntityId = entt::entity;
#define height_s (sapp_heightf() * (1.0f / ZOOM))
#define width_s (sapp_widthf() * (1.0f / ZOOM))

// The reason I'm wrapping basic memory functions so I can modify memory
// management later without user code needing to be changed

template <typename T>
using UPtr = std::unique_ptr<T>;

template <typename T>
using SPtr = std::shared_ptr<T>;

/*
template <typename T>
using MakeUPtr = std::make_unique<T>;

template <typename T>
using MakeUPtr = std::make_shared<T>;
*/
// Wrapper around math structs so every platform uses the appropriate equivalent

#ifdef PEQUOD_GRAPHICS_D3D11
using PQ_FLOAT2 = DirectX::XMFLOAT2;
using PQ_FLOAT3 = DirectX::XMFLOAT3;
using PQ_FLOAT4 = DirectX::XMFLOAT4;
using PQ_MATRIX = DirectX::XMFLOAT4X4;
#else
using PQ_FLOAT2 = glm::vec2;
using PQ_FLOAT3 = glm::vec3;
using PQ_FLOAT4 = glm::vec4;
using PQ_MATRIX = glm::mat4;
using UINT = uint32_t;
#endif

struct Vertex {
  PQ_FLOAT3 position;
  PQ_FLOAT3 normal;
  PQ_FLOAT3 color;
  PQ_FLOAT2 uv;
};

struct StaticVertex {
  PQ_FLOAT3 position;
  PQ_FLOAT3 normal;
  PQ_FLOAT4 color;
  PQ_FLOAT2 uv;
  PQ_FLOAT4 atlas_uv;
};

/**
 * @brief Scalar PBR material parameters
 *
 * No texture maps yet. Defaults give a plain matte white surface so
 * entities without an explicit material still render.
 */
struct Material {
  glm::vec3 albedo = glm::vec3(1.0f);
  float metallic = 0.0f;
  float roughness = 0.7f;
  float ao = 1.0f;
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
  glm::vec3 world_rotation_ = glm::vec3(0.0f);
  float opacity_ = 1.0f;
  glm::vec4 atlas_uv_ = glm::vec4(0.0f);
  Material material_ = {};
};

/**
 * @brief Contains per-object information required to render
 *
 * These can be changing often and provide per-object required to render
 * correctly.
 */
struct VsModelBuffer {
  PQ_FLOAT3 scale;
  float opacity;
  PQ_FLOAT3 object_position;
  float pad0_;
  PQ_FLOAT3 object_rotation;
  float pad1_;
  PQ_FLOAT4 atlas_uv;
};

/**
 * @brief Per-frame lighting data shared by every pixel-shader invocation.
 *
 * Layout matches the HLSL `PS_LIGHT_BUFFER` cbuffer (16-byte aligned).
 * Slots for additional point/spot lights are intentionally left open as
 * future extensions — extend via additional fields here and bump the
 * HLSL counterpart together.
 */
struct PsLightCBuffer {
  PQ_FLOAT3 sun_direction;
  float sun_intensity;
  PQ_FLOAT3 sun_color;
  float _pad0;
  PQ_FLOAT3 ambient;
  float _pad1;
  PQ_FLOAT3 camera_world_pos;
  float _pad2;
  PQ_MATRIX light_view_proj;
  PQ_FLOAT4 shadow_params;  // x=texel_size y=bias z=enabled w=reserved
};

/**
 * @brief Per-object PBR material parameters uploaded to the pixel shader.
 */
struct PsMaterialCBuffer {
  PQ_FLOAT3 albedo;
  float metallic;
  float roughness;
  float ao;
  PQ_FLOAT2 _pad;
};

/**
 * @brief Shadow pass per-frame VS buffer: just the light view-projection.
 */
struct VsLightCBuffer {
  PQ_MATRIX light_view_proj;
};

#define PEQUOD_SAFE_FREE(pointer) \
  {                               \
    delete pointer;               \
    pointer = nullptr;            \
  }
#endif  // PEQUODENGINE_GLOBALS_H
