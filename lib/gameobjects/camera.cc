#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include <gameobjects/camera.hh>
#include <iostream>

Camera::Camera(float aspect_ratio) {
    proj = glm::perspective<float>(glm::radians(60.0f), aspect_ratio, 0.1f, 10.0f);
    view = glm::lookAt(glm::vec3(0.0f, 3.5f, 7.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

Camera::~Camera() {
    std::cout << "Deleting camera!" << std::endl;
}

glm::mat4 Camera::getViewProjection() {
    return proj * view;
}
