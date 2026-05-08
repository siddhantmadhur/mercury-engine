//
// Created by smadhur on 5/7/2026.
//

#ifndef PEQUOD_ENGINE_CAMERA3D_H
#define PEQUOD_ENGINE_CAMERA3D_H
#include "pobject/pobject.h"

namespace Pequod {
class Camera3D : public PObject {
  Camera3D(float& width_, float& height_, glm::vec3 position = glm::vec3(0.0f));
  glm::mat4 GetView();
  glm::mat4 GetProjection();

 private:
  glm::mat4 view_{};
  glm::mat4 proj_{};
  float zoom_ = 1.0f;
  float& width_;
  float& height_;
};
}  // namespace Pequod

#endif  // PEQUOD_ENGINE_CAMERA3D_H
