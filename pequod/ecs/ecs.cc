#include "ecs/position.hh"
#include "engine/scene.hh"
#include "glm/ext/matrix_transform.hpp"
#include <ecs/ecs.hh>
#include <iostream>


ECS::ECS () {
    current_id = 0;
}
ECS::~ECS () {
    
}

entity_id ECS::createEntity() {
    return current_id++; 
}

void ECS::addMesh(entity_id id, Mesh* mesh) {
    if (mesh == nullptr) {
        return;
    }
    mesh->indices_id = indices.size();
    mesh->vertices_id = vertices.size();

    this->vertices.insert(vertices.end(), mesh->vertices.begin(), mesh->vertices.end());
    this->indices.insert(indices.end(), mesh->indices.begin(), mesh->indices.end());
    
    meshes[id] = mesh;
}


void ECS::addPosition(entity_id id, Position* position) {
    this->positions[id] = position;
}

Position* ECS::getPosition(entity_id id) {
    if (positions.contains(id)) {
        return positions[id];
    }
    return NULL;
}

Mesh* ECS::getMesh(entity_id id) {
    if (meshes.contains(id)) {
        return meshes[id];
    }
    return NULL;
}

void ECS::setupRender(sg_bindings& bind) {
    size_t vertex_size = vertices.size() * sizeof(vertex_t);
    if (vertices.size() > 0) {
        sg_update_buffer(bind.vertex_buffers[0], (sg_range){
            .ptr = vertices.data(),
            .size = vertex_size,
        });
    }
  
    size_t indices_size = indices.size() * sizeof(uint16_t);
    if (indices.size() > 0) {
        sg_update_buffer(bind.index_buffer, (sg_range){
            .ptr = indices.data(),
            .size = indices_size,
        });
    }     
}

void ECS::render(Camera& cam) {


    for (int i = 0; i < current_id; i++) {
        Mesh* mesh = meshes[i];
        Position* position = positions[i];
        if (mesh == nullptr || position == nullptr) {
            continue;
        }

        glm::vec3 pos = positions[i]->raw_position;
        //std::cout << mesh->indices_id << ", " << mesh->indices.size() << std::endl;
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, pos);
        model_params_t new_params;
        new_params.model = model; 
        new_params.use_texture0 = false ? 1.0f : 0.0f; 
        sg_apply_uniforms(UB_model_params, SG_RANGE(new_params));
        sg_draw(mesh->indices_id, mesh->indices.size(), 1);
    }
}



