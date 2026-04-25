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
    if (auto mesh = object->Get<Mesh>()) {
      Primitive primitive = {};
      primitive.indices_ = mesh->GetIndices();
      primitive.vertices_ = mesh->GetVertices();
      primitive.scale_ = mesh->GetScale();
      if (auto transform = object->Get<Transform>()) {
        auto world_position = transform->GetPosition();
        primitive.world_position_ = world_position;
      } else {
        primitive.world_position_ = glm::vec3(1.0f);
      }

      if (auto tex = object->Get<Texture2D>()) {
        primitive.texture_data_   = tex->GetData();
        primitive.texture_width_  = tex->GetWidth();
        primitive.texture_height_ = tex->GetHeight();
      }

      primitives.push_back(primitive);
    }
  }
  return primitives;
}

// [CLAUDE] TODO: DeleteObject is an empty stub — objects added via NewObject
// are never freed (memory leak)
void PObjectManager::DeleteObject(std::shared_ptr<PObject> object) {
  objects[object->id] = nullptr;
}
}  // namespace Pequod
