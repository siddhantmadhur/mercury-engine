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

constexpr float kShowDebugStats = true;
constexpr int kTicksPerSec = 60;
constexpr double kTickMs = 1000.0 / kTicksPerSec;

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
  PQ_FLOAT3 color;
  PQ_FLOAT2 uv;
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
};

#define PEQUOD_SAFE_FREE(pointer) \
  {                               \
    delete pointer;               \
    pointer = nullptr;            \
  }
#endif  // PEQUODENGINE_GLOBALS_H
