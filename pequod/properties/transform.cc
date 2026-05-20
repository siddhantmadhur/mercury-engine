//
// Created by smadhur on 4/12/2026.
//

#include "transform.h"

#include "glm/common.hpp"
#include "glm/glm.hpp"
#include "debugger/debugger.h"
#include "glm/ext/matrix_transform.hpp"
#include "glm/trigonometric.hpp"

namespace Pequod {
Transform::Transform() : transformations_(0) {
  this->position_ = glm::vec3(0.0f);
  this->interpolated_position_ = glm::vec3(0.0f);
  this->previous_position_ = glm::vec3(0.0f);
  this->velocity_ = glm::vec3(0.0f);
  this->rotation_ = glm::vec3(0.0f);
  this->interpolated_rotation_ = glm::vec3(0.0f);
  this->previous_rotation_ = glm::vec3(0.0f);
}
Transform::Transform(glm::vec3 position) : transformations_(0) {
  this->position_ = position;
  this->previous_position_ = position;
  this->interpolated_position_ = position;
  this->velocity_ = glm::vec3(0.0f);
  this->rotation_ = glm::vec3(0.0f);
  this->interpolated_rotation_ = glm::vec3(0.0f);
  this->previous_rotation_ = glm::vec3(0.0f);
}

glm::vec3 Transform::GetPosition() const { return this->position_; }
void Transform::SetPosition(glm::vec3 position) {
  this->position_ = position;
  transformations_.push_back(kTransformPosition);
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
}
void Transform::ChangeRotate(glm::vec3 delta) {
  rotation_ += delta;
  transformations_.push_back(kTransformRotation);
  dirty_ = true;
}

void Transform::SetScale(glm::vec3 scale) {
  this->scale_ = scale;
  dirty_ = true;
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
  dirty_ = true;
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
  if (interpolated_rotation_ != last_built_rotation_) {
    // Identity fast path — most entities don't rotate at all, skip three
    // glm::rotate calls (each costs a sin/cos + 4x4 multiply).
    if (interpolated_rotation_ == glm::vec3(0.0f)) {
      rotation_matrix_ = glm::mat4(1.0f);
    } else {
      constexpr float kConv = 360.0f;
      glm::mat4 m(1.0f);
      m = glm::rotate(m, glm::radians(interpolated_rotation_.x * kConv),
                      glm::vec3(1.0f, 0.0f, 0.0f));
      m = glm::rotate(m, glm::radians(interpolated_rotation_.y * kConv),
                      glm::vec3(0.0f, 1.0f, 0.0f));
      m = glm::rotate(m, glm::radians(interpolated_rotation_.z * kConv),
                      glm::vec3(0.0f, 0.0f, 1.0f));
      rotation_matrix_ = m;
    }
  }
  if (interpolated_position_ != last_built_position_ ||
      interpolated_rotation_ != last_built_rotation_ ||
      interpolated_scale_ != last_built_scale_) {
    dirty_ = true;
  }
}

void Transform::CaptureTickSnapshot() {
  previous_position_ = position_;
  previous_rotation_ = rotation_;
  previous_scale_ = scale_;
}
std::vector<TransformationType> Transform::GetTransformations() {
  auto transformations = transformations_;
  return transformations;
}
void Transform::ClearTransformations() { transformations_.clear(); }
}  // namespace Pequod