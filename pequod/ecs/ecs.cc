#include "ecs/position.hh"
#include "engine/world_scene.h"
#include "glm/ext/matrix_transform.hpp"
#include "rigidbody/rigidbody.hh"
#include <ecs/ecs.hh>
#include <format>
#include <iostream>
#include <type_traits>
#include <debugger/debugger.hh>
#include <unordered_map>

#define PEQUOD_INTERPOLATE_MOVEMENT 0

#include "pobject/pobject.h"

namespace Pequod {

template<class TProperty>
std::unordered_map<std::type_index, std::unordered_map<entity_id, std::shared_ptr<TProperty>>> ECS::properties;





ECS::ECS () {
    vertices = std::vector<vertex_t>();
    indices = std::vector<uint16_t>();
    auto root = std::make_shared<PObject>(0);
    AddProperty(0, root);
}

ECS::~ECS () {

}


void ECS::Disable(entity_id id) {

    auto mesh = GetProperty<Mesh>(id);
    if (mesh) {
        auto & meshes = properties<Mesh>[typeid(Mesh)];
        meshes.erase(id);
    }

    auto pos = GetProperty<Position>(id);
    if (pos) {
        auto & positions = properties<Position>[typeid(Position)];
        positions.erase(id);
    }

}

void ECS::ResetBuffers() {
    vertices.clear();
    indices.clear();
}

entity_id ECS::createEntity() {
    entity_id new_id = current_id;
    auto obj = std::make_shared<PObject>(new_id);
    AddProperty(new_id, obj);

    auto root = GetProperty<PObject>(0);
    root->AddChild(new_id);

    current_id += 1;
    return new_id;
}

template <class TProperty>
std::unordered_map<entity_id, std::shared_ptr<TProperty>>& ECS::GetProperties() {
    auto& arr = properties<TProperty>[typeid(TProperty)];
    return arr;
}

void ECS::addMesh(entity_id id, std::shared_ptr<Mesh> mesh) {
    auto& meshes = properties<Mesh>[typeid(Mesh)];
    if (meshes.contains(id)) {
        std::cout << "Mesh already added" << std::endl;
        return;
    }

    meshes[id] = mesh;

    mesh->indices_id = indices.size();
    mesh->vertices_id = vertices.size();

    size_t prev_size = vertices.size();
    if (prev_size > 0) {
        for (int i = 0; i < mesh->indices.size(); i++) {
            mesh->indices[i] += prev_size;
        }
    }

   // std::cout << "i: " << mesh->indices[i] << std::endl;
    this->vertices.insert(vertices.end(), mesh->vertices.begin(), mesh->vertices.end());
    this->indices.insert(indices.end(), mesh->indices.begin(), mesh->indices.end());
}

template<class TProperty>
void ECS::AddProperty(entity_id id, std::shared_ptr<TProperty> property) {
    auto& arr = properties<TProperty>[typeid(TProperty)];
    if (!arr.contains(id)) {
        if constexpr (std::is_same_v<TProperty, Mesh>) {
            addMesh(id, property);
        } else {
            arr[id] = property;
        }
    } else {
        PDebug::error(std::format("Property already exists for id: {}", id));
    }
}

template<class TProperty>
std::shared_ptr<TProperty> ECS::GetProperty(entity_id id) {
    auto arr = properties<TProperty>[typeid(TProperty)];
    if (arr.contains(id)) {
        return arr[id];
    } else {
        return nullptr;
    }
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


void ECS::render(Camera& cam, float delta_t) {
    for (int i = 0; i < current_id; i++) {
        auto& meshes = properties<Mesh>[typeid(Mesh)];
        auto mesh = meshes[i];
        auto& positions = properties<Position>[typeid(Position)];
        auto position = positions[i];

        if (mesh == nullptr || position == nullptr) {
            continue;
        }

        glm::vec3 diff = position->position - position->raw_position;

#if PEQUOD_INTERPOLATE_MOVEMENT
        if (delta_t > 0) {
            //positions[i]->raw_position = positions[i]->position;
            //PDebug::log(std::format("POSITION: {}, {}", diff.x, diff.y));
            position->raw_position += diff / delta_t;
        } else {
            //position->raw_position += diff;
            position->raw_position = position->position;
        }
#else
        position->raw_position = position->position;
#endif
        glm::vec3 pos = position->raw_position;


        //std::cout << "ID: " << i << std::endl;
        //std::cout << mesh->indices_id << ", " << mesh->indices.size() << std::endl;
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, pos);
        model_params_t new_params;
        new_params.model = model;
        new_params.use_texture0 = false ? 1.0f : 0.0f;
        new_params.scale = mesh->scale;
        sg_apply_uniforms(UB_model_params, SG_RANGE(new_params));
        sg_draw(mesh->indices_id, mesh->indices.size(), 1);
    }
}

    template std::unordered_map<std::type_index, std::unordered_map<entity_id, std::shared_ptr<Mesh>>> ECS::properties<Mesh>;
    template std::unordered_map<std::type_index, std::unordered_map<entity_id, std::shared_ptr<Position>>> ECS::properties<Position>;
    template std::unordered_map<std::type_index, std::unordered_map<entity_id, std::shared_ptr<PObject>>> ECS::properties<PObject>;
    template std::unordered_map<std::type_index, std::unordered_map<entity_id, std::shared_ptr<RigidBody>>> ECS::properties<RigidBody>;

    template void ECS::AddProperty<Mesh>(entity_id, std::shared_ptr<Mesh>);
    template void ECS::AddProperty<Position>(entity_id, std::shared_ptr<Position>);
    template void ECS::AddProperty<PObject>(entity_id, std::shared_ptr<PObject>);
    template void ECS::AddProperty<RigidBody>(entity_id, std::shared_ptr<RigidBody>);

    template std::shared_ptr<Mesh> ECS::GetProperty<Mesh>(entity_id);
    template std::shared_ptr<Position> ECS::GetProperty<Position>(entity_id);
    template std::shared_ptr<PObject> ECS::GetProperty<PObject>(entity_id);
    template std::shared_ptr<RigidBody> ECS::GetProperty<RigidBody>(entity_id);

    template std::unordered_map<entity_id, std::shared_ptr<Mesh>> & ECS::GetProperties<Mesh>();
    template std::unordered_map<entity_id, std::shared_ptr<Position>> & ECS::GetProperties<Position>();
    template std::unordered_map<entity_id, std::shared_ptr<PObject>> & ECS::GetProperties<PObject>();
    template std::unordered_map<entity_id, std::shared_ptr<RigidBody>> & ECS::GetProperties<RigidBody>();
}
