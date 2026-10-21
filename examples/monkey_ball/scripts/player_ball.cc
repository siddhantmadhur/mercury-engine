#include "player_ball.h"

#include <algorithm>
#include <cmath>

#include "banana_pickup.h"
#include "goal_trigger.h"

MERCURY_SCRIPT(PlayerBall);

void PlayerBall::OnStart() {
  contact_count_ = 0;
  grounded_ = true;
  jump_cooldown_ = 0.0f;
}

void PlayerBall::ResetPhysics() {
  contact_count_ = 0;
  grounded_ = true;
  jump_cooldown_ = 0.0f;
  GetTransform().SetVelocity(glm::vec3(0.0f));
  Physics().SetAngularVelocity(GetEntity(), glm::vec3(0.0f));
}

void PlayerBall::OnTick(double dt) {
  jump_cooldown_ = std::max(0.0f, jump_cooldown_ - static_cast<float>(dt));

  const auto& transform = GetTransform();
  const glm::vec3 vel = transform.GetVelocity();
  const glm::vec3 pos = transform.GetPosition();

  // Ground detection: either resting contact or negligible vertical movement.
  grounded_ = (contact_count_ > 0) || (std::abs(vel.y) < 0.2f);

  // Determine movement axes relative to camera look direction.
  glm::vec3 forward(0.0f, 0.0f, -1.0f);
  if (auto cam = Get(camera_node)) {
    glm::vec3 to_ball = pos - cam.GetTransform()->GetPosition();
    to_ball.y = 0.0f;
    if (glm::length(to_ball) > 1e-3f) {
      forward = glm::normalize(to_ball);
    }
  }
  const glm::vec3 right = glm::vec3(-forward.z, 0.0f, forward.x);

  // Gather directional input.
  glm::vec3 input_dir(0.0f);
  if (IsKeyPressed(GLFW_KEY_W) || IsKeyPressed(GLFW_KEY_UP)) input_dir += forward;
  if (IsKeyPressed(GLFW_KEY_S) || IsKeyPressed(GLFW_KEY_DOWN)) input_dir -= forward;
  if (IsKeyPressed(GLFW_KEY_A) || IsKeyPressed(GLFW_KEY_LEFT)) input_dir -= right;
  if (IsKeyPressed(GLFW_KEY_D) || IsKeyPressed(GLFW_KEY_RIGHT)) input_dir += right;

  if (glm::length(input_dir) > 1e-3f) {
    input_dir = glm::normalize(input_dir);
    Physics().AddForce(GetEntity(), input_dir * roll_force);

    // Active steering & braking: if horizontal velocity opposes input, apply snappy counter-force.
    const glm::vec2 horiz_vel(vel.x, vel.z);
    const glm::vec2 horiz_input(input_dir.x, input_dir.z);
    if (glm::dot(horiz_vel, horiz_input) < 0.0f) {
      Physics().AddForce(GetEntity(), input_dir * (roll_force * 0.7f));
    }
  }

  // Soft speed damping if player rolls faster than max_speed.
  const float horiz_speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
  if (horiz_speed > max_speed) {
    const float excess = horiz_speed - max_speed;
    const glm::vec3 brake(-vel.x / horiz_speed * excess * 5.0f, 0.0f,
                          -vel.z / horiz_speed * excess * 5.0f);
    Physics().AddForce(GetEntity(), brake);
  }

  // Jump execution.
  if (IsKeyJustPressed(GLFW_KEY_SPACE) && grounded_ && jump_cooldown_ <= 0.0f) {
    Physics().AddImpulse(GetEntity(), glm::vec3(0.0f, jump_impulse, 0.0f));
    grounded_ = false;
    jump_cooldown_ = 0.25f;
  }
}

void PlayerBall::OnCollisionEnter(Mercury::GameObject other) {
  if (other.GetBehaviour<BananaPickup>() || other.GetBehaviour<GoalTrigger>()) {
    return;
  }
  contact_count_++;
}

void PlayerBall::OnCollisionLeave(Mercury::GameObject other) {
  if (other.GetBehaviour<BananaPickup>() || other.GetBehaviour<GoalTrigger>()) {
    return;
  }
  contact_count_ = std::max(0, contact_count_ - 1);
}
