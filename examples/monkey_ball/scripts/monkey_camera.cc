#include "monkey_camera.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

MERCURY_SCRIPT(MonkeyCamera);

void MonkeyCamera::OnStart() {
  target_object_ = Get(target);
  if (target_object_) {
    const glm::vec3 ball_pos = target_object_.GetTransform()->GetPosition();
    const float yaw_rad = glm::radians(yaw);
    const glm::vec3 offset(std::sin(yaw_rad) * distance, height,
                           std::cos(yaw_rad) * distance);
    GetTransform().SetPosition(ball_pos + offset);
    GetTransform().SetRotate(glm::vec3(pitch, yaw, 0.0f));
  }
}

void MonkeyCamera::OnTick(double dt) {
  if (!target_object_) {
    target_object_ = Get(target);
    if (!target_object_) return;
  }

  // Camera orbit controls with Q / E.
  if (IsKeyPressed(GLFW_KEY_Q)) {
    yaw -= turn_speed * static_cast<float>(dt);
  }
  if (IsKeyPressed(GLFW_KEY_E)) {
    yaw += turn_speed * static_cast<float>(dt);
  }
  yaw = std::fmod(yaw, 360.0f);

  const glm::vec3 ball_pos = target_object_.GetTransform()->GetPosition();
  const float yaw_rad = glm::radians(yaw);
  const glm::vec3 offset(std::sin(yaw_rad) * distance, height,
                         std::cos(yaw_rad) * distance);
  const glm::vec3 target_pos = ball_pos + offset;

  // Smooth lerp to target position.
  const float factor = std::min(1.0f, smooth_speed * static_cast<float>(dt));
  const glm::vec3 current_pos = GetTransform().GetPosition();
  const glm::vec3 new_pos = glm::mix(current_pos, target_pos, factor);

  GetTransform().SetPosition(new_pos);
  GetTransform().SetRotate(glm::vec3(pitch, yaw, 0.0f));
}
