#include <gameobjects/gameobject.hh>
#include <vector>
#include <iostream>

std::vector<vertex_t> GameObject::getVertices() {
    return this->vertices; 
}
std::vector<uint16_t> GameObject::getIndices() {
    return this->indices; 
}

void GameObject::setId(uint16_t givenId) {
    this->id = givenId;
}

bool GameObject::isTexture() {
    return this->use_texture;
}

void GameObject::UseTexture(bool newVal) {
    this->use_texture = newVal;
}

GameObject::GameObject() {
   //vertices = std::vector<vertex_t>(); 
}

void GameObject::Move(glm::vec3 position) {
    const std::vector<vertex_t>& arr = vertices;
    if (position.x == 0 && position.y == 0 && position.z == 0) {
        return;
    }
    std::cout << arr.size() << std::endl;
    for (int i = 0; i < this->vertices.size(); i++) {
        // TODO: probably dumb as fuck! optimize later maybe 
        this->vertices[i].x += position.x;
        this->vertices[i].y += position.y;
        this->vertices[i].z += position.z;
    }
}
