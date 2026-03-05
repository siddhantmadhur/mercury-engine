/**
* rigidbody/box2d.hh
**/
#ifndef PEQUOD_RIGIDBODY_BOX2D_IMPL_HH_
#define PEQUOD_RIGIDBODY_BOX2D_IMPL_HH_

#include <Jolt/Jolt.h>
#include "Jolt/Physics/Body/Body.h"
#include "glm/fwd.hpp"
#include <rigidbody/rigidbody.hh>


namespace RigidBody {

class Box2D : public RigidBody {
public:
    Box2D(glm::vec2 position, glm::vec2 size); // position is center of mass
    JPH::BodyCreationSettings getCreationSettings();
    void AddForce(glm::vec2);
    void SetVelocity(glm::vec2);
protected:
};

}
#endif
