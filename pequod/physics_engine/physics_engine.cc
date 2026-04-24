#include "physics_engine.h"

#include "Jolt/Core/Factory.h"
#include "Jolt/RegisterTypes.h"
#include "properties/transform.h"

namespace Pequod {
template <>
auto PhysicsEngine::TriggerCollisionCallback<kCollisionPersisted>(
    kEntityId self, kEntityId other) -> void {
  if (on_collision_persisted_.contains(self)) {
    on_collision_persisted_[self](self, other);
  }
}

template <>
void PhysicsEngine::TriggerCollisionCallback<kCollisionEnter>(kEntityId self,
                                                              kEntityId other) {
  if (on_collision_enter_.contains(self)) {
    on_collision_enter_[self](self, other);
  }
}

template <>
void PhysicsEngine::TriggerCollisionCallback<kCollisionLeave>(kEntityId self,
                                                              kEntityId other) {
  if (on_collision_leave_.contains(self)) {
    on_collision_leave_[self](self, other);
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

PhysicsEngine::PhysicsEngine() {
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
  PDebug::info("Initialized Jolt Physics System");
}

void PhysicsEngine::RegisterBody(std::shared_ptr<PObject> self,
                                 CollisionBody collision_body,
                                 JPH::EAllowedDOFs allowed_dofs) {
  PDebug::info("Registering body to physics engine...");
  auto transform = self->Get<Transform>();
  if (transform == nullptr) {
    PDebug::error("Physics body needs transform property to register");
    assert(false);
    return;
  }
  auto position = transform->GetPosition();
  auto creation_settings = JPH::BodyCreationSettings(
      collision_body.GetShapeRef(),
      JPH::RVec3(position.x, position.y, position.z), JPH::Quat::sIdentity(),
      JPH::EMotionType::Kinematic, Layers::MOVING);

  creation_settings.mAllowedDOFs = allowed_dofs;
  creation_settings.mLinearDamping = 0.0f;
  auto& body_interface = physics_system_.GetBodyInterface();

  auto body = body_interface.CreateBody(creation_settings);

  auto body_id = body->GetID();
  this->jolt_bodies_ref_[body_id] = self->id;
  this->entity_bodies_ref_[self->id] = body_id;
  this->registered_bodies_[self->id] = self;

  body_interface.AddBody(body_id, JPH::EActivation::Activate);
}

void PhysicsEngine::Compute(int steps) {
  auto& body_interface = physics_system_.GetBodyInterface();

  // If the scene defines a velocity/position it should overwrite what the
  // physics has calculated
  {
    for (const auto& pair : registered_bodies_) {
      auto entity_id = pair.first;
      auto entity = pair.second;
      if (!entity) {
        continue;
      }
      auto transform = entity->Get<Transform>();
      if (transform) {
        auto transformations = transform->GetTransformations();
        auto jolt_id = entity_bodies_ref_[entity_id];

        for (auto transform_type : transformations) {
          switch (transform_type) {
            case kTransformLinearVelocity:
              auto vel = transform->GetVelocity();
              body_interface.SetLinearVelocity(jolt_id,
                                               JPH::Vec3(vel.x, vel.y, vel.z));
              break;

            case kTransformPosition:
              auto pos = transform->GetPosition();
              body_interface.SetPosition(jolt_id,
                                         JPH::Vec3(pos.x, pos.y, pos.z),
                                         JPH::EActivation::Activate);
              break;
          }
        }
      }
    }
  }
  float cHz = 1.0f / 60.0f;
  physics_system_.Update(cHz, steps, temp_allocator_, job_system_thread_pool_);

  for (const auto& pair : jolt_bodies_ref_) {
    auto body_id = pair.first;
    auto entity_id = pair.second;

    auto entity = registered_bodies_[entity_id];

    auto transform = entity->Get<Transform>();
    auto transformations = transform->GetTransformations();
    if (!std::binary_search(transformations.begin(), transformations.end(),
                            kTransformPosition)) {
      auto phys_position = body_interface.GetPosition(body_id);
      transform->SetPosition(glm::vec3(
          phys_position.GetX(), phys_position.GetY(), phys_position.GetZ()));
    }
    if (!std::binary_search(transformations.begin(), transformations.end(),
                            kTransformLinearVelocity)) {
      auto phys_position = body_interface.GetLinearVelocity(body_id);
      transform->SetVelocity(glm::vec3(
          phys_position.GetX(), phys_position.GetY(), phys_position.GetZ()));
    }
    transform->ClearTransformations();
  }
}

void PhysicsEngine::DisableBody(kEntityId id) {
  auto body_id = entity_bodies_ref_[id];
  jolt_bodies_ref_.erase(body_id);
  entity_bodies_ref_.erase(id);
  registered_bodies_[id] = nullptr;
}

kEntityId PhysicsEngine::Get(JPH::BodyID jolt_id) {
  if (!this->jolt_bodies_ref_.contains(jolt_id)) {
    return -1;
  }
  return this->jolt_bodies_ref_[jolt_id];
}
bool PhysicsEngine::IsRegistered(kEntityId id) {
  if (this->registered_bodies_.contains(id)) {
    return this->registered_bodies_[id] != nullptr;
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