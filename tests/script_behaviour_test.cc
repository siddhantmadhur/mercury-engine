// Tests of TypeScript scripts running as Behaviours: callback order, fields, events, errors and
// destroyed objects. The fixtures are in tests/scripts/project and are built with esbuild.
#include <gtest/gtest.h>

#include <filesystem>
#include <mutex>

#include "debugger/debugger.h"
#include "project/map.h"
#include "project/project.h"
#include "scene/map_scene.h"
#include "scripting/js/script_behaviour.h"
#include "scripting/js/script_builder.h"
#include "scripting/js/script_runtime.h"
#include "scripting/ts_types.h"
#include "scripting/type_registry.h"

using namespace Mercury;
namespace fs = std::filesystem;

namespace {

std::shared_ptr<Project> g_project;

// Copies the fixture project to the cache dir, builds it and loads its bundle (once per run).
class ScriptBehaviourTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    static std::once_flag once;
    std::call_once(once, [] {
      const fs::path source = fs::path(MERCURY_SOURCE_DIR) / "tests/scripts/project";
      const fs::path work = fs::path(MERCURY_TEST_CACHE_DIR) / "script_project";
      std::error_code ec;
      fs::remove_all(work, ec);
      fs::create_directories(work.parent_path(), ec);
      fs::copy(source, work, fs::copy_options::recursive, ec);
      ASSERT_FALSE(ec) << ec.message();
      std::string log;
      ASSERT_TRUE(ScriptBuilder::Build(work, {}, &log)) << log;
      RegisterBuiltinTypes();
      ASSERT_TRUE(ScriptRuntime::Instance().LoadBundle(
          ScriptBuilder::BundlePath(work).string(), ScriptBuilder::SourceMapPath(work).string()));
      std::string error;
      g_project = Project::Load(work / "fixture.mproj", &error);
      ASSERT_TRUE(g_project) << error;
    });
    ASSERT_TRUE(g_project);
  }

  void SetUp() override { Run("globalThis.__log.length = 0"); }

  static std::string Run(const std::string& source) {
    std::string result;
    EXPECT_TRUE(ScriptRuntime::Instance().RunString(source, "test", &result)) << source;
    return result;
  }
  static std::string Log() { return Run("globalThis.__log.join(',')"); }

  static void Tick(MapScene& scene, int ticks) {
    for (int i = 0; i < ticks; i++) {
      scene.OnTickBegin();
      scene.OnTick(1000.0 / 60.0);
      scene.SimulatePhysics(1);
    }
  }
  static void Load(MapScene& scene, const char* text) {
    Map map;
    std::string error;
    ASSERT_TRUE(map.LoadFromString(text, &error)) << error;
    scene.Instantiate(map);
    scene.OnStart();
  }
  static ScriptBehaviour* Script(MapScene& scene, const char* path, const char* type) {
    for (Behaviour* behaviour : scene.GetBehaviours(scene.Find(path).GetEntity())) {
      if (behaviour->GetTypeName() == type) return dynamic_cast<ScriptBehaviour*>(behaviour);
    }
    return nullptr;
  }
};

}  // namespace

TEST_F(ScriptBehaviourTest, CallbacksRunInOrder) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap format=1]
[node name="Root"]
[node name="A" parent="."]
[component type="Probe" node="A"]
)");
  Tick(scene, 2);
  EXPECT_EQ(Log(), "A.awake,A.onEnable,A.onStart,A.onTick,A.onTick");
}

TEST_F(ScriptBehaviourTest, DisablingStopsCallbacksAndEnablingResumesThem) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap format=1]
[node name="Root"]
[node name="A" parent="."]
[component type="Probe" node="A"]
)");
  Tick(scene, 1);
  Script(scene, "A", "Probe")->SetEnabled(false);
  Tick(scene, 2);
  Script(scene, "A", "Probe")->SetEnabled(true);
  Tick(scene, 1);
  EXPECT_EQ(Log(), "A.awake,A.onEnable,A.onStart,A.onTick,A.onDisable,A.onEnable,A.onTick");
}

TEST_F(ScriptBehaviourTest, DestroyHappensAtTheEndOfTheTick) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap format=1]
[node name="Root"]
[node name="Victim" parent="."]
[component type="Probe" node="Victim"]
[node name="K" parent="."]
[component type="Killer" node="K"]
)");
  EXPECT_TRUE(scene.Find("Victim"));
  Tick(scene, 1);
  EXPECT_FALSE(scene.Find("Victim"));
  EXPECT_NE(Log().find("Victim.onDestroy"), std::string::npos);
}

TEST_F(ScriptBehaviourTest, FieldDefaultsAreInTheSchema) {
  const ComponentInfo* info = TypeRegistry::Instance().FindComponent("Probe");
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->is_script);
  const FieldInfo* speed = info->FindField("speed");
  ASSERT_TRUE(speed);
  EXPECT_EQ(speed->type, FieldType::kFloat);
  EXPECT_DOUBLE_EQ(speed->default_value.ToFloat().value_or(-1), 2.5);
  EXPECT_EQ(speed->tooltip, "A float");
  EXPECT_EQ(info->FindField("lives")->type, FieldType::kInt);
  EXPECT_EQ(info->FindField("label")->type, FieldType::kString);
  EXPECT_EQ(info->FindField("flag")->type, FieldType::kBool);
  EXPECT_EQ(info->FindField("offset")->type, FieldType::kVector3);
  EXPECT_EQ(info->FindField("target")->type, FieldType::kNodeRef);
  EXPECT_EQ(info->FindField("fired")->type, FieldType::kEvent);
  const FieldInfo* mode = info->FindField("mode");
  ASSERT_TRUE(mode);
  EXPECT_EQ(mode->type, FieldType::kEnum);
  EXPECT_EQ(mode->enum_values, (std::vector<std::string>{"a", "b"}));
  ASSERT_EQ(info->methods.size(), 2u);
  // A bindable is not a saved field.
  EXPECT_EQ(info->FindField("doubled"), nullptr);
}

TEST_F(ScriptBehaviourTest, FieldsDefinedLaterInTheFileAndInheritedOnesWork) {
  EXPECT_EQ(TypeRegistry::Instance().FindComponent("Late")->FindField("value")->default_value.ToInt().value_or(0), 42);
  const ComponentInfo* derived = TypeRegistry::Instance().FindComponent("Derived");
  ASSERT_TRUE(derived);
  EXPECT_TRUE(derived->FindField("baseField"));
  EXPECT_TRUE(derived->FindField("derivedField"));
}

TEST_F(ScriptBehaviourTest, MapValuesOverrideTheDefaults) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap format=1]
[node name="Root"]
[node name="A" parent="."]
[component type="Probe" node="A"]
speed = 7.5
lives = 9
label = "yo"
flag = false
mode = "b"
offset = Vector3(4, 5, 6)
target = NodePath("Root")
)");
  ScriptBehaviour* probe = Script(scene, "A", "Probe");
  ASSERT_TRUE(probe);
  mtext::PropertyList saved;
  auto writer = Reflector::Write(saved);
  probe->Reflect(writer);
  EXPECT_DOUBLE_EQ(saved.GetFloat("speed"), 7.5);
  EXPECT_EQ(saved.GetInt("lives"), 9);
  EXPECT_EQ(saved.GetString("label"), "yo");
  EXPECT_FALSE(saved.GetBool("flag", true));
  EXPECT_EQ(saved.GetString("mode"), "b");
  EXPECT_EQ(saved.Get("offset")->Get<glm::vec3>()->y, 5.0f);
  EXPECT_EQ(saved.Get("target")->Get<mtext::NodePath>()->path, "Root");
  // The script sees the same values.
  mtext::Variant doubled;
  auto getter = Reflector::Get("doubled", &doubled);
  probe->ReflectData(getter);
  EXPECT_DOUBLE_EQ(doubled.ToFloat().value_or(0), 15.0);
}

TEST_F(ScriptBehaviourTest, SavedEventCallsReachCallableMethods) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap format=1]
[node name="Root"]
[node name="Target" parent="."]
[component type="Probe" node="Target"]
[node name="Source" parent="."]
[component type="Probe" node="Source"]
fired = [[NodePath("Target"), "Probe", "add", 2.5], [NodePath("Target"), "Probe", "bump"]]
)");
  ScriptBehaviour* source = Script(scene, "Source", "Probe");
  ASSERT_TRUE(source);
  Event* fired = source->FindEvent("fired");
  ASSERT_TRUE(fired);
  EXPECT_EQ(fired->calls.size(), 2u);
  Run("globalThis.__log.length = 0");
  fired->Invoke();
  fired->Invoke();
  EXPECT_EQ(Log(), "add 2.5,bump,add 2.5,bump");
}

TEST_F(ScriptBehaviourTest, AnExceptionIsLoggedAndTheNextTickStillRuns) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap format=1]
[node name="Root"]
[node name="T" parent="."]
[component type="Thrower" node="T"]
)");
  std::vector<std::string> errors;
  PDebug::SetSink([&](PDebug::Level level, const std::string& message) {
    if (level == PDebug::Level::kError) errors.push_back(message);
  });
  Tick(scene, 3);
  PDebug::SetSink({});
  EXPECT_EQ(Log(), "Thrower.tick 1,Thrower.tick 2,Thrower.tick 3");
  ASSERT_FALSE(errors.empty());
  EXPECT_NE(errors[0].find("Thrower.onTick"), std::string::npos) << errors[0];
  EXPECT_NE(errors[0].find("boom"), std::string::npos) << errors[0];
  // The stack points into the TypeScript file, not the bundle.
  EXPECT_NE(errors[0].find("probe.ts:"), std::string::npos) << errors[0];
}

TEST_F(ScriptBehaviourTest, UsingADestroyedObjectThrowsInsteadOfCrashing) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap format=1]
[node name="Root"]
[node name="Victim" parent="."]
[node name="S" parent="."]
[component type="Stale" node="S"]
[node name="K" parent="."]
[component type="Killer" node="K"]
)");
  Tick(scene, 2);
  EXPECT_EQ(Log(), "Stale.ok,Stale.caught GameObject was destroyed");
}

TEST_F(ScriptBehaviourTest, UnknownKeysInAMapAreReported) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  std::vector<std::string> warnings;
  PDebug::SetSink([&](PDebug::Level level, const std::string& message) {
    if (level == PDebug::Level::kWarn) warnings.push_back(message);
  });
  Load(scene, R"([mmap format=1]
[node name="Root"]
[node name="A" parent="."]
[component type="Probe" node="A"]
old_name = 3
)");
  PDebug::SetSink({});
  bool found = false;
  for (const auto& w : warnings) found = found || w.find("old_name") != std::string::npos;
  EXPECT_TRUE(found);
}

TEST_F(ScriptBehaviourTest, DataAssetsLoadIntoTheirClass) {
  const AssetTypeInfo* type = TypeRegistry::Instance().FindAssetType("LevelData");
  ASSERT_TRUE(type);
  EXPECT_TRUE(type->from_game);
  EXPECT_TRUE(type->FindField("par"));

  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap format=1]
[node name="Root"]
[node name="U" parent="."]
[component type="AssetUser" node="U"]
)");
  Tick(scene, 1);
  EXPECT_EQ(Log(), "level 7 Second");
}

TEST_F(ScriptBehaviourTest, EngineComponentsHaveRuntimeMethods) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap format=1]
[node name="Root"]
[node name="Floor" parent="."]
[component type="Transform" node="Floor"]
position = Vector3(0, -0.5, 0)
[component type="BoxCollider" node="Floor"]
size = Vector3(40, 1, 40)
[node name="Player" parent="."]
[component type="Transform" node="Player"]
position = Vector3(0, 1, 0)
[component type="CharacterController" node="Player"]
height = 1.8
radius = 0.3
[component type="ComponentUser" node="Player"]
)");
  Tick(scene, 30);
  const std::string log = Log();
  EXPECT_NE(log.find("grounded true v true"), std::string::npos) << log;
  EXPECT_NE(log.find("audio true"), std::string::npos) << log;
  EXPECT_EQ(log.find("no controller"), std::string::npos) << log;
  // The controller moved along -Z through the script.
  EXPECT_LT(scene.Find("Player").GetTransform()->GetPosition().z, -2.0f);
}

TEST_F(ScriptBehaviourTest, GeneratedComponentClassesHaveTheRuntimeMethods) {
  const std::string ts = GenerateComponentsTs();
  EXPECT_NE(ts.find("export class Animator extends Component"), std::string::npos);
  EXPECT_NE(ts.find("setTrigger(name: string): void"), std::string::npos);
  EXPECT_NE(ts.find("getCurrentState(layer = 0): string"), std::string::npos);
  EXPECT_NE(ts.find("playOneShot(clip: string"), std::string::npos);
  EXPECT_NE(ts.find("get isGrounded(): boolean"), std::string::npos);
  // Field names are camelCase on the TypeScript side.
  EXPECT_NE(ts.find("get applyRootMotion(): boolean"), std::string::npos);
  EXPECT_EQ(ts.find("get apply_root_motion"), std::string::npos);
}
