#ifndef PONG_PONG_GAME_H
#define PONG_PONG_GAME_H

#include "goal.h"
#include "scripting/behaviour.h"

class Ball;

// Keeps the score (shown by ui/scoreboard.uxml) and quits on Escape.
class PongGame : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("ball", ball);
    r.Field("show_debug", show_debug).Tooltip("Show the ball's position and velocity");
  }

  // The scoreboard binds to these.
  void ReflectData(Mercury::Reflector& r) override {
    r.Field("player_wins", player_wins_);
    r.Field("enemy_wins", enemy_wins_);
  }

  void OnStart() override;
  void OnFrame(double dt) override;
  void OnTick(double dt) override;

  // Awards a point and serves again. Only one point counts per tick.
  void Score(Side scorer);

  Mercury::NodeRef ball{"Ball"};
  bool show_debug = true;

 private:
  Ball* ball_ = nullptr;
  int player_wins_ = 0;
  int enemy_wins_ = 0;
  bool scored_this_tick_ = false;
};

#endif  // PONG_PONG_GAME_H
