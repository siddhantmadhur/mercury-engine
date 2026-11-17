// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.


#include "mesh_instance.h"

namespace Mercury {

MeshInstance::MeshInstance(kEntityId parent_entity) {
  this->parent_entity_ = parent_entity;
}

}  // namespace Mercury
