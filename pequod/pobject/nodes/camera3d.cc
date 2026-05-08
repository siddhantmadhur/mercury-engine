//
// Created by smadhur on 5/7/2026.
//

#include "camera3d.h"

namespace Pequod {
Camera3D::Camera3D(float& width, float& height, glm::vec3 position)
    : width_(width), height_(height) {
  // Lock in Position
  NewProperty<Transform>(position);
}
}  // namespace Pequod