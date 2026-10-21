#include "player_controller.h"

#include <cmath>

MERCURY_SCRIPT(PlayerController);

void PlayerController::OnTick(double dt) {
  glm::vec3 direction(0.0f);
  // The camera looks down -Z, so "up" on screen is -Z.
  if (IsKeyPressed(GLFW_KEY_W) || IsKeyPressed(GLFW_KEY_UP)) direction.z -= 1;
  if (IsKeyPressed(GLFW_KEY_S) || IsKeyPressed(GLFW_KEY_DOWN)) direction.z += 1;
  if (IsKeyPressed(GLFW_KEY_A) || IsKeyPressed(GLFW_KEY_LEFT)) direction.x -= 1;
  if (IsKeyPressed(GLFW_KEY_D) || IsKeyPressed(GLFW_KEY_RIGHT)) direction.x += 1;
  if (direction != glm::vec3(0.0f)) {
    Physics().AddForce(GetEntity(), glm::normalize(direction) * force);
  }

  // Only jump when the ball is resting on something.
  const auto& transform = GetTransform();
  const bool grounded = std::abs(transform.GetVelocity().y) < 0.05f;
  if (IsKeyJustPressed(GLFW_KEY_SPACE) && grounded) {
    Physics().AddImpulse(GetEntity(), glm::vec3(0.0f, jump_impulse, 0.0f));
  }
}
