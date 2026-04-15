//
// Created by smadhur on 4/12/2026.
//

#ifndef PEQUODENGINE_BOX_H
#define PEQUODENGINE_BOX_H
#include "glm/vec3.hpp"
#include "glm/detail/func_packing_simd.inl"
#include "../../properties/collision_body.h"

namespace Pequod {
    class Box : public CollisionBody
    {
    public:
        Box(glm::vec3 dim);
    };

} // Pequod

#endif //PEQUODENGINE_BOX_H
