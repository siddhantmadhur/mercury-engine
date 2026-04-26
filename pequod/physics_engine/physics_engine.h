/**
 * physics_engine.h
 *
 * Physics system for game objects
 * The way it works is that the user has to define a "shape" to encapsulate the
 * object, along with size. For example if it's a capsule the shape has to be a
 * capsule shape with the dimensions for it.
 *
 * Then during every tick, the position is inferred from the object system and
 * the velocity is synchronized with it as well. So if the physics system
 * figures out that it's reached a wall, it will set the velocity to 0.
 *
 */
#ifndef PEQUOD_PHYSICS_ENGINE_H_
#define PEQUOD_PHYSICS_ENGINE_H_
#include <Jolt/Jolt.h>
#include "shapes/plane.h"
#include <map>
#include <stack>
#include "Jolt/Core/Core.h"
#include "Jolt/Core/JobSystem.h"
#include "Jolt/Core/JobSystemThreadPool.h"
#include "Jolt/Core/TempAllocator.h"
#include "Jolt/Physics/Body/Body.h"
#include "Jolt/Physics/PhysicsSystem.h"
#include "debugger/debugger.h"
#include "globals.h"
#include "pobject/manager.h"
#include "properties/collision_body.h"
#include "shapes/box.h"

namespace Pequod {

/**
 * Empty namespace to register Jolt-specific classes
 */
namespace {

namespace Layers {
static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING = 1;
static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
};  // namespace Layers

class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
 public:
  virtual bool ShouldCollide(JPH::ObjectLayer inLayer1,
                             JPH::ObjectLayer inLayer2) const override {
    switch (inLayer1) {
      case Layers::NON_MOVING:
        return inLayer2 == Layers::MOVING;
      case Layers::MOVING:
        return true;
      default:
        JPH_ASSERT(false);
        return false;
    }
  }
};

namespace BroadPhaseLayers {
static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer MOVING(1);
static constexpr UINT NUM_LAYERS(2);
};  // namespace BroadPhaseLayers

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
 public:
  BPLayerInterfaceImpl() {
    // Create a mapping table from object to broad phase layer
    mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
    mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
  }
  virtual UINT GetNumBroadPhaseLayers() const override {
    return BroadPhaseLayers::NUM_LAYERS;
  }
  virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(
      JPH::ObjectLayer inLayer) const override {
    JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
    return mObjectToBroadPhase[inLayer];
  }

 private:
  JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl
    : public JPH::ObjectVsBroadPhaseLayerFilter {
 public:
  virtual bool ShouldCollide(JPH::ObjectLayer inLayer1,
                             JPH::BroadPhaseLayer inLayer2) const override {
    switch (inLayer1) {
      case Layers::NON_MOVING:
        return inLayer2 == BroadPhaseLayers::MOVING;
      case Layers::MOVING:
        return true;
      default:
        JPH_ASSERT(false);
        return false;
    }
  }
};

}  // namespace

/**
 * @brief Describes a type of collision
 */
enum CollisionType { kCollisionEnter, kCollisionPersisted, kCollisionLeave };

enum PhysicsProperty {
  kFriction,
  kRestitution,
  kGravity,
  kMotionType,
};

/**
 * @brief Function type that runs whenever bodies collide
 */
using CollisionCallbackLambda =
    std::function<void(kEntityId self, kEntityId other)>;

/**
 * @brief Provides easy-access to Physics using Jolt
 *
 * It is a basic wrapper around Jolt. It is designed to be simple and not add
 * features, rather rely completely on Jolt for most things like resource
 * management and physics simulations.
 *
 * Rather it provides a way to interface PObject's with Physics. Like allowing
 * on collision functions to be defined as lambdas that are called on collision
 * rather than complete classes that have to stay through the entire program
 * lifetime.
 */
class PhysicsEngine {
 public:
  PhysicsEngine();
  ~PhysicsEngine();

  /**
   * @brief Initialize Jolt before computing
   */
  void Initialize();

  /**
   * @brief Calculate the physics for the environment such as collision
   * @param steps No. of steps to take in a computation (1 every 60hz)
   *
   * First is that it synchronizes transformations between Jolt and
   * PObjectManager. If a user sets a position it has to take preference over
   * Jolt so first it copies over all manual transformations to the physics
   * system. Then it computes the physics using Jolt using these values and any
   * prior values. Then in the second step it synchronizes all the changes by
   * Jolt to the PObjectManager
   *
   */
  void Compute(int steps);

  /**
   * @brief Register a body to the physics engine before adding properties
   */
  template <class T>
  void RegisterBody(std::shared_ptr<PObject> self, T collision_body,
                    JPH::EAllowedDOFs allowed_dofs)
    requires std::derived_from<T, CollisionBody>;

  /**
   * @brief Unregister a body from the physics engine
   * @param id Entity ID
   *
   * Adds the id to a deletion queue. It's safe to call inside a callback
   * function because it does not modify any data between ticks.
   */
  void DisableBody(kEntityId id);

  /**
   * @brief Add a callback to trigger when bodies collide
   *
   * @tparam T Type of Pequod::CollisionType to trigger the lambda on
   * @param self ID of the registered PObject to add the callback to
   * @param callback_lambda Lambda that is triggered when bodies collide
   */
  template <CollisionType T>
  void AddCollisionCallback(kEntityId self,
                            CollisionCallbackLambda callback_lambda);

  /**
   * @brief Manually triggers the callback function stored in the physics engine
   *
   * @tparam T Type of Pequod::CollisionType to trigger
   * @param self ID of the body the trigger is attached to
   * @param other ID of the body being collided with
   */
  template <CollisionType T>
  void TriggerCollisionCallback(kEntityId self, kEntityId other);

  /**
   * @brief Get the associated PObject ID from Jolts system
   * @return The associated PObject ID from the JoltID
   */
  kEntityId Get(JPH::BodyID);

  /**
   * @brief Check whether an entity has a corresponding physics registration
   *
   * @param id Entity ID
   * @return Is the entity registered on the physics system
   */
  bool IsRegistered(kEntityId id);

  /**
   * @brief Synchronize the PObjects with manual transformations
   */
  void SynchronizePObjects();

  /**
   * @brief Set a physics property for an object such as Linear Accel or
   * Position
   * @tparam TProperty The physics property to modify
   * @tparam Value The value to set for the property
   */
  template <PhysicsProperty TProperty, typename Value>
  void Set(const PObject& obj, Value val);

 protected:
  std::map<kEntityId, CollisionCallbackLambda> on_collision_persisted_ = {};
  std::map<kEntityId, CollisionCallbackLambda> on_collision_enter_ = {};
  std::map<kEntityId, CollisionCallbackLambda> on_collision_leave_ = {};
  std::map<JPH::BodyID, kEntityId> jolt_bodies_ref_ = {};
  std::map<kEntityId, JPH::BodyID> entity_bodies_ref_ = {};
  std::map<kEntityId, std::shared_ptr<PObject>> registered_bodies_ = {};

 private:
  JPH::TempAllocator* temp_allocator_ = nullptr;
  JPH::JobSystemThreadPool* job_system_thread_pool_ = nullptr;
  JPH::PhysicsSystem physics_system_;

  BPLayerInterfaceImpl broad_phase_layer_interface_;
  ObjectVsBroadPhaseLayerFilterImpl object_vs_broad_phase_layer_filter_impl_;
  ObjectLayerPairFilterImpl object_layer_pair_filter_impl_;

  std::vector<kEntityId> disable_queue_;
};

}  // namespace Pequod

#endif
