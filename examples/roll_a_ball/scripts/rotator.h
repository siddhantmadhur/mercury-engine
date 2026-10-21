#ifndef ROLL_A_BALL_ROTATOR_H
#define ROLL_A_BALL_ROTATOR_H

#include "scripting/behaviour.h"

// Spins the node at a constant rate.
class Rotator : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("degrees_per_second", degrees_per_second);
  }

  void OnTick(double dt) override {
    auto& transform = GetTransform();
    transform.SetRotate(glm::mod(
        transform.GetRotate() + degrees_per_second * static_cast<float>(dt),
        360.0f));
  }

  glm::vec3 degrees_per_second = glm::vec3(15.0f, 30.0f, 45.0f);
};

#endif  // ROLL_A_BALL_ROTATOR_H
