#include <gameobjects/gameobject.hh>
#include <vector>

GameObject::GameObject() {
}


GameObject::~GameObject() {

}

std::vector<vertex_t> GameObject::getVertices() {
    return this->vertices; 
}

void GameObject::setId(uint16_t givenId) {
    this->id = givenId;
}

