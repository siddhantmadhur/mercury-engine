// Checks every example project as a whole: its TypeScript builds, its scripts load, and every
// map only uses components, properties and resources that exist. A map property that does not
// match a script field (say `fall_limit` against `fallLimit`) is otherwise ignored without a word
// and the script runs on its defaults.
//
// This runs in its own binary (tests/BUILD.bazel) because each project needs a fresh script
// runtime, and the other script tests keep one runtime for the whole process.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <set>
#include <iostream>
#include <string>
#include <vector>

#include "debugger/debugger.h"
#include "os/process.h"
#include "project/map.h"
#include "project/project.h"
#include "scripting/js/script_builder.h"
#include "scripting/js/script_runtime.h"
#include "scripting/type_registry.h"
#include "test_paths.h"

using namespace Mercury;
namespace fs = std::filesystem;

namespace {

// Properties any script component accepts in a map, besides its own fields.
const std::set<std::string> kScriptProperties = {"enabled"};

// Properties that built-in components still read from old maps without listing them as fields.
const std::set<std::string> kLegacyProperties = {"Rigidbody.allowed_dofs"};

void CollectResources(const mtext::Variant& value, std::vector<std::string>& out) {
  if (const auto* resource = value.Get<mtext::Resource>()) out.push_back(resource->path);
  if (const auto* array = value.Get<mtext::Array>()) {
    for (const auto& element : *array) CollectResources(element, out);
  }
}

class ExampleProjectTest : public ::testing::TestWithParam<std::string> {
 protected:
  void TearDown() override { ScriptRuntime::Instance().Shutdown(); }
};

TEST_P(ExampleProjectTest, BuildsLoadsAndMapsMatchTheScripts) {
  const std::string name = GetParam();
  const fs::path source = fs::path(MERCURY_SOURCE_DIR) / "examples" / name;
  ASSERT_TRUE(fs::is_directory(source)) << source;

  // Build in a copy so the source tree gets no .mercury folder. Only what esbuild and the map
  // checks need is copied: some examples carry hundreds of megabytes of models.
  const fs::path work = Mercury::test::CacheDir() / "example_projects" / name;
  std::error_code ec;
  fs::remove_all(work, ec);
  fs::create_directories(work, ec);
  for (const std::string& part : std::vector<std::string>{"scripts", "maps", "tsconfig.json", name + ".mproj"}) {
    if (!fs::exists(source / part)) continue;
    fs::copy(source / part, work / part, fs::copy_options::recursive, ec);
    ASSERT_FALSE(ec) << part << ": " << ec.message();
  }

  std::string log;
  ASSERT_TRUE(ScriptBuilder::Build(work, {}, &log)) << log;
  // esbuild does not check types. When tsc is installed the build runs it; without it the test
  // says so, and fails if MERCURY_REQUIRE_TSC is set (CI should set it). This looks for tsc the
  // way ScriptBuilder does rather than reading the build log, which mentions it only once per
  // process.
  const bool has_tsc =
      fs::exists(work / "node_modules" / ".bin" / "tsc", ec) || os::IsOnPath("tsc");
  if (!has_tsc) {
    EXPECT_EQ(std::getenv("MERCURY_REQUIRE_TSC"), nullptr)
        << name << ": MERCURY_REQUIRE_TSC is set but tsc was not found";
    std::cerr << "[  NOTE   ] " << name << ": TypeScript was NOT type checked (tsc not found)\n";
  }

  RegisterBuiltinTypes();
  ASSERT_TRUE(ScriptRuntime::Instance().Initialize());
  ASSERT_TRUE(ScriptRuntime::Instance().LoadBundle(ScriptBuilder::BundlePath(work).string(),
                                                   ScriptBuilder::SourceMapPath(work).string()));

  std::string error;
  auto project = Project::Load(work / (name + ".mproj"), &error);
  ASSERT_TRUE(project) << error;
  EXPECT_FALSE(project->GetMainMap().empty()) << "no main_map";

  const auto& registry = TypeRegistry::Instance();
  int scripts_seen = 0;
  int maps_seen = 0;
  for (const auto& entry : fs::recursive_directory_iterator(work / "maps")) {
    if (entry.path().extension() != ".mmap") continue;
    maps_seen++;
    const std::string map_name = fs::relative(entry.path(), work).generic_string();
    Map map;
    ASSERT_TRUE(map.Load(entry.path(), &error)) << map_name << ": " << error;
    map.Visit([&](const MapNode& node) {
      for (const auto& component : node.components) {
        const std::string where = map_name + ": " + node.GetPath() + "/" + component.type;
        const ComponentInfo* info = registry.FindComponent(component.type);
        if (!info) {
          ADD_FAILURE() << where << ": unknown component";
          continue;
        }
        if (info->is_script) scripts_seen++;
        for (const auto& property : component.properties) {
          if (info->FindField(property.key)) continue;
          if (info->is_script && kScriptProperties.count(property.key)) continue;
          if (kLegacyProperties.count(component.type + "." + property.key)) continue;
          ADD_FAILURE() << where << ": unknown property \"" << property.key << "\"";
        }
        for (const auto& property : component.properties) {
          std::vector<std::string> resources;
          CollectResources(property.value, resources);
          for (const auto& resource : resources) {
            if (!fs::exists(source / resource.substr(std::string("res://").size()))) {
              ADD_FAILURE() << where << "." << property.key << ": missing " << resource;
            }
          }
        }
      }
    });
  }
  EXPECT_GT(maps_seen, 0);
  EXPECT_GT(scripts_seen, 0) << name << " uses no script";
}

INSTANTIATE_TEST_SUITE_P(Examples, ExampleProjectTest,
                         ::testing::Values("pong", "roll_a_ball", "monkey_ball", "model_viewer"));

}  // namespace
