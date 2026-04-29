//
// Created by smadhur on 4/29/2026.
//

#ifndef PEQUOD_ENGINE_CAMERA_H
#define PEQUOD_ENGINE_CAMERA_H
#include <glm/glm.hpp>
#include "pobject/pobject.h"

namespace Pequod {
class Camera2D : public PObject {
 public:
  Camera2D(float& width_, float& height_, glm::vec3 position = glm::vec3(0.0f));
  glm::mat4 GetView();
  glm::mat4 GetProjection();
  void SetZoom(float zoom);

 private:
  glm::mat4 view_{};
  glm::mat4 proj_{};
  float zoom_ = 1.0f;
  float& width_;
  float& height_;
};
}  // namespace Pequod

#endif  // PEQUOD_ENGINE_CAMERA_H
