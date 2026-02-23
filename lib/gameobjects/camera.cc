#include <gameobjects/camera.hh>
#include <iostream>

GameObject::Camera::Camera() {
    std::cout << "Created new camera!" << std::endl;
}

GameObject::Camera::~Camera() {
    std::cout << "Deleting camera!" << std::endl;
}
