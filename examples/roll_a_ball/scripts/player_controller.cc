#include "player_controller.h"

#include <cmath>

MERCURY_SCRIPT(PlayerController);

void PlayerController::OnStart() {
  if (auto* actions = GetInputActions()) input_.emplace(*actions);
}

void PlayerController::OnFrame(double dt) {
  if (input_ && input_->Player.Jump->WasPressedThisFrame()) jump_requested_ = true;
}

void PlayerController::OnTick(double dt) {
  if (!input_) return;
  // The camera looks down -Z, so "up" on screen (Move's +Y) is -Z.
  const glm::vec2 move = input_->Player.Move->ReadValue<glm::vec2>();
  const glm::vec3 direction(move.x, 0.0f, -move.y);
  if (direction != glm::vec3(0.0f)) {
    Physics().AddForce(GetEntity(), direction * force);
  }

  // Only jump when the ball is resting on something.
  const auto& transform = GetTransform();
  const bool grounded = std::abs(transform.GetVelocity().y) < 0.05f;
  if (jump_requested_ && grounded) {
    Physics().AddImpulse(GetEntity(), glm::vec3(0.0f, jump_impulse, 0.0f));
  }
  jump_requested_ = false;
}
