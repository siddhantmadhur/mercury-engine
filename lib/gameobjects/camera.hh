#ifndef MERCURY_CAMERA_IMPL_HH
#define MERCURY_CAMERA_IMPL_HH

#include <glm/glm.hpp>

class Camera {
public:
    Camera(float aspect_ratio);
    ~Camera();
    glm::mat4 getViewProjection();
    void setView(glm::mat4);
    void setProj(glm::mat4);
    void setPosition(glm::vec3);
    glm::vec3 getPosition();
private:
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 cameraPosition;
};
#endif
