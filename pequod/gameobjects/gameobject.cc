#include <gameobjects/gameobject.hh>
#include <vector>
#include <iostream>
#include <sokol/sokol_gfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <shaders/generic_texture.glsl.hh>


GameObject::~GameObject() {
    //std::cout << "destroying gameobject" << std::endl;
}
std::vector<vertex_t> GameObject::getVertices() {
    return this->vertices; 
}
std::vector<uint16_t> GameObject::getIndices() {
    return this->indices; 
}

void GameObject::setId(uint16_t givenId) {
    //std::cout << "ID: " << givenId << std::endl;
    this->id = givenId;
}

bool GameObject::isTexture() {
    return this->use_texture;
}

void GameObject::UseTexture(bool newVal) {
    this->use_texture = newVal;
}

void GameObject::SetPosition(glm::vec3 newPosition) {
    this->position = newPosition;
}


void GameObject::Move(glm::vec3 movement) {
    /**
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
**/
    this->position += movement;
}

glm::vec3 GameObject::GetPosition() {
    return this->position;
}
void GameObject::Draw() {
    if (!disable) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(position.x, position.y, position.z));

        model_params_t new_params;
        new_params.model = model; 
        new_params.use_texture0 = this->use_texture ? 1.0f : 0.0f; 

        sg_apply_uniforms(UB_model_params, SG_RANGE(new_params));
        sg_draw(id, indices.size(), 1);
    }

}
