
#include "ecs/position.hh"
#include "engine/scene.hh"
#include "glm/ext/matrix_transform.hpp"
#include <ecs/ecs.hh>
#include <iostream>
#include <debugger/debugger.hh>


ECS::ECS () {
    current_id = 0;
    vertices = std::vector<vertex_t>();
    indices = std::vector<uint16_t>();
}

ECS::~ECS () {
    
}

void ECS::initializeJolt() {
    // Initialize Jolt

    JPH::RegisterDefaultAllocator();

    JPH::Factory::sInstance = new JPH::Factory();

    JPH::RegisterTypes();

    temp_allocator = new JPH::TempAllocatorImpl (10 * 1024 * 1024); // 10MB

    job_system = new JPH::JobSystemThreadPool (JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, JPH::thread::hardware_concurrency() - 1);

    const uint cMaxBodies = 1024;

    const uint cNumBodyMutexes = 0;

    const uint cMaxBodyPairs = 1024;

    const uint cMaxContactConstraints = 1024;

    physics_system.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, broad_phase_layer_interface, object_vs_broadphase_layer_filter, object_vs_object_layer_filter);

    physics_system.SetBodyActivationListener(&body_activation_listener);
    
    physics_system.SetContactListener(&contact_listener);

    PDebug::info("initialized jolt physics system");


}

entity_id ECS::createEntity() {
    entity_id new_id = current_id;
    current_id += 1;
    return new_id; 
}

void ECS::addMesh(entity_id id, Mesh* mesh) {
   
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


void ECS::addPosition(entity_id id, Position* position) {
    if (position == nullptr) {
        std::cout << "Position is null" << std::endl;
        return;
    }
    if (positions.contains(id)) {
        std::cout << "Position already added" << std::endl;
        return;
    }

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

void ECS::render(Camera& cam, float delta_t) {
    for (int i = 0; i < current_id; i++) {
        Mesh* mesh = meshes[i];
        Position* position = positions[i];

        if (mesh == nullptr || position == nullptr) {
            continue;
        }


        glm::vec3 diff = position->raw_position - position->position;
        positions[i]->position += diff;
        glm::vec3 pos = positions[i]->position;


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



bool ECS::doesCollide(entity_id A, entity_id B) {

    glm::vec2 sizeA = meshes[A]->scale;
    glm::vec2 posA = positions[A]->position;
    posA.x -= sizeA.x / 2.0f;
    posA.y -= sizeA.y / 2.0f;
    glm::vec2 sizeB = meshes[B]->scale;
    glm::vec2 posB = positions[B]->position;
    posB.x -= sizeB.x / 2.0f;
    posB.y -= sizeB.y / 2.0f;

    bool collisionX = posA.x + sizeA.x >= posB.x && posB.x + sizeB.x >= posA.x;
    bool collisionY = posA.y + sizeA.y >= posB.y && posB.y + sizeB.y >= posA.y;

    return (collisionX && collisionY);
}
