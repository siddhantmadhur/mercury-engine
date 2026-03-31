/**
* rigidbody/box2d.hh
**/
#ifndef PEQUOD_RIGIDBODY_BOX2D_IMPL_HH_
#define PEQUOD_RIGIDBODY_BOX2D_IMPL_HH_

#include <Jolt/Jolt.h>
#include "Jolt/Physics/Body/Body.h"
#include "glm/fwd.hpp"
#include <rigidbody/rigidbody.hh>

namespace Pequod {
    class Box2D : public RigidBody {
    public:
        Box2D(glm::vec2 position, glm::vec2 size); // position is center of mass
        virtual void OnCollisionEnter(entity_id) =0;
        virtual void OnCollision(entity_id) =0;
        virtual void OnCollisionLeave(entity_id) =0;

    protected:
    };

}

#endif
