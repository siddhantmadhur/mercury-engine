#include "oscillating_obstacle.h"

#include <cmath>

MERCURY_SCRIPT(OscillatingObstacle);

void OscillatingObstacle::OnStart() {
  origin_ = GetTransform().GetPosition();
}

void OscillatingObstacle::OnTick(double dt) {
  time_ += static_cast<float>(dt);
  const float offset = std::sin(time_ * speed) * distance;
  GetTransform().SetPosition(origin_ + move_axis * offset);
}
