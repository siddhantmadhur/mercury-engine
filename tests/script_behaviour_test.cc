// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of AngelScript scripts running as Behaviours: callback order, fields,
// events, errors and data assets.
#include "scripting/as/script_behaviour.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

#include "project/map.h"
#include "project/project.h"
#include "properties/transform.h"
#include "scene/map_scene.h"
#include "scripting/as/script_builder.h"
#include "scripting/as/script_event.h"
#include "scripting/as/script_runtime.h"
#include "scripting/type_registry.h"
#include "test_paths.h"
#include "test_scripts.h"

using namespace Mercury;
namespace fs = std::filesystem;

namespace {

std::shared_ptr<Project> g_project;

// A native (MERCURY_SCRIPT) script, to check the C++ registration path gets
// the same field accessors as AngelScript classes.
class NativeAccessorProbe : public Behaviour {
 public:
  void Reflect(Reflector& r) override { r.Field("amount", amount); }
  float amount = 0.0f;
};
MERCURY_TEST_BEHAVIOUR(NativeAccessorProbe);

class ScriptBehaviourTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    static std::once_flag once;
    std::call_once(once, [] {
      const fs::path source =
          fs::path(MERCURY_SOURCE_DIR) / "tests/scripts/project";
      const fs::path work = Mercury::test::CacheDir() / "script_project";
      std::error_code ec;
      fs::remove_all(work, ec);
      fs::create_directories(work.parent_path(), ec);
      fs::copy(source, work, fs::copy_options::recursive, ec);
      ASSERT_FALSE(ec) << ec.message();
      RegisterBuiltinTypes();
      std::string log;
      ASSERT_TRUE(ScriptBuilder::Build(work, {}, &log)) << log;
      std::string error;
      g_project = Project::Load(work / "fixture.mproj", &error);
      ASSERT_TRUE(g_project) << error;
    });
    ASSERT_TRUE(g_project);
  }

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
  static ScriptBehaviour* Script(MapScene& scene, const char* path,
                                 const char* type) {
    for (Behaviour* behaviour :
         scene.GetBehaviours(scene.Find(path).GetEntity())) {
      if (behaviour->GetTypeName() == type)
        return dynamic_cast<ScriptBehaviour*>(behaviour);
    }
    return nullptr;
  }
  static std::map<std::string, mtext::Variant> Fields(ScriptBehaviour* script) {
    mtext::PropertyList properties;
    auto writer = Reflector::Write(properties);
    script->Reflect(writer);
    std::map<std::string, mtext::Variant> fields;
    for (const auto& property : properties)
      fields[property.key] = property.value;
    return fields;
  }
};

}  // namespace

TEST_F(ScriptBehaviourTest, CallbacksRunInOrder) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap]
format = 2

[[component]]
node = "A"
type = "Probe"

[[node]]
name = "Root"

[[node]]
name = "A"
parent = "."
)");
  Tick(scene, 1);
  EXPECT_EQ(scene.Find("A").GetTag(), "aest");
  Tick(scene, 1);
  EXPECT_EQ(scene.Find("A").GetTag(), "aestt");
}

TEST_F(ScriptBehaviourTest, DisablingStopsCallbacksAndEnablingResumesThem) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap]
format = 2

[[component]]
node = "A"
type = "Probe"

[[node]]
name = "Root"

[[node]]
name = "A"
parent = "."
)");
  Tick(scene, 1);
  Script(scene, "A", "Probe")->SetEnabled(false);
  Tick(scene, 2);
  Script(scene, "A", "Probe")->SetEnabled(true);
  Tick(scene, 1);
  EXPECT_EQ(scene.Find("A").GetTag(), "aestdet");
}

TEST_F(ScriptBehaviourTest, FieldDefaultsAreInTheSchema) {
  const ComponentInfo* probe = TypeRegistry::Instance().FindComponent("Probe");
  ASSERT_NE(probe, nullptr);
  ASSERT_TRUE(probe->is_script);
  const FieldInfo* speed = probe->FindField("speed");
  ASSERT_NE(speed, nullptr);
  EXPECT_EQ(speed->type, FieldType::kFloat);
  EXPECT_FLOAT_EQ(speed->default_value.ToFloat().value_or(0.0), 2.5);
  EXPECT_EQ(speed->tooltip, "A float");
  const FieldInfo* mode = probe->FindField("mode");
  ASSERT_NE(mode, nullptr);
  EXPECT_EQ(mode->type, FieldType::kEnum);
  EXPECT_EQ(mode->enum_values, (std::vector<std::string>{"a", "b"}));
  ASSERT_NE(probe->FindField("target"), nullptr);
  EXPECT_EQ(probe->FindField("target")->type, FieldType::kNodeRef);
  ASSERT_NE(probe->FindField("fired"), nullptr);
  EXPECT_EQ(probe->FindField("fired")->type, FieldType::kEvent);
}

TEST_F(ScriptBehaviourTest, MapValuesOverrideTheDefaults) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap]
format = 2

[[component]]
node = "A"
type = "Probe"
[component.props]
label = "map"
mode = "b"
speed = 9.0

[[node]]
name = "Root"

[[node]]
name = "A"
parent = "."
)");
  auto fields = Fields(Script(scene, "A", "Probe"));
  EXPECT_FLOAT_EQ(fields["speed"].ToFloat().value_or(0.0), 9.0f);
  EXPECT_EQ(fields["label"].ToString().value_or(""), "map");
  EXPECT_EQ(fields["mode"].ToString().value_or(""), "b");
}

TEST_F(ScriptBehaviourTest, SavedEventCallsReachCallableMethods) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap]
format = 2

[[component]]
node = "A"
type = "Probe"
[component.props]
fired = [[{ path = "A" }, "Probe", "Add", 15.0]]

[[node]]
name = "Root"

[[node]]
name = "A"
parent = "."
)");
  Tick(scene, 1);
  EXPECT_EQ(scene.Find("A").GetTag(), "aest+");
}

// A script can copy its Event field and keep the copy. The copy refers to the
// Event by id, and the id must stop resolving once the owning behaviour is
// gone, so using it then reports an unbound Event instead of reading freed
// memory.
TEST_F(ScriptBehaviourTest, EventIdsStopResolvingWhenTheBehaviourIsDestroyed) {
  auto& runtime = ScriptRuntime::Instance();
  uint32_t id = 0;
  {
    MapScene scene(g_project, MapScene::Mode::kPlay);
    Load(scene, R"([mmap]
format = 2

[[component]]
node = "A"
type = "Probe"

[[node]]
name = "Root"

[[node]]
name = "A"
parent = "."
)");
    ScriptBehaviour* script = Script(scene, "A", "Probe");
    ASSERT_NE(script, nullptr);
    const ScriptClass* script_class = runtime.FindScriptClass("Probe");
    ASSERT_NE(script_class, nullptr);
    for (const ScriptField& field : script_class->fields) {
      if (field.type != FieldType::kEvent) continue;
      auto* value = static_cast<ScriptEvent*>(
          script->Instance()->GetAddressOfProperty(field.property_index));
      ASSERT_NE(value, nullptr);
      id = value->id;
    }
    ASSERT_NE(id, 0u);
    EXPECT_NE(runtime.FindEvent(id), nullptr);
  }
  EXPECT_EQ(runtime.FindEvent(id), nullptr);
}

TEST_F(ScriptBehaviourTest, AnExceptionIsLoggedAndTheNextTickStillRuns) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap]
format = 2

[[component]]
node = "A"
type = "Thrower"

[[node]]
name = "Root"

[[node]]
name = "A"
parent = "."
)");
  Tick(scene, 3);
  EXPECT_EQ(scene.Find("A").GetTag(), "ttt");
}

TEST_F(ScriptBehaviourTest, TheScriptClassesAreRegistered) {
  EXPECT_NE(ScriptRuntime::Instance().FindScriptClass("Probe"), nullptr);
  EXPECT_NE(ScriptRuntime::Instance().FindScriptClass("Thrower"), nullptr);
}

TEST_F(ScriptBehaviourTest, ScriptAPIReadsAnotherScriptsFields) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap]
format = 2

[[component]]
node = "A"
type = "Probe"
[component.props]
speed = 8.5
lives = 7
label = "from map"

[[component]]
node = "A"
type = "Reader"

[[node]]
name = "Root"

[[node]]
name = "A"
parent = "."
)");
  ScriptBehaviour* reader = Script(scene, "A", "Reader");
  ASSERT_NE(reader, nullptr);
  const auto fields = Fields(reader);
  EXPECT_EQ(fields.at("has_probe").ToBool().value_or(false), true);
  EXPECT_FLOAT_EQ(fields.at("read_speed").ToFloat().value_or(0.0), 8.5);
  EXPECT_EQ(fields.at("read_lives").ToInt().value_or(0), 7);
  EXPECT_EQ(fields.at("read_label").ToString().value_or(""), "from map");
}

TEST_F(ScriptBehaviourTest, NativeScriptComponentsHaveFieldAccessors) {
  MapScene scene(g_project, MapScene::Mode::kPlay);
  Load(scene, R"([mmap]
format = 2

[[component]]
node = "A"
type = "NativeAccessorProbe"
[component.props]
amount = 4.5

[[node]]
name = "Root"

[[node]]
name = "A"
parent = "."
)");
  const ComponentInfo* info =
      TypeRegistry::Instance().FindComponent("NativeAccessorProbe");
  ASSERT_NE(info, nullptr);
  ASSERT_TRUE(info->has);
  ASSERT_TRUE(info->get_field);
  ASSERT_TRUE(info->set_field);
  const kEntityId entity = scene.Find("A").GetEntity();
  EXPECT_TRUE(info->has(scene, entity));
  mtext::Variant value;
  ASSERT_TRUE(info->get_field(scene, entity, "amount", &value));
  EXPECT_FLOAT_EQ(value.ToFloat().value_or(0.0), 4.5);
  ASSERT_TRUE(info->set_field(scene, entity, "amount", mtext::Variant(7.5f)));
  ASSERT_TRUE(info->get_field(scene, entity, "amount", &value));
  EXPECT_FLOAT_EQ(value.ToFloat().value_or(0.0), 7.5);
}
