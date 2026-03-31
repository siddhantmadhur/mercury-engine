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

#include <iostream>
#include <memory>
#include <typeindex>
#include <sokol/sokol_gfx.h>

#include "gameobjects/camera.hh"
#include <unordered_map>
#include <vector>

#include <ecs/entity.hh> 
#include <ecs/mesh.hh> 
#include <ecs/position.hh> 

#include <Jolt/Jolt.h>
#include <Jolt/Core/Memory.h>
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

#include <debugger/debugger.hh>

using JPH::ObjectLayer;
using JPH::ObjectLayerPairFilter;
using JPH::ObjectLayerPairFilter;
using JPH::BroadPhaseLayer;
using JPH::BroadPhaseLayerInterface;
using JPH::ObjectVsBroadPhaseLayerFilter;
using JPH::ContactListener;
using JPH::ValidateResult;
using JPH::Body;


namespace Pequod {
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


class ECS {
public:
    ECS();
    ~ECS();
    void initializeJolt(); // run this once
    void simulatePhysics(int steps);
    entity_id createEntity(); // use to create new entities that are part of the engine

    //void addPosition(entity_id, std::shared_ptr<Position>);

    //std::shared_ptr<Mesh> getMesh(entity_id);
    //std::shared_ptr<Position> getPosition(entity_id);

    void addRigidBody(entity_id, RigidBody*);
    RigidBody* getRigidBody(entity_id);
    void setVelocity(entity_id, glm::vec3);
    glm::vec3 GetVelocity(entity_id);
    void SetPosition(entity_id, glm::vec3);
    void SetRestitution(entity_id, float);
    void SetFriction(entity_id, float);

	template <class TProperty>
	void AddProperty(entity_id, std::shared_ptr<TProperty>); // Add property

	template <class TProperty>
	std::shared_ptr<TProperty> GetProperty(entity_id); // Add property

    void SetMotionType(entity_id, JPH::EMotionType);

    void Disable(entity_id);

    JPH::BodyID* getJoltId(entity_id);

    void setupRender(sg_bindings& bind);
    void render(Camera& cam, float delta_t);

    void processOnTick(float delta_t);
    void processOnFrame(float delta_t);
    JPH::TempAllocatorImpl *temp_allocator; // 10MB
    JPH::JobSystemThreadPool *job_system;
    JPH::PhysicsSystem physics_system;

    std::unordered_map<JPH::BodyID, entity_id> jolt_bodies;
    std::unordered_map<entity_id, RigidBody*> rigid_bodies;
protected:
	template <class TProperty>
	static std::unordered_map<std::type_index, std::unordered_map<entity_id, std::shared_ptr<TProperty>>> properties;

    std::vector<vertex_t> vertices;
    std::vector<uint16_t> indices;

    BPLayerInterfaceImpl broad_phase_layer_interface;
    ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase_layer_filter;
    ObjectLayerPairFilterImpl object_vs_object_layer_filter;
    MyBodyActivationListener body_activation_listener;


private:
    void addMesh(entity_id, std::shared_ptr<Mesh>);
    entity_id current_id;
};

}

#endif