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
#ifndef MERCURY_PHYSICS_ENGINE_H_
#define MERCURY_PHYSICS_ENGINE_H_
#include <Jolt/Jolt.h>
#include <glm/gtc/quaternion.hpp>
#include <map>
#include <optional>
#include <set>
#include <stack>
#include <vector>
#include "Jolt/Core/Core.h"
#include "Jolt/Core/JobSystem.h"
#include "Jolt/Core/JobSystemThreadPool.h"
#include "Jolt/Core/TempAllocator.h"
#include "Jolt/Physics/Body/Body.h"
#include "Jolt/Physics/Character/CharacterVirtual.h"
#include "Jolt/Physics/PhysicsSystem.h"
#include "debugger/debugger.h"
#include "globals.h"
#include "pobject/manager.h"

namespace Mercury {

/**
 * Jolt's layer setup, internal to the physics engine
 */
namespace physics_detail {

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

}  // namespace physics_detail

using namespace physics_detail;

/**
 * [1]: The entity id of the body it is connecting with
 * [2]: The point at which the raycast hits
 */
using RayCastResult = std::pair<kEntityId, glm::vec3>;

/**
 * @brief What a ray hit (Unity's RaycastHit)
 */
struct RaycastHit {
  kEntityId entity = entt::null;
  glm::vec3 point = glm::vec3(0.0f);
  glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
  float distance = 0.0f;
  bool is_trigger = false;
};

// Unity's Physics.DefaultRaycastLayers: everything but "Ignore Raycast" (2).
constexpr uint32_t kDefaultRaycastLayers = ~(1u << 2);

/**
 * @brief One collision shape of a body, already scaled to world size
 */
struct ColliderShape {
  enum class Kind { kBox, kSphere, kCapsule, kCylinder, kMesh, kConvexHull };
  Kind kind = Kind::kBox;
  glm::vec3 size = glm::vec3(1.0f);  // kBox: full extents
  float radius = 0.5f;               // kSphere, kCapsule, kCylinder
  float height = 2.0f;               // kCapsule (total), kCylinder
  glm::vec3 center = glm::vec3(0.0f);  // offset from the body origin
  glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  // kMesh: triangles; kConvexHull: points (indices unused)
  std::vector<glm::vec3> vertices;
  std::vector<uint32_t> indices;
  bool is_trigger = false;
  float friction = 0.2f;
  float restitution = 0.0f;
};

/**
 * @brief How a body built with CreateBody moves
 */
struct BodyDesc {
  enum class Motion { kStatic, kKinematic, kDynamic };
  Motion motion = Motion::kStatic;
  float mass = 1.0f;  // <= 0: from the shape's volume
  bool linear_cast = false;
  float gravity_factor = 1.0f;
  float linear_damping = 0.05f;
  float angular_damping = 0.05f;
  bool allow_sleeping = true;
  uint8_t allowed_dofs = 0b111111;  // JPH::EAllowedDOFs bits
};

/**
 * @brief A capsule character moved by Move() (Unity's CharacterController)
 */
struct CharacterDesc {
  float height = 2.0f;
  float radius = 0.5f;
  glm::vec3 center = glm::vec3(0.0f);
  float slope_limit = 45.0f;  // degrees
  float step_offset = 0.3f;
  float skin_width = 0.08f;
  float min_move_distance = 0.001f;
};

// Unity's CollisionFlags, returned by CharacterController.Move.
enum CharacterCollisionFlags : uint32_t {
  kCollidedNone = 0,
  kCollidedSides = 1,
  kCollidedAbove = 2,
  kCollidedBelow = 4,
};

enum class TriggerEvent { kEnter, kStay, kExit };
enum class ContactEvent { kEnter, kStay, kExit };

/**
 * @brief Describes a type of collision
 */
enum CollisionType { kCollisionEnter, kCollisionPersisted, kCollisionLeave };

enum class ForceMode {
  kForce,          // Continuous force, using mass (F = m * a)
  kImpulse,        // Instantaneous impulse, using mass
  kVelocityChange, // Instantaneous velocity change, ignoring mass
  kAcceleration,   // Continuous acceleration, ignoring mass
};

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
  PhysicsEngine(PObjectManager& object_manager);
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
   *
   * @param self The object id it is being applied to
   * @param dimensions The dimension of the box being created
   * @param override Lambda function that runs to override any body creating
   * settings
   * @return Successful registration or not
   */
  bool CreateBox3D(
      kEntityId self, glm::vec3 dimensions,
      std::function<void(JPH::BodyCreationSettings&)> override =
          [](JPH::BodyCreationSettings&) {});

  /**
   * @brief Create a sphere body; see CreateBox3D
   */
  bool CreateSphere(
      kEntityId self, float radius,
      std::function<void(JPH::BodyCreationSettings&)> override_function =
          [](JPH::BodyCreationSettings&) {});

  /**
   * @brief Create a capsule body standing along Y; see CreateBox3D
   * @param half_height Half the height of the straight part, without the caps
   */
  bool CreateCapsule(
      kEntityId self, float half_height, float radius,
      std::function<void(JPH::BodyCreationSettings&)> override_function =
          [](JPH::BodyCreationSettings&) {});

  /**
   * @brief Create a cylinder body standing along Y; see CreateBox3D
   */
  bool CreateCylinder(
      kEntityId self, float half_height, float radius,
      std::function<void(JPH::BodyCreationSettings&)> override_function =
          [](JPH::BodyCreationSettings&) {});

  void SetGravity(glm::vec3 gravity);
  glm::vec3 GetGravity();

  // Fixed steps per second Compute() simulates (60 by default).
  void SetTickRate(int ticks_per_second);
  int GetTickRate() const { return tick_rate_; }

  /**
   * @brief Create the body (or bodies) of an entity from any number of shapes
   *
   * Solid shapes share one body; trigger shapes go on a second sensor body
   * that follows it. The body is placed at the entity's Transform.
   * @param layer The node's layer, used by layer-masked raycasts
   */
  bool CreateBody(kEntityId self, const std::vector<ColliderShape>& shapes,
                  const BodyDesc& desc, int layer = 0);

  // The node layer stored for an entity's bodies (see CreateBody).
  void SetLayer(kEntityId self, int layer);

  // Remove an entity's bodies from the world (or add them back) without
  // destroying them, e.g. for SetActive(false).
  void SetBodyEnabled(kEntityId self, bool enabled);

  glm::vec3 GetLinearVelocity(kEntityId self);
  void SetLinearVelocity(kEntityId self, glm::vec3 velocity);
  // Switches a body between dynamic and kinematic (Rigidbody.isKinematic).
  void SetKinematic(kEntityId self, bool kinematic);
  bool IsKinematic(kEntityId self);
  // Moves a kinematic body so it arrives at the pose by the next step.
  void MoveKinematic(kEntityId self, glm::vec3 position, glm::quat rotation);
  // Stop the entity from colliding (Rigidbody.detectCollisions = false).
  void SetCollisionsEnabled(kEntityId self, bool enabled);

  /**
   * @brief Closest hit along a ray, like Unity's Physics.Raycast
   * @param layer_mask Bit N set: hit nodes on layer N
   * @param query_triggers Also hit trigger colliders
   */
  std::optional<RaycastHit> Raycast(glm::vec3 origin, glm::vec3 direction,
                                    float max_distance = 1e30f,
                                    uint32_t layer_mask = kDefaultRaycastLayers,
                                    bool query_triggers = true);
  // Every hit along a ray, nearest first.
  std::vector<RaycastHit> RaycastAll(glm::vec3 origin, glm::vec3 direction,
                                     float max_distance = 1e30f,
                                     uint32_t layer_mask = kDefaultRaycastLayers,
                                     bool query_triggers = true);
  // Entities whose colliders overlap a sphere (Physics.OverlapSphere).
  std::vector<kEntityId> OverlapSphere(glm::vec3 center, float radius,
                                       uint32_t layer_mask = kDefaultRaycastLayers,
                                       bool query_triggers = true);

  // Whether raycasts hit triggers when not told (Physics.queriesHitTriggers).
  void SetQueriesHitTriggers(bool hit) { queries_hit_triggers_ = hit; }
  bool GetQueriesHitTriggers() const { return queries_hit_triggers_; }

  // ---- Characters (Unity's CharacterController) ----
  bool CreateCharacter(kEntityId self, const CharacterDesc& desc, int layer = 0);
  bool HasCharacter(kEntityId self) const { return characters_.contains(self); }
  /**
   * @brief Move the character right away, sliding along what it hits
   * @return CharacterCollisionFlags bits for this move
   */
  uint32_t MoveCharacter(kEntityId self, glm::vec3 displacement);
  bool IsCharacterGrounded(kEntityId self) const;
  glm::vec3 GetCharacterVelocity(kEntityId self) const;
  // Change the capsule; false (and nothing changes) if the new shape would
  // overlap something.
  bool SetCharacterShape(kEntityId self, float height, float radius,
                         glm::vec3 center);
  void RemoveCharacter(kEntityId self);

  /**
   * @brief Receives trigger enter/stay/exit pairs after each step (main
   * thread). `trigger` is the entity with the trigger collider.
   */
  using TriggerCallback = std::function<void(TriggerEvent event, kEntityId trigger,
                                             kEntityId other)>;
  void SetTriggerCallback(TriggerCallback callback) {
    trigger_callback_ = std::move(callback);
  }
  /**
   * @brief Receives collision enter/stay/exit for every pair of entities
   * touching, after each step (main thread)
   */
  using ContactCallback = std::function<void(ContactEvent event, kEntityId a,
                                             kEntityId b)>;
  void SetContactCallback(ContactCallback callback) {
    contact_callback_ = std::move(callback);
  }
  // Called by the contact listener from Jolt's threads.
  void RecordContact(kEntityId a, kEntityId b, bool added);

  // Forces and impulses act on dynamic bodies at the next physics step.
  void AddForce(kEntityId self, glm::vec3 force, ForceMode mode = ForceMode::kForce);
  void AddImpulse(kEntityId self, glm::vec3 impulse);
  void AddTorque(kEntityId self, glm::vec3 torque, ForceMode mode = ForceMode::kForce);
  void SetAngularVelocity(kEntityId self, glm::vec3 radians_per_second);
  glm::vec3 GetAngularVelocity(kEntityId self);

  /**
   * @brief Create a static infinite plane
   * @param self The object id it is being applied to
   * @param normal The normal of the plane
   * @param override_function Lambda function that runs to override any body
   * creating settings
   * @return Successful registration or not
   */
  bool CreatePlane(
      kEntityId self, glm::vec3 normal,
      std::function<void(JPH::BodyCreationSettings&)> override_function =
          [](JPH::BodyCreationSettings&) {});

  /**
   * @brief Unregister a body from the physics engine
   * @param id Entity ID
   *
   * Adds the id to a deletion queue. It's safe to call inside a callback
   * function because it does not modify any data between ticks.
   */
  void DisableBody(kEntityId id);

  // Destroys an entity's bodies (and character) immediately. Only call
  // between steps, never from a collision callback.
  void RemoveBodyNow(kEntityId id);

  /**
   * @brief Add a callback to trigger when bodies collide
   *
   * @tparam T Type of Mercury::CollisionType to trigger the lambda on
   * @param self ID of the registered PObject to add the callback to
   * @param callback_lambda Lambda that is triggered when bodies collide
   */
  template <CollisionType T>
  void AddCollisionCallback(kEntityId self,
                            CollisionCallbackLambda callback_lambda);

  /**
   * @brief Manually triggers the callback function stored in the physics engine
   *
   * @tparam T Type of Mercury::CollisionType to trigger
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
  void Set(kEntityId self, Value val);

  /**
   *   * @param origin The position in world space from which the ray is
   * originating
   * @param direction The direction of the ray as a 3d vector
   * @return whether it has hit a body
   */
  std::optional<RayCastResult> RayCast(glm::vec3 origin, glm::vec3 direction);

 protected:
  std::map<kEntityId, CollisionCallbackLambda> on_collision_persisted_ = {};
  std::map<kEntityId, CollisionCallbackLambda> on_collision_enter_ = {};
  std::map<kEntityId, CollisionCallbackLambda> on_collision_leave_ = {};
  std::map<JPH::BodyID, kEntityId> jolt_bodies_ref_ = {};
  std::map<kEntityId, JPH::BodyID> entity_bodies_ref_ = {};

 private:
  JPH::TempAllocator* temp_allocator_ = nullptr;
  JPH::JobSystem* job_system_thread_pool_ = nullptr;
  JPH::PhysicsSystem physics_system_;

  BPLayerInterfaceImpl broad_phase_layer_interface_;
  ObjectVsBroadPhaseLayerFilterImpl object_vs_broad_phase_layer_filter_impl_;
  ObjectLayerPairFilterImpl object_layer_pair_filter_impl_;

  std::vector<kEntityId> disable_queue_;

  struct CharacterData {
    JPH::Ref<JPH::CharacterVirtual> character;
    CharacterDesc desc;
    int layer = 0;
    glm::vec3 last_position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    bool grounded = false;
  };
  std::map<kEntityId, CharacterData> characters_;
  // Character inner bodies and trigger bodies that share an entity with a
  // solid body; looked up by Get() but never written back to Transforms.
  std::map<JPH::BodyID, kEntityId> secondary_bodies_;
  std::map<kEntityId, JPH::BodyID> sensor_bodies_;  // entity -> its sensor
  std::map<kEntityId, int> layers_;
  std::set<kEntityId> disabled_;
  std::set<kEntityId> no_collisions_;

  // Trigger overlaps found in the last step: (trigger, other).
  std::set<std::pair<kEntityId, kEntityId>> trigger_pairs_;
  TriggerCallback trigger_callback_;
  void UpdateTriggers();

  // Touching pairs (a < b) with how many sub-shape contacts each has.
  std::mutex contact_mutex_;
  std::map<std::pair<kEntityId, kEntityId>, int> contact_counts_;
  std::set<std::pair<kEntityId, kEntityId>> contact_pairs_;
  ContactCallback contact_callback_;
  void UpdateContacts();

  int tick_rate_ = 60;
  bool queries_hit_triggers_ = true;

  JPH::Ref<JPH::Shape> BuildShape(const std::vector<const ColliderShape*>& shapes);
  bool BodyMatches(JPH::BodyID body, uint32_t layer_mask, bool query_triggers);

  // Shared by the Create* functions: places the shape at the entity's
  // Transform (position and rotation) and registers the body.
  bool CreateBodyFromShape(
      kEntityId self, const JPH::ShapeSettings& shape_settings,
      const std::function<void(JPH::BodyCreationSettings&)>& override_function);

  PObjectManager& object_manager_;
};

}  // namespace Mercury

#endif
