//
// Created by smadhur on 4/12/2026.
//

#include "transform.h"

#include "debugger/debugger.h"

namespace Pequod
{
    Transform::Transform()
    {
        this->position_ = glm::vec3(0.0f);
        this->interpolated_position_ = glm::vec3(0.0f);
        this->velocity_ = glm::vec3(0.0f);
    }

    glm::vec3 Transform::GetPosition() const
    {
        return this->position_;
    }
    void Transform::SetPosition(glm::vec3 position)
    {
        this->position_ = position;
        this->interpolated_position_ = position;
    }

    glm::vec3 Transform::GetVelocity() const
    {
        return this->velocity_;
    }
    void Transform::SetVelocity(glm::vec3 velocity)
    {
        this->velocity_ = velocity;
    }

    void Transform::Move(glm::vec3 movement)
    {
        this->position_ += movement;
    }

    glm::vec3 Transform::GetInterpolatedPosition() const
    {
        return this->interpolated_position_;
    }

    void Transform::InterpolatePosition(float delta_t, float tick_t)
    {
        if (delta_t == 0 || (tick_t / delta_t) == 0)
        {
            return;
        }
        /*
         * The goal is to smooth out position between ticks
         * If FPS is 240 and TPS is 20 there are 240/20 -> 12 frames between every tick
         * Tick 1 <--> Tick 2
         * If there
         */
        interpolated_position_ += (position_ - interpolated_position_) * (delta_t / tick_t);

    }
} // Pequod