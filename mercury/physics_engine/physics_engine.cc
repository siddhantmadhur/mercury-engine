#include "physics_engine.h"

#include <algorithm>
#include <cmath>

#include "Jolt/Core/Factory.h"
#include "Jolt/Core/JobSystemSingleThreaded.h"
#include "Jolt/Core/JobSystemThreadPool.h"
#include "Jolt/RegisterTypes.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/CylinderShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/PlaneShape.h"
#include "Jolt/Physics/Collision/Shape/ConvexHullShape.h"
#include "Jolt/Physics/Collision/Shape/MeshShape.h"
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"
#include "Jolt/Physics/Collision/Shape/StaticCompoundShape.h"
#include "Jolt/Physics/Collision/CollideShape.h"
#include "Jolt/Physics/Collision/CollisionCollectorImpl.h"
#include "Jolt/Physics/Collision/ShapeCast.h"
#include "components/components.h"
#include "properties/transform.h"

namespace Mercury {
template <>
auto PhysicsEngine::TriggerCollisionCallback<kCollisionPersisted>(
    kEntityId self, kEntityId other) -> void {
  if (on_collision_persisted_.contains(self)) {
    on_collision_persisted_[self](self, other);
  }
  if (on_collision_persisted_.contains(other)) {
    on_collision_persisted_[other](other, self);
  }
}

template <>
void PhysicsEngine::TriggerCollisionCallback<kCollisionEnter>(kEntityId self,
                                                              kEntityId other) {
  if (on_collision_enter_.contains(self)) {
    on_collision_enter_[self](self, other);
  }
  if (on_collision_enter_.contains(other)) {
    on_collision_enter_[other](other, self);
  }
}

template <>
void PhysicsEngine::TriggerCollisionCallback<kCollisionLeave>(kEntityId self,
                                                              kEntityId other) {
  if (on_collision_leave_.contains(self)) {
    on_collision_leave_[self](self, other);
  }
  if (on_collision_leave_.contains(other)) {
    on_collision_leave_[other](other, self);
  }
}

namespace {
class PhysicsContactListener : public JPH::ContactListener {
 public:
  PhysicsContactListener(PhysicsEngine* physics_engine) {
    this->physics_engine_ = physics_engine;
    PDebug::info("Created physics listener");
  }

  virtual JPH::ValidateResult OnContactValidate(
      const JPH::Body& inBody1, const JPH::Body& inBody2,
      JPH::RVec3Arg inBaseOffset,
      const JPH::CollideShapeResult& inCollisionResult) override {
    return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
  }
  virtual void OnContactAdded(const JPH::Body& inBody1,
                              const JPH::Body& inBody2,
                              const JPH::ContactManifold& inManifold,
                              JPH::ContactSettings& ioSettings) override {
    auto self = physics_engine_->Get(inBody1.GetID());
    auto other = physics_engine_->Get(inBody2.GetID());

    // Triggers are found by overlap queries instead (PhysicsEngine::
    // UpdateTriggers), which also see characters and static triggers.
    if (inBody1.IsSensor() || inBody2.IsSensor()) return;
    physics_engine_->TriggerCollisionCallback<kCollisionEnter>(self, other);
    physics_engine_->RecordContact(self, other, true);
  }
  virtual void OnContactPersisted(const JPH::Body& inBody1,
                                  const JPH::Body& inBody2,
                                  const JPH::ContactManifold& inManifold,
                                  JPH::ContactSettings& ioSettings) override {
    if (inBody1.IsSensor() || inBody2.IsSensor()) return;
    auto self = physics_engine_->Get(inBody1.GetID());
    auto other = physics_engine_->Get(inBody2.GetID());

    physics_engine_->TriggerCollisionCallback<kCollisionPersisted>(self, other);
  }
  virtual void OnContactRemoved(
      const JPH::SubShapeIDPair& inSubShapePair) override {
    auto bodyID1 = inSubShapePair.GetBody1ID();
    auto bodyID2 = inSubShapePair.GetBody2ID();
    auto self = physics_engine_->Get(bodyID1);
    auto other = physics_engine_->Get(bodyID2);

    physics_engine_->TriggerCollisionCallback<kCollisionLeave>(self, other);
    physics_engine_->RecordContact(self, other, false);
  }

 private:
  PhysicsEngine* physics_engine_ = nullptr;
};
}  // namespace

PhysicsEngine::PhysicsEngine(PObjectManager& object_manager)
    : object_manager_(object_manager) {
  PDebug::info("Created physics engine...");
  JPH::RegisterDefaultAllocator();
  JPH::Factory::sInstance = new JPH::Factory();
  JPH::RegisterTypes();
  temp_allocator_ = new JPH::TempAllocatorImpl(10 * 1024 * 1024);  // 10MB

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
  job_system_thread_pool_ =
      new JPH::JobSystemSingleThreaded(JPH::cMaxPhysicsJobs);
#else
  job_system_thread_pool_ = new JPH::JobSystemThreadPool(
      JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
      JPH::thread::hardware_concurrency() - 1);
#endif
}
PhysicsEngine::~PhysicsEngine() {
  PDebug::info("Deleting physics engine...");
  if (job_system_thread_pool_) {
    delete job_system_thread_pool_;
  }
  if (temp_allocator_) {
    delete temp_allocator_;
  }
}

void PhysicsEngine::Initialize() {
  using JPH::uint;
  const uint cMaxBodies = 8192;
  const uint cNumBodyMutexes = 0;
  const uint cMaxBodyPairs = 8192;
  const uint cMaxContactConstraints = 8192;

  physics_system_.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs,
                       cMaxContactConstraints, broad_phase_layer_interface_,
                       object_vs_broad_phase_layer_filter_impl_,
                       object_layer_pair_filter_impl_);

  auto contact_listener = new PhysicsContactListener(this);
  physics_system_.SetContactListener(contact_listener);
  physics_system_.SetGravity(JPH::Vec3Arg{0.0, -98.0, 0.0});
  PDebug::info("Initialized Jolt Physics System");
}

namespace {
JPH::Quat ToJolt(glm::quat q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
glm::quat FromJolt(JPH::Quat q) {
  return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}
}  // namespace

bool PhysicsEngine::CreateBodyFromShape(
    kEntityId self, const JPH::ShapeSettings& shape_settings,
    const std::function<void(JPH::BodyCreationSettings&)>& override_function) {
  auto transform = object_manager_.GetProperty<Transform>(self);
  if (transform == nullptr) {
    PDebug::error("Physics body needs transform property to register");
    return false;
  }
  JPH::ShapeSettings::ShapeResult shape_result = shape_settings.Create();
  if (shape_result.HasError()) {
    PDebug::error("Physics: could not create shape: {}",
                  shape_result.GetError().c_str());
    return false;
  }

  auto position = transform->GetPosition();
  auto creation_settings = JPH::BodyCreationSettings(
      shape_result.Get(), JPH::RVec3(position.x, position.y, position.z),
      ToJolt(transform->GetRotationQuat()), JPH::EMotionType::Static,
      Layers::MOVING);

  override_function(creation_settings);
  // Static bodies never need to be tested against each other.
  if (creation_settings.mMotionType == JPH::EMotionType::Static) {
    creation_settings.mObjectLayer = Layers::NON_MOVING;
  }

  auto& body_interface = physics_system_.GetBodyInterface();
  auto body = body_interface.CreateBody(creation_settings);
  if (body == nullptr) {
    PDebug::error("Physics: out of bodies");
    return false;
  }

  auto body_id = body->GetID();
  this->jolt_bodies_ref_[body_id] = self;
  this->entity_bodies_ref_[self] = body_id;

  body_interface.AddBody(body_id, JPH::EActivation::Activate);
  return true;
}

bool PhysicsEngine::CreateBox3D(
    kEntityId self, glm::vec3 dimensions,
    std::function<void(JPH::BodyCreationSettings&)> override_function) {
  // Jolt needs every half extent to be at least the convex radius (the
  // rounding of the edges), so thin boxes get sharper edges.
  glm::vec3 half = glm::max(glm::abs(dimensions) / 2.0f, glm::vec3(1e-3f));
  const float convex_radius =
      std::min(JPH::cDefaultConvexRadius, std::min({half.x, half.y, half.z}));
  JPH::BoxShapeSettings box_shape_settings(JPH::Vec3(half.x, half.y, half.z),
                                           convex_radius);
  box_shape_settings.SetEmbedded();
  return CreateBodyFromShape(self, box_shape_settings, override_function);
}

bool PhysicsEngine::CreateSphere(
    kEntityId self, float radius,
    std::function<void(JPH::BodyCreationSettings&)> override_function) {
  JPH::SphereShapeSettings settings(std::max(std::abs(radius), 1e-3f));
  settings.SetEmbedded();
  return CreateBodyFromShape(self, settings, override_function);
}

bool PhysicsEngine::CreateCapsule(
    kEntityId self, float half_height, float radius,
    std::function<void(JPH::BodyCreationSettings&)> override_function) {
  radius = std::max(std::abs(radius), 1e-3f);
  // A capsule with no straight part is a sphere.
  if (half_height <= 1e-4f) return CreateSphere(self, radius, override_function);
  JPH::CapsuleShapeSettings settings(half_height, radius);
  settings.SetEmbedded();
  return CreateBodyFromShape(self, settings, override_function);
}

bool PhysicsEngine::CreateCylinder(
    kEntityId self, float half_height, float radius,
    std::function<void(JPH::BodyCreationSettings&)> override_function) {
  half_height = std::max(std::abs(half_height), 1e-3f);
  radius = std::max(std::abs(radius), 1e-3f);
  const float convex_radius =
      std::min(JPH::cDefaultConvexRadius, std::min(half_height, radius));
  JPH::CylinderShapeSettings settings(half_height, radius, convex_radius);
  settings.SetEmbedded();
  return CreateBodyFromShape(self, settings, override_function);
}

void PhysicsEngine::SetGravity(glm::vec3 gravity) {
  physics_system_.SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
}

namespace {
// Jolt asserts (SIGTRAP, exit 133) on NaN/inf vectors; e.g. glm::normalize of
// a zero-length vector yields NaN.
bool IsFiniteVec(const glm::vec3& v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
}  // namespace

void PhysicsEngine::AddForce(kEntityId self, glm::vec3 force, ForceMode mode) {
  if (!IsFiniteVec(force)) {
    PDebug::error("AddForce ignored: non-finite force (normalized a zero vector?)");
    return;
  }
  auto it = entity_bodies_ref_.find(self);
  if (it == entity_bodies_ref_.end()) return;
  auto& bi = physics_system_.GetBodyInterface();
  const JPH::BodyID body_id = it->second;

  float mass = 1.0f;
  if (auto rb = object_manager_.GetProperty<Rigidbody>(self)) {
    if (rb->mass > 0.0f) mass = rb->mass;
  }

  switch (mode) {
    case ForceMode::kForce:
      bi.AddForce(body_id, JPH::Vec3(force.x, force.y, force.z));
      break;
    case ForceMode::kImpulse:
      bi.AddImpulse(body_id, JPH::Vec3(force.x, force.y, force.z));
      break;
    case ForceMode::kAcceleration:
      bi.AddForce(body_id,
                  JPH::Vec3(force.x * mass, force.y * mass, force.z * mass));
      break;
    case ForceMode::kVelocityChange:
      bi.AddImpulse(body_id,
                    JPH::Vec3(force.x * mass, force.y * mass, force.z * mass));
      break;
  }
}

void PhysicsEngine::AddImpulse(kEntityId self, glm::vec3 impulse) {
  AddForce(self, impulse, ForceMode::kImpulse);
}

void PhysicsEngine::AddTorque(kEntityId self, glm::vec3 torque, ForceMode mode) {
  if (!IsFiniteVec(torque)) {
    PDebug::error("AddTorque ignored: non-finite torque (normalized a zero vector?)");
    return;
  }
  auto it = entity_bodies_ref_.find(self);
  if (it == entity_bodies_ref_.end()) return;
  auto& bi = physics_system_.GetBodyInterface();
  const JPH::BodyID body_id = it->second;

  float mass = 1.0f;
  if (auto rb = object_manager_.GetProperty<Rigidbody>(self)) {
    if (rb->mass > 0.0f) mass = rb->mass;
  }

  switch (mode) {
    case ForceMode::kForce:
      bi.AddTorque(body_id, JPH::Vec3(torque.x, torque.y, torque.z));
      break;
    case ForceMode::kImpulse:
      bi.AddAngularImpulse(body_id, JPH::Vec3(torque.x, torque.y, torque.z));
      break;
    case ForceMode::kAcceleration:
      bi.AddTorque(body_id,
                   JPH::Vec3(torque.x * mass, torque.y * mass, torque.z * mass));
      break;
    case ForceMode::kVelocityChange:
      bi.AddAngularImpulse(body_id,
                           JPH::Vec3(torque.x * mass, torque.y * mass, torque.z * mass));
      break;
  }
}

void PhysicsEngine::SetAngularVelocity(kEntityId self, glm::vec3 velocity) {
  if (!IsFiniteVec(velocity)) return;
  auto it = entity_bodies_ref_.find(self);
  if (it == entity_bodies_ref_.end()) return;
  physics_system_.GetBodyInterface().SetAngularVelocity(
      it->second, JPH::Vec3(velocity.x, velocity.y, velocity.z));
}

glm::vec3 PhysicsEngine::GetAngularVelocity(kEntityId self) {
  auto it = entity_bodies_ref_.find(self);
  if (it == entity_bodies_ref_.end()) return glm::vec3(0.0f);
  auto v = physics_system_.GetBodyInterface().GetAngularVelocity(it->second);
  return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

bool PhysicsEngine::CreatePlane(
    kEntityId self, glm::vec3 normal,
    std::function<void(JPH::BodyCreationSettings&)> override_function) {
  PDebug::info("Registering body to physics engine...");
  auto transform = object_manager_.GetProperty<Transform>(self);
  if (transform == nullptr) {
    PDebug::error("Physics body needs transform property to register");
    return false;
  }

  auto position = transform->GetPosition();
  JPH::Plane plane(JPH::Vec3(normal.x, normal.y, normal.z), 0);
  JPH::PlaneShapeSettings plane_shape_settings(plane);
  plane_shape_settings.SetEmbedded();

  JPH::ShapeSettings::ShapeResult shape_result = plane_shape_settings.Create();
  auto shape_ref = shape_result.Get();

  auto creation_settings = JPH::BodyCreationSettings(
      shape_ref, JPH::RVec3(position.x, position.y, position.z),
      JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NON_MOVING);

  override_function(creation_settings);

  auto& body_interface = physics_system_.GetBodyInterface();

  auto body = body_interface.CreateBody(creation_settings);

  auto body_id = body->GetID();
  this->jolt_bodies_ref_[body_id] = self;
  this->entity_bodies_ref_[self] = body_id;

  body_interface.AddBody(body_id, JPH::EActivation::Activate);
  return true;
}

void PhysicsEngine::SynchronizePObjects() {
  auto& body_interface = physics_system_.GetBodyInterface();
  for (const auto& pair : entity_bodies_ref_) {
    auto entity_id = pair.first;
    auto jolt_id = pair.second;
    auto transform = object_manager_.GetProperty<Transform>(entity_id);
    if (transform) {
      auto transformations = transform->GetTransformations();
      auto jolt_id = entity_bodies_ref_[entity_id];

      for (auto transform_type : transformations) {
        switch (transform_type) {
          case kTransformLinearVelocity: {
            auto vel = transform->GetVelocity();
            body_interface.SetLinearVelocity(jolt_id,
                                             JPH::Vec3(vel.x, vel.y, vel.z));
            break;
          }
          case kTransformPosition: {
            auto pos = transform->GetPosition();
            body_interface.SetPosition(jolt_id, JPH::Vec3(pos.x, pos.y, pos.z),
                                       JPH::EActivation::Activate);
            break;
          }
          case kTransformRotation: {
            body_interface.SetRotation(jolt_id,
                                       ToJolt(transform->GetRotationQuat()),
                                       JPH::EActivation::Activate);
            break;
          }
          default:
            break;
        }
      }
    }
  }
}
std::optional<RayCastResult> PhysicsEngine::RayCast(glm::vec3 origin,
                                                    glm::vec3 direction) {
  JPH::RRayCast ray{JPH::Vec3(origin.x, origin.y, origin.z),
                    JPH::Vec3(direction.x, direction.y, direction.z)};
  JPH::RayCastResult ray_hit;

  if (physics_system_.GetNarrowPhaseQuery().CastRay(ray, ray_hit)) {
    auto collision_point = ray.GetPointOnRay(ray_hit.mFraction);
    auto body_id = ray_hit.mBodyID;
    auto entity_id = jolt_bodies_ref_[body_id];
    RayCastResult result = std::make_pair(
        entity_id, glm::vec3(collision_point.GetX(), collision_point.GetY(),
                             collision_point.GetZ()));

    return result;
  }

  return std::nullopt;
}

void PhysicsEngine::Compute(int steps) {
  // If the scene defines a velocity/position it should overwrite what the
  // physics has calculated
  this->SynchronizePObjects();
  const float step = 1.0f / static_cast<float>(tick_rate_);
  for (int i = 0; i < steps; i++) {
    physics_system_.Update(step, 1, temp_allocator_, job_system_thread_pool_);
  }

  auto& body_interface = physics_system_.GetBodyInterface();

  // Handle deletion queue here
  while (!disable_queue_.empty()) {
    kEntityId id = disable_queue_.back();

    auto body_id = entity_bodies_ref_[id];

    if (!disabled_.contains(id)) body_interface.RemoveBody(body_id);
    body_interface.DestroyBody(body_id);
    jolt_bodies_ref_.erase(body_id);
    entity_bodies_ref_.erase(id);
    if (auto sensor = sensor_bodies_.find(id); sensor != sensor_bodies_.end()) {
      if (!disabled_.contains(id)) body_interface.RemoveBody(sensor->second);
      body_interface.DestroyBody(sensor->second);
      secondary_bodies_.erase(sensor->second);
      sensor_bodies_.erase(sensor);
    }
    disabled_.erase(id);
    layers_.erase(id);
    no_collisions_.erase(id);
    on_collision_enter_.erase(id);
    on_collision_leave_.erase(id);
    on_collision_persisted_.erase(id);
    disable_queue_.pop_back();
  }

  // The reason this runs again is because sometimes if a collision changes the
  // position it needs to overwrite jolt again
  this->SynchronizePObjects();

  for (const auto& pair : jolt_bodies_ref_) {
    auto body_id = pair.first;
    auto entity_id = pair.second;

    auto transform = object_manager_.GetProperty<Transform>(entity_id);
    if (transform == nullptr) continue;
    auto transformations = transform->GetTransformations();
    // The list is in call order, not sorted, so it has to be searched
    // linearly.
    auto was_set = [&](TransformationType type) {
      return std::find(transformations.begin(), transformations.end(), type) !=
             transformations.end();
    };
    if (!was_set(kTransformPosition)) {
      auto phys_position = body_interface.GetPosition(body_id);
      transform->SetPosition(glm::vec3(
          phys_position.GetX(), phys_position.GetY(), phys_position.GetZ()));
    }
    // Report Jolt's velocity back so Transform::GetVelocity() reflects
    // collisions, gravity and damping rather than the last value set.
    if (!was_set(kTransformLinearVelocity)) {
      auto phys_velocity = body_interface.GetLinearVelocity(body_id);
      transform->SetVelocity(glm::vec3(
          phys_velocity.GetX(), phys_velocity.GetY(), phys_velocity.GetZ()));
    }
    if (!was_set(kTransformRotation)) {
      // Only write back real turns (over ~0.2 degrees from the Transform),
      // so the angles a map set survive while a body is still instead of
      // becoming Jolt's equivalent ones.
      const glm::quat rotation = FromJolt(body_interface.GetRotation(body_id));
      if (std::abs(glm::dot(rotation, transform->GetRotationQuat())) <
          1.0f - 1e-6f) {
        transform->SetRotate(Transform::QuatToEuler(rotation));
      }
    }
    transform->ClearTransformations();
  }

  // Trigger bodies that share an entity follow its solid body.
  for (const auto& [entity, sensor] : sensor_bodies_) {
    auto main = entity_bodies_ref_.find(entity);
    if (main == entity_bodies_ref_.end() || disabled_.contains(entity)) continue;
    body_interface.SetPositionAndRotation(
        sensor, body_interface.GetPosition(main->second),
        body_interface.GetRotation(main->second), JPH::EActivation::DontActivate);
  }

  UpdateTriggers();
  UpdateContacts();
}

void PhysicsEngine::RemoveBodyNow(kEntityId id) {
  RemoveCharacter(id);
  std::erase(disable_queue_, id);
  auto& body_interface = physics_system_.GetBodyInterface();
  if (auto it = entity_bodies_ref_.find(id); it != entity_bodies_ref_.end()) {
    if (!disabled_.contains(id)) body_interface.RemoveBody(it->second);
    body_interface.DestroyBody(it->second);
    jolt_bodies_ref_.erase(it->second);
    entity_bodies_ref_.erase(it);
  }
  if (auto sensor = sensor_bodies_.find(id); sensor != sensor_bodies_.end()) {
    if (!disabled_.contains(id)) body_interface.RemoveBody(sensor->second);
    body_interface.DestroyBody(sensor->second);
    secondary_bodies_.erase(sensor->second);
    sensor_bodies_.erase(sensor);
  }
  disabled_.erase(id);
  std::erase_if(trigger_pairs_, [&](const auto& pair) {
    return pair.first == id || pair.second == id;
  });
  {
    std::lock_guard lock(contact_mutex_);
    std::erase_if(contact_counts_, [&](const auto& item) {
      return item.first.first == id || item.first.second == id;
    });
  }
  std::erase_if(contact_pairs_, [&](const auto& pair) {
    return pair.first == id || pair.second == id;
  });
}

void PhysicsEngine::DisableBody(kEntityId id) {
  // operator[] here would insert an invalid BodyID that the deletion queue
  // later hands to Jolt.
  if (!entity_bodies_ref_.contains(id)) return;
  PDebug::log("Disabling body: {}", static_cast<uint32_t>(id));

  if (std::find(disable_queue_.begin(), disable_queue_.end(), id) !=
      disable_queue_.end()) {
    return;
  }

  disable_queue_.push_back(id);
}

kEntityId PhysicsEngine::Get(JPH::BodyID jolt_id) {
  if (auto it = jolt_bodies_ref_.find(jolt_id); it != jolt_bodies_ref_.end()) {
    return it->second;
  }
  if (auto it = secondary_bodies_.find(jolt_id); it != secondary_bodies_.end()) {
    return it->second;
  }
  return entt::null;
}
bool PhysicsEngine::IsRegistered(kEntityId id) {
  if (this->entity_bodies_ref_.contains(id)) {
    auto jolt_id = this->entity_bodies_ref_[id];
    return this->jolt_bodies_ref_[jolt_id] != entt::null;
  } else {
    return false;
  }
}

template <>
void PhysicsEngine::Set<kRestitution>(kEntityId id, float restitution) {
  auto jolt_body = entity_bodies_ref_[id];
  auto& body_interface = physics_system_.GetBodyInterface();
  body_interface.SetRestitution(jolt_body, restitution);
}

template <>
void PhysicsEngine::Set<kMotionType>(kEntityId id, JPH::EMotionType motion) {
  auto jolt_body = entity_bodies_ref_[id];
  auto& body_interface = physics_system_.GetBodyInterface();
  body_interface.SetMotionType(jolt_body, motion, JPH::EActivation::Activate);
}
template <>
void PhysicsEngine::Set<kFriction>(kEntityId id, float friction) {
  auto jolt_body = entity_bodies_ref_[id];
  auto& body_interface = physics_system_.GetBodyInterface();
  body_interface.SetFriction(jolt_body, friction);
}

template <>
void PhysicsEngine::Set<kGravity>(kEntityId id, float value) {
  auto jolt_body = entity_bodies_ref_[id];
  auto& body_interface = physics_system_.GetBodyInterface();
  body_interface.SetGravityFactor(jolt_body, value);
}

template <>
void PhysicsEngine::AddCollisionCallback<kCollisionPersisted>(
    kEntityId self, CollisionCallbackLambda callback_lambda) {
  this->on_collision_persisted_[self] = callback_lambda;
}

template <>
void PhysicsEngine::AddCollisionCallback<kCollisionEnter>(
    kEntityId self, CollisionCallbackLambda callback_lambda) {
  this->on_collision_enter_[self] = callback_lambda;
}
template <>
void PhysicsEngine::AddCollisionCallback<kCollisionLeave>(
    kEntityId self, CollisionCallbackLambda callback_lambda) {
  this->on_collision_leave_[self] = callback_lambda;
}

// ------------------------------------------------------- Generic bodies ---

namespace {

JPH::Vec3 ToJoltVec(glm::vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
glm::vec3 FromJoltVec(JPH::Vec3 v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }

// One shape's settings, centered on its own origin.
JPH::Ref<JPH::Shape> BuildSingleShape(const ColliderShape& shape) {
  JPH::ShapeSettings::ShapeResult result;
  switch (shape.kind) {
    case ColliderShape::Kind::kBox: {
      glm::vec3 half = glm::max(glm::abs(shape.size) / 2.0f, glm::vec3(1e-3f));
      const float convex_radius =
          std::min(JPH::cDefaultConvexRadius, std::min({half.x, half.y, half.z}) * 0.5f);
      JPH::BoxShapeSettings settings(ToJoltVec(half), convex_radius);
      settings.SetEmbedded();
      result = settings.Create();
      break;
    }
    case ColliderShape::Kind::kSphere: {
      JPH::SphereShapeSettings settings(std::max(std::abs(shape.radius), 1e-3f));
      settings.SetEmbedded();
      result = settings.Create();
      break;
    }
    case ColliderShape::Kind::kCapsule: {
      const float radius = std::max(std::abs(shape.radius), 1e-3f);
      const float half_height = std::max(shape.height / 2.0f - radius, 0.0f);
      if (half_height <= 1e-4f) {
        JPH::SphereShapeSettings settings(radius);
        settings.SetEmbedded();
        result = settings.Create();
      } else {
        JPH::CapsuleShapeSettings settings(half_height, radius);
        settings.SetEmbedded();
        result = settings.Create();
      }
      break;
    }
    case ColliderShape::Kind::kCylinder: {
      const float half_height = std::max(std::abs(shape.height) / 2.0f, 1e-3f);
      const float radius = std::max(std::abs(shape.radius), 1e-3f);
      JPH::CylinderShapeSettings settings(
          half_height, radius,
          std::min(JPH::cDefaultConvexRadius, std::min(half_height, radius) * 0.5f));
      settings.SetEmbedded();
      result = settings.Create();
      break;
    }
    case ColliderShape::Kind::kMesh: {
      JPH::VertexList vertices;
      vertices.reserve(shape.vertices.size());
      for (const auto& v : shape.vertices) vertices.push_back(JPH::Float3(v.x, v.y, v.z));
      JPH::IndexedTriangleList triangles;
      triangles.reserve(shape.indices.size() / 3);
      for (size_t i = 0; i + 2 < shape.indices.size(); i += 3) {
        triangles.push_back(JPH::IndexedTriangle(shape.indices[i], shape.indices[i + 1],
                                                 shape.indices[i + 2]));
      }
      if (triangles.empty()) return nullptr;
      JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
      settings.SetEmbedded();
      result = settings.Create();
      break;
    }
    case ColliderShape::Kind::kConvexHull: {
      JPH::Array<JPH::Vec3> points;
      points.reserve(shape.vertices.size());
      for (const auto& v : shape.vertices) points.push_back(ToJoltVec(v));
      if (points.size() < 4) return nullptr;
      JPH::ConvexHullShapeSettings settings(points, 0.0f);
      settings.SetEmbedded();
      result = settings.Create();
      break;
    }
  }
  if (result.HasError()) {
    PDebug::error("Physics: could not create shape: {}", result.GetError().c_str());
    return nullptr;
  }
  return result.Get();
}

}  // namespace

JPH::Ref<JPH::Shape> PhysicsEngine::BuildShape(
    const std::vector<const ColliderShape*>& shapes) {
  if (shapes.empty()) return nullptr;
  auto is_offset = [](const ColliderShape& shape) {
    return glm::length(shape.center) > 1e-6f ||
           std::abs(glm::dot(shape.rotation, glm::quat(1, 0, 0, 0))) < 1.0f - 1e-6f;
  };
  if (shapes.size() == 1) {
    JPH::Ref<JPH::Shape> single = BuildSingleShape(*shapes[0]);
    if (!single || !is_offset(*shapes[0])) return single;
    JPH::RotatedTranslatedShapeSettings settings(ToJoltVec(shapes[0]->center),
                                                 ToJolt(shapes[0]->rotation), single);
    settings.SetEmbedded();
    auto result = settings.Create();
    return result.HasError() ? nullptr : result.Get();
  }
  JPH::StaticCompoundShapeSettings compound;
  compound.SetEmbedded();
  int added = 0;
  for (const ColliderShape* shape : shapes) {
    JPH::Ref<JPH::Shape> part = BuildSingleShape(*shape);
    if (!part) continue;
    compound.AddShape(ToJoltVec(shape->center), ToJolt(shape->rotation), part);
    added++;
  }
  if (added == 0) return nullptr;
  auto result = compound.Create();
  if (result.HasError()) {
    PDebug::error("Physics: could not create compound shape: {}",
                  result.GetError().c_str());
    return nullptr;
  }
  return result.Get();
}

bool PhysicsEngine::CreateBody(kEntityId self, const std::vector<ColliderShape>& shapes,
                               const BodyDesc& desc, int layer) {
  auto transform = object_manager_.GetProperty<Transform>(self);
  if (!transform) {
    PDebug::error("Physics body needs transform property to register");
    return false;
  }
  std::vector<const ColliderShape*> solid, sensors;
  for (const auto& shape : shapes) {
    (shape.is_trigger ? sensors : solid).push_back(&shape);
  }
  // A moving body cannot use a triangle mesh; Jolt (like Unity) needs it
  // convex.
  std::vector<ColliderShape> converted;
  converted.reserve(shapes.size());
  if (desc.motion != BodyDesc::Motion::kStatic) {
    for (auto*& shape : solid) {
      if (shape->kind != ColliderShape::Kind::kMesh) continue;
      converted.push_back(*shape);
      converted.back().kind = ColliderShape::Kind::kConvexHull;
      shape = &converted.back();
    }
  }
  for (auto*& shape : sensors) {
    if (shape->kind != ColliderShape::Kind::kMesh) continue;
    converted.push_back(*shape);
    converted.back().kind = ColliderShape::Kind::kConvexHull;
    shape = &converted.back();
  }

  const glm::vec3 position = transform->GetPosition();
  const JPH::Quat rotation = ToJolt(transform->GetRotationQuat());
  auto& body_interface = physics_system_.GetBodyInterface();

  auto make_body = [&](const std::vector<const ColliderShape*>& parts,
                       bool sensor) -> JPH::BodyID {
    JPH::Ref<JPH::Shape> shape = BuildShape(parts);
    if (!shape) return JPH::BodyID();
    JPH::EMotionType motion = JPH::EMotionType::Static;
    if (desc.motion == BodyDesc::Motion::kKinematic) motion = JPH::EMotionType::Kinematic;
    if (desc.motion == BodyDesc::Motion::kDynamic) motion = JPH::EMotionType::Dynamic;
    // A trigger riding on a moving body is moved along with it.
    if (sensor && motion == JPH::EMotionType::Dynamic && !solid.empty()) {
      motion = JPH::EMotionType::Kinematic;
    }
    JPH::BodyCreationSettings settings(
        shape, JPH::RVec3(position.x, position.y, position.z), rotation, motion,
        motion == JPH::EMotionType::Static ? Layers::NON_MOVING : Layers::MOVING);
    settings.mIsSensor = sensor;
    settings.mFriction = parts[0]->friction;
    settings.mRestitution = parts[0]->restitution;
    if (motion == JPH::EMotionType::Dynamic) {
      settings.mGravityFactor = desc.gravity_factor;
      settings.mLinearDamping = desc.linear_damping;
      settings.mAngularDamping = desc.angular_damping;
      settings.mAllowSleeping = desc.allow_sleeping;
      settings.mMotionQuality = desc.linear_cast ? JPH::EMotionQuality::LinearCast
                                                 : JPH::EMotionQuality::Discrete;
      if (desc.allowed_dofs != 0 && desc.allowed_dofs != 0b111111) {
        settings.mAllowedDOFs = JPH::EAllowedDOFs(desc.allowed_dofs);
      }
      if (desc.mass > 0.0f) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.mass;
      }
    }
    if (motion == JPH::EMotionType::Kinematic) {
      // Unity's kinematic bodies still report contacts with static colliders.
      settings.mCollideKinematicVsNonDynamic = true;
      if (desc.mass > 0.0f) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.mass;
      }
    }
    JPH::Body* body = body_interface.CreateBody(settings);
    if (!body) {
      PDebug::error("Physics: out of bodies");
      return JPH::BodyID();
    }
    body_interface.AddBody(body->GetID(), JPH::EActivation::Activate);
    return body->GetID();
  };

  bool created = false;
  if (!solid.empty()) {
    JPH::BodyID id = make_body(solid, false);
    if (!id.IsInvalid()) {
      jolt_bodies_ref_[id] = self;
      entity_bodies_ref_[self] = id;
      created = true;
    }
  }
  if (!sensors.empty()) {
    JPH::BodyID id = make_body(sensors, true);
    if (!id.IsInvalid()) {
      if (created) {
        secondary_bodies_[id] = self;
        sensor_bodies_[self] = id;
      } else {
        // Only triggers: the sensor is the entity's main body, so it moves
        // with (and like) the entity.
        jolt_bodies_ref_[id] = self;
        entity_bodies_ref_[self] = id;
      }
      created = true;
    }
  }
  if (created) layers_[self] = layer;
  return created;
}

void PhysicsEngine::SetLayer(kEntityId self, int layer) { layers_[self] = layer; }

void PhysicsEngine::SetBodyEnabled(kEntityId self, bool enabled) {
  auto& bi = physics_system_.GetBodyInterface();
  const bool currently_enabled = !disabled_.contains(self);
  if (enabled == currently_enabled) return;
  std::vector<JPH::BodyID> ids;
  if (auto it = entity_bodies_ref_.find(self); it != entity_bodies_ref_.end()) {
    ids.push_back(it->second);
  }
  if (auto it = sensor_bodies_.find(self); it != sensor_bodies_.end()) {
    ids.push_back(it->second);
  }
  if (auto it = characters_.find(self); it != characters_.end()) {
    JPH::BodyID inner = it->second.character->GetInnerBodyID();
    if (!inner.IsInvalid()) ids.push_back(inner);
  }
  if (ids.empty()) return;
  for (JPH::BodyID id : ids) {
    if (enabled) {
      bi.AddBody(id, JPH::EActivation::Activate);
    } else {
      bi.RemoveBody(id);
    }
  }
  if (enabled) {
    disabled_.erase(self);
  } else {
    disabled_.insert(self);
  }
}

glm::vec3 PhysicsEngine::GetGravity() {
  return FromJoltVec(physics_system_.GetGravity());
}

void PhysicsEngine::SetTickRate(int ticks_per_second) {
  tick_rate_ = std::max(1, ticks_per_second);
}

glm::vec3 PhysicsEngine::GetLinearVelocity(kEntityId self) {
  if (auto it = characters_.find(self); it != characters_.end()) {
    return it->second.velocity;
  }
  auto it = entity_bodies_ref_.find(self);
  if (it == entity_bodies_ref_.end()) return glm::vec3(0.0f);
  return FromJoltVec(physics_system_.GetBodyInterface().GetLinearVelocity(it->second));
}

void PhysicsEngine::SetLinearVelocity(kEntityId self, glm::vec3 velocity) {
  if (!IsFiniteVec(velocity)) return;
  auto it = entity_bodies_ref_.find(self);
  if (it == entity_bodies_ref_.end()) return;
  physics_system_.GetBodyInterface().SetLinearVelocity(it->second, ToJoltVec(velocity));
  if (auto transform = object_manager_.GetProperty<Transform>(self)) {
    transform->SetVelocity(velocity);
  }
}

void PhysicsEngine::SetKinematic(kEntityId self, bool kinematic) {
  auto it = entity_bodies_ref_.find(self);
  if (it == entity_bodies_ref_.end()) return;
  auto& bi = physics_system_.GetBodyInterface();
  if (bi.GetMotionType(it->second) == JPH::EMotionType::Static) return;
  bi.SetMotionType(it->second,
                   kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic,
                   JPH::EActivation::Activate);
}

bool PhysicsEngine::IsKinematic(kEntityId self) {
  auto it = entity_bodies_ref_.find(self);
  if (it == entity_bodies_ref_.end()) return false;
  return physics_system_.GetBodyInterface().GetMotionType(it->second) ==
         JPH::EMotionType::Kinematic;
}

void PhysicsEngine::MoveKinematic(kEntityId self, glm::vec3 position, glm::quat rotation) {
  auto it = entity_bodies_ref_.find(self);
  if (it == entity_bodies_ref_.end()) return;
  physics_system_.GetBodyInterface().MoveKinematic(
      it->second, JPH::RVec3(position.x, position.y, position.z), ToJolt(rotation),
      1.0f / static_cast<float>(tick_rate_));
}

void PhysicsEngine::SetCollisionsEnabled(kEntityId self, bool enabled) {
  if (enabled) {
    no_collisions_.erase(self);
  } else {
    no_collisions_.insert(self);
  }
  auto it = entity_bodies_ref_.find(self);
  if (it == entity_bodies_ref_.end()) return;
  auto& bi = physics_system_.GetBodyInterface();
  // A body that collides with nothing behaves like Jolt's sensor without
  // being reported as a trigger.
  bi.SetIsSensor(it->second, !enabled);
}

// ----------------------------------------------------------------- Queries --

bool PhysicsEngine::BodyMatches(JPH::BodyID body, uint32_t layer_mask,
                                bool query_triggers) {
  const kEntityId entity = Get(body);
  if (entity == entt::null) return false;
  if (no_collisions_.contains(entity)) return false;
  auto layer = layers_.find(entity);
  const int node_layer = layer != layers_.end() ? layer->second : 0;
  if ((layer_mask & (1u << (node_layer & 31))) == 0) return false;
  if (!query_triggers) {
    JPH::BodyLockRead lock(physics_system_.GetBodyLockInterfaceNoLock(), body);
    if (lock.Succeeded() && lock.GetBody().IsSensor()) return false;
  }
  return true;
}

namespace {
class LambdaBodyFilter : public JPH::BodyFilter {
 public:
  explicit LambdaBodyFilter(std::function<bool(const JPH::BodyID&)> fn)
      : fn_(std::move(fn)) {}
  bool ShouldCollide(const JPH::BodyID& id) const override { return fn_(id); }

 private:
  std::function<bool(const JPH::BodyID&)> fn_;
};
}  // namespace

std::vector<RaycastHit> PhysicsEngine::RaycastAll(glm::vec3 origin, glm::vec3 direction,
                                                  float max_distance,
                                                  uint32_t layer_mask,
                                                  bool query_triggers) {
  std::vector<RaycastHit> hits;
  if (!IsFiniteVec(origin) || !IsFiniteVec(direction)) return hits;
  const float length = glm::length(direction);
  if (length < 1e-8f) return hits;
  direction /= length;
  max_distance = std::min(max_distance, 1e5f);

  JPH::RRayCast ray{JPH::RVec3(origin.x, origin.y, origin.z),
                    ToJoltVec(direction * max_distance)};
  JPH::RayCastSettings settings;
  // A ray that starts inside a collider does not hit it, as in Unity.
  settings.mTreatConvexAsSolid = false;
  settings.SetBackFaceMode(JPH::EBackFaceMode::IgnoreBackFaces);
  JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
  LambdaBodyFilter filter([&](const JPH::BodyID& id) {
    return BodyMatches(id, layer_mask, query_triggers);
  });
  physics_system_.GetNarrowPhaseQuery().CastRay(ray, settings, collector, {}, {}, filter);
  collector.Sort();

  std::set<kEntityId> seen;
  for (const auto& hit : collector.mHits) {
    const kEntityId entity = Get(hit.mBodyID);
    RaycastHit result;
    result.entity = entity;
    result.distance = hit.mFraction * max_distance;
    result.point = origin + direction * result.distance;
    JPH::BodyLockRead lock(physics_system_.GetBodyLockInterfaceNoLock(), hit.mBodyID);
    if (lock.Succeeded()) {
      const JPH::Body& body = lock.GetBody();
      result.is_trigger = body.IsSensor();
      result.normal = FromJoltVec(body.GetWorldSpaceSurfaceNormal(
          hit.mSubShapeID2, JPH::RVec3(result.point.x, result.point.y, result.point.z)));
    }
    hits.push_back(result);
  }
  return hits;
}

std::optional<RaycastHit> PhysicsEngine::Raycast(glm::vec3 origin, glm::vec3 direction,
                                                 float max_distance,
                                                 uint32_t layer_mask,
                                                 bool query_triggers) {
  auto hits = RaycastAll(origin, direction, max_distance, layer_mask, query_triggers);
  if (hits.empty()) return std::nullopt;
  return hits.front();
}

std::vector<kEntityId> PhysicsEngine::OverlapSphere(glm::vec3 center, float radius,
                                                    uint32_t layer_mask,
                                                    bool query_triggers) {
  std::vector<kEntityId> result;
  JPH::SphereShape sphere(std::max(radius, 1e-3f));
  sphere.SetEmbedded();
  JPH::CollideShapeSettings settings;
  JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
  LambdaBodyFilter filter([&](const JPH::BodyID& id) {
    return BodyMatches(id, layer_mask, query_triggers);
  });
  physics_system_.GetNarrowPhaseQuery().CollideShape(
      &sphere, JPH::Vec3::sOne(),
      JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z)), settings,
      JPH::RVec3::sZero(), collector, {}, {}, filter);
  std::set<kEntityId> seen;
  for (const auto& hit : collector.mHits) {
    const kEntityId entity = Get(hit.mBodyID2);
    if (entity != entt::null && seen.insert(entity).second) result.push_back(entity);
  }
  return result;
}

void PhysicsEngine::UpdateTriggers() {
  if (!trigger_callback_ && trigger_pairs_.empty()) return;
  auto& lock_interface = physics_system_.GetBodyLockInterfaceNoLock();
  std::set<std::pair<kEntityId, kEntityId>> current;

  auto scan = [&](kEntityId trigger_entity, JPH::BodyID sensor_id) {
    if (disabled_.contains(trigger_entity)) return;
    JPH::RefConst<JPH::Shape> shape;
    JPH::RMat44 com;
    bool sensor_static = false;
    {
      JPH::BodyLockRead lock(lock_interface, sensor_id);
      if (!lock.Succeeded() || !lock.GetBody().IsSensor()) return;
      const JPH::Body& body = lock.GetBody();
      shape = body.GetShape();
      com = body.GetCenterOfMassTransform();
      sensor_static = body.IsStatic();
    }
    JPH::CollideShapeSettings settings;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    LambdaBodyFilter filter([&](const JPH::BodyID& id) {
      if (id == sensor_id) return false;
      const kEntityId entity = Get(id);
      if (entity == entt::null || entity == trigger_entity) return false;
      if (disabled_.contains(entity) || no_collisions_.contains(entity)) return false;
      JPH::BodyLockRead other(lock_interface, id);
      if (!other.Succeeded()) return false;
      const JPH::Body& body = other.GetBody();
      // Two triggers, or a static trigger and a static collider, never
      // report each other (Unity needs a Rigidbody or a CharacterController).
      if (body.IsSensor()) return false;
      if (sensor_static && body.IsStatic()) return false;
      return true;
    });
    physics_system_.GetNarrowPhaseQuery().CollideShape(
        shape, JPH::Vec3::sOne(), com, settings, JPH::RVec3::sZero(), collector, {}, {},
        filter);
    for (const auto& hit : collector.mHits) {
      const kEntityId other = Get(hit.mBodyID2);
      if (other != entt::null) current.insert({trigger_entity, other});
    }
  };
  for (const auto& [body_id, entity] : jolt_bodies_ref_) scan(entity, body_id);
  for (const auto& [entity, sensor_id] : sensor_bodies_) scan(entity, sensor_id);

  if (trigger_callback_) {
    for (const auto& pair : current) {
      if (!trigger_pairs_.contains(pair)) {
        trigger_callback_(TriggerEvent::kEnter, pair.first, pair.second);
      }
    }
    for (const auto& pair : trigger_pairs_) {
      if (!current.contains(pair)) {
        trigger_callback_(TriggerEvent::kExit, pair.first, pair.second);
      }
    }
    for (const auto& pair : current) {
      trigger_callback_(TriggerEvent::kStay, pair.first, pair.second);
    }
  }
  trigger_pairs_ = std::move(current);
}

void PhysicsEngine::RecordContact(kEntityId a, kEntityId b, bool added) {
  if (a == entt::null || b == entt::null || a == b) return;
  if (b < a) std::swap(a, b);
  std::lock_guard lock(contact_mutex_);
  int& count = contact_counts_[{a, b}];
  count += added ? 1 : -1;
  if (count <= 0) contact_counts_.erase({a, b});
}

void PhysicsEngine::UpdateContacts() {
  std::set<std::pair<kEntityId, kEntityId>> current;
  {
    std::lock_guard lock(contact_mutex_);
    for (const auto& [pair, count] : contact_counts_) {
      if (count > 0 && entity_bodies_ref_.contains(pair.first) &&
          entity_bodies_ref_.contains(pair.second)) {
        current.insert(pair);
      }
    }
  }
  if (contact_callback_) {
    for (const auto& pair : current) {
      if (!contact_pairs_.contains(pair)) {
        contact_callback_(ContactEvent::kEnter, pair.first, pair.second);
      }
    }
    for (const auto& pair : contact_pairs_) {
      if (!current.contains(pair)) {
        contact_callback_(ContactEvent::kExit, pair.first, pair.second);
      }
    }
    for (const auto& pair : current) {
      contact_callback_(ContactEvent::kStay, pair.first, pair.second);
    }
  }
  contact_pairs_ = std::move(current);
}

// -------------------------------------------------------------- Characters --

namespace {

JPH::Ref<JPH::Shape> CharacterShape(float height, float radius, glm::vec3 center) {
  radius = std::max(radius, 0.01f);
  const float half_height = std::max(height / 2.0f - radius, 0.0f);
  JPH::Ref<JPH::Shape> capsule;
  if (half_height <= 1e-4f) {
    capsule = new JPH::SphereShape(radius);
  } else {
    capsule = new JPH::CapsuleShape(half_height, radius);
  }
  if (glm::length(center) < 1e-6f) return capsule;
  return JPH::RotatedTranslatedShapeSettings(ToJoltVec(center), JPH::Quat::sIdentity(),
                                             capsule)
      .Create()
      .Get();
}

// Characters collide with solid bodies but not with their own inner body.
class CharacterBodyFilter : public JPH::BodyFilter {
 public:
  explicit CharacterBodyFilter(JPH::BodyID self) : self_(self) {}
  bool ShouldCollide(const JPH::BodyID& id) const override { return id != self_; }

 private:
  JPH::BodyID self_;
};

}  // namespace

bool PhysicsEngine::CreateCharacter(kEntityId self, const CharacterDesc& desc, int layer) {
  auto transform = object_manager_.GetProperty<Transform>(self);
  if (!transform) return false;
  RemoveCharacter(self);

  JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
  settings->mShape = CharacterShape(desc.height, desc.radius, desc.center);
  settings->mInnerBodyShape = settings->mShape;
  settings->mInnerBodyLayer = Layers::MOVING;
  settings->mMaxSlopeAngle = glm::radians(desc.slope_limit);
  settings->mCharacterPadding = std::clamp(desc.skin_width, 0.001f, 0.1f);
  settings->mPenetrationRecoverySpeed = 1.0f;
  settings->mPredictiveContactDistance = 0.1f;
  settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -desc.radius);
  const glm::vec3 position = transform->GetPosition();
  CharacterData data;
  data.desc = desc;
  data.layer = layer;
  data.last_position = position;
  data.character = new JPH::CharacterVirtual(
      settings, JPH::RVec3(position.x, position.y, position.z), JPH::Quat::sIdentity(),
      0, &physics_system_);
  const JPH::BodyID inner = data.character->GetInnerBodyID();
  if (!inner.IsInvalid()) secondary_bodies_[inner] = self;
  layers_[self] = layer;
  characters_[self] = std::move(data);
  return true;
}

void PhysicsEngine::RemoveCharacter(kEntityId self) {
  auto it = characters_.find(self);
  if (it == characters_.end()) return;
  const JPH::BodyID inner = it->second.character->GetInnerBodyID();
  if (!inner.IsInvalid()) secondary_bodies_.erase(inner);
  characters_.erase(it);  // the CharacterVirtual removes its inner body
}

uint32_t PhysicsEngine::MoveCharacter(kEntityId self, glm::vec3 displacement) {
  auto it = characters_.find(self);
  if (it == characters_.end() || !IsFiniteVec(displacement)) return kCollidedNone;
  CharacterData& data = it->second;
  auto transform = object_manager_.GetProperty<Transform>(self);
  if (!transform) return kCollidedNone;
  JPH::CharacterVirtual& character = *data.character;

  // The Transform may have been moved by a script or a parent since the last
  // move; the Transform wins, as with Unity's CharacterController.
  const glm::vec3 current = transform->GetPosition();
  if (glm::length(current - data.last_position) > 1e-5f) {
    character.SetPosition(JPH::RVec3(current.x, current.y, current.z));
  }
  if (glm::length(displacement) < data.desc.min_move_distance) {
    data.last_position = current;
    return kCollidedNone;
  }

  const float dt = 1.0f / static_cast<float>(tick_rate_);
  character.SetLinearVelocity(ToJoltVec(displacement / dt));
  JPH::CharacterVirtual::ExtendedUpdateSettings update_settings;
  update_settings.mStickToFloorStepDown = JPH::Vec3::sZero();
  update_settings.mWalkStairsStepUp = JPH::Vec3(0.0f, data.desc.step_offset, 0.0f);
  CharacterBodyFilter body_filter(character.GetInnerBodyID());
  const JPH::BroadPhaseLayerFilter broad_phase_filter;
  const JPH::ObjectLayerFilter layer_filter;
  const JPH::ShapeFilter shape_filter;
  character.ExtendedUpdate(dt, JPH::Vec3::sZero(), update_settings, broad_phase_filter,
                           layer_filter, body_filter, shape_filter, *temp_allocator_);

  const glm::vec3 before = current;
  const JPH::RVec3 after_jolt = character.GetPosition();
  const glm::vec3 after(after_jolt.GetX(), after_jolt.GetY(), after_jolt.GetZ());
  data.velocity = (after - before) / dt;
  data.last_position = after;
  transform->SetPosition(after);

  uint32_t flags = kCollidedNone;
  const auto ground = character.GetGroundState();
  data.grounded = ground == JPH::CharacterBase::EGroundState::OnGround ||
                  ground == JPH::CharacterBase::EGroundState::OnSteepGround;
  for (const auto& contact : character.GetActiveContacts()) {
    if (!contact.mHadCollision || contact.mIsSensorB) continue;
    const float up = contact.mContactNormal.GetY();
    if (up > 0.5f) {
      flags |= kCollidedBelow;
    } else if (up < -0.5f) {
      flags |= kCollidedAbove;
    } else {
      flags |= kCollidedSides;
    }
  }
  if (data.grounded) flags |= kCollidedBelow;
  return flags;
}

bool PhysicsEngine::IsCharacterGrounded(kEntityId self) const {
  auto it = characters_.find(self);
  return it != characters_.end() && it->second.grounded;
}

glm::vec3 PhysicsEngine::GetCharacterVelocity(kEntityId self) const {
  auto it = characters_.find(self);
  return it != characters_.end() ? it->second.velocity : glm::vec3(0.0f);
}

bool PhysicsEngine::SetCharacterShape(kEntityId self, float height, float radius,
                                      glm::vec3 center) {
  auto it = characters_.find(self);
  if (it == characters_.end()) return false;
  JPH::Ref<JPH::Shape> shape = CharacterShape(height, radius, center);
  CharacterBodyFilter body_filter(it->second.character->GetInnerBodyID());
  const JPH::BroadPhaseLayerFilter broad_phase_filter;
  const JPH::ObjectLayerFilter layer_filter;
  const JPH::ShapeFilter shape_filter;
  // Unity resizes a CharacterController unconditionally; the caller checks
  // for room first (as PlayerMotor does with a raycast).
  it->second.character->SetShape(shape, FLT_MAX, broad_phase_filter, layer_filter,
                                 body_filter, shape_filter, *temp_allocator_);
  it->second.character->SetInnerBodyShape(shape);
  it->second.desc.height = height;
  it->second.desc.radius = radius;
  it->second.desc.center = center;
  return true;
}

}  // namespace Mercury
