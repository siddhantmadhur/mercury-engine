#ifndef ROLL_A_BALL_GAME_MANAGER_H
#define ROLL_A_BALL_GAME_MANAGER_H

#include "scripting/behaviour.h"

// Keeps the score and shows it. Escape quits, R restarts after a win.
class GameManager : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("player", player).Tooltip("Node with the PlayerController");
    r.Field("fall_limit", fall_limit)
        .Tooltip("Below this height the player is put back at the start");
  }

  void OnStart() override;
  void OnTick(double dt) override;
  void OnFrame(double dt) override;

  void Collect(int points);

  Mercury::NodeRef player{"Player"};
  float fall_limit = -10.0f;

 private:
  int score_ = 0;
  int total_ = 0;
  glm::vec3 start_position_ = glm::vec3(0.0f);
};

#endif  // ROLL_A_BALL_GAME_MANAGER_H
