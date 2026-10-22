//
// Created by smadhur on 4/10/2026.
//

#ifndef MERCURY_ENGINE_VERSION
#define MERCURY_ENGINE_VERSION "dev"
#include "entt/entt.hpp"
#endif

#ifndef MERCURYENGINE_GLOBALS_H
#define MERCURYENGINE_GLOBALS_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>
#include <vector>
#include <entt/entt.hpp>
#include <memory>

using entity_id = uint16_t;
using kEntityId = entt::entity;

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

using PQ_FLOAT2 = glm::vec2;
using PQ_FLOAT3 = glm::vec3;
using PQ_FLOAT4 = glm::vec4;
using PQ_MATRIX = glm::mat4;
using UINT = uint32_t;

struct Vertex {
  PQ_FLOAT3 position;
  PQ_FLOAT3 color;
  PQ_FLOAT2 uv;
  // Zero means unlit: sprites and 2D shapes ignore scene lighting.
  PQ_FLOAT3 normal = PQ_FLOAT3(0.0f);
};

struct StaticVertex {
  PQ_FLOAT3 position;
  PQ_FLOAT4 color;
  PQ_FLOAT2 uv;
  PQ_FLOAT4 atlas_uv;
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
  PQ_MATRIX world_;
  PQ_FLOAT4 atlas_uv;
  PQ_FLOAT4 material_params;  // x: roughness, y: metallic, zw: texture tiling
};

// The Vulkan vertex input descriptions and the HLSL vertex shaders depend on
// these exact layouts.
static_assert(sizeof(Vertex) == 44);
static_assert(offsetof(Vertex, position) == 0);
static_assert(offsetof(Vertex, color) == 12);
static_assert(offsetof(Vertex, uv) == 24);
static_assert(offsetof(Vertex, normal) == 32);

static_assert(sizeof(StaticVertex) == 52);
static_assert(offsetof(StaticVertex, position) == 0);
static_assert(offsetof(StaticVertex, color) == 12);
static_assert(offsetof(StaticVertex, uv) == 28);
static_assert(offsetof(StaticVertex, atlas_uv) == 36);

static_assert(sizeof(VsModelBuffer) == 112);
static_assert(offsetof(VsModelBuffer, scale) == 0);
static_assert(offsetof(VsModelBuffer, opacity) == 12);
static_assert(offsetof(VsModelBuffer, world_) == 16);
static_assert(offsetof(VsModelBuffer, atlas_uv) == 80);
static_assert(offsetof(VsModelBuffer, material_params) == 96);

#define MERCURY_SAFE_FREE(pointer) \
  {                               \
    delete pointer;               \
    pointer = nullptr;            \
  }
#endif  // MERCURYENGINE_GLOBALS_H
