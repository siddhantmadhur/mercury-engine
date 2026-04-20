//
// Created by smadhur on 4/12/2026.
//

#ifndef PEQUODENGINE_BOX_H
#define PEQUODENGINE_BOX_H
#include "../../properties/collision_body.h"
#include "glm/detail/func_packing_simd.inl"
#include "glm/vec3.hpp"

namespace Pequod {
class Box : public CollisionBody {
 public:
  Box(glm::vec3 dim);
};

}  // namespace Pequod

#endif  // PEQUODENGINE_BOX_H
