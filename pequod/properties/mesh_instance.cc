
#include "mesh_instance.h"

namespace Pequod {

MeshInstance::MeshInstance(kEntityId parent_entity) {
  this->parent_entity_ = parent_entity;
}

}  // namespace Pequod