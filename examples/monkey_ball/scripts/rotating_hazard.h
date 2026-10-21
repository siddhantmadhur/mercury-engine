#ifndef MONKEY_BALL_ROTATING_HAZARD_H
#define MONKEY_BALL_ROTATING_HAZARD_H

#include "scripting/behaviour.h"

class RotatingHazard : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("degrees_per_second", degrees_per_second)
        .Tooltip("Euler rotation speed in degrees per second");
  }

  void OnTick(double dt) override;

  glm::vec3 degrees_per_second = glm::vec3(0.0f, 60.0f, 0.0f);
};

#endif  // MONKEY_BALL_ROTATING_HAZARD_H
