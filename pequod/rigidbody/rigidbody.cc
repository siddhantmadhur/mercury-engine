#include <Jolt/Jolt.h>
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include <rigidbody/rigidbody.hh>

JPH::BodyCreationSettings RigidBody::getBodyCreationSettings() {
    return this->creationSettings;
}
