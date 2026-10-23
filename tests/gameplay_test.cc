// Tests for the Unity-style gameplay systems: events, triggers, layered
// raycasts, CharacterController, prefab instances, models, the animator
// controller format, input scripts and ListView.itemsSource.

#include <gtest/gtest.h>

#include <GLFW/glfw3.h>

#include <filesystem>
#include <fstream>

#include "animation/animator_controller.h"
#include "components/components.h"
#include "input/input_manager.h"
#include "input/input_script.h"
#include "math/mathf.h"
#include "project/map.h"
#include "project/project.h"
#include "properties/model.h"
#include "scene/map_scene.h"
#include "scripting/behaviour.h"
#include "ui/controls.h"

using namespace Mercury;

namespace {

const std::filesystem::path kRollABall =
    std::filesystem::path(MERCURY_SOURCE_DIR) / "examples/roll_a_ball";

// Collects what other scripts and events do to it.
class TestCounter : public Behaviour {
 public:
  void ReflectMethods(MethodReflector& m) override {
    m.Method("Add", [this](float amount) { total += amount; });
    m.Method("Bump", [this] { bumps++; });
  }
  void OnTriggerEnter(GameObject) override { entered++; }
  void OnTriggerStay(GameObject) override { stayed++; }
  void OnTriggerExit(GameObject) override { exited++; }

  float total = 0.0f;
  int bumps = 0;
  int entered = 0;
  int stayed = 0;
  int exited = 0;
};
MERCURY_SCRIPT(TestCounter);

class TestEmitter : public Behaviour {
 public:
  void Reflect(Reflector& r) override { r.Field("fired", fired); }
  Event fired;
};
MERCURY_SCRIPT(TestEmitter);

// Walks its CharacterController along -Z every tick.
class TestWalker : public Behaviour {
 public:
  void OnStart() override { controller = GetComponent<CharacterController>(); }
  void OnTick(double) override {
    if (controller) controller->Move(glm::vec3(0.0f, -0.05f, -0.1f));
  }
  CharacterController* controller = nullptr;
};
MERCURY_SCRIPT(TestWalker);

std::shared_ptr<Project> LoadProject() {
  std::string error;
  auto project = Project::Load(kRollABall / "roll_a_ball.mproj", &error);
  EXPECT_TRUE(project) << error;
  return project;
}

void Tick(MapScene& scene, int ticks) {
  for (int i = 0; i < ticks; i++) {
    scene.OnTickBegin();
    scene.OnTick(1000.0 / 60.0);
    scene.SimulatePhysics(1);
  }
}

}  // namespace

TEST(Gameplay, EventCallsSavedMethodWithArgument) {
  auto project = LoadProject();
  ASSERT_TRUE(project);
  Map map;
  std::string error;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]
[node name="Root"]
[node name="Target" parent="."]
[component type="TestCounter" node="Target"]
[node name="Source" parent="."]
[component type="TestEmitter" node="Source"]
fired = [[NodePath("Target"), "TestCounter", "Add", 2.5], [NodePath("Target"), "TestCounter", "Bump"]]
)",
                                 &error))
      << error;
  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);
  scene.OnStart();

  auto* emitter = scene.Find("Source").GetBehaviour<TestEmitter>();
  auto* counter = scene.Find("Target").GetBehaviour<TestCounter>();
  ASSERT_TRUE(emitter && counter);
  ASSERT_EQ(emitter->fired.calls.size(), 2u);

  int listened = 0;
  const auto id = emitter->fired.AddListener([&] { listened++; });
  emitter->fired.Invoke();
  emitter->fired.Invoke();
  EXPECT_FLOAT_EQ(counter->total, 5.0f);
  EXPECT_EQ(counter->bumps, 2);
  EXPECT_EQ(listened, 2);

  emitter->fired.RemoveListener(id);
  emitter->fired.Invoke();
  EXPECT_EQ(listened, 2);
}

TEST(Gameplay, CharacterWalksThroughTrigger) {
  auto project = LoadProject();
  ASSERT_TRUE(project);
  Map map;
  std::string error;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]
[node name="Root"]
[node name="Floor" parent="."]
[component type="Transform" node="Floor"]
position = Vector3(0, -0.5, 0)
[component type="BoxCollider" node="Floor"]
size = Vector3(40, 1, 40)
[node name="Zone" parent="."]
[component type="Transform" node="Zone"]
position = Vector3(0, 1, -5)
[component type="BoxCollider" node="Zone"]
size = Vector3(4, 2, 2)
is_trigger = true
[component type="TestCounter" node="Zone"]
[node name="Player" parent="."]
[component type="Transform" node="Player"]
position = Vector3(0, 1, 0)
[component type="CharacterController" node="Player"]
height = 1.8
radius = 0.3
[component type="TestWalker" node="Player"]
)",
                                 &error))
      << error;
  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);
  scene.OnStart();
  Tick(scene, 120);  // 12 m: in front of, through and past the zone

  auto* counter = scene.Find("Zone").GetBehaviour<TestCounter>();
  ASSERT_TRUE(counter);
  EXPECT_EQ(counter->entered, 1);
  EXPECT_EQ(counter->exited, 1);
  EXPECT_GT(counter->stayed, 5);

  GameObject player = scene.Find("Player");
  auto* controller = player.GetComponent<CharacterController>();
  ASSERT_TRUE(controller);
  EXPECT_TRUE(controller->IsGrounded());
  const glm::vec3 position = player.GetTransform()->GetPosition();
  EXPECT_NEAR(position.y, 0.9f, 0.1f);  // standing on the floor
  EXPECT_LT(position.z, -10.0f);
}

TEST(Gameplay, RaycastHonoursLayerMask) {
  auto project = LoadProject();
  ASSERT_TRUE(project);
  Map map;
  std::string error;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]
[node name="Root"]
[node name="Near" parent="."]
[component type="Transform" node="Near"]
position = Vector3(0, 0, -3)
[component type="BoxCollider" node="Near"]
[node name="Far" parent="." layer=8]
[component type="Transform" node="Far"]
position = Vector3(0, 0, -6)
[component type="BoxCollider" node="Far"]
[node name="Ghost" parent="."]
[component type="Transform" node="Ghost"]
position = Vector3(0, 0, -1.5)
[component type="BoxCollider" node="Ghost"]
size = Vector3(0.5, 0.5, 0.5)
is_trigger = true
)",
                                 &error))
      << error;
  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);
  scene.OnStart();
  Tick(scene, 1);

  const glm::vec3 origin(0.0f);
  const glm::vec3 forward(0.0f, 0.0f, -1.0f);
  // Triggers count by default, as with Unity's queriesHitTriggers.
  auto hit = scene.Physics().Raycast(origin, forward, 20.0f);
  ASSERT_TRUE(hit);
  EXPECT_EQ(scene.GetGameObject(hit->entity).GetName(), "Ghost");

  hit = scene.Physics().Raycast(origin, forward, 20.0f, kDefaultRaycastLayers, false);
  ASSERT_TRUE(hit);
  EXPECT_EQ(scene.GetGameObject(hit->entity).GetName(), "Near");
  EXPECT_NEAR(hit->distance, 2.5f, 1e-3f);

  hit = scene.Physics().Raycast(origin, forward, 20.0f, 1u << 8);
  ASSERT_TRUE(hit);
  EXPECT_EQ(scene.GetGameObject(hit->entity).GetName(), "Far");

  EXPECT_FALSE(scene.Physics().Raycast(origin, forward, 2.0f, kDefaultRaycastLayers, false));
}

TEST(Gameplay, PrefabInstanceKeepsChildrenAndOverrides) {
  auto project = LoadProject();
  ASSERT_TRUE(project);
  std::string error;
  Map prefab;
  ASSERT_TRUE(prefab.LoadFromString(R"([mprefab format=1]
[node name="Gun"]
[component type="Transform" node="."]
position = Vector3(1, 2, 3)
scale = Vector3(2, 2, 2)
[node name="Barrel" parent="."]
[component type="Transform" node="Barrel"]
position = Vector3(0, 0, -1)
)",
                                    &error))
      << error;
  auto loader = [&](const std::string& path) -> const Map* {
    return path == "res://gun.mprefab" ? &prefab : nullptr;
  };
  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]
[node name="Root"]
[node name="Pistol" parent="." instance="res://gun.mprefab"]
[component type="Transform" node="Pistol"]
position = Vector3(5, 0, 0)
)",
                                 &error, loader))
      << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);
  GameObject pistol = scene.Find("Pistol");
  ASSERT_TRUE(pistol.IsValid());
  // The override replaces the position and keeps the prefab's scale.
  EXPECT_EQ(pistol.GetTransform()->GetPosition(), glm::vec3(5.0f, 0.0f, 0.0f));
  EXPECT_EQ(pistol.GetTransform()->GetScale(), glm::vec3(2.0f));
  GameObject barrel = scene.Find("Pistol/Barrel");
  ASSERT_TRUE(barrel.IsValid());
  EXPECT_NEAR(barrel.GetTransform()->GetPosition().z, -2.0f, 1e-4f);

  // Saving writes the instance, not the prefab's contents.
  const std::string saved = map.SaveToString();
  EXPECT_NE(saved.find("instance"), std::string::npos);
  EXPECT_EQ(saved.find("Barrel"), std::string::npos);
}

TEST(Gameplay, ModelLoadsNodesAndMeshes) {
  const auto dir = std::filesystem::temp_directory_path() / "mercury_model_test";
  std::filesystem::create_directories(dir);
  const auto path = dir / "two_parts.obj";
  {
    std::ofstream obj(path);
    obj << "o Left\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"
           "o Right\nv 2 0 0\nv 3 0 0\nv 2 1 0\nf 4 5 6\n";
  }
  std::string error;
  auto model = Model::Load(path.string(), ModelImportSettings{}, &error);
  ASSERT_TRUE(model) << error;
  EXPECT_EQ(model->Meshes().size(), 2u);
  EXPECT_GE(model->FindNode("Left"), 0);
  EXPECT_GE(model->FindNode("Right"), 0);
  std::filesystem::remove_all(dir);
}

TEST(Gameplay, AnimatorControllerParses) {
  std::string error;
  auto controller = AnimatorController::FromString(R"([manimator format=1]
[model]
model = Resource("res://hero.fbx")
loop_clips = ["Idle", "Run"]
[parameter name="Speed" type="float"]
[parameter name="Jump" type="trigger"]
[layer name="Base"]
[state name="Locomotion" layer="Base" default=true]
blend = "1d"
parameter = "Speed"
motions = [["Idle", 0.0], ["Run", 1.0]]
[state name="Jump" layer="Base"]
clip = "Jump"
[transition from="Any State" to="Jump" layer="Base"]
conditions = [["Jump", "if"]]
duration = 0.1
[transition from="Jump" to="Locomotion" layer="Base"]
has_exit_time = true
exit_time = 0.9
)",
                                                   &error);
  ASSERT_TRUE(controller) << error;
  EXPECT_EQ(controller->GetModel(), "res://hero.fbx");
  ASSERT_EQ(controller->Parameters().size(), 2u);
  EXPECT_EQ(controller->Parameters()[1].type, AnimatorParameter::Type::kTrigger);
  ASSERT_EQ(controller->Layers().size(), 1u);
  const auto& layer = controller->Layers()[0];
  ASSERT_EQ(layer.states.size(), 2u);
  EXPECT_EQ(layer.states[0].blend, AnimatorState::Blend::k1D);
  EXPECT_EQ(layer.states[0].motions.size(), 2u);
  ASSERT_EQ(layer.transitions.size(), 2u);
  EXPECT_EQ(layer.transitions[0].from, -1);
  EXPECT_TRUE(layer.transitions[1].has_exit_time);
  EXPECT_TRUE(controller->IsLooping("Run"));
  EXPECT_FALSE(controller->IsLooping("Jump"));

  EXPECT_FALSE(AnimatorController::FromString(
      "[layer name=\"Base\"]\n[transition from=\"A\" to=\"B\" layer=\"Base\"]\n", &error));
}

TEST(Gameplay, InputScriptPressesAndReleasesKeys) {
  InputScript script;
  std::string error;
  EXPECT_FALSE(script.LoadFromString("10 press nokey\n", &error));
  EXPECT_FALSE(script.LoadFromString("10 dance\n", &error));
  ASSERT_TRUE(script.LoadFromString("# walk\n2 press w\n3 tap space\n5 release w\n6 quit\n",
                                    &error))
      << error;

  InputManager input;
  bool quit = false;
  InputScript::Hooks hooks;
  hooks.quit = [&] { quit = true; };
  auto frame = [&](uint64_t n) {
    input.BeginFrame();
    script.Apply(n, input, nullptr, hooks);
  };
  frame(0);
  EXPECT_FALSE(input.IsPressed(GLFW_KEY_W));
  frame(2);
  EXPECT_TRUE(input.IsPressed(GLFW_KEY_W));
  frame(3);
  EXPECT_TRUE(input.IsPressed(GLFW_KEY_SPACE));
  frame(4);
  EXPECT_FALSE(input.IsPressed(GLFW_KEY_SPACE));
  EXPECT_TRUE(input.IsPressed(GLFW_KEY_W));
  frame(5);
  EXPECT_FALSE(input.IsPressed(GLFW_KEY_W));
  EXPECT_FALSE(quit);
  frame(6);
  EXPECT_TRUE(quit);
}

TEST(Gameplay, ListViewFollowsItemsSource) {
  UI::ListView list;
  std::vector<int> items = {1, 2, 3};
  list.SetItemsSource(&items);
  EXPECT_EQ(list.itemCount(), 3);
  items.push_back(4);
  EXPECT_EQ(list.itemCount(), 3);  // until refreshed, as in Unity
  list.RefreshItems();
  EXPECT_EQ(list.itemCount(), 4);
  list.SetItemsSource<int>(nullptr);
  EXPECT_EQ(list.itemsSource(), nullptr);
}

TEST(Gameplay, MathfMatchesUnity) {
  float velocity = 0.0f;
  float value = 0.0f;
  for (int i = 0; i < 200; i++) value = Mathf::SmoothDamp(value, 10.0f, velocity, 0.2f, 0.02f);
  EXPECT_NEAR(value, 10.0f, 1e-2f);

  Random::InitState(7);
  for (int i = 0; i < 1000; i++) {
    const int n = Random::Range(0, 3);
    EXPECT_GE(n, 0);
    EXPECT_LT(n, 3);
    const float f = Random::Range(0.9f, 1.1f);
    EXPECT_GE(f, 0.9f);
    EXPECT_LE(f, 1.1f);
  }
}
