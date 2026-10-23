//
// Created by smadhur on 4/12/2026.
//

#include "transform.h"

#include "glm/common.hpp"
#include "glm/glm.hpp"
#include "debugger/debugger.h"
#include "glm/ext/matrix_transform.hpp"
#include "glm/trigonometric.hpp"

#include <cmath>

namespace Mercury {
Transform::Transform() : transformations_(0) {
  this->position_ = glm::vec3(0.0f);
  this->interpolated_position_ = glm::vec3(0.0f);
  this->previous_position_ = glm::vec3(0.0f);
  this->velocity_ = glm::vec3(0.0f);
  this->rotation_ = glm::vec3(0.0f);
  this->interpolated_rotation_ = glm::vec3(0.0f);
  this->previous_rotation_ = glm::vec3(0.0f);
  this->local_position_ = glm::vec3(0.0f);
  this->local_rotation_ = glm::vec3(0.0f);
  this->local_scale_ = glm::vec3(1.0f);
}
Transform::Transform(glm::vec3 position) : transformations_(0) {
  this->position_ = position;
  this->previous_position_ = position;
  this->interpolated_position_ = position;
  this->velocity_ = glm::vec3(0.0f);
  this->rotation_ = glm::vec3(0.0f);
  this->interpolated_rotation_ = glm::vec3(0.0f);
  this->previous_rotation_ = glm::vec3(0.0f);
  this->local_position_ = position;
  this->local_rotation_ = glm::vec3(0.0f);
  this->local_scale_ = glm::vec3(1.0f);
}

glm::vec3 Transform::GetPosition() const { return this->position_; }
void Transform::SetPosition(glm::vec3 position) {
  this->position_ = position;
  transformations_.push_back(kTransformPosition);
  dirty_ = true;
  if (on_world_changed_) on_world_changed_();
}

void Transform::SetLocalPosition(glm::vec3 position) {
  this->local_position_ = position;
  dirty_ = true;
  if (on_local_changed_) on_local_changed_();
}

void Transform::SetLocalRotate(glm::vec3 rotation) {
  this->local_rotation_ = rotation;
  dirty_ = true;
  if (on_local_changed_) on_local_changed_();
}

void Transform::SetLocalScale(glm::vec3 scale) {
  this->local_scale_ = scale;
  dirty_ = true;
  if (on_local_changed_) on_local_changed_();
}

void Transform::SetWorldTransformInternal(glm::vec3 pos, glm::vec3 rot,
                                         glm::vec3 scale) {
  this->position_ = pos;
  this->rotation_ = rot;
  this->scale_ = scale;
  transformations_.push_back(kTransformPosition);
  transformations_.push_back(kTransformRotation);
  dirty_ = true;
}

void Transform::SetWorldPositionInternal(glm::vec3 pos) {
  this->position_ = pos;
  transformations_.push_back(kTransformPosition);
  dirty_ = true;
}

void Transform::SetWorldRotationInternal(glm::vec3 rot) {
  this->rotation_ = rot;
  transformations_.push_back(kTransformRotation);
  dirty_ = true;
}

void Transform::SetWorldScaleInternal(glm::vec3 scale) {
  this->scale_ = scale;
  dirty_ = true;
}

void Transform::SetInterpolatedTransformInternal(glm::vec3 pos, glm::vec3 rot,
                                                glm::vec3 scale) {
  interpolated_position_ = pos;
  interpolated_rotation_ = rot;
  interpolated_scale_ = scale;
  if (rot == glm::vec3(0.0f)) {
    rotation_matrix_ = glm::mat4(1.0f);
  } else {
    rotation_matrix_ = glm::mat4_cast(EulerToQuat(rot));
  }
  dirty_ = true;
}

glm::vec3 Transform::GetVelocity() const { return this->velocity_; }
void Transform::SetVelocity(glm::vec3 velocity) {
  this->velocity_ = velocity;
  transformations_.push_back(kTransformLinearVelocity);
}
void Transform::SetRotate(glm::vec3 new_value) {
  this->rotation_ = new_value;
  transformations_.push_back(kTransformRotation);
  dirty_ = true;
  if (on_world_changed_) on_world_changed_();
}
void Transform::ChangeRotate(glm::vec3 delta) {
  rotation_ += delta;
  transformations_.push_back(kTransformRotation);
  dirty_ = true;
  if (on_world_changed_) on_world_changed_();
}

void Transform::SetScale(glm::vec3 scale) {
  this->scale_ = scale;
  dirty_ = true;
  if (on_world_changed_) on_world_changed_();
}
glm::vec3 Transform::GetRotate() const { return this->rotation_; }
glm::vec3 Transform::GetScale() const { return this->scale_; }
glm::vec3 Transform::GetInterpolatedRotation() const {
  return this->interpolated_rotation_;
}
glm::mat4 Transform::GetRotationMatrix() const { return rotation_matrix_; }
void Transform::SetVelocityMagnitude(float) {
  transformations_.push_back(kTransformLinearMagnitude);
}

void Transform::Move(glm::vec3 movement) {
  this->position_ += movement;
  transformations_.push_back(kTransformPosition);
  dirty_ = true;
  if (on_world_changed_) on_world_changed_();
}

glm::vec3 Transform::GetInterpolatedPosition() const {
  return this->interpolated_position_;
}

glm::vec3 Transform::GetInterpolatedScale() const {
  return this->interpolated_scale_;
}

void Transform::Interpolate(float alpha) {
  float a = glm::clamp(alpha, 0.0f, 1.0f);
  interpolated_position_ = glm::mix(previous_position_, position_, a);
  interpolated_rotation_ = glm::mix(previous_rotation_, rotation_, a);
  interpolated_scale_ = glm::mix(previous_scale_, scale_, a);
  // Rebuild the matrix only when what it is built from changed.
  const float matrix_alpha = previous_rotation_ == rotation_ ? 1.0f : a;
  if (previous_rotation_ != matrix_previous_ || rotation_ != matrix_current_ ||
      matrix_alpha != matrix_alpha_) {
    matrix_previous_ = previous_rotation_;
    matrix_current_ = rotation_;
    matrix_alpha_ = matrix_alpha;
    if (previous_rotation_ == glm::vec3(0.0f) &&
        rotation_ == glm::vec3(0.0f)) {
      rotation_matrix_ = glm::mat4(1.0f);  // most entities never rotate
    } else {
      // Slerp rather than mixing angles, so a body whose angle wraps from
      // 179 to -179 degrees turns 2 degrees instead of spinning back.
      rotation_matrix_ = glm::mat4_cast(
          glm::slerp(EulerToQuat(previous_rotation_), EulerToQuat(rotation_),
                     matrix_alpha));
    }
  }
  if (interpolated_position_ != last_built_position_ ||
      interpolated_rotation_ != last_built_rotation_ ||
      interpolated_scale_ != last_built_scale_) {
    dirty_ = true;
  }
}

glm::quat Transform::EulerToQuat(glm::vec3 degrees) {
  return glm::quat(glm::radians(degrees));
}

glm::vec3 Transform::QuatToEuler(glm::quat rotation) {
  glm::vec3 degrees = glm::degrees(glm::eulerAngles(rotation));
  // Tidy values that are 0 up to rounding, so saved maps stay readable.
  for (int i = 0; i < 3; i++) {
    if (std::abs(degrees[i]) < 1e-4f) degrees[i] = 0.0f;
  }
  return degrees;
}

void Transform::CaptureTickSnapshot() {
  previous_position_ = position_;
  previous_rotation_ = rotation_;
  previous_scale_ = scale_;
}
void Transform::SnapInterpolation() {
  CaptureTickSnapshot();
  Interpolate(1.0f);
  dirty_ = true;
}

std::vector<TransformationType> Transform::GetTransformations() {
  auto transformations = transformations_;
  return transformations;
}
void Transform::ClearTransformations() { transformations_.clear(); }
}  // namespace Mercury