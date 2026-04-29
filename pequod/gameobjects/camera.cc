#include <debugger/debugger.h>

#include <gameobjects/camera.hh>
#include <iostream>

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

namespace Pequod {
Camera::Camera(float aspect_ratio) {
  proj =
      glm::perspective<float>(glm::radians(60.0f), aspect_ratio, 0.1f, 10.0f);
  view = glm::lookAt(glm::vec3(0.0f, 3.5f, 7.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                     glm::vec3(0.0f, 1.0f, 0.0f));
}

Camera::~Camera() { PDebug::log("deleting camera..."); }

glm::mat4 Camera::getProjection() {
  glm::mat4 proj = glm::ortho<float>(-width_ / 2, width_ / 2, -height_ / 2,
                                     height_ / 2, -1.0f, 1.0f);
  setProj(proj);
  return proj;
}

glm::mat4 Camera::getView() {
  glm::vec3 pos = cameraPosition;
  glm::mat4 view =
      glm::translate(glm::mat4(1.0f), glm::vec3(-pos.x, -pos.y, 0.0f));
  view = glm::scale(view, glm::vec3(zoom_, zoom_, 1.0f));
  setView(view);
  return view;
}

void Camera::setView(glm::mat4 view) { this->view = view; }

void Camera::setProj(glm::mat4 proj) { this->proj = proj; }

void Camera::setPosition(glm::vec3 pos) { this->cameraPosition = pos; }

glm::vec3 Camera::getPosition() { return this->cameraPosition; }

void Camera::configure2D(float width, float height, float zoom) {
  glm::vec3 pos = cameraPosition;
  glm::mat4 view =
      glm::translate(glm::mat4(1.0f), glm::vec3(-pos.x, -pos.y, 0.0f));
  view = glm::scale(view, glm::vec3(zoom, zoom, 1.0f));

  // Centered ortho: camera position is the center of the view
  glm::mat4 proj = glm::ortho<float>(-width / 2, width / 2, -height / 2,
                                     height / 2, -1.0f, 1.0f);

  zoom_ = zoom;
  width_ = width;
  height_ = height;
  setProj(proj);
  setView(view);
}
}  // namespace Pequod
