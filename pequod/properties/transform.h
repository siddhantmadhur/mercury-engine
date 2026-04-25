//
// Created by smadhur on 4/12/2026.
//

#ifndef PEQUODENGINE_TRANSFORM_H
#define PEQUODENGINE_TRANSFORM_H
#include "glm/detail/func_packing_simd.inl"
#include "glm/vec3.hpp"
#include "property.h"

namespace Pequod {

enum TransformationType {
  kTransformPosition = 0,
  kTransformLinearVelocity = 1,
  kTransformLinearMagnitude = 2
};

class Transform : public Property {
 public:
  Transform();
  Transform(glm::vec3 position);
  glm::vec3 GetPosition() const;
  void SetPosition(glm::vec3);
  glm::vec3 GetInterpolatedPosition() const;

  glm::vec3 GetVelocity() const;
  void SetVelocity(glm::vec3);

  void SetVelocityMagnitude(float);

  void Move(glm::vec3);

  // Run this every frame to interpolate position between ticks
  void InterpolatePosition(float delta_t, float tick_t);

  std::vector<TransformationType> GetTransformations();
  void ClearTransformations();

 private:
  glm::vec3 position_;
  glm::vec3 velocity_{};
  /*
   * Position only updates per tick (which can be significantly lower than fps)
   * so this makes sure its smoothed out
   */
  glm::vec3 interpolated_position_;

  /**
   * A queue of fixed transformations made (like position and velocity) so that
   * the physics system knows whether to listen to the entities property or the
   * calculated physics property
   */
  std::vector<TransformationType> transformations_;
};
}  // namespace Pequod

#endif  // PEQUODENGINE_TRANSFORM_H
