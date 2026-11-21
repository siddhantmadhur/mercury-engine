// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include <filesystem>
#include <thread>

#include "components/components.h"
#include "project/map.h"
#include "project/project.h"
#include "scene/map_scene.h"
#include "scripting/reflection.h"

using namespace Mercury;

namespace {

const std::filesystem::path kRollABall =
    std::filesystem::path(MERCURY_SOURCE_DIR) / "examples/roll_a_ball";

}  // namespace

TEST(PhysicsConstraints, BitmaskCalculation) {
  Rigidbody rb;
  // Default: all degrees of freedom allowed
  EXPECT_EQ(rb.ComputeAllowedDOFs(), 0b111111);

  // Lock rotation completely (e.g. for character or box)
  rb.lock_rotation_x = true;
  rb.lock_rotation_y = true;
  rb.lock_rotation_z = true;
  EXPECT_EQ(rb.ComputeAllowedDOFs(), 0b000111);

  // Lock rotation X and Z only (capsule upright, free to yaw around Y)
  rb.lock_rotation_y = false;
  EXPECT_EQ(rb.ComputeAllowedDOFs(), 0b010111);

  // Lock translation X and Z (e.g. vertical elevator or 1D motion)
  rb.lock_rotation_x = false;
  rb.lock_rotation_z = false;
  rb.lock_translation_x = true;
  rb.lock_translation_z = true;
  EXPECT_EQ(rb.ComputeAllowedDOFs(), 0b111010);

  // Lock all axes
  rb.lock_translation_y = true;
  rb.lock_rotation_x = true;
  rb.lock_rotation_y = true;
  rb.lock_rotation_z = true;
  EXPECT_EQ(rb.ComputeAllowedDOFs(), 0b000000);
}

TEST(PhysicsConstraints, LegacyAllowedDofsMigration) {
  // Case 1: Legacy "translation_y" (like Pong paddles)
  {
    mtext::PropertyList props;
    props.Set("allowed_dofs", std::string("translation_y"));
    auto rb = ReadFields<Rigidbody>(props, "Test");
    EXPECT_TRUE(rb.lock_translation_x);
    EXPECT_FALSE(rb.lock_translation_y);
    EXPECT_TRUE(rb.lock_translation_z);
    EXPECT_TRUE(rb.lock_rotation_x);
    EXPECT_TRUE(rb.lock_rotation_y);
    EXPECT_TRUE(rb.lock_rotation_z);
    EXPECT_EQ(rb.ComputeAllowedDOFs(), 0b000010);
  }

  // Case 2: Legacy "translation_xy"
  {
    mtext::PropertyList props;
    props.Set("allowed_dofs", std::string("translation_xy"));
    auto rb = ReadFields<Rigidbody>(props, "Test");
    EXPECT_FALSE(rb.lock_translation_x);
    EXPECT_FALSE(rb.lock_translation_y);
    EXPECT_TRUE(rb.lock_translation_z);
    EXPECT_TRUE(rb.lock_rotation_x);
    EXPECT_TRUE(rb.lock_rotation_y);
    EXPECT_TRUE(rb.lock_rotation_z);
    EXPECT_EQ(rb.ComputeAllowedDOFs(), 0b000011);
  }

  // Case 3: Legacy "plane_2d" (translation XY + rotation Z)
  {
    mtext::PropertyList props;
    props.Set("allowed_dofs", std::string("plane_2d"));
    auto rb = ReadFields<Rigidbody>(props, "Test");
    EXPECT_FALSE(rb.lock_translation_x);
    EXPECT_FALSE(rb.lock_translation_y);
    EXPECT_TRUE(rb.lock_translation_z);
    EXPECT_TRUE(rb.lock_rotation_x);
    EXPECT_TRUE(rb.lock_rotation_y);
    EXPECT_FALSE(rb.lock_rotation_z);
    EXPECT_EQ(rb.ComputeAllowedDOFs(), 0b100011);
  }

  // Case 4: Explicit lock flags take precedence over legacy
  {
    mtext::PropertyList props;
    props.Set("allowed_dofs", std::string("translation_y"));
    props.Set("lock_translation_x", false);
    props.Set("lock_rotation_z", true);
    auto rb = ReadFields<Rigidbody>(props, "Test");
    // Explicit flags were present, so legacy is ignored
    EXPECT_FALSE(rb.lock_translation_x);
    EXPECT_FALSE(rb.lock_translation_y);
    EXPECT_FALSE(rb.lock_translation_z);
    EXPECT_FALSE(rb.lock_rotation_x);
    EXPECT_FALSE(rb.lock_rotation_y);
    EXPECT_TRUE(rb.lock_rotation_z);
  }

  // Case 5: Aliases "lock_transform_*" and "lock_position_*"
  {
    mtext::PropertyList props;
    props.Set("lock_transform_x", true);
    props.Set("lock_position_y", true);
    props.Set("lock_rotation_z", true);
    auto rb = ReadFields<Rigidbody>(props, "Test");
    EXPECT_TRUE(rb.lock_translation_x);
    EXPECT_TRUE(rb.lock_translation_y);
    EXPECT_FALSE(rb.lock_translation_z);
    EXPECT_TRUE(rb.lock_rotation_z);
  }
}

TEST(PhysicsConstraints, CapsuleDoesNotRollOrTip) {
  std::string error;
  auto project = Project::Load(kRollABall / "roll_a_ball.mproj", &error);
  ASSERT_TRUE(project) << error;

  // Setup a capsule collider with locked rotation X, Y, Z
  // This ensures the capsule can move straight without rolling or tipping over.
  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap]
format = 2

[[component]]
node = "PlayerCapsule"
type = "Transform"
[component.props]
position = { vec3 = [0.0, 2.0, 0.0] }

[[component]]
node = "PlayerCapsule"
type = "CapsuleCollider"
[component.props]
height = 2.0
radius = 0.5

[[component]]
node = "PlayerCapsule"
type = "Rigidbody"
[component.props]
gravity_factor = 0.0
lock_rotation_x = true
lock_rotation_y = true
lock_rotation_z = true
mass = 5.0
motion_type = "dynamic"

[[node]]
name = "Root"

[[node]]
name = "PlayerCapsule"
parent = "."
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);

  auto capsule = scene.Find("PlayerCapsule");
  ASSERT_TRUE(capsule.IsValid());

  // Apply both a forward force and an off-center angular impulse (torque)
  capsule.AddForce(glm::vec3(25.0f, 0.0f, 0.0f), ForceMode::kImpulse);
  // Add torque that would normally cause severe rolling / tipping
  capsule.AddTorque(glm::vec3(100.0f, 50.0f, 100.0f), ForceMode::kImpulse);

  // Step physics simulation multiple ticks
  for (int i = 0; i < 5; ++i) {
    scene.SimulatePhysics(1);
  }

  const glm::vec3 pos = capsule.GetTransform()->GetPosition();
  const glm::vec3 rot = capsule.GetTransform()->GetRotate();

  // Capsule should have moved straight along X (positive X position)
  EXPECT_GT(pos.x, 0.1f);
  // Capsule should not have tipped or rolled: rotation must remain zero!
  EXPECT_NEAR(rot.x, 0.0f, 1e-4f);
  EXPECT_NEAR(rot.y, 0.0f, 1e-4f);
  EXPECT_NEAR(rot.z, 0.0f, 1e-4f);
}

TEST(PhysicsConstraints, CapsuleYawAllowedWhenOnlyXZLocked) {
  std::string error;
  auto project = Project::Load(kRollABall / "roll_a_ball.mproj", &error);
  ASSERT_TRUE(project) << error;

  // Lock X and Z rotation (stays upright, but can turn/yaw around Y)
  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap]
format = 2

[[component]]
node = "PlayerCapsule"
type = "Transform"
[component.props]
position = { vec3 = [0.0, 2.0, 0.0] }

[[component]]
node = "PlayerCapsule"
type = "CapsuleCollider"
[component.props]
height = 2.0
radius = 0.5

[[component]]
node = "PlayerCapsule"
type = "Rigidbody"
[component.props]
gravity_factor = 0.0
lock_rotation_x = true
lock_rotation_z = true
mass = 5.0
motion_type = "dynamic"

[[node]]
name = "Root"

[[node]]
name = "PlayerCapsule"
parent = "."
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);

  auto capsule = scene.Find("PlayerCapsule");
  ASSERT_TRUE(capsule.IsValid());

  // Apply torque on X, Y, Z
  capsule.AddTorque(glm::vec3(50.0f, 50.0f, 50.0f), ForceMode::kImpulse);

  for (int i = 0; i < 5; ++i) {
    scene.SimulatePhysics(1);
  }

  const glm::quat q = capsule.GetTransform()->GetRotationQuat();
  const glm::vec3 up = q * glm::vec3(0.0f, 1.0f, 0.0f);

  // Since X and Z rotation are locked, capsule must stay strictly upright along
  // Y:
  EXPECT_NEAR(up.x, 0.0f, 1e-4f);
  EXPECT_NEAR(up.y, 1.0f, 1e-4f);
  EXPECT_NEAR(up.z, 0.0f, 1e-4f);

  // Since Y rotation was NOT locked, it should have rotated around Y:
  const glm::vec3 forward = q * glm::vec3(0.0f, 0.0f, -1.0f);
  // Forward vector is no longer pointing purely along -Z
  EXPECT_TRUE(std::abs(forward.x) > 1e-4f ||
              std::abs(forward.z - (-1.0f)) > 1e-4f);
}

TEST(PhysicsConstraints, AllLockedAxesDoNotCrash) {
  std::string error;
  auto project = Project::Load(kRollABall / "roll_a_ball.mproj", &error);
  ASSERT_TRUE(project) << error;

  // Body with all 6 axes locked
  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap]
format = 2

[[component]]
node = "LockedBody"
type = "Transform"
[component.props]
position = { vec3 = [0.0, 2.0, 0.0] }

[[component]]
node = "LockedBody"
type = "BoxCollider"
[component.props]
size = { vec3 = [1.0, 1.0, 1.0] }

[[component]]
node = "LockedBody"
type = "Rigidbody"
[component.props]
lock_rotation_x = true
lock_rotation_y = true
lock_rotation_z = true
lock_translation_x = true
lock_translation_y = true
lock_translation_z = true
motion_type = "dynamic"

[[node]]
name = "Root"

[[node]]
name = "LockedBody"
parent = "."
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  // Should successfully instantiate and configure as static body without
  // crashing
  scene.Instantiate(map);

  auto body = scene.Find("LockedBody");
  ASSERT_TRUE(body.IsValid());

  body.AddForce(glm::vec3(100.0f, 100.0f, 100.0f), ForceMode::kImpulse);
  for (int i = 0; i < 5; ++i) {
    scene.SimulatePhysics(1);
  }

  EXPECT_EQ(body.GetTransform()->GetPosition(), glm::vec3(0.0f, 2.0f, 0.0f));
}

// The per-entity collision callbacks used to run straight from Jolt's job
// threads in the middle of the step. They are queued and run after the step,
// on the thread that steps the physics.
TEST(PhysicsConstraints, CollisionCallbacksRunOnTheSteppingThread) {
  std::string error;
  auto project = Project::Load(kRollABall / "roll_a_ball.mproj", &error);
  ASSERT_TRUE(project) << error;

  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap]
format = 2

[[component]]
node = "Floor"
type = "Transform"
[component.props]
position = { vec3 = [0.0, -0.5, 0.0] }

[[component]]
node = "Floor"
type = "BoxCollider"
[component.props]
size = { vec3 = [40.0, 1.0, 40.0] }

[[component]]
node = "Ball"
type = "Transform"
[component.props]
position = { vec3 = [0.0, 0.6, 0.0] }

[[component]]
node = "Ball"
type = "SphereCollider"
[component.props]
radius = 0.5

[[component]]
node = "Ball"
type = "Rigidbody"
[component.props]
mass = 1.0
motion_type = "dynamic"

[[node]]
name = "Root"

[[node]]
name = "Floor"
parent = "."

[[node]]
name = "Ball"
parent = "."
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);
  auto ball = scene.Find("Ball");
  ASSERT_TRUE(ball.IsValid());

  const auto stepping_thread = std::this_thread::get_id();
  int enters = 0;
  bool wrong_thread = false;
  bool during_step = false;
  scene.Physics().AddCollisionCallback<kCollisionEnter>(
      ball.GetEntity(), [&](kEntityId, kEntityId) {
        enters++;
        if (std::this_thread::get_id() != stepping_thread) wrong_thread = true;
        // Jolt's own threads can be the stepping thread, so also ask whether
        // a step is in progress: it must not be.
        if (scene.Physics().IsStepping()) during_step = true;
      });
  for (int i = 0; i < 60; ++i) scene.SimulatePhysics(1);

  EXPECT_GT(enters, 0);
  EXPECT_FALSE(wrong_thread);
  EXPECT_FALSE(during_step);
}
