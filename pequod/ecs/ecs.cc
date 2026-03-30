#include <Jolt/Jolt.h>
#include "Jolt/Physics/Body/MotionType.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/EActivation.h"
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

template<class TProperty>
std::unordered_map<std::type_index, std::unordered_map<entity_id, std::shared_ptr<TProperty>>> ECS::properties;


class MyContactListener : public ContactListener
{
public:
    MyContactListener(
        std::unordered_map<JPH::BodyID, entity_id> &init_map, 
        std::unordered_map<entity_id, RigidBody*>& init_rigid_bodies
    ) : jolt_bodies(init_map), rigid_bodies(init_rigid_bodies)
    { 
        PDebug::info("created contact listener");
    };


    std::unordered_map<JPH::BodyID, entity_id> & jolt_bodies;
    std::unordered_map<entity_id, RigidBody*> & rigid_bodies;
    virtual ValidateResult	OnContactValidate(const Body &inBody1, const Body &inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult &inCollisionResult) override
    {

        return ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    virtual void OnContactAdded(const Body &inBody1, const Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings) override
    {
        PDebug::info("contact was added");
        if (jolt_bodies.contains(inBody1.GetID()) && jolt_bodies.contains(inBody2.GetID())) {
            entity_id self = jolt_bodies[inBody1.GetID()];
            entity_id other = jolt_bodies[inBody2.GetID()];
            RigidBody* self_body = rigid_bodies[self];
            if (self_body != NULL) {
                self_body->OnCollisionEnter(other);
                self_body->OnCollision(other);
            }
            RigidBody* other_body = rigid_bodies[other];
            if (other_body != NULL) {
                other_body->OnCollisionEnter(self);
                other_body->OnCollision(self);
            }
        } else {
            PDebug::info("could not recognize id");
        }
    }

    virtual void OnContactPersisted(const Body &inBody1, const Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings) override
    {
        if (jolt_bodies.contains(inBody1.GetID()) && jolt_bodies.contains(inBody2.GetID())) {
            entity_id self = jolt_bodies[inBody1.GetID()];
            entity_id other = jolt_bodies[inBody2.GetID()];
            RigidBody* self_body = rigid_bodies[self];
            if (self_body != NULL) {
                self_body->OnCollision(other);
            }
            RigidBody* other_body = rigid_bodies[other];
            if (other_body != NULL) {
                other_body->OnCollision(self);
            }
        } else {
            PDebug::info("could not recognize id");
        }
    }

    virtual void OnContactRemoved(const JPH::SubShapeIDPair &inSubShapePair) override
    {
        PDebug::info("contact was removed");
        if (jolt_bodies.contains(inSubShapePair.GetBody1ID()) && jolt_bodies.contains(inSubShapePair.GetBody2ID())) {
            entity_id self = jolt_bodies[inSubShapePair.GetBody1ID()];
            entity_id other = jolt_bodies[inSubShapePair.GetBody2ID()];

            RigidBody* self_body = rigid_bodies[self];
            if (self_body != NULL) {
                self_body->OnCollisionLeave(other);
            }
            RigidBody* other_body = rigid_bodies[other];
            if (other_body != NULL) {
                other_body->OnCollisionLeave(self);
            }
        } else {
            PDebug::log("could not recognize id");
        }
    }
};


ECS::ECS () {
    current_id = 0;
    vertices = std::vector<vertex_t>();
    indices = std::vector<uint16_t>();
}

ECS::~ECS () {
    
}

void ECS::setVelocity(entity_id id, glm::vec3 vel) {
    RigidBody* body = rigid_bodies[id];
    JPH::BodyID body_id = body->jolt_id;
    
    auto& body_interface = physics_system.GetBodyInterface();
    body_interface.SetLinearVelocity(body_id, JPH::Vec3(vel.x, vel.y, vel.z));
}

glm::vec3 ECS::GetVelocity(entity_id id) {
    RigidBody* body = rigid_bodies[id];
    JPH::BodyID body_id = body->jolt_id;
    
    auto& body_interface = physics_system.GetBodyInterface();
    JPH::Vec3 vel = body_interface.GetLinearVelocity(body_id);
    return glm::vec3(vel.GetX(), vel.GetY(), vel.GetZ());
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
    
    RigidBody* body = getRigidBody(id);
    if (body) {
        auto& body_interface = physics_system.GetBodyInterface();
        body_interface.RemoveBody(body->jolt_id);  
        body_interface.DestroyBody(body->jolt_id);
        jolt_bodies.erase(body->jolt_id);
        rigid_bodies[id] = NULL;

    }

}

void ECS::SetPosition(entity_id id, glm::vec3 pos) {
    RigidBody* body = rigid_bodies[id];
    JPH::BodyID body_id = body->jolt_id;
    
    auto& body_interface = physics_system.GetBodyInterface();
    body_interface.SetPosition(body_id, JPH::Vec3(pos.x, pos.y, pos.z), JPH::EActivation::Activate);
}

void ECS::simulatePhysics(int steps) { // call every 60hz
    float cHz = 1.0f / 60.0f;
    physics_system.Update(cHz, steps, temp_allocator, job_system);

    auto& body_interface = physics_system.GetBodyInterface();

    for (const auto& pair : jolt_bodies) {
        JPH::BodyID body_id = pair.first;
        entity_id id = pair.second;
        auto phys_position = body_interface.GetCenterOfMassPosition(body_id);
        auto positions = properties<Position>[typeid(Position)];
        glm::vec3 & position = positions[id]->position;
        position.x = phys_position.GetX();
        position.y = phys_position.GetY();
        position.z = phys_position.GetZ();

    }
}

void ECS::addRigidBody(entity_id id, RigidBody* rigid_body) {

    auto& body_interface = physics_system.GetBodyInterface();
    
    auto creation_settings = rigid_body->getBodyCreationSettings();
    creation_settings.mAllowedDOFs = rigid_body->allowed_dofs;


    JPH::Body* body = body_interface.CreateBody(creation_settings);
    if (body == nullptr) {
        PDebug::error("body is NULL");
    }

    JPH::BodyID body_id = body->GetID();

    rigid_body->body = body;
    jolt_bodies[body_id] = id;
    rigid_bodies[id] = rigid_body;

    body_interface.AddBody(body_id, JPH::EActivation::Activate);
    body_interface.SetGravityFactor(body_id, 0.0f);

    rigid_body->id = id;
    rigid_body->jolt_id = body_id;
}

RigidBody* ECS::getRigidBody(entity_id id) {
    return rigid_bodies[id];
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
   
    MyContactListener* contact_listener = new MyContactListener(jolt_bodies, rigid_bodies);

    physics_system.SetContactListener(contact_listener);

    PDebug::info("initialized jolt physics system");


}

entity_id ECS::createEntity() {
    entity_id new_id = current_id;
    current_id += 1;
    return new_id; 
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

/**

void ECS::addPosition(entity_id id, std::shared_ptr<Position> position) {
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

std::shared_ptr<Position> ECS::getPosition(entity_id id) {
    if (positions.contains(id)) {
        return positions[id];
    }
    return NULL;
}

std::shared_ptr<Mesh> ECS::GetProperty<Mesh>(entity_id id) {
    if (meshes.contains(id)) {
        return meshes[id];
    }
    return NULL;
}
**/


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

void ECS::SetMotionType(entity_id id, JPH::EMotionType motion_type) {
    auto* rigid_body = rigid_bodies[id];
    JPH::BodyID body_id =  rigid_body->jolt_id;
    auto & body_interface = physics_system.GetBodyInterface();
    body_interface.SetMotionType(body_id, motion_type, JPH::EActivation::Activate);
}

void ECS::SetRestitution(entity_id id, float restitution) {
    auto* rigid_body = rigid_bodies[id];
    JPH::BodyID body_id =  rigid_body->jolt_id;
    auto & body_interface = physics_system.GetBodyInterface();
    body_interface.SetRestitution(body_id, restitution);
}

void ECS::SetFriction(entity_id id, float friction) {
    auto* rigid_body = rigid_bodies[id];
    JPH::BodyID body_id =  rigid_body->jolt_id;
    auto & body_interface = physics_system.GetBodyInterface();
    body_interface.SetFriction(body_id, friction);
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

        if (delta_t > 0) {
            //positions[i]->raw_position = positions[i]->position;
            //PDebug::log(std::format("POSITION: {}, {}", diff.x, diff.y));
            position->raw_position += diff / delta_t;
        } else {
            //position->raw_position += diff;
            position->raw_position = position->position;
        }

        //position->raw_position = position->position;
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
template void ECS::AddProperty<Mesh>(entity_id, std::shared_ptr<Mesh>);
template void ECS::AddProperty<Position>(entity_id, std::shared_ptr<Position>);
template std::shared_ptr<Mesh> ECS::GetProperty<Mesh>(entity_id);
template std::shared_ptr<Position> ECS::GetProperty<Position>(entity_id);
