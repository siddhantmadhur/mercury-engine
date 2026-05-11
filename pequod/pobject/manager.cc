//

// Created by smadhur on 4/1/26.
//

#include "manager.h"

#include <stack>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "assimp/code/Common/StackAllocator.h"
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
        new std::optional<Mesh>[kMaxEntities]{};
    std::get<PEQUOD_MACRO_DO_NOT_USE_PROPERTY_LIST_TYPE(Transform)>(properties_) =
        new std::optional<Transform>[kMaxEntities]{};
    std::get<PEQUOD_MACRO_DO_NOT_USE_PROPERTY_LIST_TYPE(Camera)>(properties_) =
        new std::optional<Camera>[kMaxEntities]{};
    std::get<PEQUOD_MACRO_DO_NOT_USE_PROPERTY_LIST_TYPE(Texture2D)>(properties_) =
        new std::optional<Texture2D>[kMaxEntities]{};
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
          primitive.world_position_ = glm::vec3(0.0f);
        }

        auto tex = GetProperty<Texture2D>(id);
        if (tex) {
          atlas_.AddTexture(tex);
        }
        primitives.push_back(primitive);
      }
    }
    atlas_.UpdateAtlas();
    RefreshStaticAtlasUVs();
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
                                       kEntityId end) {
  }

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
      {PQ_FLOAT3{0.5f, 0.5f, 0.0f}, dx_color, PQ_FLOAT2{1.0f, 0.0f}},
      {PQ_FLOAT3{0.5f, -0.5f, 0.0f}, dx_color, PQ_FLOAT2{1.0f, 1.0f}},
      {PQ_FLOAT3{-0.5f, -0.5f, 0.0f}, dx_color, PQ_FLOAT2{0.0f, 1.0f}},
      {PQ_FLOAT3{-0.5f, 0.5f, 0.0f}, dx_color, PQ_FLOAT2{0.0f, 0.0f}},
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

  kEntityId PObjectManager::NewPlane2D(glm::vec3 position, glm::vec2 size,
                                       glm::vec4 color) {
    kEntityId id = NewObject();

    auto pos = Transform();
    pos.SetPosition(position);

    auto mesh = Mesh();
    PQ_FLOAT3 dx_color(color.r, color.g, color.b);
    Vertex raw_vertices[4] = {
      {PQ_FLOAT3{0.5f, 0.0f, 0.5f}, dx_color, PQ_FLOAT2{1.0f, 0.0f}},
      {PQ_FLOAT3{0.5f, 0.0f, -0.5f}, dx_color, PQ_FLOAT2{1.0f, 1.0f}},
      {PQ_FLOAT3{-0.5f, 0.0f, -0.5f}, dx_color, PQ_FLOAT2{0.0f, 1.0f}},
      {PQ_FLOAT3{-0.5f, 0.0f, 0.5f}, dx_color, PQ_FLOAT2{0.0f, 0.0f}},
    };
    mesh.SetVertices(
      std::vector<Vertex>(std::begin(raw_vertices), std::end(raw_vertices)));

    uint16_t raw_indices[6] = {0, 1, 3, 1, 2, 3};
    // other way around
    //uint16_t raw_indices[6] = {0, 3, 1, 1, 3, 2};
    mesh.SetIndices(
      std::vector<UINT>(std::begin(raw_indices), std::end(raw_indices)));

    mesh.opacity_ = color[3];
    mesh.SetScale(glm::vec3(size.x, 0.0f, size.y));

    AddProperty(id, pos);
    AddProperty(id, mesh);
    return id;
  }

  kEntityId PObjectManager::NewObjectFromFile(const std::string &file_path,
                                              float scale, glm::vec4 color
  ) {
    Assimp::Importer importer;

    const aiScene *scene = importer.ReadFile(
      file_path,
      aiProcess_Triangulate |
      aiProcess_FlipUVs |
      aiProcess_ConvertToLeftHanded |
      aiProcess_GenNormals |
      aiProcess_PreTransformVertices |
      aiProcess_CalcTangentSpace
    );

    if (scene == nullptr) {
      PDebug::error("Could not load object");
      return -1;
    }
    kEntityId id = NewObject();
    auto mesh = Mesh();
    mesh.SetScale(glm::vec3(scale));
    std::vector<Vertex> vertices = {};
    std::vector<UINT> indices = {};

    std::vector<aiNode *> nodes = {};
    nodes.push_back(scene->mRootNode);

    int vertex_offset = 0;
    while (nodes.size()) {
      aiNode *cur = nodes.back();
      nodes.pop_back();

      for (int i = 0; i < cur->mNumChildren; i++) {
        nodes.push_back(cur->mChildren[i]);
      }

      for (int x = 0; x < cur->mNumMeshes; x++) {
        auto idx = cur->mMeshes[x];
        auto *aiMesh = scene->mMeshes[idx];
        vertex_offset = vertices.size();

        for (int i = 0; i < aiMesh->mNumVertices; i++) {
          Vertex dir_vertex;
          auto original = aiMesh->mVertices[i];
          dir_vertex.position = PQ_FLOAT3{original.x, original.y, original.z};
          dir_vertex.color = PQ_FLOAT3{color.x, color.y, color.z};
          if (aiMesh->HasTextureCoords(0)) {
            auto uv = aiMesh->mTextureCoords[0][i];
            dir_vertex.uv = PQ_FLOAT2{uv.x, uv.y};
          } else {
            dir_vertex.uv = PQ_FLOAT2{0.0f, 0.0f};
          }
          vertices.push_back(dir_vertex);
        }

        for (int i = 0; i < aiMesh->mNumFaces; i++) {
          auto face = aiMesh->mFaces[i];
          for (int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j] + vertex_offset);
          }
        }
      }
    }

    mesh.SetVertices(vertices);
    mesh.SetIndices(indices);

    AddProperty<Mesh>(id, mesh);


    return id;
  }

  kEntityId PObjectManager::NewObject() {
    if (current_entity_size_ == kMaxEntities) {
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

      for (auto index: indices) {
        static_indices_.push_back(index + offset);
      }
      const size_t range_start = static_vertices_.size();
      for (auto vertex: vertices) {
        StaticVertex sv = {};
        sv.position.x = (vertex.position.x * scale.x) + pos.x;
        sv.position.y = (vertex.position.y * scale.y) + pos.y;
        sv.position.z = (vertex.position.z * scale.z) + pos.z;
        sv.color = PQ_FLOAT4{
          vertex.color.x, vertex.color.y, vertex.color.z,
          mesh->opacity_
        };
        sv.uv = vertex.uv;
        sv.atlas_uv = PQ_FLOAT4{atlas_uv.x, atlas_uv.y, atlas_uv.z, atlas_uv.w};
        static_vertices_.push_back(sv);
      }
      static_ranges_.push_back(StaticRange{
        range_start, vertices.size(), tex
      });

      // Mesh/Transform can go — the static buffer owns the baked copy now.
      // Keep Texture2D so the atlas's pointer (and our StaticRange::tex) stays
      // valid for re-baking UVs whenever the atlas repacks.
      DeleteProperty<Mesh>(id);
      DeleteProperty<Transform>(id);

      // The atlas may already have repacked above; make sure prior static
      // ranges pick up their new sub-rects.
      RefreshStaticAtlasUVs();
    } else {
      PDebug::warn(
        "Could not make static: Object did not have transform or mesh");
    }
  }

  void PObjectManager::RefreshStaticAtlasUVs() {
    const glm::vec4 white_uv = atlas_.GetWhitePixelUV();
    for (const auto &range: static_ranges_) {
      glm::vec4 uv = range.tex ? range.tex->GetAtlasUV() : white_uv;
      const PQ_FLOAT4 packed{uv.x, uv.y, uv.z, uv.w};
      const size_t end = range.vertex_start + range.vertex_count;
      for (size_t i = range.vertex_start; i < end && i < static_vertices_.size();
           ++i) {
        static_vertices_[i].atlas_uv = packed;
      }
    }
  }

  std::vector<StaticVertex> PObjectManager::GetStaticVertices() const {
    return this->static_vertices_;
  }

  std::vector<UINT> PObjectManager::GetStaticIndices() const {
    return this->static_indices_;
  }

  void PObjectManager::GenerateVertices() {
  }

  void PObjectManager::DeleteObject(kEntityId id) {
    DeleteProperty<Mesh>(id);
    DeleteProperty<Texture2D>(id);
    DeleteProperty<Transform>(id);
    DeleteProperty<Camera>(id);
  }
} // namespace Pequod
