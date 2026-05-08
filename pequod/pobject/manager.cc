//
// Created by smadhur on 4/1/26.
//

#include "manager.h"

#include "debugger/debugger.h"
#include "nodes/box2d.h"
#include "properties/mesh.h"
#include "properties/texture2d.h"
#include "properties/transform.h"

// [CLAUDE] TODO: RUN_ON_ALL_NODES macro is defined but never called — remove
#define RUN_ON_ALL_NODES(func) \
  {                            \
    func(Box2D);               \
  }

namespace Pequod {
PObjectManager::PObjectManager() = default;
std::vector<Primitive> PObjectManager::GetPrimitives() {
  std::vector<Primitive> primitives;
  for (const auto& object : objects) {
    if (object == nullptr) {
      continue;
    }
    if (auto mesh = object->Get<Mesh>()) {
      Primitive primitive = {};
      primitive.indices_ = mesh->GetIndices();
      primitive.vertices_ = mesh->GetVertices();
      primitive.scale_ = mesh->GetScale();
      primitive.opacity_ = mesh->opacity_;
      if (auto transform = object->Get<Transform>()) {
        auto world_position = transform->GetPosition();
        primitive.world_position_ = world_position;
      } else {
        primitive.world_position_ = glm::vec3(1.0f);
      }

      auto tex = object->Get<Texture2D>();
      if (tex) {
        atlas_.AddTexture(tex);
      }
      primitives.push_back(primitive);
    }
  }
  atlas_.UpdateAtlas();
  // Populate atlas UVs after the atlas has been (re)packed so each primitive
  // sees its current sub-rect.
  size_t pi = 0;
  for (const auto& object : objects) {
    if (object == nullptr) continue;
    if (!object->Get<Mesh>()) continue;
    if (auto tex = object->Get<Texture2D>()) {
      primitives[pi].atlas_uv_ = tex->GetAtlasUV();
    } else {
      primitives[pi].atlas_uv_ = atlas_.GetWhitePixelUV();
    }
    ++pi;
  }
  return primitives;
}
void PObjectManager::GroupPrimitives(kEntityId primary, kEntityId begin,
                                     kEntityId end) {}
void PObjectManager::MakeStatic(kEntityId id) {
  auto object = objects[id];

  auto transform = object->Get<Transform>();
  auto mesh = object->Get<Mesh>();
  if (transform && mesh) {
    auto pos = transform->GetPosition();
    auto vertices = mesh->GetVertices();
    auto indices = mesh->GetIndices();
    auto scale = mesh->GetScale();
    auto offset = static_vertices_.size();

    auto tex = object->Get<Texture2D>();
    glm::vec4 atlas_uv;
    if (tex) {
      atlas_.AddTexture(tex);
      atlas_.UpdateAtlas();
      atlas_uv = tex->GetAtlasUV();
    } else {
      atlas_uv = atlas_.GetWhitePixelUV();
    }

    for (auto index : indices) {
      static_indices_.push_back(index + offset);
    }
    for (auto vertex : vertices) {
      StaticVertex sv = {};
      sv.position.x = vertex.position.x * scale.x * 0.5f + pos.x;
      sv.position.y = vertex.position.y * scale.y * 0.5f + pos.y;
      sv.position.z = vertex.position.z * scale.z * 0.5f + pos.z;
      sv.color = PQ_FLOAT4{vertex.color.x, vertex.color.y, vertex.color.z,
                           mesh->opacity_};
      sv.uv = vertex.uv;
      sv.atlas_uv = PQ_FLOAT4{atlas_uv.x, atlas_uv.y, atlas_uv.z, atlas_uv.w};
      static_vertices_.push_back(sv);
    }
    // Instead of deleting, removing from active objects so physics (for
    // collision) still works
    // DeleteObject(id);

    object->Remove<Mesh>();
    object->Remove<Texture2D>();
  } else {
    PDebug::warn(
        "Could not make static: Object did not have transform or mesh");
  }
}
std::vector<StaticVertex> PObjectManager::GetStaticVertices() const {
  return this->static_vertices_;
}
std::vector<UINT> PObjectManager::GetStaticIndices() const {
  return this->static_indices_;
}
void PObjectManager::GenerateVertices() {}

void PObjectManager::DeleteObject(kEntityId id) { objects[id] = nullptr; }
}  // namespace Pequod
