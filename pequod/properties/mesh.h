#ifndef PEQUOD_MESH_IMPL_HH_
#define PEQUOD_MESH_IMPL_HH_

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "globals.h"

namespace Pequod {
// TODO: allow two gameobjects to combine their vertices so that the no. of draw
// calls is reduced

typedef struct mAABB {
  glm::vec3 min;
  glm::vec3 max;
} mAABB;

class Mesh {
 public:
  Mesh();

  ~Mesh();

  void SetVertices(std::vector<Vertex>);

  std::vector<Vertex> GetVertices() const;

  void SetIndices(const std::vector<UINT>&);

  std::vector<UINT> GetIndices() const;

  uint32_t GetIndicesID() const;
  void SetAABB(glm::vec3 min, glm::vec3 max);
  mAABB GetAABB();

  float opacity_ = 1.0f;

  float GetHeight();

  std::vector<Vertex> vertices_;
  std::vector<UINT> indices_;
  UINT vertices_id = 0;  // the id to get this in the ECS vertex vector
  UINT indices_id = 0;   // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ indices vector
 private:
  float height_ = 0.0;

  mAABB aabb_;
};
}  // namespace Pequod
#endif
