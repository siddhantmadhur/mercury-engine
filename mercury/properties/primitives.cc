#include "primitives.h"

#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "properties/vfs_io_system.h"

namespace Mercury::Primitives {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// Collects vertices and triangles, then turns them into a Mesh.
class Builder {
 public:
  explicit Builder(glm::vec4 color) : color_(color) {}

  UINT Add(glm::vec3 position, glm::vec3 normal, glm::vec2 uv) {
    Vertex vertex;
    vertex.position = position;
    vertex.color = glm::vec3(color_);
    vertex.uv = uv;
    vertex.normal = normal;
    vertices_.push_back(vertex);
    return static_cast<UINT>(vertices_.size() - 1);
  }

  // Front faces are counter-clockwise seen from outside. Rather than get
  // every generator's index order right by hand, each triangle is flipped if
  // it faces away from its vertices' normals.
  void Triangle(UINT a, UINT b, UINT c) {
    const glm::vec3 pa = vertices_[a].position;
    const glm::vec3 face = glm::cross(vertices_[b].position - pa,
                                      vertices_[c].position - pa);
    if (glm::dot(face, face) < 1e-12f) return;  // degenerate (sphere poles)
    const glm::vec3 normal =
        vertices_[a].normal + vertices_[b].normal + vertices_[c].normal;
    if (glm::dot(face, normal) < 0.0f) std::swap(b, c);
    indices_.insert(indices_.end(), {a, b, c});
  }

  void QuadFace(UINT a, UINT b, UINT c, UINT d) {
    Triangle(a, b, c);
    Triangle(a, c, d);
  }

  // Rows of `columns + 1` vertices (the seam is duplicated for the UVs),
  // joined into quads.
  void Grid(UINT first, int rows, int columns) {
    const UINT stride = static_cast<UINT>(columns + 1);
    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < columns; c++) {
        const UINT i = first + static_cast<UINT>(r) * stride + c;
        QuadFace(i, i + 1, i + stride + 1, i + stride);
      }
    }
  }

  Mesh Build() {
    glm::vec3 min(0.0f), max(0.0f);
    if (!vertices_.empty()) {
      min = max = vertices_.front().position;
      for (const auto& vertex : vertices_) {
        min = glm::min(min, vertex.position);
        max = glm::max(max, vertex.position);
      }
    }
    Mesh mesh;
    mesh.SetVertices(std::move(vertices_));
    mesh.SetIndices(indices_);
    mesh.SetAABB(min, max);
    mesh.opacity_ = color_.a;
    return mesh;
  }

 private:
  glm::vec4 color_;
  std::vector<Vertex> vertices_;
  std::vector<UINT> indices_;
};

// One ring of a sphere-like shape at polar angle `phi` (0 at the top), with
// its center moved up or down by `y_offset`.
void Ring(Builder& builder, int segments, float phi, float y_offset, float v) {
  for (int s = 0; s <= segments; s++) {
    const float u = static_cast<float>(s) / segments;
    const float theta = u * 2.0f * kPi;
    const glm::vec3 normal(std::sin(phi) * std::cos(theta), std::cos(phi),
                           std::sin(phi) * std::sin(theta));
    builder.Add(normal * 0.5f + glm::vec3(0.0f, y_offset, 0.0f), normal,
                glm::vec2(u, v));
  }
}

}  // namespace

Mesh Cube(glm::vec4 color) {
  Builder builder(color);
  // For each face: its normal and two axes spanning it.
  const glm::vec3 faces[6][3] = {
      {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
      {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
      {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
  };
  for (const auto& face : faces) {
    const glm::vec3 n = face[0], u = face[1], v = face[2];
    const glm::vec3 center = n * 0.5f;
    const UINT a = builder.Add(center - u * 0.5f - v * 0.5f, n, {0, 1});
    const UINT b = builder.Add(center + u * 0.5f - v * 0.5f, n, {1, 1});
    const UINT c = builder.Add(center + u * 0.5f + v * 0.5f, n, {1, 0});
    const UINT d = builder.Add(center - u * 0.5f + v * 0.5f, n, {0, 0});
    builder.QuadFace(a, b, c, d);
  }
  return builder.Build();
}

Mesh Sphere(glm::vec4 color, int segments, int rings) {
  segments = std::max(segments, 3);
  rings = std::max(rings, 2);
  Builder builder(color);
  for (int r = 0; r <= rings; r++) {
    const float v = static_cast<float>(r) / rings;
    Ring(builder, segments, v * kPi, 0.0f, v);
  }
  builder.Grid(0, rings, segments);
  return builder.Build();
}

Mesh Capsule(glm::vec4 color, int segments, int rings) {
  segments = std::max(segments, 3);
  rings = std::max(rings, 2);
  Builder builder(color);
  // Two hemispheres of radius 0.5 whose centers are 1 apart: total height 2.
  // The equator ring appears in both halves; the band between them is the
  // cylinder. v runs from 0 at the top to 1 at the bottom.
  for (int r = 0; r <= rings; r++) {
    const float t = static_cast<float>(r) / rings;
    Ring(builder, segments, t * kPi / 2.0f, 0.5f, t * 0.25f);
  }
  for (int r = 0; r <= rings; r++) {
    const float t = static_cast<float>(r) / rings;
    Ring(builder, segments, kPi / 2.0f + t * kPi / 2.0f, -0.5f,
         0.75f + t * 0.25f);
  }
  builder.Grid(0, 2 * rings + 1, segments);
  return builder.Build();
}

Mesh Cylinder(glm::vec4 color, int segments) {
  segments = std::max(segments, 3);
  Builder builder(color);
  // Side: a top row and a bottom row with outward normals.
  for (float y : {1.0f, -1.0f}) {
    for (int s = 0; s <= segments; s++) {
      const float u = static_cast<float>(s) / segments;
      const float theta = u * 2.0f * kPi;
      const glm::vec3 normal(std::cos(theta), 0.0f, std::sin(theta));
      builder.Add(normal * 0.5f + glm::vec3(0.0f, y, 0.0f), normal,
                  glm::vec2(u, y > 0.0f ? 0.0f : 1.0f));
    }
  }
  builder.Grid(0, 1, segments);
  // Caps: a center vertex and a fan around it.
  for (float y : {1.0f, -1.0f}) {
    const glm::vec3 normal(0.0f, y, 0.0f);
    const UINT center =
        builder.Add(glm::vec3(0.0f, y, 0.0f), normal, glm::vec2(0.5f));
    for (int s = 0; s <= segments; s++) {
      const float theta = static_cast<float>(s) / segments * 2.0f * kPi;
      const glm::vec2 circle(std::cos(theta), std::sin(theta));
      builder.Add(glm::vec3(circle.x * 0.5f, y, circle.y * 0.5f), normal,
                  circle * 0.5f + 0.5f);
    }
    for (int s = 0; s < segments; s++) {
      builder.Triangle(center, center + 1 + s, center + 2 + s);
    }
  }
  return builder.Build();
}

Mesh Plane(glm::vec4 color) {
  Builder builder(color);
  const glm::vec3 n(0.0f, 1.0f, 0.0f);
  const UINT a = builder.Add({-0.5f, 0.0f, 0.5f}, n, {0, 1});
  const UINT b = builder.Add({0.5f, 0.0f, 0.5f}, n, {1, 1});
  const UINT c = builder.Add({0.5f, 0.0f, -0.5f}, n, {1, 0});
  const UINT d = builder.Add({-0.5f, 0.0f, -0.5f}, n, {0, 0});
  builder.QuadFace(a, b, c, d);
  return builder.Build();
}

Mesh Quad(glm::vec4 color) {
  Builder builder(color);
  const glm::vec3 n(0.0f, 0.0f, 1.0f);
  const UINT a = builder.Add({-0.5f, -0.5f, 0.0f}, n, {0, 1});
  const UINT b = builder.Add({0.5f, -0.5f, 0.0f}, n, {1, 1});
  const UINT c = builder.Add({0.5f, 0.5f, 0.0f}, n, {1, 0});
  const UINT d = builder.Add({-0.5f, 0.5f, 0.0f}, n, {0, 0});
  builder.QuadFace(a, b, c, d);
  return builder.Build();
}

std::optional<Mesh> LoadModel(const std::string& path, glm::vec4 color,
                              std::string* error) {
  Assimp::Importer importer;
  UseVfs(importer);
  const aiScene* scene = importer.ReadFile(
      path, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                aiProcess_PreTransformVertices | aiProcess_FlipUVs |
                aiProcess_JoinIdenticalVertices);
  if (!scene || !scene->mRootNode) {
    if (error) *error = importer.GetErrorString();
    return std::nullopt;
  }

  Builder builder(color);
  // PreTransformVertices has already baked the node transforms in.
  for (unsigned m = 0; m < scene->mNumMeshes; m++) {
    const aiMesh* mesh = scene->mMeshes[m];
    const UINT first = static_cast<UINT>(-1);
    UINT base = first;
    for (unsigned i = 0; i < mesh->mNumVertices; i++) {
      const aiVector3D& p = mesh->mVertices[i];
      glm::vec3 normal(0.0f, 1.0f, 0.0f);
      if (mesh->HasNormals()) {
        normal = {mesh->mNormals[i].x, mesh->mNormals[i].y,
                  mesh->mNormals[i].z};
      }
      glm::vec2 uv(0.0f);
      if (mesh->HasTextureCoords(0)) {
        uv = {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
      }
      const UINT index = builder.Add({p.x, p.y, p.z}, normal, uv);
      if (base == first) base = index;
    }
    if (base == first) continue;
    for (unsigned f = 0; f < mesh->mNumFaces; f++) {
      const aiFace& face = mesh->mFaces[f];
      if (face.mNumIndices != 3) continue;  // points and lines
      builder.Triangle(base + face.mIndices[0], base + face.mIndices[1],
                       base + face.mIndices[2]);
    }
  }
  return builder.Build();
}

}  // namespace Mercury::Primitives
