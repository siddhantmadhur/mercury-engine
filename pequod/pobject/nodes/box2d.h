/*
 * box2d.h
 *
 * Box2D to create a default cube easily
 */

#ifndef PEQUOD_POBJECT_BOX_2D_H_
#define PEQUOD_POBJECT_BOX_2D_H_
#include "pobject/pobject.h"

namespace Pequod {
    class Box2D : public PObject {
    public:
        Box2D(
            glm::vec2 position = glm::vec2(0.0f),
            glm::vec2 size = glm::vec2(1.0f),
            glm::vec4 color = glm::vec4(1.0f)
            );
    };
}

#endif //