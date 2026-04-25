//
// Created by smadhur on 4/25/2026.
//

#include "plane.h"

#include "Jolt/Physics/Collision/Shape/PlaneShape.h"
namespace Pequod::Physics {

Plane::Plane(glm::vec3 normal) {
  JPH::Plane plane(JPH::Vec3(normal.x, normal.y, normal.z), 0);
  JPH::PlaneShapeSettings plane_shape_settings(plane);
  plane_shape_settings.SetEmbedded();

  JPH::ShapeSettings::ShapeResult shape_result = plane_shape_settings.Create();
  this->shape_ref_c_ = shape_result.Get();
}

void Plane::OverrideBodyCreation(JPH::BodyCreationSettings& creation_settings) {
}

}  // namespace Pequod::Physics