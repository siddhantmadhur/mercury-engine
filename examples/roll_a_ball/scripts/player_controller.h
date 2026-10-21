#ifndef ROLL_A_BALL_PLAYER_CONTROLLER_H
#define ROLL_A_BALL_PLAYER_CONTROLLER_H

#include "scripting/behaviour.h"

// Rolls the ball by pushing it with W/A/S/D or the arrow keys.
class PlayerController : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("force", force).Tooltip("Push in newtons while a key is held");
    r.Field("jump_impulse", jump_impulse)
        .Tooltip("Upward push when Space is pressed on the ground");
  }

  void OnTick(double dt) override;

  float force = 12.0f;
  float jump_impulse = 5.0f;
};

#endif  // ROLL_A_BALL_PLAYER_CONTROLLER_H
