/**
* ecs.hh
* 
* This is the ECS for an entire "scene" 
* It is a *singleton* class and hence only created and used once (again per scene)
* It handles object properties like meshes, collisions and gravity
* It does NOT handle engine properties like ticks or key events
**/

#ifndef PEQUOD_ECS_IMPL_HH_
#define PEQUOD_ECS_IMPL_HH_

#include <format>
#include <iostream>
#include <memory>
#include <typeindex>
#include <sokol/sokol_gfx.h>

#include "gameobjects/camera.hh"
#include <unordered_map>
#include <vector>

#include <ecs/entity.hh>
#include <properties/property.h>
#include <properties/mesh.h>


#include <debugger/debugger.h>

namespace Pequod {
    class ECS {
    public:
        ECS();

        ~ECS();

        // TODO: ECS is now managed by PObjectManager
        entity_id max_id = 0;

        //void initializeJolt(); // run this once
        //void simulatePhysics(int steps);
        //entity_id createEntity(); // use to create new entities that are part of the engine

        //void addPosition(entity_id, std::shared_ptr<Position>);

        //std::shared_ptr<Mesh> getMesh(entity_id);
        //std::shared_ptr<Position> getPosition(entity_id);

        // TODO: Move this to a different component
        //void addRigidBody(entity_id, std::shared_ptr<RigidBody>);
        //std::shared_ptr<RigidBody> getRigidBody(entity_id);
        //void setVelocity(entity_id, glm::vec3);
        //glm::vec3 GetVelocity(entity_id);
        //void SetPosition(entity_id, glm::vec3);
        //void SetRestitution(entity_id, float);
        //void SetFriction(entity_id, float);

        template<std::derived_from<Property> TProperty>
        void AddProperty(entity_id id, std::shared_ptr<TProperty> property) {
            auto &arr = GetProperties<TProperty>();
            if (!arr.contains(id)) {
                if constexpr (std::is_same_v<TProperty, Mesh>) {
                    addMesh(id, property);
                } else {
                    arr[id] = property;
                }
            } else {
                PDebug::error("Property already exists for id: {}", id);
            }
        }

        template<std::derived_from<Property> TProperty>
        std::shared_ptr<TProperty> GetProperty(entity_id id) {
            auto arr = GetProperties<TProperty>();
            if (arr.contains(id)) {
                return arr[id];
            } else {
                return nullptr;
            }
        }

        template<std::derived_from<Property> TProperty>
        std::unordered_map<entity_id, std::shared_ptr<TProperty> > &GetProperties() {
            auto &arr = properties<TProperty>[typeid(TProperty)];
            return arr;
        }

        void Disable(entity_id);

        void ResetBuffers(); // clears vertex/index buffers for scene reload

        void setupRender(sg_bindings &bind);

        void render(Camera &cam, float delta_t);

        void processOnTick(float delta_t);

        void processOnFrame(float delta_t);

    protected:
        template<class TProperty>
        static std::unordered_map<std::type_index, std::unordered_map<entity_id, std::shared_ptr<TProperty> > >
        properties;

        std::vector<vertex_t> vertices;
        std::vector<uint16_t> indices;

    private:
        void addMesh(entity_id, std::shared_ptr<Mesh>);
    };
}

#endif
