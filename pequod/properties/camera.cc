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

  glm::mat4 proj = glm::perspective(glm::radians(fov_), aspect, 0.1f, 1000.0f);

#endif

  return proj;
}

void Camera::SetZoom(float zoom) { this->zoom_ = zoom; }
float Camera::GetZoom() { return this->zoom_; }

static glm::vec3 DirectionFromEuler(float pitch_deg, float yaw_deg) {
  glm::vec3 direction;
  float yaw = glm::radians(yaw_deg);
  float pitch = glm::radians(pitch_deg);
  direction.x = cos(yaw) * cos(pitch);
  direction.y = sin(pitch);
  direction.z = sin(yaw) * cos(pitch);
  return glm::normalize(direction);
}

glm::vec3 Camera::GetDirection(const Transform& t) const {
  auto rot = t.GetRotate();
  return DirectionFromEuler(rot.x, rot.y);
}

glm::vec3 Camera::GetInterpolatedDirection(const Transform& t) const {
  auto rot = t.GetInterpolatedRotation();
  return DirectionFromEuler(rot.x, rot.y);
}

void Camera::SetFOV(float fov) { this->fov_ = fov; }

glm::vec3 Camera::GetRayFromScreen(glm::vec2 cursor, glm::vec2 res,
                                   const Transform& t) {
  auto mouse = glm::vec2((2.0f * cursor.x) / res.x - 1.0f,
                         1.0 - (2.0f * cursor.y) / res.y);

  glm::vec4 ray_start_ndc(mouse.x, mouse.y, -1.0f, 1.0f);
  glm::vec4 ray_end_ndc(mouse.x, mouse.y, 1.0f, 1.0f);

  glm::mat4 inv_vp = glm::inverse(GetProjection(res.x, res.y) * GetView(t));

  glm::vec4 ray_start_world = inv_vp * ray_start_ndc;
  ray_start_world /= ray_start_world.w;

  glm::vec4 ray_end_world = inv_vp * ray_end_ndc;
  ray_end_world /= ray_end_world.w;

  glm::vec3 ray_dir =
      glm::normalize(glm::vec3(ray_end_world) - glm::vec3(ray_start_world));
  return ray_dir;
}

glm::mat4 Camera::GetView(const Transform& t) {
  glm::vec3 position = t.GetInterpolatedPosition();
#ifdef TOPDOWN_2D_VIEW
  glm::mat4 view = glm::scale(glm::mat4(1.0f), glm::vec3(zoom_, zoom_, 1.0f));
  view = glm::translate(view, glm::vec3(-position.x, -position.y, 0.0));
#else

  glm::vec3 cameraFront = GetInterpolatedDirection(t);

  glm::vec3 cameraPos(position);
  glm::vec3 cameraUp(0.0, 1.0, 0.0);

  glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
#endif

  return view;
}

}  // namespace Pequod
