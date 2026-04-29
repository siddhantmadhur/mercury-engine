#ifndef MERCURY_CAMERA_IMPL_HH
#define MERCURY_CAMERA_IMPL_HH

#include <glm/glm.hpp>

namespace Pequod {
class Camera {
 public:
  Camera(float aspect_ratio);

  ~Camera();

  glm::mat4 getView();

  glm::mat4 getProjection();

  void setView(glm::mat4);

  void setProj(glm::mat4);

  void setPosition(glm::vec3);

  glm::vec3 getPosition();

  void configure2D(float width, float height, float zoom);

 private:
  glm::mat4 view{};
  glm::mat4 proj{};
  glm::vec3 cameraPosition{};
  float zoom_ = 1.0f;
  float width_ = 0.0f;
  float height_ = 0.0f;
};
}  // namespace Pequod
#endif
