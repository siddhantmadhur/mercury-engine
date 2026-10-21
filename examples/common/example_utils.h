/*
 * example_utils.h
 *
 * Small helpers shared by the example games.
 */
#ifndef MERCURY_EXAMPLES_EXAMPLE_UTILS_H
#define MERCURY_EXAMPLES_EXAMPLE_UTILS_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include <cmath>
#include <mutex>
#include <utility>
#include <vector>

#include "globals.h"
#include "physics_engine/physics_engine.h"
#include "pobject/manager.h"
#include "properties/transform.h"

namespace Examples {

/**
 * @brief Point the scene camera straight down -Z so the z = 0 plane fills a
 * `view_height` world-unit tall view
 *
 * The engine camera is a 60 degree perspective camera, so the distance is
 * picked to make one world unit cover exactly height_px / view_height pixels
 * on the z = 0 plane. +X is right and +Y is up.
 */
inline void SetupCamera2D(Mercury::PObjectManager& objects, kEntityId camera,
                          float view_height) {
  constexpr float kHalfFovRadians = 30.0f * 3.14159265f / 180.0f;
  const float distance = (view_height / 2.0f) / std::tan(kHalfFovRadians);
  Mercury::Transform transform(glm::vec3(0.0f, 0.0f, distance));
  transform.SetRotate(glm::vec3(0.0f, -90.0f, 0.0f));  // yaw -90 -> look -Z
  objects.AddProperty(camera, transform);
}

/**
 * @brief Body settings shared by every moving body in the examples
 *
 * Restricts motion to the XY plane without rotation and removes damping,
 * friction and gravity, so bounces keep their speed.
 */
inline void Configure2DBody(JPH::BodyCreationSettings& settings,
                            JPH::EMotionType motion) {
  settings.mMotionType = motion;
  settings.mAllowedDOFs =
      JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY;
  settings.mLinearDamping = 0.0f;
  settings.mAngularDamping = 0.0f;
  settings.mFriction = 0.0f;
  settings.mGravityFactor = 0.0f;
  settings.mAllowSleeping = false;
  settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
}

/**
 * @brief Collisions recorded from Jolt's contact callbacks
 *
 * Jolt calls contact listeners from its worker threads, so the callbacks only
 * record (self, other) pairs here; the scene drains them in OnTick, where it
 * is safe to touch the registry and the physics engine.
 */
class CollisionQueue {
 public:
  void Push(kEntityId self, kEntityId other) {
    std::lock_guard lock(mutex_);
    events_.emplace_back(self, other);
  }

  std::vector<std::pair<kEntityId, kEntityId>> Drain() {
    std::lock_guard lock(mutex_);
    return std::exchange(events_, {});
  }

 private:
  std::mutex mutex_;
  std::vector<std::pair<kEntityId, kEntityId>> events_;
};

// An entity counts as alive until DestroyEntity removes its Transform.
inline bool IsAlive(Mercury::PObjectManager& objects, kEntityId id) {
  return objects.GetProperty<Mercury::Transform>(id) != nullptr;
}

/**
 * @brief Remove an entity's physics body and its renderable properties
 *
 * Call from OnTick, never from a collision callback. Safe to call twice.
 */
inline void DestroyEntity(Mercury::PhysicsEngine& physics,
                          Mercury::PObjectManager& objects, kEntityId id) {
  if (!IsAlive(objects, id)) return;
  physics.DisableBody(id);
  objects.DeleteObject(id);
}

}  // namespace Examples

#endif  // MERCURY_EXAMPLES_EXAMPLE_UTILS_H
