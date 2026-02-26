#ifndef MERCURY_CAMERA_IMPL_HH
#define MERCURY_CAMERA_IMPL_HH

#include <glm/glm.hpp>

class Camera {
public:
    Camera(float aspect_ratio);
    ~Camera();
    glm::mat4 getViewProjection();
    glm::vec3 cameraPosition;
private:
    glm::mat4 view;
    glm::mat4 proj;
};
#endif
