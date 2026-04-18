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

#include <memory>
#include <Jolt/Jolt.h>
#include "Jolt/Physics/Body/AllowedDOFs.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/MotionType.h"
#include "ecs/entity.hh"
#include "glm/fwd.hpp"
#include "../properties/collision_body.h"


namespace Pequod {
    namespace Layers {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
        static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
    };


    // [CLAUDE] TODO: All enum values are = 0; they should be JUST_ACTIVE=0, ACTIVE=1, INACTIVE=2
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

    private:
        std::shared_ptr<CollisionBody> collision_body_;

    protected:
        JPH::BodyCreationSettings creationSettings;
    };
}

#endif
