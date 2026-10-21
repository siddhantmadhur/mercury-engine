#ifndef PONG_PADDLE_H
#define PONG_PADDLE_H

#include "scripting/behaviour.h"

/**
 * @brief Drives a kinematic paddle vertically, stopping it at the edges
 *
 * Kinematic bodies do not collide with the static walls, so the limit is
 * enforced here. PlayerController and EnemyAI call Move.
 */
class Paddle : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("limit", limit).Tooltip("Furthest the paddle's center may go from y = 0");
  }

  void Move(float velocity_y);

  float limit = 40.0f;
};

#endif  // PONG_PADDLE_H
