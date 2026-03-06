/**
* rigidbody.hh
* RigidBody abstract type - DO NOT EXTEND IN GAME CODE
* It's purpose is to provide a class for set shapes (such as box2d) so they can be used easily
*
* It depends on Jolt for actual callbacks and functioning and does not include any logic
* 
* Contains 3 basic collision function
* The parameter to all functions is the entity_id of the colliding object
*
**/
#ifndef PEQUOD_RIGIDBODY_IMPL_HH_
#define PEQUOD_RIGIDBODY_IMPL_HH_

#include <Jolt/Jolt.h>
#include "Jolt/Physics/Body/AllowedDOFs.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/MotionType.h"
#include "ecs/entity.hh"
#include "glm/fwd.hpp"


namespace Layers
{
	static constexpr JPH::ObjectLayer NON_MOVING = 0;
	static constexpr JPH::ObjectLayer MOVING = 1;
	static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
};


enum CollisionStatus {
	JUST_ACTIVE = 0,
	ACTIVE = 0,
	INACTIVE = 0,
};

class RigidBody {
public:
    virtual void OnCollisionEnter(entity_id) =0;
    virtual void OnCollision(entity_id) =0;
    virtual void OnCollisionLeave(entity_id) =0;

    JPH::BodyCreationSettings getBodyCreationSettings();
    entity_id id;
    JPH::BodyID jolt_id;
    JPH::EAllowedDOFs allowed_dofs = JPH::EAllowedDOFs::Plane2D;
    JPH::Body* body;
protected:     
    JPH::BodyCreationSettings creationSettings;

};


#endif
