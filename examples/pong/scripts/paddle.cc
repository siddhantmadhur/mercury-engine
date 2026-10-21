#include "paddle.h"

#include <algorithm>

MERCURY_SCRIPT(Paddle);

void Paddle::Move(float velocity_y) {
  auto& transform = GetTransform();
  auto pos = transform.GetPosition();
  if (pos.y > limit || pos.y < -limit) {
    pos.y = std::clamp(pos.y, -limit, limit);
    transform.SetPosition(pos);
  }
  if ((pos.y >= limit && velocity_y > 0.0f) ||
      (pos.y <= -limit && velocity_y < 0.0f)) {
    velocity_y = 0.0f;
  }
  transform.SetVelocity(glm::vec3(0.0f, velocity_y, 0.0f));
}
