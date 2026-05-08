//
// Created by smadhur on 4/12/2026.
//

#include "transform.h"

#include "debugger/debugger.h"

namespace Pequod {
Transform::Transform() {
  this->position_ = glm::vec3(0.0f);
  this->interpolated_position_ = glm::vec3(0.0f);
  this->velocity_ = glm::vec3(0.0f);
}
Transform::Transform(glm::vec3 position) { SetPosition(position); }

glm::vec3 Transform::GetPosition() const { return this->position_; }
void Transform::SetPosition(glm::vec3 position) {
  this->position_ = position;
  this->interpolated_position_ = position;
  transformations_.push_back(kTransformPosition);
}

glm::vec3 Transform::GetVelocity() const { return this->velocity_; }
void Transform::SetVelocity(glm::vec3 velocity) {
  this->velocity_ = velocity;
  transformations_.push_back(kTransformLinearVelocity);
}
void Transform::SetRotate(glm::vec3 new_value) { this->rotation_ = new_value; }
void Transform::ChangeRotate(glm::vec3 delta) { rotation_ += delta; }
glm::vec3 Transform::GetRotate() const { return this->rotation_; }
void Transform::SetVelocityMagnitude(float) {
  transformations_.push_back(kTransformLinearMagnitude);
}

void Transform::Move(glm::vec3 movement) { this->position_ += movement; }

glm::vec3 Transform::GetInterpolatedPosition() const {
  return this->interpolated_position_;
}

void Transform::InterpolatePosition(float delta_t, float tick_t) {
  if (delta_t == 0 || tick_t == 0) {
    return;
  }
  /*
   * The goal is to smooth out position between ticks
   * If FPS is 240 and TPS is 20 there are 240/20 -> 12 frames between every
   * tick Tick 1 <--> Tick 2 If there
   */
  interpolated_position_ +=
      (position_ - interpolated_position_) * (delta_t / tick_t);
}
std::vector<TransformationType> Transform::GetTransformations() {
  auto transformations = transformations_;
  return transformations;
}
void Transform::ClearTransformations() { transformations_.clear(); }
}  // namespace Pequod