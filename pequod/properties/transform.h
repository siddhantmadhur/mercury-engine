//
// Created by smadhur on 4/12/2026.
//

#ifndef PEQUODENGINE_TRANSFORM_H
#define PEQUODENGINE_TRANSFORM_H
#include "property.h"
#include "glm/vec3.hpp"
#include "glm/detail/func_packing_simd.inl"

namespace Pequod
{
    class Transform : public Property
    {
    public:
        Transform();
        glm::vec3 GetPosition() const;
        void SetPosition(glm::vec3);
        glm::vec3 GetInterpolatedPosition() const;

        glm::vec3 GetVelocity() const;
        void SetVelocity(glm::vec3);

        void Move(glm::vec3);

        // Run this every frame to interpolate position between ticks
        void InterpolatePosition(float delta_t, float tick_t);
    private:
        glm::vec3 position_;
        glm::vec3 velocity_{};
        /*
         * Position only updates per tick (which can be significantly lower than fps)
         * so this makes sure its smoothed out
         */
        glm::vec3 interpolated_position_;

    };
} // Pequod

#endif //PEQUODENGINE_TRANSFORM_H
