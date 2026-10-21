#ifndef PONG_PLAYER_CONTROLLER_H
#define PONG_PLAYER_CONTROLLER_H

#include "scripting/behaviour.h"

class Paddle;

// Moves the Paddle on the same node with the keyboard.
class PlayerController : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("speed", speed).Tooltip("World units per second");
    r.Field("up_key", up_key).Tooltip("Key name, e.g. W or Up");
    r.Field("down_key", down_key);
  }

  void OnStart() override;
  void OnTick(double dt) override;

  float speed = 70.0f;
  std::string up_key = "W";
  std::string down_key = "S";

 private:
  Paddle* paddle_ = nullptr;
  Mercury::Key up_ = GLFW_KEY_UNKNOWN;
  Mercury::Key down_ = GLFW_KEY_UNKNOWN;
};

#endif  // PONG_PLAYER_CONTROLLER_H
