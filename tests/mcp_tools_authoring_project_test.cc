// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the project authoring tool group
// (editor/mcp/tools/authoring_project.cc): manage_input_actions and
// manage_maps_and_settings, on a copy of examples/roll_a_ball.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "input/input_actions.h"
#include "mcp/tool_registry.h"
#include "mcp_tools_fixture.h"
#include "project/map.h"
#include "project/project.h"

using namespace Mercury;
using Mercury::mcp::ToolRegistry;
using Mercury::mcp::ToolResult;
using Mercury::mcp::testing::McpToolFixture;
using nlohmann::json;

namespace fs = std::filesystem;

namespace {

std::string ReadFile(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

fs::path ProjectFile(const fs::path& root) {
  for (const auto& entry : fs::directory_iterator(root)) {
    if (entry.path().extension() == ".mproj") return entry.path();
  }
  return {};
}

json FindAction(const json& actions, const std::string& map,
                const std::string& name) {
  for (const auto& action : actions) {
    if (action["map"] == map && action["name"] == name) return action;
  }
  return json::object();
}

}  // namespace

TEST(McpToolsAuthoringProjectTest, InputOpsRoundTrip) {
  McpToolFixture fixture;
  const ToolResult written =
      fixture.CallTool("manage_input_actions",
                       {{"ops", json::array({
                                    json{{"op", "add_map"}, {"map", "Enemies"}},
                                    json{{"op", "add_action"},
                                         {"map", "Enemies"},
                                         {"name", "Patrol"},
                                         {"type", "value"},
                                         {"vector2", true}},
                                    json{{"op", "add_binding"},
                                         {"map", "Enemies"},
                                         {"name", "Patrol"},
                                         {"path", "<Gamepad>/leftStick"}},
                                    json{{"op", "add_composite"},
                                         {"map", "Enemies"},
                                         {"name", "Patrol"},
                                         {"up", {"<Keyboard>/w"}},
                                         {"down", {"<Keyboard>/s"}},
                                         {"left", {"<Keyboard>/a"}},
                                         {"right", {"<Keyboard>/d"}}},
                                })}});
  ASSERT_TRUE(written.ok) << written.result.dump(2);
  EXPECT_EQ(written.result["path"], "res://input/actions.minput");
  EXPECT_TRUE(written.result["header_written"].get<bool>());

  const ToolResult read = fixture.CallTool("manage_input_actions");
  ASSERT_TRUE(read.ok) << read.result.dump(2);
  EXPECT_NE(std::find(read.result["maps"].begin(), read.result["maps"].end(),
                      "Enemies"),
            read.result["maps"].end());
  const json action = FindAction(read.result["actions"], "Enemies", "Patrol");
  ASSERT_FALSE(action.empty()) << read.result.dump(2);
  EXPECT_EQ(action["type"], "value");
  EXPECT_EQ(action["vector2"], true);
  EXPECT_EQ(action["bindings"], json({"<Gamepad>/leftStick"}));
  ASSERT_EQ(action["composites"].size(), 1u);
  EXPECT_EQ(action["composites"][0]["up"], json({"<Keyboard>/w"}));
  EXPECT_EQ(action["composites"][0]["right"], json({"<Keyboard>/d"}));

  // And the same data is on disk, not just in the response.
  InputActionsDesc desc;
  std::string error;
  ASSERT_TRUE(ParseInputActions(
      ReadFile(fixture.Root() / "input" / "actions.minput"), &desc, &error))
      << error;
  ASSERT_EQ(desc.actions.size(), 3u);  // the two original actions + Patrol
  EXPECT_EQ(desc.actions.back().FullName(), "Enemies/Patrol");
  EXPECT_EQ(desc.actions.back().type, InputAction::Type::kValue);
  EXPECT_TRUE(desc.actions.back().vector2);
}

TEST(McpToolsAuthoringProjectTest, FailedBatchLeavesFileByteIdentical) {
  McpToolFixture fixture;
  const fs::path file = fixture.Root() / "input" / "actions.minput";
  const std::string before = ReadFile(file);
  const ToolResult result = fixture.CallTool(
      "manage_input_actions",
      {{"ops", json::array({json{{"op", "add_map"}, {"map", "McpAtomic"}},
                            json{{"op", "add_action"},
                                 {"map", "Playr"},
                                 {"name", "Jump"},
                                 {"type", "button"}}})}});
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.result["error"].get<std::string>().find("unknown map"),
            std::string::npos)
      << result.result.dump(2);
  EXPECT_EQ(result.result["suggestion"], "Player");
  EXPECT_EQ(result.result["operation"], 1);
  EXPECT_EQ(ReadFile(file), before);
}

TEST(McpToolsAuthoringProjectTest, HeaderRegeneratedWithClassName) {
  McpToolFixture fixture;
  const ToolResult result =
      fixture.CallTool("manage_input_actions",
                       {{"ops", json::array({json{{"op", "add_action"},
                                                  {"map", "Player"},
                                                  {"name", "Mcp Header Test"},
                                                  {"type", "button"}}})}});
  ASSERT_TRUE(result.ok) << result.result.dump(2);
  EXPECT_TRUE(result.result["header_written"].get<bool>());
  EXPECT_TRUE(result.result["header_errors"].empty());

  const std::string header =
      ReadFile(fixture.Root() / "scripts" / "generated" / "game_input.as");
  EXPECT_NE(header.find("namespace GameInput"), std::string::npos);
  EXPECT_NE(header.find("namespace Player"), std::string::npos);
  // GameInput::<Map>::<Action>: the generated constant is the symbol scripts
  // use with Input::RequireAction(GameInput::Player::McpHeaderTest).
  EXPECT_NE(header.find("const string McpHeaderTest = "
                        "\"Player/Mcp Header Test\";"),
            std::string::npos)
      << header;
}

TEST(McpToolsAuthoringProjectTest, HeaderErrorsAreSurfacedAndHeaderUntouched) {
  McpToolFixture fixture;
  const fs::path header =
      fixture.Root() / "scripts" / "generated" / "game_input.as";
  const std::string before = ReadFile(header);
  const ToolResult result =
      fixture.CallTool("manage_input_actions",
                       {{"ops", json::array({json{{"op", "add_action"},
                                                  {"map", "Player"},
                                                  {"name", "Fire 1"},
                                                  {"type", "button"}},
                                             json{{"op", "add_action"},
                                                  {"map", "Player"},
                                                  {"name", "fire-1"},
                                                  {"type", "button"}}})}});
  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.result["header_errors"].empty()) << result.result.dump(2);
  EXPECT_NE(result.result["header_errors"][0].get<std::string>().find("Fire1"),
            std::string::npos);
  EXPECT_FALSE(result.result["header_written"].get<bool>());
  EXPECT_EQ(ReadFile(header), before);
}

TEST(McpToolsAuthoringProjectTest, CreateFileFromTemplateWhenNoActionsPath) {
  McpToolFixture fixture;
  auto project = fixture.Scene().GetProject();
  ASSERT_NE(project, nullptr);
  auto* input = project->GetDocument().Find("input");
  ASSERT_NE(input, nullptr);
  input->properties.Remove("actions");
  ASSERT_TRUE(project->Save());

  const ToolResult result =
      fixture.CallTool("manage_input_actions",
                       {{"ops", json::array({json{{"op", "create_file"}}})}});
  ASSERT_TRUE(result.ok) << result.result.dump(2);
  // The example already has input/actions.minput on disk (even though the
  // project no longer points at it), so create_file picks a free name.
  EXPECT_EQ(result.result["path"], "res://input/actions2.minput");
  EXPECT_TRUE(fs::exists(fixture.Root() / "input" / "actions2.minput"));
  EXPECT_EQ(project->GetInputActionsPath(), "res://input/actions2.minput");
  const ToolResult read = fixture.CallTool("manage_input_actions");
  ASSERT_TRUE(read.ok) << read.result.dump(2);
  EXPECT_NE(std::find(read.result["maps"].begin(), read.result["maps"].end(),
                      "Player"),
            read.result["maps"].end());
}

TEST(McpToolsAuthoringProjectTest, CreateMap2dAnd3dParseAndRefuseExisting) {
  McpToolFixture fixture;
  for (const char* kind : {"2d", "3d"}) {
    const std::string path = std::string("res://maps/mcp_") + kind + ".mmap";
    const ToolResult result = fixture.CallTool(
        "manage_maps_and_settings",
        {{"action", "create_map"}, {"path", path}, {"template", kind}});
    ASSERT_TRUE(result.ok) << kind << ": " << result.result.dump(2);
    EXPECT_EQ(result.result["template"], kind);
    EXPECT_FALSE(result.result["opened"].get<bool>());

    const fs::path file =
        fixture.Root() / "maps" / (std::string("mcp_") + kind + ".mmap");
    ASSERT_TRUE(fs::exists(file)) << file;
    Map map;
    std::string error;
    ASSERT_TRUE(map.Load(file, &error)) << error;
    ASSERT_NE(map.GetRoot(), nullptr);
    EXPECT_EQ(map.GetRoot()->name, std::string("mcp_") + kind);
    if (std::string(kind) == "3d") {
      EXPECT_NE(map.FindNode("Sun"), nullptr);
      EXPECT_NE(map.FindNode("Ground"), nullptr);
    } else {
      EXPECT_NE(map.FindNode("Camera"), nullptr);
    }

    const ToolResult again = fixture.CallTool(
        "manage_maps_and_settings",
        {{"action", "create_map"}, {"path", path}, {"template", kind}});
    EXPECT_FALSE(again.ok);
    EXPECT_NE(again.result["error"].get<std::string>().find("already exists"),
              std::string::npos);
  }
}

TEST(McpToolsAuthoringProjectTest, OpenMapRefusesDirtyMapWithoutDiscard) {
  McpToolFixture fixture;
  fixture.Scene().MarkChanged();
  ASSERT_TRUE(fixture.Scene().IsDirty());
  const ToolResult refused = fixture.CallTool(
      "manage_maps_and_settings",
      {{"action", "open_map"}, {"path", "res://maps/main.mmap"}});
  EXPECT_FALSE(refused.ok);
  EXPECT_NE(refused.result["error"].get<std::string>().find(
                "unsaved changes; call save_map first"),
            std::string::npos)
      << refused.result.dump(2);
  EXPECT_EQ(refused.result["suggestion"],
            "call save_map first or pass \"discard\": true");
  EXPECT_TRUE(fixture.Scene().IsDirty());

  const ToolResult discarded = fixture.CallTool(
      "manage_maps_and_settings", {{"action", "open_map"},
                                   {"path", "res://maps/main.mmap"},
                                   {"discard", true}});
  ASSERT_TRUE(discarded.ok) << discarded.result.dump(2);
  EXPECT_TRUE(discarded.result["discarded"].get<bool>());
  EXPECT_FALSE(fixture.Scene().IsDirty());
}

TEST(McpToolsAuthoringProjectTest, SetMainMapPersists) {
  McpToolFixture fixture;
  const std::string path = "res://maps/mcp_main.mmap";
  ASSERT_TRUE(
      fixture
          .CallTool(
              "manage_maps_and_settings",
              {{"action", "create_map"}, {"path", path}, {"template", "2d"}})
          .ok);
  const ToolResult result = fixture.CallTool(
      "manage_maps_and_settings", {{"action", "set_main_map"}, {"path", path}});
  ASSERT_TRUE(result.ok) << result.result.dump(2);
  EXPECT_EQ(result.result["path"], path);
  EXPECT_EQ(fixture.Scene().GetProject()->GetMainMap(), path);

  auto reloaded = Project::Load(ProjectFile(fixture.Root()));
  ASSERT_NE(reloaded, nullptr);
  EXPECT_EQ(reloaded->GetMainMap(), path);
}

TEST(McpToolsAuthoringProjectTest, UpdateSettingsPersistsWithValidation) {
  McpToolFixture fixture;
  const ToolResult result =
      fixture.CallTool("manage_maps_and_settings",
                       {{"action", "update_settings"},
                        {"settings",
                         {{"name", "MCP Renamed"},
                          {"render",
                           {{"anti_aliasing", "fxaa"},
                            {"fsr", true},
                            {"fsr_quality", "high"},
                            {"scale", 0.8},
                            {"fsr_sharpness", 0.4}}},
                          {"input_actions", "res://input/actions.minput"}}}});
  ASSERT_TRUE(result.ok) << result.result.dump(2);
  EXPECT_EQ(result.result["settings_written"].size(), 7u);

  auto reloaded = Project::Load(ProjectFile(fixture.Root()));
  ASSERT_NE(reloaded, nullptr);
  EXPECT_EQ(reloaded->GetName(), "MCP Renamed");
  EXPECT_EQ(reloaded->GetRenderAntiAliasing(), "fxaa");
  EXPECT_TRUE(reloaded->GetRenderFsr());
  EXPECT_EQ(reloaded->GetRenderFsrQuality(), "high");
  EXPECT_FLOAT_EQ(reloaded->GetRenderScale(), 0.8f);
  EXPECT_FLOAT_EQ(reloaded->GetRenderFsrSharpness(), 0.4f);
  EXPECT_EQ(reloaded->GetInputActionsPath(), "res://input/actions.minput");

  const ToolResult unknown =
      fixture.CallTool("manage_maps_and_settings",
                       {{"action", "update_settings"},
                        {"settings", {{"render.antialias", "fxaa"}}}});
  EXPECT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.result["suggestion"], "render.anti_aliasing");
  EXPECT_NE(unknown.result["error"].get<std::string>().find("unknown setting"),
            std::string::npos);

  const ToolResult unsupported = fixture.CallTool(
      "manage_maps_and_settings",
      {{"action", "update_settings"},
       {"settings", {{"physics.gravity", {{"vec3", {0.0, -9.81, 0.0}}}}}}});
  EXPECT_FALSE(unsupported.ok);
  EXPECT_NE(
      unsupported.result["error"].get<std::string>().find("not supported yet"),
      std::string::npos)
      << unsupported.result.dump(2);
}

TEST(McpToolsAuthoringProjectTest, ToolsAreWriteGatedAndRegistered) {
  for (const char* name :
       {"manage_input_actions", "manage_maps_and_settings"}) {
    const mcp::ToolEntry* entry = ToolRegistry::Instance().Find(name);
    ASSERT_NE(entry, nullptr) << name;
    EXPECT_EQ(entry->mode, mcp::ToolMode::kWrite) << name;
    EXPECT_FALSE(entry->IsAsync()) << name;
  }
  McpToolFixture fixture;
  const ToolResult denied =
      fixture.CallTool("manage_input_actions", json::object(), false);
  EXPECT_FALSE(denied.ok);
}
