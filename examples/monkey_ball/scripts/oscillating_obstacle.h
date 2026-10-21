#ifndef MONKEY_BALL_OSCILLATING_OBSTACLE_H
#define MONKEY_BALL_OSCILLATING_OBSTACLE_H

#include "scripting/behaviour.h"

class OscillatingObstacle : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("move_axis", move_axis).Tooltip("Direction of translation");
    r.Field("distance", distance).Tooltip("Half-distance of movement");
    r.Field("speed", speed).Tooltip("Oscillation frequency in radians/sec");
  }

  void OnStart() override;
  void OnTick(double dt) override;

  glm::vec3 move_axis = glm::vec3(1.0f, 0.0f, 0.0f);
  float distance = 2.5f;
  float speed = 2.0f;

 private:
  glm::vec3 origin_{0.0f};
  float time_ = 0.0f;
};

#endif  // MONKEY_BALL_OSCILLATING_OBSTACLE_H
