#include <gtest/gtest.h>

#include <filesystem>

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
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]

[node name="Root"]

[node name="PlayerCapsule" parent="."]
[component type="Transform" node="PlayerCapsule"]
position = Vector3(0, 2, 0)

[component type="CapsuleCollider" node="PlayerCapsule"]
radius = 0.5
height = 2.0

[component type="Rigidbody" node="PlayerCapsule"]
motion_type = "dynamic"
mass = 5.0
gravity_factor = 0.0
lock_rotation_x = true
lock_rotation_y = true
lock_rotation_z = true
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
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]

[node name="Root"]

[node name="PlayerCapsule" parent="."]
[component type="Transform" node="PlayerCapsule"]
position = Vector3(0, 2, 0)

[component type="CapsuleCollider" node="PlayerCapsule"]
radius = 0.5
height = 2.0

[component type="Rigidbody" node="PlayerCapsule"]
motion_type = "dynamic"
mass = 5.0
gravity_factor = 0.0
lock_rotation_x = true
lock_rotation_z = true
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

  // Since X and Z rotation are locked, capsule must stay strictly upright along Y:
  EXPECT_NEAR(up.x, 0.0f, 1e-4f);
  EXPECT_NEAR(up.y, 1.0f, 1e-4f);
  EXPECT_NEAR(up.z, 0.0f, 1e-4f);

  // Since Y rotation was NOT locked, it should have rotated around Y:
  const glm::vec3 forward = q * glm::vec3(0.0f, 0.0f, -1.0f);
  // Forward vector is no longer pointing purely along -Z
  EXPECT_TRUE(std::abs(forward.x) > 1e-4f || std::abs(forward.z - (-1.0f)) > 1e-4f);
}

TEST(PhysicsConstraints, AllLockedAxesDoNotCrash) {
  std::string error;
  auto project = Project::Load(kRollABall / "roll_a_ball.mproj", &error);
  ASSERT_TRUE(project) << error;

  // Body with all 6 axes locked
  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]

[node name="Root"]

[node name="LockedBody" parent="."]
[component type="Transform" node="LockedBody"]
position = Vector3(0, 2, 0)

[component type="BoxCollider" node="LockedBody"]
size = Vector3(1, 1, 1)

[component type="Rigidbody" node="LockedBody"]
motion_type = "dynamic"
lock_translation_x = true
lock_translation_y = true
lock_translation_z = true
lock_rotation_x = true
lock_rotation_y = true
lock_rotation_z = true
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  // Should successfully instantiate and configure as static body without crashing
  scene.Instantiate(map);

  auto body = scene.Find("LockedBody");
  ASSERT_TRUE(body.IsValid());

  body.AddForce(glm::vec3(100.0f, 100.0f, 100.0f), ForceMode::kImpulse);
  for (int i = 0; i < 5; ++i) {
    scene.SimulatePhysics(1);
  }

  EXPECT_EQ(body.GetTransform()->GetPosition(), glm::vec3(0.0f, 2.0f, 0.0f));
}
