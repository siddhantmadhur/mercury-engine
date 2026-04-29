#ifndef PEQUOD_MESH_IMPL_HH_
#define PEQUOD_MESH_IMPL_HH_

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "globals.h"
#include "property.h"

namespace Pequod {

// TODO: allow two gameobjects to combine their vertices so that the no. of draw
// calls is reduced
class Mesh : public Property {
 public:
  Mesh();

  ~Mesh();

  void SetVertices(std::vector<Vertex>);
  std::vector<Vertex> GetVertices() const;

  void SetIndices(const std::vector<UINT>&);
  std::vector<UINT> GetIndices() const;

  void SetScale(glm::vec3);

  glm::vec3 GetScale() const;

  uint32_t GetIndicesID() const;

  std::vector<Vertex> vertices_;
  std::vector<UINT> indices_;
  UINT vertices_id = 0;  // the id to get this in the ECS vertex vector
  UINT indices_id = 0;   // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ indices vector
 private:
  glm::vec3 scale{};
};
}  // namespace Pequod
#endif
