//
// Created by smadhur on 4/29/2026.
//

#include "camera2d.h"

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "properties/transform.h"

namespace Pequod {

Camera2D::Camera2D(float& width, float& height, glm::vec3 position)
    : width_(width), height_(height) {
  Add<Transform>(position);
}

glm::mat4 Camera2D::GetProjection() {
  glm::mat4 proj = glm::ortho<float>(-width_ / 2, width_ / 2, -height_ / 2,
                                     height_ / 2, -1.0f, 1.0f);
  return proj;
}

void Camera2D::SetZoom(float zoom) { this->zoom_ = zoom; }
float Camera2D::GetZoom() { return this->zoom_; }

glm::mat4 Camera2D::GetView() {
  auto transform = Get<Transform>();
  auto pos = transform->GetPosition();
  glm::mat4 view = glm::scale(glm::mat4(1.0f), glm::vec3(zoom_, zoom_, 1.0f));
  view = glm::translate(view, glm::vec3(-pos.x, -pos.y, 0.0f));
  return view;
}

}  // namespace Pequod