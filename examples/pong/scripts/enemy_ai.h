#ifndef PONG_ENEMY_AI_H
#define PONG_ENEMY_AI_H

#include "scripting/behaviour.h"

class Paddle;

// Chases the ball with the Paddle on the same node, capped at `speed` so it
// can lose.
class EnemyAI : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("ball", ball);
    r.Field("speed", speed).Tooltip("Keep it below the ball's speed");
    r.Field("gain", gain).Tooltip("How hard it steers towards the ball");
  }

  void OnStart() override;
  void OnTick(double dt) override;

  Mercury::NodeRef ball{"Ball"};
  float speed = 45.0f;
  float gain = 8.0f;

 private:
  Paddle* paddle_ = nullptr;
  Mercury::GameObject ball_;
};

#endif  // PONG_ENEMY_AI_H
