#ifndef ROLL_A_BALL_PLAYER_CONTROLLER_H
#define ROLL_A_BALL_PLAYER_CONTROLLER_H

#include <optional>

#include "game_input.h"
#include "scripting/behaviour.h"

// Rolls the ball with the Player/Move action (W/A/S/D, the arrow keys or the
// left stick) and jumps with Player/Jump; see input/actions.minput.
class PlayerController : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("force", force).Tooltip("Push in newtons at full Move input");
    r.Field("jump_impulse", jump_impulse)
        .Tooltip("Upward push when Jump is pressed on the ground");
  }

  void OnStart() override;
  void OnFrame(double dt) override;
  void OnTick(double dt) override;

  float force = 12.0f;
  float jump_impulse = 5.0f;

 private:
  std::optional<GameInput> input_;
  // Actions update once per frame, and a frame can run zero or several
  // ticks, so a press is remembered until the next tick uses it.
  bool jump_requested_ = false;
};

#endif  // ROLL_A_BALL_PLAYER_CONTROLLER_H
