#include "player_controller.h"

#include "paddle.h"

MERCURY_SCRIPT(PlayerController);

void PlayerController::OnStart() {
  paddle_ = GetBehaviour<Paddle>();
  if (!paddle_) PDebug::error("PlayerController needs a Paddle on the same node");
  up_ = Mercury::InputManager::KeyFromName(up_key);
  down_ = Mercury::InputManager::KeyFromName(down_key);
}

void PlayerController::OnTick(double dt) {
  if (!paddle_) return;
  float direction = 0.0f;
  if (IsKeyPressed(up_)) direction += 1.0f;
  if (IsKeyPressed(down_)) direction -= 1.0f;
  paddle_->Move(direction * speed);
}
