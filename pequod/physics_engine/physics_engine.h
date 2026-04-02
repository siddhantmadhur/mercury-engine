//
// Created by smadhur on 3/31/26.
//

#ifndef PEQUOD_PHYSICS_ENGINE_H_
#define PEQUOD_PHYSICS_ENGINE_H_

#include <Jolt/Jolt.h>
#include <Jolt/Core/Memory.h>

#include "debugger/debugger.hh"
#include "ecs/ecs.hh"
#include "Jolt/Physics/Body/BodyID.h"
#include "Jolt/Physics/Collision/CollideShape.h"
#include "Jolt/Physics/Body/MotionType.h"

#include "Jolt/Core/Factory.h"
#include "Jolt/Core/JobSystemThreadPool.h"
#include "Jolt/Core/TempAllocator.h"
#include "Jolt/Physics/Body/BodyActivationListener.h"
#include "Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h"
#include "Jolt/Physics/Collision/ContactListener.h"
#include "Jolt/Physics/Collision/ObjectLayer.h"
#include "Jolt/Physics/PhysicsSettings.h"
#include "Jolt/Physics/PhysicsSystem.h"
#include "Jolt/RegisterTypes.h"
#include "rigidbody/rigidbody.hh"

// [CLAUDE] TODO: 'using namespace JPH' in a header pollutes every TU that includes this — move to .cc or use JPH:: prefix
using namespace JPH;


namespace Pequod {
    namespace {
        class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter
        {
        public:
            virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override
            {
                switch (inObject1)
                {
                    case Layers::NON_MOVING:
                        return inObject2 == Layers::MOVING; // Non moving only collides with moving
                    case Layers::MOVING:
                        return true; // Moving collides with everything
                    default:
                        JPH_ASSERT(false);
                        return false;
                }
            }
        };

        namespace BroadPhaseLayers
        {
            static constexpr BroadPhaseLayer NON_MOVING(0);
            static constexpr BroadPhaseLayer MOVING(1);
            static constexpr uint NUM_LAYERS(2);
        };

        class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface
        {
        public:
            BPLayerInterfaceImpl()
            {
                // Create a mapping table from object to broad phase layer
                mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
                mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
            }

            virtual uint GetNumBroadPhaseLayers() const override
            {
                return BroadPhaseLayers::NUM_LAYERS;
            }

            virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override
            {
                JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
                return mObjectToBroadPhase[inLayer];
            }


        private:
            BroadPhaseLayer	mObjectToBroadPhase[Layers::NUM_LAYERS];
        };

        class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter
        {
        public:
            virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override
            {
                switch (inLayer1)
                {
                    case Layers::NON_MOVING:
                        PDebug::info("Non moving!");
                        return inLayer2 == BroadPhaseLayers::MOVING;
                    case Layers::MOVING:
                        return true;
                    default:
                        JPH_ASSERT(false);
                        return false;
                }
            }
        };

        class MyBodyActivationListener : public JPH::BodyActivationListener
        {
        public:
            virtual void OnBodyActivated(const JPH::BodyID &inBodyID, JPH::uint64 inBodyUserData) override {}
            virtual void OnBodyDeactivated(const JPH::BodyID &inBodyID, JPH::uint64 inBodyUserData) override {}
        };
    }
    class PhysicsEngine {
    public:
        PhysicsEngine(std::shared_ptr<ECS>);
        void RegisterEntity(entity_id);
        void Compute(int steps);
        // [CLAUDE] TODO: DisableBody is declared but never implemented
        void DisableBody(entity_id);
    private:
        // [CLAUDE] TODO: body_id is declared but never used
        BodyID* body_id = nullptr;
        std::shared_ptr<ECS> ecs = nullptr;

        // [CLAUDE] TODO: jolt_bodies is never populated — physics system not initialized
        std::unordered_map<JPH::BodyID, entity_id> jolt_bodies; // maps jolt id <--> pequods entity_id

        // [CLAUDE] TODO: temp_allocator and job_system are declared but never initialized
        TempAllocatorImpl *temp_allocator; // 10MB
        JobSystemThreadPool *job_system;
        //PhysicsSystem physics_system;

        BPLayerInterfaceImpl broad_phase_layer_interface;
        ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase_layer_filter;
        ObjectLayerPairFilterImpl object_vs_object_layer_filter;
        MyBodyActivationListener body_activation_listener;
    };
}

#endif //