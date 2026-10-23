#ifndef MONKEY_BALL_BANANA_PICKUP_H
#define MONKEY_BALL_BANANA_PICKUP_H

#include "scripting/behaviour.h"

class BananaPickup : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("points", points).Tooltip("Score awarded upon collection");
    r.Field("rotate_speed", rotate_speed).Tooltip("Rotation speed in degrees/sec");
    r.Field("game_manager", game_manager).Tooltip("Node with GameManager script");
  }

  void OnTick(double dt) override;
  void OnTriggerEnter(Mercury::GameObject other) override;

  int points = 100;
  float rotate_speed = 90.0f;
  Mercury::NodeRef game_manager{"GameManager"};

 private:
  bool collected_ = false;
};

#endif  // MONKEY_BALL_BANANA_PICKUP_H
