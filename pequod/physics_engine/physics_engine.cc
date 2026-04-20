//
// Created by smadhur on 3/31/26.
//

#include "physics_engine.h"

#include "properties/transform.h"
#include "shapes/box.h"

namespace Pequod {
namespace {
class PhysicsContactListener : public ContactListener {
 public:
  PhysicsContactListener(PhysicsEngine* physics_engine) {
    this->physics_engine_ = physics_engine;
    PDebug::info("created contact listener");
  };

  PhysicsEngine* physics_engine_ = nullptr;

  virtual ValidateResult OnContactValidate(
      const Body& inBody1, const Body& inBody2, JPH::RVec3Arg inBaseOffset,
      const JPH::CollideShapeResult& inCollisionResult) override {
    return ValidateResult::AcceptAllContactsForThisBodyPair;
  }

  virtual void OnContactAdded(const Body& inBody1, const Body& inBody2,
                              const JPH::ContactManifold& inManifold,
                              JPH::ContactSettings& ioSettings) override {
    PDebug::info("contact was added");
  }

  virtual void OnContactPersisted(const Body& inBody1, const Body& inBody2,
                                  const JPH::ContactManifold& inManifold,
                                  JPH::ContactSettings& ioSettings) override {
    PDebug::info("contact was persisted");
  }

  virtual void OnContactRemoved(
      const JPH::SubShapeIDPair& inSubShapePair) override {
    PDebug::info("contact was removed");
  }
};
}  // namespace

PhysicsEngine::PhysicsEngine() {
  JPH::RegisterDefaultAllocator();

  JPH::Factory::sInstance = new JPH::Factory();

  JPH::RegisterTypes();

  temp_allocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);  // 10MB
  job_system = new JPH::JobSystemThreadPool(
      JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
      JPH::thread::hardware_concurrency() - 1);

  constexpr uint cMaxBodies = 1024;
  constexpr uint cNumBodyMutexes = 0;
  constexpr uint cMaxBodyPairs = 1024;
  constexpr uint cMaxContactConstraints = 1024;

  physics_system.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs,
                      cMaxContactConstraints, broad_phase_layer_interface,
                      object_vs_broadphase_layer_filter,
                      object_vs_object_layer_filter);
  physics_system.SetBodyActivationListener(&body_activation_listener);

  PhysicsContactListener* contact_listener = new PhysicsContactListener(this);

  physics_system.SetContactListener(contact_listener);

  PDebug::info("initialized jolt physics system");
}

PhysicsEngine::~PhysicsEngine() {
  delete temp_allocator;
  delete job_system;
}

void PhysicsEngine::RegisterEntity(std::shared_ptr<PObject> object,
                                   CollisionBody body) {
  PDebug::log("Adding box shape to {}", object->id);
}

void PhysicsEngine::Compute(int steps, std::shared_ptr<ECS> ecs) {
  if (ecs == nullptr) {
    PDebug::error("PhysicsEngine::Compute --> ECS passed as nullptr");
    return;
  }

  float cHz = 1.0f / 60.0f;
  physics_system.Update(cHz, steps, temp_allocator, job_system);
  auto& body_interface = physics_system.GetBodyInterface();

  for (const auto& pair : jolt_bodies) {
    JPH::BodyID body_id = pair.first;
    entity_id id = pair.second;
    auto phys_position = body_interface.GetCenterOfMassPosition(body_id);
    auto transforms = ecs->GetProperty<Transform>(id);
  }
}
}  // namespace Pequod
