//
// Created by smadhur on 5/8/2026.
//

#include "camera.h"

#include <glm/glm.hpp>

#include "imgui.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

namespace Pequod {
Camera::Camera() {}

Camera::~Camera() {}

glm::mat4 Camera::GetProjection(float width, float height) {
#ifdef TOPDOWN_2D_VIEW
  glm::mat4 proj = glm::ortho<float>(-width / 2, width / 2, -height / 2,
                                     height / 2, -1.0f, 1.0f);
#else
  float size = 1.0f;
  float aspect = width / height;

  float fov = 50.0f;
  glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, 0.1f, 1000.0f);

#endif

  return proj;
}

void Camera::SetZoom(float zoom) { this->zoom_ = zoom; }
float Camera::GetZoom() { return this->zoom_; }
glm::vec3 Camera::GetDirection() {
  glm::vec3 direction;
  direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
  direction.y = sin(glm::radians(pitch));
  direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
  return glm::normalize(direction);
}
void Camera::SetPitch(float new_val) { this->pitch = new_val; }
void Camera::SetYaw(float new_val) { this->yaw = new_val; }
void Camera::UpdateYaw(float new_val) { this->yaw += new_val; }
void Camera::UpdatePitch(float new_val) { this->pitch += new_val; }

glm::mat4 Camera::GetView(glm::vec3 position) {
#ifdef TOPDOWN_2D_VIEW
  glm::mat4 view = glm::scale(glm::mat4(1.0f), glm::vec3(zoom_, zoom_, 1.0f));
  view = glm::translate(view, glm::vec3(-position.x, -position.y, 0.0));
#else

  glm::vec3 cameraFront = GetDirection();

  glm::vec3 cameraPos(position);
  glm::vec3 cameraUp(0.0, 1.0, 0.0);

  glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
#endif

  return view;
}

}  // namespace Pequod
