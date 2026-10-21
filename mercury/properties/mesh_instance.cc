
#include "mesh_instance.h"

namespace Mercury {

MeshInstance::MeshInstance(kEntityId parent_entity) {
  this->parent_entity_ = parent_entity;
}

}  // namespace Mercury