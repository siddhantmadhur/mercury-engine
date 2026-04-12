//
// Created by smadhur on 3/31/26.
//

#include "physics_engine.h"

namespace Pequod {
    namespace {
        class MyContactListener : public ContactListener {
        public:
            MyContactListener(
                std::unordered_map<JPH::BodyID, entity_id> &init_map,
                std::unordered_map<entity_id, std::shared_ptr<RigidBody> > &init_rigid_bodies
            ) : jolt_bodies(init_map), rigid_bodies(init_rigid_bodies) {
                PDebug::info("created contact listener");
            };


            std::unordered_map<JPH::BodyID, entity_id> &jolt_bodies;
            std::unordered_map<entity_id, std::shared_ptr<RigidBody> > &rigid_bodies;

            virtual ValidateResult OnContactValidate(const Body &inBody1, const Body &inBody2,
                                                     JPH::RVec3Arg inBaseOffset,
                                                     const JPH::CollideShapeResult &inCollisionResult) override {
                return ValidateResult::AcceptAllContactsForThisBodyPair;
            }

            virtual void OnContactAdded(const Body &inBody1, const Body &inBody2,
                                        const JPH::ContactManifold &inManifold,
                                        JPH::ContactSettings &ioSettings) override {
                PDebug::info("contact was added");
                if (jolt_bodies.contains(inBody1.GetID()) && jolt_bodies.contains(inBody2.GetID())) {
                    entity_id self = jolt_bodies[inBody1.GetID()];
                    entity_id other = jolt_bodies[inBody2.GetID()];
                    auto self_body = rigid_bodies[self];
                    if (self_body) {
                        self_body->OnCollisionEnter(other);
                        self_body->OnCollision(other);
                    }
                    auto other_body = rigid_bodies[other];
                    if (other_body) {
                        other_body->OnCollisionEnter(self);
                        other_body->OnCollision(self);
                    }
                } else {
                    PDebug::info("could not recognize id");
                }
            }

            virtual void OnContactPersisted(const Body &inBody1, const Body &inBody2,
                                            const JPH::ContactManifold &inManifold,
                                            JPH::ContactSettings &ioSettings) override {
                if (jolt_bodies.contains(inBody1.GetID()) && jolt_bodies.contains(inBody2.GetID())) {
                    entity_id self = jolt_bodies[inBody1.GetID()];
                    entity_id other = jolt_bodies[inBody2.GetID()];
                    auto self_body = rigid_bodies[self];
                    if (self_body) {
                        self_body->OnCollision(other);
                    }
                    auto other_body = rigid_bodies[other];
                    if (other_body) {
                        other_body->OnCollision(self);
                    }
                } else {
                    PDebug::info("could not recognize id");
                }
            }

            virtual void OnContactRemoved(const JPH::SubShapeIDPair &inSubShapePair) override {
                PDebug::info("contact was removed");
                if (jolt_bodies.contains(inSubShapePair.GetBody1ID()) && jolt_bodies.contains(
                        inSubShapePair.GetBody2ID())) {
                    entity_id self = jolt_bodies[inSubShapePair.GetBody1ID()];
                    entity_id other = jolt_bodies[inSubShapePair.GetBody2ID()];

                    auto self_body = rigid_bodies[self];
                    if (self_body != NULL) {
                        self_body->OnCollisionLeave(other);
                    }
                    auto other_body = rigid_bodies[other];
                    if (other_body != NULL) {
                        other_body->OnCollisionLeave(self);
                    }
                } else {
                    PDebug::log("could not recognize id");
                }
            }
        };
    }

    PhysicsEngine::PhysicsEngine(std::shared_ptr<ECS> ecs) {
        // TODO: Implement this
        /**
            this->ecs = ecs;
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
    
            //MyContactListener* contact_listener = new MyContactListener(jolt_bodies, rigid_bodies);
    
            //physics_system.SetContactListener(contact_listener);
    
            PDebug::info("initialized jolt physics system");
            **/
    }

    void PhysicsEngine::RegisterEntity(entity_id id) {
        // register object here
        // TODO: IMPLEMENT THIS
    }

    void PhysicsEngine::Compute(int steps) {
        // TODO: Implement
        /**
            float cHz = 1.0f / 60.0f;
            physics_system.Update(cHz, steps, temp_allocator, job_system);
    
            auto& body_interface = physics_system.GetBodyInterface();
    
            for (const auto& pair : jolt_bodies) {
                JPH::BodyID body_id = pair.first;
                entity_id id = pair.second;
                auto phys_position = body_interface.GetCenterOfMassPosition(body_id);
                auto positions = ecs->GetProperty<Position>(id);
                glm::vec3 & position = positions->position;
                position.x = phys_position.GetX();
                position.y = phys_position.GetY();
                position.z = phys_position.GetZ();
    
            }
            **/
    }
}
