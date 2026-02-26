#include <gameobjects/gameobject.hh>
#include <vector>


std::vector<vertex_t> GameObject::getVertices() {
    return this->vertices; 
}
std::vector<uint16_t> GameObject::getIndices() {
    return this->indices; 
}

void GameObject::setId(uint16_t givenId) {
    this->id = givenId;
}

