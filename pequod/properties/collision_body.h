/*
 * collision_body.h
 *
 * Provides details and information required to create physical bodies
 * on the game engine. Supposed to be agnostic but is directly related
 * to Jolt Physics.
 *
 */

#ifndef PEQUOD_PHYSICS_COLLISION_SHAPE_H_
#define PEQUOD_PHYSICS_COLLISION_SHAPE_H_

#include "pobject/pobject.h"
#include "properties/property.h"

namespace Pequod
{
    class CollisionBody : public Property
    {
    public:
        // reference to the body its handling
        CollisionBody();
    };
}

#endif //
