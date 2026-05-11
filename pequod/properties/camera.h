//
// Created by smadhur on 5/8/2026.
//

#ifndef PEQUOD_ENGINE_CAMERA_H
#define PEQUOD_ENGINE_CAMERA_H
#include "property.h"
#include <glm/glm.hpp>

namespace Pequod {
class Camera : public Property {
 public:
  Camera();
  ~Camera();
  glm::mat4 GetView(glm::vec3);
  glm::mat4 GetProjection(float width, float height);
  void SetZoom(float zoom);
  float GetZoom();
  glm::vec3 GetDirection();

  void SetYaw(float);
  void UpdateYaw(float);
  void SetPitch(float);
  void UpdatePitch(float);

 private:
  glm::mat4 view_{};
  glm::mat4 proj_{};
  float yaw = 45.0f;
  float pitch = -30.0f;

  float coord[3] = {0.0, 100.0, 0.0};
  float zoom_ = 1.0f;
};
}  // namespace Pequod

#endif  // PEQUOD_ENGINE_CAMERA_H
