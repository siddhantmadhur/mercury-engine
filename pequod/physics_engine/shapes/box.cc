//
// Created by smadhur on 4/12/2026.
//

#include "box.h"

#include <Jolt/Jolt.h>

#include "Jolt/Physics/Collision/Shape/BoxShape.h"

namespace Pequod::Physics {
Box::Box(glm::vec2 dim) : Box(glm::vec3(dim, 1.0f)) {}

Box::Box(glm::vec3 dim) {
  dim /= 2;
  JPH::BoxShapeSettings box_shape_settings(JPH::Vec3(dim.x, dim.y, dim.z));
  box_shape_settings.SetEmbedded();

  JPH::ShapeSettings::ShapeResult box_shape_result =
      box_shape_settings.Create();
  this->shape_ref_c_ = box_shape_result.Get();
}
void Box::OverrideBodyCreation(JPH::BodyCreationSettings& creation_settings) {
  // creation_settings.mMotionQuality = JPH::EMotionQuality::Discrete;
  creation_settings.mLinearDamping = 0.0f;
  creation_settings.mAngularDamping = 0.0f;
  creation_settings.mFriction = 0.0f;
  creation_settings.mAllowDynamicOrKinematic = true;
  creation_settings.mAllowSleeping = false;
}
}  // namespace Pequod::Physics