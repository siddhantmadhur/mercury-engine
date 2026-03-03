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

glm::mat4 Camera::getProjection() {
    return proj;
}
glm::mat4 Camera::getView() {
    return view;
}

void Camera::setView(glm::mat4 view) {
    this->view = view;
}
void Camera::setProj(glm::mat4 proj) {
    this->proj = proj;
}
void Camera::setPosition(glm::vec3 pos) {
    this->cameraPosition = pos;
}

glm::vec3 Camera::getPosition() {
    return this->cameraPosition;
}

void Camera::configure2D(float width, float height, float zoom) {
    // TODO: add createOrtho and createPerspective in the engine (maybe to scene?) itself to reduce this code as well
    glm::vec3 pos = cameraPosition;
    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-pos.x, pos.y, 0.0f));
    view = glm::scale(view, glm::vec3(zoom, zoom, 1.0f));

    glm::mat4 proj = glm::ortho<float>(0, width, 0, height, -1.0f, 1.0f);

    setProj(proj);
    setView(view);
}
