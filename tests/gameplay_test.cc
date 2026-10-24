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
#include "stb/stb_image_write.h"
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

// Asks for another map from inside OnFrame, as a menu button would.
class TestMapSwitcher : public Behaviour {
 public:
  void Reflect(Reflector& r) override { r.Field("next", next); }
  void OnFrame(double) override { LoadMap(next); }
  std::string next;
};
MERCURY_SCRIPT(TestMapSwitcher);

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

TEST(Gameplay, ObjMaterialsImportTexturesEmissionAndGloss) {
  const auto dir = std::filesystem::temp_directory_path() / "mercury_obj_material_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  // Textures named in a different case than the .mtl spells them, as in
  // models made on Windows.
  auto write_png = [&](const char* name, int size, uint8_t (*pixel)(int, int)) {
    std::vector<uint8_t> pixels(size_t(size) * size * 4);
    for (int y = 0; y < size; y++) {
      for (int x = 0; x < size; x++) {
        uint8_t* p = &pixels[(size_t(y) * size + x) * 4];
        p[0] = p[1] = p[2] = pixel(x, y);
        p[3] = 255;
      }
    }
    stbi_write_png((dir / name).string().c_str(), size, size, 4, pixels.data(), size * 4);
  };
  write_png("color.png", 64, [](int, int) -> uint8_t { return 200; });
  // Bright stripes every 4 texels: a bump map with slopes along x only.
  write_png("bumps.png", 16, [](int x, int) -> uint8_t { return x % 4 == 0 ? 255 : 0; });
  {
    std::ofstream mtl(dir / "scene.mtl");
    mtl << "newmtl Lamp\nKd 0.5 0.5 0.5\nKe 4 3 2\nKs 0 0 0\nNs 10\n"
           "newmtl Stone\nKd 0 0 0\nKs 0.5 0.5 0.5\nNs 200\n"
           "map_Kd COLOR.PNG\nbump Bumps.png\n";
  }
  {
    std::ofstream obj(dir / "scene.obj");
    obj << "mtllib scene.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\n"
           "usemtl Lamp\nf 1/1 2/2 3/3\nusemtl Stone\nf 1/1 2/2 3/3\n";
  }
  ModelImportSettings settings;
  settings.max_texture_size = 32;
  std::string error;
  auto model = Model::Load((dir / "scene.obj").string(), settings, &error);
  ASSERT_TRUE(model) << error;

  const ModelMaterial* lamp = nullptr;
  const ModelMaterial* stone = nullptr;
  for (const auto& material : model->Materials()) {
    if (material.name == "Lamp") lamp = &material;
    if (material.name == "Stone") stone = &material;
  }
  ASSERT_TRUE(lamp && stone);
  EXPECT_EQ(lamp->emission, glm::vec3(4.0f, 3.0f, 2.0f));
  // No specular color: matte, whatever Ns says.
  EXPECT_FLOAT_EQ(lamp->smoothness, 0.0f);
  EXPECT_GT(stone->smoothness, 0.8f);

  // Found despite the case, scaled down to the limit, and Kd 0 next to a
  // texture means the texture alone.
  ASSERT_TRUE(stone->texture);
  EXPECT_EQ(stone->texture->width, 32);
  EXPECT_EQ(stone->texture->height, 32);
  EXPECT_EQ(glm::vec3(stone->color), glm::vec3(1.0f));

  // The bump map becomes a linear normal map tilted along x, not y.
  ASSERT_TRUE(stone->normal_texture);
  EXPECT_FALSE(stone->normal_texture->srgb);
  const auto& normals = stone->normal_texture->pixels;
  int tilted_x = 0;
  for (size_t i = 0; i < normals.size(); i += 4) {
    if (std::abs(normals[i] - 128) > 20) tilted_x++;
    EXPECT_NEAR(normals[i + 1], 128, 2);
  }
  EXPECT_GT(tilted_x, 0);

  // Normal-mapped meshes get unit tangents along +u.
  for (const auto& mesh : model->Meshes()) {
    if (&model->Materials()[mesh.material] != stone) continue;
    for (const auto& v : mesh.geometry->vertices) {
      EXPECT_NEAR(v.tangent.x, 1.0f, 1e-4f);
      EXPECT_NEAR(std::abs(v.tangent.w), 1.0f, 1e-6f);
    }
  }
  std::filesystem::remove_all(dir);
}

TEST(Gameplay, LoadMapFromScriptReplacesTheScene) {
  const auto dir = std::filesystem::temp_directory_path() / "mercury_load_map_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "maps");
  {
    std::ofstream(dir / "test.mproj") << "[project]\nname = \"Test\"\n"
                                         "main_map = \"res://maps/a.mmap\"\n";
    std::ofstream(dir / "maps/a.mmap")
        << "[mmap format=1]\n[node name=\"A\"]\n[node name=\"Switch\" parent=\".\"]\n"
           "[component type=\"TestMapSwitcher\" node=\"Switch\"]\n"
           "next = \"res://maps/b.mmap\"\n";
    std::ofstream(dir / "maps/b.mmap")
        << "[mmap format=1]\n[node name=\"B\"]\n[node name=\"Lamp\" parent=\".\"]\n"
           "[component type=\"Transform\" node=\"Lamp\"]\nposition = Vector3(1, 2, 3)\n"
           "[component type=\"PointLight\" node=\"Lamp\"]\nintensity = 2\nrange = 5\n";
  }
  std::string error;
  auto project = Project::Load(dir / "test.mproj", &error);
  ASSERT_TRUE(project) << error;
  Map map;
  ASSERT_TRUE(map.Load(dir / "maps/a.mmap", &error)) << error;
  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);
  scene.OnStart();
  ASSERT_TRUE(scene.Find("Switch").IsValid());

  // The request made during this frame takes effect at the next one.
  scene.OnFrame(1000.0 / 60.0);
  EXPECT_TRUE(scene.Find("Switch").IsValid());
  scene.OnFrame(1000.0 / 60.0);
  EXPECT_FALSE(scene.Find("Switch").IsValid());
  ASSERT_TRUE(scene.Find("Lamp").IsValid());

  ASSERT_EQ(scene.GetLighting().point_lights.size(), 1u);
  const PointLightData& light = scene.GetLighting().point_lights[0];
  EXPECT_EQ(light.position, glm::vec3(1.0f, 2.0f, 3.0f));
  EXPECT_EQ(light.color, glm::vec3(2.0f));
  EXPECT_FLOAT_EQ(light.range, 5.0f);
  std::filesystem::remove_all(dir);
}

TEST(Gameplay, AreaLightsAndProbeVolumeReachTheRenderer) {
  auto project = LoadProject();
  ASSERT_TRUE(project);
  Map map;
  std::string error;
  ASSERT_TRUE(map.LoadFromString(R"([mmap format=1]
[node name="Root"]
[node name="Panel" parent="."]
[component type="Transform" node="Panel"]
position = Vector3(0, 2, 0)
rotation = Vector3(-90, 0, 0)
[component type="AreaLight" node="Panel"]
intensity = 17
size = Vector2(0.4, 0.2)
[node name="Bulb" parent="."]
[component type="Transform" node="Bulb"]
position = Vector3(1, 1, 1)
[component type="PointLight" node="Bulb"]
shadows = "soft"
radius = 0.3
[node name="Probes" parent="."]
[component type="Transform" node="Probes"]
position = Vector3(0, 1, 0)
[component type="LightProbeVolume" node="Probes"]
size = Vector3(2, 2, 4)
resolution = Vector3(4, 40, 1)
include_emission = false
)",
                                 &error))
      << error;
  MapScene scene(project, MapScene::Mode::kPlay);
  scene.Instantiate(map);
  const SceneLighting& lighting = scene.GetLighting();

  ASSERT_EQ(lighting.area_lights.size(), 1u);
  const AreaLightData& panel = lighting.area_lights[0];
  // Pitched down: the rectangle lies flat and shines toward -Y.
  const glm::vec3 normal = panel.Normal();
  EXPECT_NEAR(normal.y, -1.0f, 1e-4f);
  EXPECT_NEAR(glm::length(panel.right), 0.2f, 1e-4f);
  EXPECT_NEAR(glm::length(panel.up), 0.1f, 1e-4f);
  EXPECT_EQ(panel.color, glm::vec3(17.0f));
  EXPECT_EQ(panel.shadows, 2);  // soft by default

  ASSERT_EQ(lighting.point_lights.size(), 1u);
  EXPECT_EQ(lighting.point_lights[0].shadows, 2);
  EXPECT_FLOAT_EQ(lighting.point_lights[0].radius, 0.3f);

  // Centered on its node; counts are kept between 2 and 16.
  const ProbeVolumeData& probes = lighting.probes;
  ASSERT_TRUE(probes.enabled);
  EXPECT_EQ(probes.min, glm::vec3(-1.0f, 0.0f, -2.0f));
  EXPECT_EQ(probes.max, glm::vec3(1.0f, 2.0f, 2.0f));
  EXPECT_EQ(probes.counts, glm::ivec3(4, 16, 2));
  EXPECT_FALSE(probes.include_emission);
}

TEST(Gameplay, GlobalIlluminationIsOnByDefault) {
  auto project = LoadProject();
  ASSERT_TRUE(project);
  auto lighting_of = [&](const char* text) {
    Map map;
    std::string error;
    EXPECT_TRUE(map.LoadFromString(text, &error)) << error;
    MapScene scene(project, MapScene::Mode::kPlay);
    scene.Instantiate(map);
    return scene.GetLighting();
  };

  // No volume: the renderer fits one around the scene; point lights cast
  // soft shadows unless told otherwise.
  const SceneLighting plain = lighting_of(R"([mmap format=1]
[node name="Root"]
[node name="Bulb" parent="."]
[component type="Transform" node="Bulb"]
[component type="PointLight" node="Bulb"]
)");
  EXPECT_TRUE(plain.probes.enabled);
  EXPECT_TRUE(plain.probes.automatic);
  ASSERT_EQ(plain.point_lights.size(), 1u);
  EXPECT_EQ(plain.point_lights[0].shadows, 2);

  // A fitted volume keeps its own settings.
  const SceneLighting fitted = lighting_of(R"([mmap format=1]
[node name="Root"]
[component type="Transform" node="."]
[component type="LightProbeVolume" node="."]
fit_to_scene = true
bounces = 5
)");
  EXPECT_TRUE(fitted.probes.enabled);
  EXPECT_TRUE(fitted.probes.automatic);
  EXPECT_EQ(fitted.probes.bounces, 5);

  // Intensity 0 turns it off.
  const SceneLighting off = lighting_of(R"([mmap format=1]
[node name="Root"]
[component type="Transform" node="."]
[component type="LightProbeVolume" node="."]
intensity = 0
)");
  EXPECT_FALSE(off.probes.enabled);
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
