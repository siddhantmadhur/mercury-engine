//
// Created by smadhur on 4/12/2026.
//

#ifndef MERCURYENGINE_TRANSFORM_H
#define MERCURYENGINE_TRANSFORM_H
#include "glm/detail/func_packing_simd.inl"
#include <functional>
#include <vector>

#include "glm/gtc/quaternion.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"

namespace Mercury {

enum TransformationType {
  kTransformPosition = 0,
  kTransformLinearVelocity = 1,
  kTransformLinearMagnitude = 2,
  kTransformRotation = 3,
};

class Transform {
 public:
  Transform();
  Transform(glm::vec3 position);
  glm::vec3 GetPosition() const;
  void SetPosition(glm::vec3);
  glm::vec3 GetInterpolatedPosition() const;
  glm::vec3 GetScale() const;
  void SetScale(glm::vec3);

  // Local transform relative to parent.
  glm::vec3 GetLocalPosition() const { return local_position_; }
  void SetLocalPosition(glm::vec3);
  glm::vec3 GetLocalRotate() const { return local_rotation_; }
  void SetLocalRotate(glm::vec3);
  glm::quat GetLocalRotationQuat() const { return EulerToQuat(local_rotation_); }
  glm::vec3 GetLocalScale() const { return local_scale_; }
  void SetLocalScale(glm::vec3);

  void SetOnWorldChanged(std::function<void()> callback) {
    on_world_changed_ = std::move(callback);
  }
  void SetOnLocalChanged(std::function<void()> callback) {
    on_local_changed_ = std::move(callback);
  }
  void SetOnChanged(std::function<void()> callback) {
    on_world_changed_ = std::move(callback);
  }
  void SetWorldTransformInternal(glm::vec3 pos, glm::vec3 rot, glm::vec3 scale);
  void SetWorldPositionInternal(glm::vec3 pos);
  void SetWorldRotationInternal(glm::vec3 rot);
  void SetWorldScaleInternal(glm::vec3 scale);
  void SetInterpolatedTransformInternal(glm::vec3 pos, glm::vec3 rot, glm::vec3 scale);

  glm::vec3 GetVelocity() const;
  void SetVelocity(glm::vec3);

  // Rotation is stored as Euler angles in degrees, vec3 = (pitch, yaw, roll)
  // around the X, Y, and Z axes respectively. Meshes and physics bodies apply
  // X first, then Y, then Z (R = Rz * Ry * Rx), the order glm::quat uses.
  void SetRotate(glm::vec3);
  void ChangeRotate(glm::vec3);
  glm::vec3 GetRotate() const;
  glm::vec3 GetInterpolatedRotation() const;
  glm::vec3 GetInterpolatedScale() const;
  glm::mat4 GetRotationMatrix() const;
  glm::quat GetRotationQuat() const { return EulerToQuat(rotation_); }

  // Degrees <-> quaternion with the rotation order described above.
  static glm::quat EulerToQuat(glm::vec3 degrees);
  static glm::vec3 QuatToEuler(glm::quat rotation);

  void SetVelocityMagnitude(float);

  void Move(glm::vec3);

  // Run this every frame to interpolate position and rotation between ticks.
  // alpha is the normalized progress through the current tick in [0, 1].
  void Interpolate(float alpha);

  // Snapshot current state into previous_*; call once at the start of each
  // tick before any per-tick updates run.
  void CaptureTickSnapshot();

  // Make the interpolated (rendered) state equal the current state, so a
  // freshly placed object does not blend in from the origin or from scale 0.
  void SnapInterpolation();

  std::vector<TransformationType> GetTransformations();
  void ClearTransformations();

  bool IsDirty() const { return dirty_; }
  void ClearDirty() {
    dirty_ = false;
    last_built_position_ = interpolated_position_;
    last_built_rotation_ = interpolated_rotation_;
    last_built_scale_ = interpolated_scale_;
  }
  void MarkDirty() { dirty_ = true; }

 private:
  glm::vec3 position_;
  glm::vec3 velocity_{};
  glm::vec3 rotation_{};
  glm::vec3 scale_ = glm::vec3(1.0);

  glm::vec3 local_position_ = glm::vec3(0.0f);
  glm::vec3 local_rotation_ = glm::vec3(0.0f);
  glm::vec3 local_scale_ = glm::vec3(1.0f);
  std::function<void()> on_world_changed_;
  std::function<void()> on_local_changed_;
  /*
   * Position only updates per tick (which can be significantly lower than fps)
   * so this makes sure its smoothed out
   */
  glm::vec3 interpolated_position_;
  glm::vec3 previous_position_{};
  glm::vec3 interpolated_rotation_{};
  glm::vec3 previous_rotation_{};
  glm::vec3 interpolated_scale_{};
  glm::vec3 previous_scale_{};

  glm::mat4 rotation_matrix_ = glm::mat4(1.0f);
  // What rotation_matrix_ was built from.
  glm::vec3 matrix_previous_{};
  glm::vec3 matrix_current_{};
  float matrix_alpha_ = 1.0f;

  /**
   * A queue of fixed transformations made (like position and velocity) so that
   * the physics system knows whether to listen to the entities property or the
   * calculated physics property
   */
  std::vector<TransformationType> transformations_;

  // Set whenever any value influencing the rendered world matrix changes
  // (position, rotation, scale, or their interpolated counterparts). The
  // renderer reads + clears this each frame to decide which instance slots
  // need re-upload. Start dirty so the first frame seeds every slot.
  bool dirty_ = true;
  glm::vec3 last_built_position_{};
  glm::vec3 last_built_rotation_{};
  glm::vec3 last_built_scale_{};
};
}  // namespace Mercury

#endif  // MERCURYENGINE_TRANSFORM_H
