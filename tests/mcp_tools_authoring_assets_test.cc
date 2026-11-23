// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the authoring tool group (editor/mcp/tools/authoring_assets.cc):
// scaffold_script and create_or_update_asset, on a copy of
// examples/roll_a_ball.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "mcp/tool_registry.h"
#include "mcp/tools/assets_build.h"
#include "mcp_tools_fixture.h"
#include "play_session.h"

using namespace Mercury;
using Mercury::mcp::ToolRegistry;
using Mercury::mcp::ToolResult;
using Mercury::mcp::testing::McpToolFixture;
using nlohmann::json;

namespace fs = std::filesystem;

TEST(McpToolsAuthoringAssetsTest, ScaffoldCreatesFilesAndRefusesOverwrite) {
  McpToolFixture fixture;
  const json args = {{"name", "McpFreshBehaviour"}, {"kind", "angel_script"}};
  const ToolResult result = fixture.CallTool("scaffold_script", args);
  ASSERT_TRUE(result.ok) << result.result.dump(2);
  EXPECT_EQ(result.result["class_name"], "McpFreshBehaviour");
  EXPECT_EQ(result.result["kind"], "angel_script");
  ASSERT_EQ(result.result["created"].size(), 1u);
  EXPECT_EQ(result.result["created"][0],
            "res://scripts/mcp_fresh_behaviour.as");
  EXPECT_TRUE(
      fs::exists(fixture.Root() / "scripts" / "mcp_fresh_behaviour.as"));

  // A second call must not clobber what the first one wrote.
  const ToolResult again = fixture.CallTool("scaffold_script", args);
  EXPECT_FALSE(again.ok);
  EXPECT_NE(again.result["error"].get<std::string>().find("already exists"),
            std::string::npos)
      << again.result.dump();

  const mcp::ToolEntry* entry =
      ToolRegistry::Instance().Find("scaffold_script");
  ASSERT_NE(entry, nullptr);
  EXPECT_TRUE(entry->IsAsync());  // build=true needs the build's completion
  EXPECT_EQ(entry->mode, mcp::ToolMode::kWrite);
}

TEST(McpToolsAuthoringAssetsTest, ScaffoldCppKindCreatesHeaderAndSource) {
  McpToolFixture fixture;
  const ToolResult result = fixture.CallTool(
      "scaffold_script", {{"name", "McpFreshSystem"}, {"kind", "cpp_system"}});
  ASSERT_TRUE(result.ok) << result.result.dump(2);
  EXPECT_EQ(result.result["created"], json({"res://src/mcp_fresh_system.h",
                                            "res://src/mcp_fresh_system.cc"}));
  EXPECT_TRUE(fs::exists(fixture.Root() / "src" / "mcp_fresh_system.h"));
  EXPECT_TRUE(fs::exists(fixture.Root() / "src" / "mcp_fresh_system.cc"));
}

TEST(McpToolsAuthoringAssetsTest, ScaffoldInvalidNameReturnsValidatorReason) {
  McpToolFixture fixture;
  const ToolResult result = fixture.CallTool(
      "scaffold_script", {{"name", "class"}, {"kind", "angel_script"}});
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.result["error"].get<std::string>().find("C++ keyword"),
            std::string::npos)
      << result.result.dump();
  EXPECT_FALSE(fs::exists(fixture.Root() / "scripts" / "class.as"));
}

TEST(McpToolsAuthoringAssetsTest, ScaffoldRejectsUnknownKind) {
  McpToolFixture fixture;
  const ToolResult result = fixture.CallTool(
      "scaffold_script", {{"name", "McpFreshBehaviour"}, {"kind", "other"}});
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.result["error"].get<std::string>().find("cpp_behaviour"),
            std::string::npos)
      << result.result.dump();
}

TEST(McpToolsAuthoringAssetsTest, AssetCreateAndUpdateRoundTrip) {
  McpToolFixture fixture;
  const json path = "res://assets/mcp_tool_material.masset";
  const ToolResult created = fixture.CallTool(
      "create_or_update_asset", {{"path", path},
                                 {"type", "Material"},
                                 {"fields", {{"roughness", 0.25}}},
                                 {"create_only", true}});
  ASSERT_TRUE(created.ok) << created.result.dump(2);
  EXPECT_EQ(created.result["path"], path);
  EXPECT_EQ(created.result["type"], "Material");
  EXPECT_TRUE(created.result["created"].get<bool>());
  EXPECT_EQ(created.result["fields_written"], json({"roughness"}));
  ASSERT_TRUE(
      fs::exists(fixture.Root() / "assets" / "mcp_tool_material.masset"));

  const ToolResult first = fixture.CallTool(
      "inspect_asset", {{"res_path", "res://assets/mcp_tool_material.masset"}});
  ASSERT_TRUE(first.ok) << first.result.dump(2);
  ASSERT_DOUBLE_EQ(first.result["properties"]["roughness"].get<double>(), 0.25);

  const ToolResult updated = fixture.CallTool(
      "create_or_update_asset",
      {{"path", path}, {"fields", {{"roughness", 0.75}, {"unlit", true}}}});
  ASSERT_TRUE(updated.ok) << updated.result.dump(2);
  EXPECT_FALSE(updated.result["created"].get<bool>());
  EXPECT_EQ(updated.result["type"], "Material");
  EXPECT_EQ(updated.result["fields_written"], json({"roughness", "unlit"}));

  const ToolResult second = fixture.CallTool(
      "inspect_asset", {{"res_path", "res://assets/mcp_tool_material.masset"}});
  ASSERT_TRUE(second.ok) << second.result.dump(2);
  EXPECT_DOUBLE_EQ(second.result["properties"]["roughness"].get<double>(),
                   0.75);
  EXPECT_TRUE(second.result["properties"]["unlit"].get<bool>());

  // create_only must refuse the file the previous call just updated.
  const ToolResult again =
      fixture.CallTool("create_or_update_asset", {{"path", path},
                                                  {"type", "Material"},
                                                  {"fields", json::object()},
                                                  {"create_only", true}});
  EXPECT_FALSE(again.ok);
  EXPECT_NE(again.result["error"].get<std::string>().find("already exists"),
            std::string::npos);
}

TEST(McpToolsAuthoringAssetsTest, BadFieldNameOffersSuggestion) {
  McpToolFixture fixture;
  const ToolResult result =
      fixture.CallTool("create_or_update_asset",
                       {{"path", "res://assets/mcp_tool_material.masset"},
                        {"type", "Material"},
                        {"fields", {{"roughnes", 0.5}}}});
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.result["suggestion"], "roughness");
  EXPECT_NE(result.result["error"].get<std::string>().find("roughness"),
            std::string::npos)
      << result.result.dump();
  EXPECT_FALSE(
      fs::exists(fixture.Root() / "assets" / "mcp_tool_material.masset"));
}

TEST(McpToolsAuthoringAssetsTest, RejectsBadAssetPaths) {
  McpToolFixture fixture;
  for (const char* path : {"res://../outside.masset", "assets/x.masset",
                           "/tmp/x.masset", "res://assets/x.txt"}) {
    const ToolResult result = fixture.CallTool(
        "create_or_update_asset",
        {{"path", path}, {"type", "Material"}, {"fields", json::object()}});
    EXPECT_FALSE(result.ok) << path << ": " << result.result.dump();
  }
  const ToolResult no_type = fixture.CallTool(
      "create_or_update_asset", {{"path", "res://assets/mcp_no_type.masset"},
                                 {"fields", json::object()}});
  EXPECT_FALSE(no_type.ok);
  EXPECT_NE(no_type.result["error"].get<std::string>().find("type is required"),
            std::string::npos)
      << no_type.result.dump();
}

// The build=true path shares build_scripts' job. Skip when this run has no
// mercury_player next to the test (the `check` preset builds it).
TEST(McpToolsAuthoringAssetsTest, ScaffoldCanBuildTheNewScript) {
  if (PlaySession::PlayerPath().empty()) {
    GTEST_SKIP() << "no mercury_player available for a script build";
  }
  McpToolFixture fixture;
  fixture.PumpTimeout(std::chrono::seconds(120));
  fixture.SetPump([] { mcp::TickScriptBuild(); });
  const ToolResult result =
      fixture.CallTool("scaffold_script", {{"name", "McpBuildableBehaviour"},
                                           {"kind", "angel_script"},
                                           {"build", true}});
  ASSERT_TRUE(result.ok) << result.result.dump(2);
  ASSERT_EQ(result.result["created"].size(), 1u);
  ASSERT_TRUE(result.result.contains("build")) << result.result.dump(2);
  EXPECT_TRUE(result.result["build"]["ok"].get<bool>())
      << result.result.dump(2);
  EXPECT_TRUE(result.result["build"]["diagnostics"].is_array());
}
