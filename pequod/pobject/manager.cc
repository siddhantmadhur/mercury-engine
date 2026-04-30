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

      std::shared_ptr<Texture2D> tex = object->Get<Texture2D>();
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

void PObjectManager::DeleteObject(kEntityId id) { objects[id] = nullptr; }
}  // namespace Pequod
