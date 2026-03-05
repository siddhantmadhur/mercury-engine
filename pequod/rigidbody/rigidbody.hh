/**
* rigidbody.hh
* RigidBody abstract type - DO NOT EXTEND IN GAME CODE
* It's purpose is to provide a class for set shapes (such as box2d) so they can be used easily
**/
#ifndef PEQUOD_RIGIDBODY_IMPL_HH_
#define PEQUOD_RIGIDBODY_IMPL_HH_

#include <Jolt/Jolt.h>
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "glm/fwd.hpp"


namespace RigidBody {

class RigidBody {
public:
    virtual void OnCollisionEnter() =0;
    virtual void OnCollision() =0;
    virtual void OnCollisionLeave() =0;
    virtual void AddForce(glm::vec2) =0;
    virtual void SetVelocity(glm::vec2) = 0;
    virtual JPH::BodyCreationSettings getCreationSettings() =0;
protected:     
    JPH::BodyID jolt_id;
};

}

#endif
