#include "ball.h"

#include <cmath>
#include <cstdlib>
#include <ctime>

MERCURY_SCRIPT(Ball);

void Ball::OnStart() {
  srand(time(NULL));
  Serve();
}

void Ball::Serve() {
  if (served_this_tick_) return;
  served_this_tick_ = true;
  PDebug::log("Starting new round...");

  const int spread = static_cast<int>(serve_angle * 2.0f);
  float degree = spread > 0 ? static_cast<float>(rand() % spread) - serve_angle
                            : 0.0f;
  float side = (rand() % 2 == 0) ? 1.0f : -1.0f;
  auto direction = glm::vec3(side * glm::cos(glm::radians(degree)),
                             glm::sin(glm::radians(degree)), 0.0f);

  auto& transform = GetTransform();
  transform.SetPosition(glm::vec3(0.0f));
  transform.SetVelocity(direction * speed);
}

void Ball::OnTick(double dt) {
  // A velocity set this tick overrides that tick's bounces, so leave a fresh
  // serve alone.
  if (served_this_tick_) {
    served_this_tick_ = false;
    return;
  }

  auto& transform = GetTransform();
  auto vel = transform.GetVelocity();
  const float current = glm::length(vel);
  if (current <= 0.0f) return;

  auto dir = vel / current;
  bool correct = std::abs(current - speed) > 1.0f;
  if (std::abs(dir.x) < min_horizontal) {
    dir.x = std::copysign(min_horizontal, dir.x == 0.0f ? 1.0f : dir.x);
    dir.y = std::copysign(std::sqrt(1.0f - min_horizontal * min_horizontal),
                          dir.y);
    correct = true;
  }
  if (correct) transform.SetVelocity(dir * speed);
}
