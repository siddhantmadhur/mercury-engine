//
// Created by smadhur on 4/12/2026.
//

#ifndef PEQUODENGINE_TRANSFORM_H
#define PEQUODENGINE_TRANSFORM_H
#include "glm/detail/func_packing_simd.inl"
#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"
#include "property.h"

namespace Pequod {

enum TransformationType {
  kTransformPosition = 0,
  kTransformLinearVelocity = 1,
  kTransformLinearMagnitude = 2,
  kTransformRotation = 3,
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

  // Rotation is stored as Euler angles in degrees, vec3 = (pitch, yaw, roll)
  // around the X, Y, and Z axes respectively.
  void SetRotate(glm::vec3);
  void ChangeRotate(glm::vec3);
  glm::vec3 GetRotate() const;
  glm::vec3 GetInterpolatedRotation() const;
  glm::mat4 GetRotationMatrix() const;

  void SetVelocityMagnitude(float);

  void Move(glm::vec3);

  // Run this every frame to interpolate position and rotation between ticks.
  // alpha is the normalized progress through the current tick in [0, 1].
  void Interpolate(float alpha);

  // Snapshot current state into previous_*; call once at the start of each
  // tick before any per-tick updates run.
  void CaptureTickSnapshot();

  std::vector<TransformationType> GetTransformations();
  void ClearTransformations();

 private:
  glm::vec3 position_;
  glm::vec3 velocity_{};
  glm::vec3 rotation_{};
  /*
   * Position only updates per tick (which can be significantly lower than fps)
   * so this makes sure its smoothed out
   */
  glm::vec3 interpolated_position_;
  glm::vec3 previous_position_{};
  glm::vec3 interpolated_rotation_{};
  glm::vec3 previous_rotation_{};

  /**
   * A queue of fixed transformations made (like position and velocity) so that
   * the physics system knows whether to listen to the entities property or the
   * calculated physics property
   */
  std::vector<TransformationType> transformations_;
};
}  // namespace Pequod

#endif  // PEQUODENGINE_TRANSFORM_H
