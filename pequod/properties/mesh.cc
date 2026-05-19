#include "mesh.h"

#include <utility>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "assimp/code/Common/StackAllocator.h"
#include "debugger/debugger.h"

namespace Pequod {
Mesh::Mesh() {
  this->vertices_ = {};
  this->indices_ = {};
}

Mesh::Mesh(const std::string &file_path) {
  Assimp::Importer importer;

  const aiScene *scene = importer.ReadFile(
      file_path, aiProcess_Triangulate | aiProcess_FlipUVs |
                     aiProcess_ConvertToLeftHanded | aiProcess_GenNormals |
                     aiProcess_PreTransformVertices |
                     aiProcess_GenBoundingBoxes | aiProcess_CalcTangentSpace);

  if (scene == nullptr) {
    PDebug::error("Could not load object");
    exit(1);
  }

  std::vector<Vertex> vertices = {};
  std::vector<UINT> indices = {};

  std::vector<aiNode *> nodes = {};
  nodes.push_back(scene->mRootNode);

  int vertex_offset = 0;
  float lowest_y = 0.0;
  glm::vec3 mesh_min(0.0);
  glm::vec3 mesh_max(0.0);

  while (nodes.size()) {
    aiNode *cur = nodes.back();
    nodes.pop_back();

    for (int i = 0; i < cur->mNumChildren; i++) {
      nodes.push_back(cur->mChildren[i]);
    }

    for (int x = 0; x < cur->mNumMeshes; x++) {
      auto idx = cur->mMeshes[x];
      auto *aiMesh = scene->mMeshes[idx];
      aiAABB &aabb = aiMesh->mAABB;
      aiVector3D center = (aabb.mMin + aabb.mMax) / 2.0f;

      if (mesh_min.x > aabb.mMin.x) mesh_min.x = aabb.mMin.x;
      if (mesh_min.y > aabb.mMin.y) mesh_min.y = aabb.mMin.y;
      if (mesh_min.z > aabb.mMin.z) mesh_min.z = aabb.mMin.z;

      if (mesh_max.x < aabb.mMax.x) mesh_max.x = aabb.mMax.x;
      if (mesh_max.y < aabb.mMax.y) mesh_max.y = aabb.mMax.y;
      if (mesh_max.z < aabb.mMax.z) mesh_max.z = aabb.mMax.z;

      vertex_offset = vertices.size();

      for (int i = 0; i < aiMesh->mNumVertices; i++) {
        Vertex dir_vertex;
        auto original = aiMesh->mVertices[i] - center;

        dir_vertex.position = PQ_FLOAT3{original.x, original.y, original.z};
        dir_vertex.color = PQ_FLOAT3{1.0, 1.0, 1.0};

        if (aiMesh->HasTextureCoords(0)) {
          auto uv = aiMesh->mTextureCoords[0][i];
          dir_vertex.uv = PQ_FLOAT2{uv.x, uv.y};
        } else {
          dir_vertex.uv = PQ_FLOAT2{0.0, 0.0};
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

  SetAABB(mesh_min, mesh_max);
  SetVertices(vertices);
  SetIndices(indices);
}
bool Mesh::AddMesh(const Mesh &mesh) {
  auto index_offset = vertices_.size();

  for (auto vertex : mesh.vertices_) {
    vertices_.push_back(vertex);
  }
  for (auto index : mesh.indices_) {
    indices_.push_back(index + index_offset);
  }

  aabb_.max = glm::max(aabb_.max, mesh.aabb_.max);
  aabb_.min = glm::min(aabb_.min, mesh.aabb_.min);
  return true;
}

Mesh::~Mesh() {}

void Mesh::SetVertices(std::vector<Vertex> vertices) {
  this->vertices_ = std::move(vertices);
}

std::vector<Vertex> Mesh::GetVertices() const { return this->vertices_; }

void Mesh::SetIndices(const std::vector<UINT> &indices) {
  this->indices_ = indices;
}

uint32_t Mesh::GetIndicesID() const { return indices_id; }
void Mesh::SetAABB(glm::vec3 min, glm::vec3 max) {
  this->aabb_.min = min;
  this->aabb_.max = max;
}

mAABB Mesh::GetAABB() { return this->aabb_; }

float Mesh::GetHeight() {
  float local_height = (aabb_.max.y - aabb_.min.y);
  return local_height;
}

std::vector<UINT> Mesh::GetIndices() const { return indices_; }
}  // namespace Pequod
