//
// Created by smadhur on 5/19/2026.
//

#ifndef MERCURY_ENGINE_MESH_INSTANCE_H
#define MERCURY_ENGINE_MESH_INSTANCE_H
#include "globals.h"

namespace Mercury {

/**
 * @brief MeshInstance is a node that allows you to copy meshes over as multiple
 * instances without requiring copying vertices again and again. This is useful
 * for geometry like trees, floors, people etc.
 *
 */
class MeshInstance {
 public:
  MeshInstance(kEntityId);

  [[nodiscard]] kEntityId GetParentEntity() const { return parent_entity_; }

 private:
  kEntityId parent_entity_ = entt::null;
};

}  // namespace Mercury

#endif  // MERCURY_ENGINE_MESH_INSTANCE_H
