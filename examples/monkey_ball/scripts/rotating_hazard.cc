#include "rotating_hazard.h"

#include <cmath>

MERCURY_SCRIPT(RotatingHazard);

void RotatingHazard::OnTick(double dt) {
  auto& transform = GetTransform();
  glm::vec3 rot = transform.GetRotate() + degrees_per_second * static_cast<float>(dt);
  rot.x = std::fmod(rot.x, 360.0f);
  rot.y = std::fmod(rot.y, 360.0f);
  rot.z = std::fmod(rot.z, 360.0f);
  transform.SetRotate(rot);
}
