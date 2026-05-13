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
  float size = 40.0f;
  float aspect = width / height;

  float fov = 45.0f;
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
float Camera::GetPitch() { return this->pitch; }
float Camera::GetYaw() { return this->yaw; }
void Camera::SetYaw(float new_val) { this->yaw = new_val; }
void Camera::UpdateYaw(float new_val) { this->yaw += new_val; }
void Camera::UpdatePitch(float new_val) { this->pitch += new_val; }
glm::vec3 Camera::GetRayFromScreen(glm::vec2 cursor, glm::vec2 res,
                                   glm::vec3 pos) {
  auto mouse = glm::vec2((2.0f * cursor.x) / res.x - 1.0f,
                         1.0 - (2.0f * cursor.y) / res.y);

  glm::vec4 ray_start_ndc(mouse.x, mouse.y, -1.0f, 1.0f);
  glm::vec4 ray_end_ndc(mouse.x, mouse.y, 1.0f, 1.0f);

  glm::mat4 inv_vp = glm::inverse(GetProjection(res.x, res.y) * GetView(pos));

  glm::vec4 ray_start_world = inv_vp * ray_start_ndc;
  ray_start_world /= ray_start_world.w;

  glm::vec4 ray_end_world = inv_vp * ray_end_ndc;
  ray_end_world /= ray_end_world.w;

  glm::vec3 ray_dir =
      glm::normalize(glm::vec3(ray_end_world) - glm::vec3(ray_start_world));
  // glm::vec3 ray_origin = glm::vec3(ray_start_world) * 1.0f;
  return ray_dir;
}

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
