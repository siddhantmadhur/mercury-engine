//
// Created by smadhur on 5/8/2026.
//

#ifndef PEQUOD_ENGINE_CAMERA_H
#define PEQUOD_ENGINE_CAMERA_H
#include "property.h"
#include "transform.h"
#include <glm/glm.hpp>

namespace Pequod {
class Camera : public Property {
 public:
  Camera();
  ~Camera();

  // The camera reads its orientation (pitch, yaw, roll) from the entity's
  // Transform — rotation_.x is pitch, rotation_.y is yaw, rotation_.z is roll,
  // all in degrees. Position likewise comes from the Transform.
  glm::mat4 GetView(const Transform& t);
  glm::mat4 GetProjection(float width, float height);
  void SetZoom(float zoom);
  float GetZoom();
  glm::vec3 GetDirection(const Transform& t) const;
  glm::vec3 GetInterpolatedDirection(const Transform& t) const;

  void SetFOV(float);

  glm::vec3 GetRayFromScreen(glm::vec2 cursor, glm::vec2 res,
                             const Transform& t);

 private:
  glm::mat4 view_{};
  glm::mat4 proj_{};

  float fov_ = 60.0f;

  float zoom_ = 1.0f;
};
}  // namespace Pequod

#endif  // PEQUOD_ENGINE_CAMERA_H
