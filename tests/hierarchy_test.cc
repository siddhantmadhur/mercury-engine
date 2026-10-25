#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>

#include "components/components.h"
#include "project/map.h"
#include "project/project.h"
#include "scene/map_scene.h"

using namespace Mercury;

namespace {

const std::filesystem::path kPong =
    std::filesystem::path(MERCURY_SOURCE_DIR) / "examples/pong";

}  // namespace

TEST(Hierarchy, ChildFollowsParentTranslation) {
  std::string error;
  auto project = Project::Load(kPong / "pong.mproj", &error);
  ASSERT_TRUE(project) << error;

  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]

[node name="Root"]

[node name="Player" parent="."]
[component type="Transform" node="Player"]
position = Vector3(10, 0, 0)

[node name="Weapon" parent="Player"]
[component type="Transform" node="Player/Weapon"]
position = Vector3(2, 3, 4)
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);

  auto player = scene.Find("Player");
  auto weapon = scene.Find("Player/Weapon");
  ASSERT_TRUE(player.IsValid());
  ASSERT_TRUE(weapon.IsValid());

  // Initially: Player at (10, 0, 0), Weapon at (12, 3, 4)
  EXPECT_EQ(player.GetTransform()->GetPosition(), glm::vec3(10.0f, 0.0f, 0.0f));
  EXPECT_EQ(weapon.GetTransform()->GetPosition(), glm::vec3(12.0f, 3.0f, 4.0f));
  EXPECT_EQ(weapon.GetTransform()->GetLocalPosition(), glm::vec3(2.0f, 3.0f, 4.0f));

  // Move player by (5, 10, -2)
  player.GetTransform()->Move(glm::vec3(5.0f, 10.0f, -2.0f));

  EXPECT_EQ(player.GetTransform()->GetPosition(), glm::vec3(15.0f, 10.0f, -2.0f));
  // Weapon world position should have moved with player
  EXPECT_EQ(weapon.GetTransform()->GetPosition(), glm::vec3(17.0f, 13.0f, 2.0f));
  // Weapon local position should still be (2, 3, 4)
  EXPECT_EQ(weapon.GetTransform()->GetLocalPosition(), glm::vec3(2.0f, 3.0f, 4.0f));
}

TEST(Hierarchy, SubchildRecursion) {
  std::string error;
  auto project = Project::Load(kPong / "pong.mproj", &error);
  ASSERT_TRUE(project) << error;

  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]

[node name="Root"]

[node name="Parent" parent="."]
[component type="Transform" node="Parent"]
position = Vector3(0, 0, 0)

[node name="Child" parent="Parent"]
[component type="Transform" node="Parent/Child"]
position = Vector3(1, 0, 0)

[node name="Grandchild" parent="Parent/Child"]
[component type="Transform" node="Parent/Child/Grandchild"]
position = Vector3(0, 5, 0)
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);

  auto parent = scene.Find("Parent");
  auto child = scene.Find("Parent/Child");
  auto grandchild = scene.Find("Parent/Child/Grandchild");

  ASSERT_TRUE(parent.IsValid());
  ASSERT_TRUE(child.IsValid());
  ASSERT_TRUE(grandchild.IsValid());

  EXPECT_EQ(parent.GetTransform()->GetPosition(), glm::vec3(0.0f, 0.0f, 0.0f));
  EXPECT_EQ(child.GetTransform()->GetPosition(), glm::vec3(1.0f, 0.0f, 0.0f));
  EXPECT_EQ(grandchild.GetTransform()->GetPosition(), glm::vec3(1.0f, 5.0f, 0.0f));

  // Move root-level Parent to (100, 200, 300)
  parent.GetTransform()->SetPosition(glm::vec3(100.0f, 200.0f, 300.0f));

  EXPECT_EQ(parent.GetTransform()->GetPosition(), glm::vec3(100.0f, 200.0f, 300.0f));
  EXPECT_EQ(child.GetTransform()->GetPosition(), glm::vec3(101.0f, 200.0f, 300.0f));
  EXPECT_EQ(grandchild.GetTransform()->GetPosition(), glm::vec3(101.0f, 205.0f, 300.0f));
}

TEST(Hierarchy, ChildOrbitsOnParentRotation) {
  std::string error;
  auto project = Project::Load(kPong / "pong.mproj", &error);
  ASSERT_TRUE(project) << error;

  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]

[node name="Root"]

[node name="Player" parent="."]
[component type="Transform" node="Player"]
position = Vector3(0, 0, 0)

[node name="Eye" parent="Player"]
[component type="Transform" node="Player/Eye"]
position = Vector3(5, 0, 0)
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);

  auto player = scene.Find("Player");
  auto eye = scene.Find("Player/Eye");

  ASSERT_TRUE(player.IsValid());
  ASSERT_TRUE(eye.IsValid());

  // Turn player 90 degrees around Y (yaw = 90)
  player.GetTransform()->SetRotate(glm::vec3(0.0f, 90.0f, 0.0f));

  // Eye was at local (5, 0, 0) (+X). With +90 deg around Y:
  // (5, 0, 0) rotated by 90 deg around Y points down -Z: (0, 0, -5).
  glm::vec3 eye_pos = eye.GetTransform()->GetPosition();
  EXPECT_NEAR(eye_pos.x, 0.0f, 1e-4f);
  EXPECT_NEAR(eye_pos.y, 0.0f, 1e-4f);
  EXPECT_NEAR(eye_pos.z, -5.0f, 1e-4f);

  // Eye's world rotation should also reflect player's 90 deg yaw
  glm::vec3 eye_rot = eye.GetTransform()->GetRotate();
  EXPECT_NEAR(eye_rot.y, 90.0f, 0.05f);
}

TEST(Hierarchy, LocalTransformIndependence) {
  std::string error;
  auto project = Project::Load(kPong / "pong.mproj", &error);
  ASSERT_TRUE(project) << error;

  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]

[node name="Root"]

[node name="Body" parent="."]
[component type="Transform" node="Body"]
position = Vector3(10, 10, 10)

[node name="Arm" parent="Body"]
[component type="Transform" node="Body/Arm"]
position = Vector3(1, 0, 0)
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);

  auto body = scene.Find("Body");
  auto arm = scene.Find("Body/Arm");

  // Change Arm's local position directly
  arm.GetTransform()->SetLocalPosition(glm::vec3(0.0f, 2.0f, 0.0f));

  // Arm's world position updates, Body's position unaffected
  EXPECT_EQ(body.GetTransform()->GetPosition(), glm::vec3(10.0f, 10.0f, 10.0f));
  EXPECT_EQ(arm.GetTransform()->GetPosition(), glm::vec3(10.0f, 12.0f, 10.0f));

  // Now move body
  body.GetTransform()->SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
  EXPECT_EQ(arm.GetTransform()->GetPosition(), glm::vec3(0.0f, 2.0f, 0.0f));
}

TEST(Hierarchy, GameObjectGetParentAndChildren) {
  std::string error;
  auto project = Project::Load(kPong / "pong.mproj", &error);
  ASSERT_TRUE(project) << error;

  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]

[node name="Root"]

[node name="Car" parent="."]

[node name="WheelFront" parent="Car"]
[node name="WheelBack" parent="Car"]
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);

  auto car = scene.Find("Car");
  auto front = scene.Find("Car/WheelFront");
  auto back = scene.Find("Car/WheelBack");

  ASSERT_TRUE(car.IsValid());
  ASSERT_TRUE(front.IsValid());
  ASSERT_TRUE(back.IsValid());

  EXPECT_EQ(front.GetParent(), car);
  EXPECT_EQ(back.GetParent(), car);

  auto children = car.GetChildren();
  EXPECT_EQ(children.size(), 2u);
  EXPECT_TRUE((children[0] == front && children[1] == back) ||
              (children[0] == back && children[1] == front));

  // Reparent back wheel under front wheel
  back.SetParent(front, true);
  EXPECT_EQ(back.GetParent(), front);
  EXPECT_EQ(car.GetChildren().size(), 1u);
  EXPECT_EQ(front.GetChildren().size(), 1u);
  EXPECT_EQ(front.GetChildren()[0], back);
}

TEST(Hierarchy, AddForceModes) {
  std::string error;
  auto project = Project::Load(std::filesystem::path(MERCURY_SOURCE_DIR) /
                                   "examples/roll_a_ball/roll_a_ball.mproj",
                               &error);
  ASSERT_TRUE(project) << error;

  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]

[node name="Root"]

[node name="Ball" parent="."]
[component type="Transform" node="Ball"]
position = Vector3(0, 5, 0)

[component type="SphereCollider" node="Ball"]
radius = 0.5

[component type="Rigidbody" node="Ball"]
motion_type = "dynamic"
mass = 2.0
gravity_factor = 0.0
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);

  auto ball = scene.Find("Ball");
  ASSERT_TRUE(ball.IsValid());

  // Apply impulse using Unity-like AddForce on GameObject
  ball.AddForce(glm::vec3(10.0f, 0.0f, 0.0f), ForceMode::kImpulse);

  // Step physics simulation
  scene.SimulatePhysics(1);

  // Ball velocity should be impulse / mass = 10 / 2 = 5 m/s
  glm::vec3 vel = ball.GetTransform()->GetVelocity();
  EXPECT_NEAR(vel.x, 5.0f, 0.2f);
}

TEST(Hierarchy, PhysicsMovementCascadesToChildren) {
  std::string error;
  auto project = Project::Load(std::filesystem::path(MERCURY_SOURCE_DIR) /
                                   "examples/roll_a_ball/roll_a_ball.mproj",
                               &error);
  ASSERT_TRUE(project) << error;

  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]

[node name="Root"]

[node name="Ball" parent="."]
[component type="Transform" node="Ball"]
position = Vector3(0, 5, 0)

[component type="SphereCollider" node="Ball"]
radius = 0.5

[component type="Rigidbody" node="Ball"]
motion_type = "dynamic"
mass = 1.0
gravity_factor = 0.0

[node name="Antenna" parent="Ball"]
[component type="Transform" node="Ball/Antenna"]
position = Vector3(0, 1, 0)
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);

  auto ball = scene.Find("Ball");
  auto antenna = scene.Find("Ball/Antenna");
  ASSERT_TRUE(ball.IsValid());
  ASSERT_TRUE(antenna.IsValid());

  EXPECT_EQ(ball.GetTransform()->GetPosition(), glm::vec3(0.0f, 5.0f, 0.0f));
  EXPECT_EQ(antenna.GetTransform()->GetPosition(), glm::vec3(0.0f, 6.0f, 0.0f));

  // Apply velocity to ball via AddForce VelocityChange
  ball.AddForce(glm::vec3(60.0f, 0.0f, 0.0f), ForceMode::kVelocityChange);

  // Simulate 1 tick (1/60 s, body moves ~1 unit along +X)
  scene.SimulatePhysics(1);

  const glm::vec3 ball_pos = ball.GetTransform()->GetPosition();
  const glm::vec3 antenna_pos = antenna.GetTransform()->GetPosition();

  EXPECT_GT(ball_pos.x, 0.5f);
  // Antenna must maintain exact relative offset (0, 1, 0) to Ball
  EXPECT_NEAR(antenna_pos.x, ball_pos.x, 1e-4f);
  EXPECT_NEAR(antenna_pos.y, ball_pos.y + 1.0f, 1e-4f);
  EXPECT_NEAR(antenna_pos.z, ball_pos.z, 1e-4f);
}

TEST(Hierarchy, InterpolatedHierarchy) {
  std::string error;
  auto project = Project::Load(kPong / "pong.mproj", &error);
  ASSERT_TRUE(project) << error;

  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]

[node name="Root"]

[node name="Parent" parent="."]
[component type="Transform" node="Parent"]
position = Vector3(0, 0, 0)

[node name="Child" parent="Parent"]
[component type="Transform" node="Parent/Child"]
position = Vector3(0, 10, 0)
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);

  auto parent = scene.Find("Parent");
  auto child = scene.Find("Parent/Child");

  // Tick begins: snapshot captured at (0, 0, 0) for parent and (0, 10, 0) for child
  scene.OnTickBegin();

  // During tick, parent moves to (10, 0, 0)
  parent.GetTransform()->SetPosition(glm::vec3(10.0f, 0.0f, 0.0f));

  // Child world position should be (10, 10, 0)
  EXPECT_EQ(child.GetTransform()->GetPosition(), glm::vec3(10.0f, 10.0f, 0.0f));

  // Interpolate at alpha = 0.5
  scene.ProcessOnFrame(0.5f);

  // Parent interpolated position: (5, 0, 0)
  EXPECT_EQ(parent.GetTransform()->GetInterpolatedPosition(), glm::vec3(5.0f, 0.0f, 0.0f));
  // Child interpolated position must be (5, 10, 0)
  EXPECT_EQ(child.GetTransform()->GetInterpolatedPosition(), glm::vec3(5.0f, 10.0f, 0.0f));
}

// The editor moves one node this way while a handle is dragged, instead of
// rebuilding the scene every frame: the result must match a rebuild.
TEST(Hierarchy, SyncNodeTransformMatchesRebuild) {
  std::string error;
  auto project = Project::Load(kPong / "pong.mproj", &error);
  ASSERT_TRUE(project) << error;

  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]

[node name="Root"]

[node name="Player" parent="."]
[component type="Transform" node="Player"]
position = Vector3(10, 0, 0)

[node name="Weapon" parent="Player"]
[component type="Transform" node="Player/Weapon"]
position = Vector3(2, 3, 4)
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kEdit);
  scene.Instantiate(map);

  MapNode* player = map.GetRoot()->children[0].get();
  ASSERT_TRUE(player);
  ComponentData* transform = player->FindComponent("Transform");
  ASSERT_TRUE(transform);
  transform->properties.Set("position", glm::vec3(1.0f, 2.0f, 3.0f));
  transform->properties.Set("rotation", glm::vec3(0.0f, 90.0f, 0.0f));
  ASSERT_TRUE(scene.SyncNodeTransform(*player));

  const glm::vec3 synced = scene.Find("Player/Weapon").GetTransform()->GetPosition();
  EXPECT_EQ(scene.Find("Player").GetTransform()->GetPosition(), glm::vec3(1, 2, 3));

  MapScene rebuilt(project, MapScene::Mode::kEdit);
  rebuilt.Instantiate(map);
  const glm::vec3 expected = rebuilt.Find("Player/Weapon").GetTransform()->GetPosition();
  EXPECT_TRUE(glm::all(glm::epsilonEqual(synced, expected, 1e-2f)));
}
