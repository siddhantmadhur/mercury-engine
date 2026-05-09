//
// Created by smadhur on 5/8/2026.
//

#include "camera.h"

#include <glm/glm.hpp>
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

namespace Pequod {

Camera::Camera() {}
Camera::~Camera() {}

glm::mat4 Camera::GetProjection(float width, float height) {
  glm::mat4 proj = glm::ortho<float>(-width / 2, width / 2, -height / 2,
                                     height / 2, -1.0f, 1.0f);
  return proj;
}

void Camera::SetZoom(float zoom) { this->zoom_ = zoom; }
float Camera::GetZoom() { return this->zoom_; }

glm::mat4 Camera::GetView(glm::vec3 position) {
  glm::mat4 view = glm::scale(glm::mat4(1.0f), glm::vec3(zoom_, zoom_, 1.0f));
  view = glm::translate(view, glm::vec3(-position.x, -position.y, 0.0));
  return view;
}

}  // namespace Pequod
