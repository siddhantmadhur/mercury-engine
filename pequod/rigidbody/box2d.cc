#include "box2d.hh"
#include <Jolt/Jolt.h>
#include "Jolt/Physics/Body/AllowedDOFs.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include <debugger/debugger.hh>
#include <format>


namespace Pequod {
    Box2D::Box2D (glm::vec2 position, glm::vec2 size) {
        JPH::BoxShapeSettings player_shape_settings(JPH::Vec3(size.x / 2.0f, size.y / 2.0f, 1));
        player_shape_settings.SetEmbedded();

        JPH::ShapeSettings::ShapeResult player_shape_result = player_shape_settings.Create();
        JPH::ShapeRefC player_shape = player_shape_result.Get();


        this->creationSettings = JPH::BodyCreationSettings(player_shape, JPH::RVec3(position.x, position.y, 0.0f), JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic, Layers::MOVING);
        this->creationSettings.mAllowedDOFs = JPH::EAllowedDOFs::Plane2D;
    }


}