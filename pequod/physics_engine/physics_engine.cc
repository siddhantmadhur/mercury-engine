#include "physics_engine.h"

#include "Jolt/Core/Factory.h"
#include "Jolt/RegisterTypes.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/PlaneShape.h"
#include "properties/transform.h"

namespace Pequod {
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

    physics_engine_->TriggerCollisionCallback<kCollisionEnter>(self, other);
  }
  virtual void OnContactPersisted(const JPH::Body& inBody1,
                                  const JPH::Body& inBody2,
                                  const JPH::ContactManifold& inManifold,
                                  JPH::ContactSettings& ioSettings) override {
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

  job_system_thread_pool_ = new JPH::JobSystemThreadPool(
      JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
      JPH::thread::hardware_concurrency() - 1);
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
  const uint cMaxBodies = 1024;
  const uint cNumBodyMutexes = 0;
  const uint cMaxBodyPairs = 1024;
  const uint cMaxContactConstraints = 1024;

  physics_system_.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs,
                       cMaxContactConstraints, broad_phase_layer_interface_,
                       object_vs_broad_phase_layer_filter_impl_,
                       object_layer_pair_filter_impl_);

  auto contact_listener = new PhysicsContactListener(this);
  physics_system_.SetContactListener(contact_listener);
  physics_system_.SetGravity(JPH::Vec3Arg{0.0, -98.0, 0.0});
  PDebug::info("Initialized Jolt Physics System");
}

bool PhysicsEngine::CreateBox3D(
    kEntityId self, glm::vec3 dimensions,
    std::function<void(JPH::BodyCreationSettings&)> override_function) {
  PDebug::info("Registering body to physics engine...");
  auto transform = object_manager_.GetProperty<Transform>(self);
  if (transform == nullptr) {
    PDebug::error("Physics body needs transform property to register");
    return false;
  }

  auto position = transform->GetPosition();
  dimensions /= 2;
  JPH::BoxShapeSettings box_shape_settings(
      JPH::Vec3(dimensions.x, dimensions.y, dimensions.z));
  box_shape_settings.SetEmbedded();

  JPH::ShapeSettings::ShapeResult box_shape_result =
      box_shape_settings.Create();
  auto shape_ref = box_shape_result.Get();

  auto creation_settings = JPH::BodyCreationSettings(
      shape_ref, JPH::RVec3(position.x, position.y, position.z),
      JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::MOVING);

  override_function(creation_settings);

  auto& body_interface = physics_system_.GetBodyInterface();

  auto body = body_interface.CreateBody(creation_settings);

  auto body_id = body->GetID();
  this->jolt_bodies_ref_[body_id] = self;
  this->entity_bodies_ref_[self] = body_id;

  body_interface.AddBody(body_id, JPH::EActivation::Activate);
  return true;
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
            auto rot = transform->GetRotate();
            // body_interface.SetRotation(jolt_id, JPH::QuatArg{rot.x, rot.y,
            // rot.z});
            break;
          }
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
  float cHz = 1.0f / 60.0f;
  physics_system_.Update(cHz, steps, temp_allocator_, job_system_thread_pool_);

  auto& body_interface = physics_system_.GetBodyInterface();

  // Handle deletion queue here
  while (!disable_queue_.empty()) {
    kEntityId id = disable_queue_.back();

    auto body_id = entity_bodies_ref_[id];

    body_interface.RemoveBody(body_id);
    jolt_bodies_ref_.erase(body_id);
    entity_bodies_ref_.erase(id);
    disable_queue_.pop_back();
  }

  // The reason this runs again is because sometimes if a collision changes the
  // position it needs to overwrite jolt again
  this->SynchronizePObjects();

  for (const auto& pair : jolt_bodies_ref_) {
    auto body_id = pair.first;
    auto entity_id = pair.second;

    auto transform = object_manager_.GetProperty<Transform>(entity_id);
    auto transformations = transform->GetTransformations();
    if (!std::binary_search(transformations.begin(), transformations.end(),
                            kTransformPosition)) {
      auto phys_position = body_interface.GetPosition(body_id);
      transform->SetPosition(glm::vec3(
          phys_position.GetX(), phys_position.GetY(), phys_position.GetZ()));
    }
    if (!std::binary_search(transformations.begin(), transformations.end(),
                            kTransformRotation)) {
      auto phys_position = body_interface.GetRotation(body_id);
      transform->SetRotate(glm::vec3(phys_position.GetX(), phys_position.GetY(),
                                     phys_position.GetZ()));
    }
    transform->ClearTransformations();
  }
}

void PhysicsEngine::DisableBody(kEntityId id) {
  auto body_id = entity_bodies_ref_[id];
  PDebug::log("Disabling body: {}", static_cast<uint32_t>(id));

  if (std::binary_search(disable_queue_.begin(), disable_queue_.end(), id)) {
    return;
  }

  disable_queue_.push_back(id);
}

kEntityId PhysicsEngine::Get(JPH::BodyID jolt_id) {
  if (!this->jolt_bodies_ref_.contains(jolt_id)) {
    return entt::null;
  }
  return this->jolt_bodies_ref_[jolt_id];
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

}  // namespace Pequod
