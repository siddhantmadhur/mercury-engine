#include <gameobjects/camera.hh>
#include <iostream>

Camera::Camera() {
    std::cout << "Created new camera!" << std::endl;
}

Camera::~Camera() {
    std::cout << "Deleting camera!" << std::endl;
}
