//
// Created by smadhur on 4/1/26.
//

#include "manager.h"

#include "debugger/debugger.h"
#include "properties/mesh.h"
#include "properties/texture2d.h"
#include "properties/transform.h"

// [CLAUDE] TODO: RUN_ON_ALL_NODES macro is defined but never called — remove
#define RUN_ON_ALL_NODES(func) \
  {                            \
    func(Box2D);               \
  }

namespace Pequod {
PObjectManager::PObjectManager() {
  std::get<PEQUOD_MACRO_DO_NOT_USE_PROPERTY_LIST_TYPE(Mesh)>(properties_) =
      new std::optional<Mesh>[kMaxProperties] {};
  std::get<PEQUOD_MACRO_DO_NOT_USE_PROPERTY_LIST_TYPE(Transform)>(properties_) =
      new std::optional<Transform>[kMaxProperties] {};
  std::get<PEQUOD_MACRO_DO_NOT_USE_PROPERTY_LIST_TYPE(CollisionBody)>(
      properties_) = new std::optional<CollisionBody>[kMaxProperties] {};
  std::get<PEQUOD_MACRO_DO_NOT_USE_PROPERTY_LIST_TYPE(Camera)>(properties_) =
      new std::optional<Camera>[kMaxProperties] {};
  std::get<PEQUOD_MACRO_DO_NOT_USE_PROPERTY_LIST_TYPE(Texture2D)>(properties_) =
      new std::optional<Texture2D>[kMaxProperties] {};
};
std::vector<Primitive> PObjectManager::GetPrimitives() {
  std::vector<Primitive> primitives;
  for (int id = 0; id < current_entity_size_; id++) {
    auto mesh = GetProperty<Mesh>(id);
    if (mesh) {
      Primitive primitive = {};
      primitive.indices_ = mesh->GetIndices();
      primitive.vertices_ = mesh->GetVertices();
      primitive.scale_ = mesh->GetScale();
      primitive.opacity_ = mesh->opacity_;
      auto transform = GetProperty<Transform>(id);
      if (transform) {
        auto world_position = transform->GetPosition();
        primitive.world_position_ = world_position;
      } else {
        primitive.world_position_ = glm::vec3(1.0f);
      }

      auto tex = GetProperty<Texture2D>(id);
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
  for (int id = 0; id < current_entity_size_; id++) {
    auto mesh = GetProperty<Mesh>(id);
    if (!mesh) continue;
    auto tex = GetProperty<Texture2D>(id);
    if (tex) {
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

#define PEQUOD_MACRO_DO_NOT_USE_COPY_PROPERTY_FROM_NODE(PROPERTY_TYPE) \
  {                                                                    \
    auto property = node.GetProperty<PROPERTY_TYPE>();                 \
    if (property) {                                                    \
      AddProperty(id, PROPERTY_TYPE(*property));                       \
    }                                                                  \
  }

kEntityId PObjectManager::NewBox2D(glm::vec2 position = glm::vec2(0.0),
                                   glm::vec2 size = glm::vec2(0.0),
                                   glm::vec4 color = glm::vec4(1.0)) {
  kEntityId id = NewObject();

  auto pos = Transform();
  pos.SetPosition(glm::vec3(position, 0.0f));

  auto mesh = Mesh();
  PQ_FLOAT3 dx_color(color.r, color.g, color.b);
  Vertex raw_vertices[4] = {
      {PQ_FLOAT3{1.0f, 1.0f, 0.0f}, dx_color, PQ_FLOAT2{1.0f, 0.0f}},
      {PQ_FLOAT3{1.0f, -1.0f, 0.0f}, dx_color, PQ_FLOAT2{1.0f, 1.0f}},
      {PQ_FLOAT3{-1.0f, -1.0f, 0.0f}, dx_color, PQ_FLOAT2{0.0f, 1.0f}},
      {PQ_FLOAT3{-1.0f, 1.0f, 0.0f}, dx_color, PQ_FLOAT2{0.0f, 0.0f}},
  };
  mesh.SetVertices(
      std::vector<Vertex>(std::begin(raw_vertices), std::end(raw_vertices)));

  uint16_t raw_indices[6] = {0, 1, 3, 1, 2, 3};
  mesh.SetIndices(
      std::vector<UINT>(std::begin(raw_indices), std::end(raw_indices)));

  mesh.opacity_ = color[3];
  mesh.SetScale(glm::vec3(size.x, size.y, 1.0f));

  AddProperty(id, pos);
  AddProperty(id, mesh);

  return id;
}

kEntityId PObjectManager::NewObject() {
  if (current_entity_size_ == kMaxProperties) {
    PDebug::error("Reached max properties size");
    return -1;
  }
  auto id = current_entity_size_;
  current_entity_size_ += 1;
  return id;
}

void PObjectManager::MakeStatic(kEntityId id) {
  auto transform = GetProperty<Transform>(id);
  auto mesh = GetProperty<Mesh>(id);
  if (transform && mesh) {
    auto pos = transform->GetPosition();
    auto vertices = mesh->GetVertices();
    auto indices = mesh->GetIndices();
    auto scale = mesh->GetScale();
    auto offset = static_vertices_.size();

    auto tex = GetProperty<Texture2D>(id);
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

    DeleteProperty<Mesh>(id);
    DeleteProperty<Texture2D>(id);
    DeleteProperty<Transform>(id);
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

void PObjectManager::DeleteObject(kEntityId id) {
  DeleteProperty<Mesh>(id);
  DeleteProperty<Texture2D>(id);
  DeleteProperty<Transform>(id);
  DeleteProperty<Camera>(id);
}
}  // namespace Pequod
