//
// Created by smadhur on 4/25/2026.
//

#ifndef PEQUODENGINE_PLANE_H
#define PEQUODENGINE_PLANE_H
#include "glm/glm.hpp"
#include "properties/collision_body.h"

namespace Pequod::Physics {
class Plane : public CollisionBody {
 public:
  Plane(glm::vec3 normal);
  void OverrideBodyCreation(JPH::BodyCreationSettings&) override;
};
}  // namespace Pequod::Physics

#endif  // PEQUODENGINE_PLANE_H
