// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the assets, validation and script build tool group
// (editor/mcp/tools/assets_build.cc): list_project_assets, inspect_asset,
// validate_project and build_scripts, on a copy of examples/roll_a_ball.
//
// build_scripts runs the real mercury_player (--build-scripts, then
// --dump-schema) as a child process, exactly as the editor's Build Scripts
// button does. The player is a dependency of the `check` target; when a run
// has no player (an ad-hoc `--target mcp_tools_test` build) the build tests
// skip and say so.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mcp/tool_registry.h"
#include "mcp/tools/assets_build.h"
#include "mcp_tools_fixture.h"
#include "play_session.h"
#include "scripting/type_registry.h"

using namespace Mercury;
using Mercury::mcp::BuildDiagnostic;
using Mercury::mcp::GlobMatch;
using Mercury::mcp::ParseBuildDiagnostics;
using Mercury::mcp::ToolResult;
using Mercury::mcp::testing::McpToolFixture;
using nlohmann::json;

namespace fs = std::filesystem;

namespace {

std::vector<std::string> ResPaths(const json& assets) {
  std::vector<std::string> paths;
  for (const json& asset : assets) {
    paths.push_back(asset["res_path"].get<std::string>());
  }
  return paths;
}

std::string ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

void WriteFile(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

}  // namespace

// ------------------------------------------------------------------- glob --

TEST(McpToolsAssetsBuildTest, GlobStarsStopAtFolders) {
  EXPECT_TRUE(GlobMatch("res://assets/*.masset", "res://assets/player.masset"));
  EXPECT_FALSE(GlobMatch("res://assets/*.masset", "res://assets/sub/a.masset"));
  EXPECT_TRUE(GlobMatch("res://**/*.masset", "res://assets/sub/a.masset"));
  EXPECT_TRUE(GlobMatch("res://**/*.masset", "res://a.masset"))
      << "** followed by / may match no folder";
  EXPECT_TRUE(GlobMatch("res://scripts/**", "res://scripts/generated/x.as"));
  EXPECT_TRUE(
      GlobMatch("res://assets/p?ayer.masset", "res://assets/player.masset"));
  EXPECT_FALSE(GlobMatch("res://a?b", "res://a/b"));
  EXPECT_FALSE(GlobMatch("res://assets/*.png", "res://assets/player.masset"));
  EXPECT_TRUE(GlobMatch("", ""));
  EXPECT_FALSE(GlobMatch("", "x"));
}

// -------------------------------------------------------- diagnostic parsing

TEST(McpToolsAssetsBuildTest, ParsesAngelScriptMessages) {
  const std::vector<std::string> lines = {
      "> /x/mercury_player /p/game.mproj --build-scripts",
      "[INFO] 2026-11-21 10:15:42 Compiling 5 AngelScript file(s)",
      "[INFO] 2026-11-21 10:15:42 scripts/player.as:11:1: Compiling void "
      "Player::onTick(double)",
      "[ERROR] 2026-11-21 10:15:42 scripts/player.as:12:5: Expected ';'",
      "[WARN] 2026-11-21 10:15:42 scripts/other.as:3:9: Implicit conversion "
      "changed sign of value",
      "[ERROR] 2026-11-21 10:15:42 Script compilation failed",
  };
  const auto found = ParseBuildDiagnostics(lines, "/p");
  ASSERT_EQ(found.size(), 3u);
  EXPECT_EQ(found[0].severity, "note");
  EXPECT_EQ(found[0].line, 11);
  EXPECT_EQ(found[1].file, "scripts/player.as");
  EXPECT_EQ(found[1].line, 12);
  EXPECT_EQ(found[1].col, 5);
  EXPECT_EQ(found[1].severity, "error");
  EXPECT_EQ(found[1].message, "Expected ';'");
  EXPECT_EQ(found[2].severity, "warning");
  EXPECT_EQ(found[2].file, "scripts/other.as");
}

TEST(McpToolsAssetsBuildTest, ParsesCompilerAndCMakeMessages) {
  const std::vector<std::string> lines = {
      "/p/src/a.cc:12:5: error: expected ';' after expression",
      "/p/src/a.cc:13: warning: unused variable 'x'",
      "In file included from /p/src/b.cc:1:",
      "/p/src/b.h:7:3: note: declared here",
      "src\\c.cc(20,4): error C2143: syntax error: missing ';' before '}'",
      "src\\c.cc(21): warning C4244: conversion from 'double' to 'float'",
      "clang: error: linker command failed with exit code 1",
      "CMake Error at CMakeLists.txt:12 (add_executable):",
      "  Cannot find source file:",
      "",
      "    missing.cc",
      "CMake Warning (dev) at CMakeLists.txt:3 (project):",
      "  Policy CMP0000 is not set.",
      "",
      "CMake Error: Error in cmake code at",
      "FAILED: CMakeFiles/game.dir/a.cc.o",
  };
  const auto found = ParseBuildDiagnostics(lines, "/p");
  ASSERT_EQ(found.size(), 9u);
  EXPECT_EQ(found[0].file, "src/a.cc");
  EXPECT_EQ(found[0].line, 12);
  EXPECT_EQ(found[0].col, 5);
  EXPECT_EQ(found[0].severity, "error");
  EXPECT_EQ(found[1].line, 13);
  EXPECT_EQ(found[1].col, 0);
  EXPECT_EQ(found[1].severity, "warning");
  EXPECT_EQ(found[2].severity, "note");
  EXPECT_EQ(found[2].file, "src/b.h");
  EXPECT_EQ(found[3].file, "src\\c.cc");
  EXPECT_EQ(found[3].line, 20);
  EXPECT_EQ(found[3].col, 4);
  EXPECT_EQ(found[3].message, "syntax error: missing ';' before '}'");
  EXPECT_EQ(found[4].severity, "warning");
  EXPECT_EQ(found[5].file, "");
  EXPECT_EQ(found[5].message, "linker command failed with exit code 1");
  EXPECT_EQ(found[6].file, "CMakeLists.txt");
  EXPECT_EQ(found[6].line, 12);
  EXPECT_EQ(found[6].message, "Cannot find source file:");
  EXPECT_EQ(found[7].severity, "warning");
  EXPECT_EQ(found[7].line, 3);
  EXPECT_EQ(found[8].file, "");
  EXPECT_EQ(found[8].severity, "error");
  EXPECT_EQ(found[8].message, "Error in cmake code at");
}

// ------------------------------------------------------ list_project_assets

TEST(McpToolsAssetsBuildTest, ListsAssetsSortedWithTypeAndSize) {
  McpToolFixture fixture;
  const ToolResult result = fixture.CallTool("list_project_assets");
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_FALSE(result.result["truncated"].get<bool>());
  const json& assets = result.result["assets"];
  const auto paths = ResPaths(assets);
  EXPECT_TRUE(std::is_sorted(paths.begin(), paths.end()));
  EXPECT_NE(std::find(paths.begin(), paths.end(), "res://maps/main.mmap"),
            paths.end());
  for (const json& asset : assets) {
    EXPECT_FALSE(asset["type"].get<std::string>().empty());
    EXPECT_GT(asset["bytes"].get<uint64_t>(), 0u);
    EXPECT_FALSE(asset.contains("details"));
  }
  // A map, a data asset (type from its header), a script and a UI file.
  const auto type_of = [&](const std::string& path) {
    for (const json& asset : assets) {
      if (asset["res_path"] == path) return asset["type"].get<std::string>();
    }
    return std::string("<missing>");
  };
  EXPECT_EQ(type_of("res://maps/main.mmap"), "Map");
  EXPECT_EQ(type_of("res://assets/grippy.masset"), "PhysicsMaterial");
  EXPECT_EQ(type_of("res://assets/player.masset"), "Material");
  EXPECT_EQ(type_of("res://scripts/rotator.as"), "Script");
  EXPECT_EQ(type_of("res://ui/score.uxml"), "UXML");
  // Not assets: the project file and the test recording.
  EXPECT_EQ(type_of("res://roll_a_ball.mproj"), "<missing>");
  EXPECT_EQ(type_of("res://tests/collect.input"), "<missing>");
}

TEST(McpToolsAssetsBuildTest, FiltersByTypeAndGlob) {
  McpToolFixture fixture;
  // Type filter, case-insensitive.
  ToolResult result =
      fixture.CallTool("list_project_assets", {{"type", "material"}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(ResPaths(result.result["assets"]),
            (std::vector<std::string>{
                "res://assets/ground.masset", "res://assets/pickup.masset",
                "res://assets/player.masset", "res://assets/wall.masset"}));

  // Glob, with and without the res:// prefix.
  result =
      fixture.CallTool("list_project_assets", {{"path_glob", "scripts/*.as"}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(
      ResPaths(result.result["assets"]),
      (std::vector<std::string>{
          "res://scripts/camera_follow.as", "res://scripts/game_manager.as",
          "res://scripts/pickup.as", "res://scripts/player_controller.as",
          "res://scripts/rotator.as"}));
  result = fixture.CallTool("list_project_assets",
                            {{"path_glob", "res://scripts/**"}});
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.result["assets"].size(), 6u) << "** reaches generated/";

  // Both together.
  result = fixture.CallTool("list_project_assets",
                            {{"type", "Script"}, {"path_glob", "**/p*.as"}});
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(ResPaths(result.result["assets"]),
            (std::vector<std::string>{"res://scripts/pickup.as",
                                      "res://scripts/player_controller.as"}));

  // Nothing matches: an empty list, not an error.
  result = fixture.CallTool("list_project_assets", {{"type", "Model"}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_TRUE(result.result["assets"].empty());
  EXPECT_FALSE(result.result["truncated"].get<bool>());
}

TEST(McpToolsAssetsBuildTest, UnknownTypeGetsASuggestion) {
  McpToolFixture fixture;
  const ToolResult result =
      fixture.CallTool("list_project_assets", {{"type", "Modle"}});
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.result["suggestion"], "Model");
  EXPECT_NE(result.result["error"].get<std::string>().find("did you mean"),
            std::string::npos);
  EXPECT_TRUE(result.result["known_types"].is_array());
}

TEST(McpToolsAssetsBuildTest, LimitTruncates) {
  McpToolFixture fixture;
  const ToolResult all = fixture.CallTool("list_project_assets");
  ASSERT_TRUE(all.ok);
  const size_t total = all.result["assets"].size();
  ASSERT_GT(total, 3u);

  ToolResult limited = fixture.CallTool("list_project_assets", {{"limit", 3}});
  ASSERT_TRUE(limited.ok) << limited.result.dump();
  EXPECT_EQ(limited.result["assets"].size(), 3u);
  EXPECT_TRUE(limited.result["truncated"].get<bool>());
  // The first three of the full, sorted list.
  const std::vector<std::string> everything = ResPaths(all.result["assets"]);
  EXPECT_EQ(
      ResPaths(limited.result["assets"]),
      std::vector<std::string>(everything.begin(), everything.begin() + 3));

  // Exactly as many as match: not truncated.
  limited = fixture.CallTool("list_project_assets",
                             {{"limit", static_cast<int>(total)}});
  ASSERT_TRUE(limited.ok);
  EXPECT_FALSE(limited.result["truncated"].get<bool>());

  // Above the maximum is clamped, below one is an error.
  EXPECT_TRUE(fixture.CallTool("list_project_assets", {{"limit", 5000}}).ok);
  EXPECT_FALSE(fixture.CallTool("list_project_assets", {{"limit", 0}}).ok);
  EXPECT_FALSE(fixture.CallTool("list_project_assets", {{"limit", "ten"}}).ok);
}

TEST(McpToolsAssetsBuildTest, IncludeDetailsAddsTheInspectJson) {
  McpToolFixture fixture;
  const ToolResult result = fixture.CallTool(
      "list_project_assets",
      {{"path_glob", "assets/player.masset"}, {"include_details", true}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  ASSERT_EQ(result.result["assets"].size(), 1u);
  const json& details = result.result["assets"][0]["details"];
  EXPECT_EQ(details["type"], "Material");
  EXPECT_EQ(details["path"], "res://assets/player.masset");
  // The same JSON inspect_asset returns.
  const ToolResult inspected = fixture.CallTool(
      "inspect_asset", {{"res_path", "res://assets/player.masset"}});
  ASSERT_TRUE(inspected.ok);
  EXPECT_EQ(details, inspected.result);
}

// ----------------------------------------------------------- inspect_asset

TEST(McpToolsAssetsBuildTest, InspectsADataAsset) {
  McpToolFixture fixture;
  const ToolResult result = fixture.CallTool(
      "inspect_asset", {{"res_path", "res://assets/grippy.masset"}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(result.result["type"], "PhysicsMaterial");
  EXPECT_TRUE(result.result.contains("properties"));

  const ToolResult map =
      fixture.CallTool("inspect_asset", {{"res_path", "res://maps/main.mmap"}});
  ASSERT_TRUE(map.ok) << map.result.dump();
  EXPECT_EQ(map.result["type"], "Map");
  EXPECT_GT(map.result["bytes"].get<uint64_t>(), 0u);
}

TEST(McpToolsAssetsBuildTest, ABadAssetPathGetsASuggestion) {
  McpToolFixture fixture;
  ToolResult result = fixture.CallTool(
      "inspect_asset", {{"res_path", "res://assets/playr.masset"}});
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.result["suggestion"], "res://assets/player.masset");
  EXPECT_NE(result.result["error"].get<std::string>().find("asset not found"),
            std::string::npos);
  EXPECT_NE(result.result["error"].get<std::string>().find("did you mean"),
            std::string::npos);

  // Without the scheme: the full path is offered.
  result =
      fixture.CallTool("inspect_asset", {{"res_path", "assets/player.masset"}});
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.result["suggestion"], "res://assets/player.masset");

  // Nothing close: an error without a suggestion.
  result = fixture.CallTool("inspect_asset",
                            {{"res_path", "res://zzzzzzzzzzzzzzzzzzzz.bin"}});
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.result.contains("suggestion")) << result.result.dump();

  // A path that leaves the project, and a missing argument.
  result =
      fixture.CallTool("inspect_asset", {{"res_path", "res://../outside.txt"}});
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(fixture.CallTool("inspect_asset").ok);
}

// --------------------------------------------------------- validate_project

TEST(McpToolsAssetsBuildTest, ValidatesACleanProject) {
  McpToolFixture fixture;
  const ToolResult result = fixture.CallTool("validate_project");
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_FALSE(result.result["has_errors"].get<bool>())
      << result.result.dump(2);
  for (const json& diagnostic : result.result["diagnostics"]) {
    // Before the first script build the project's own script components are
    // unknown to the registry, which is a warning, never an error.
    EXPECT_EQ(diagnostic["severity"], "warning") << diagnostic.dump();
  }
}

TEST(McpToolsAssetsBuildTest, ValidateReportsABrokenFile) {
  McpToolFixture fixture;
  std::string map = ReadFile(fixture.Root() / "maps" / "main.mmap");
  map += "\n[this is not toml\n";
  WriteFile(fixture.Root() / "maps" / "main.mmap", map);
  const ToolResult result = fixture.CallTool("validate_project");
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_TRUE(result.result["has_errors"].get<bool>());
}

// ------------------------------------------------------------ build_scripts

namespace {

// Pumps the tool the way the editor's wxTimer does, and skips the test when
// this run has no mercury_player to build with.
#define SKIP_WITHOUT_PLAYER()                                                \
  do {                                                                       \
    if (PlaySession::PlayerPath().empty()) {                                 \
      GTEST_SKIP() << "mercury_player was not found next to the test or in " \
                      "the build's bin folder (build the `check` target, "   \
                      "which depends on it)";                                \
    }                                                                        \
  } while (0)

void UsePump(McpToolFixture& fixture) {
  fixture.PumpTimeout(std::chrono::seconds(120));
  fixture.SetPump([] { mcp::TickScriptBuild(); });
}

bool HasDiagnostic(const json& diagnostics, const std::string& file, int line,
                   const std::string& severity) {
  for (const json& d : diagnostics) {
    if (d["file"] == file && d["line"] == line && d["severity"] == severity) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST(McpToolsAssetsBuildTest, BuildScriptsIsRejectedWhenEditsAreNotAllowed) {
  McpToolFixture fixture;
  const ToolResult result =
      fixture.CallTool("build_scripts", {}, /*allow_edits=*/false);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(
      result.result["error"].get<std::string>().find("edits are not allowed"),
      std::string::npos)
      << result.result.dump();
  const mcp::ToolEntry* entry =
      mcp::ToolRegistry::Instance().Find("build_scripts");
  ASSERT_NE(entry, nullptr);
  EXPECT_TRUE(entry->IsAsync());
  EXPECT_EQ(entry->mode, mcp::ToolMode::kExec);
}

TEST(McpToolsAssetsBuildTest, BuildScriptsBuildsRollABall) {
  SKIP_WITHOUT_PLAYER();
  McpToolFixture fixture;
  UsePump(fixture);
  const auto started = std::chrono::steady_clock::now();
  const ToolResult result = fixture.CallTool("build_scripts");
  const auto seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  ASSERT_TRUE(result.ok) << result.result.dump(2);
  std::cout << "[ build_scripts on roll_a_ball took " << seconds << " s ]\n";
  EXPECT_TRUE(result.result["ok"].get<bool>());
  EXPECT_TRUE(result.result["diagnostics"].is_array());
  for (const json& d : result.result["diagnostics"]) {
    EXPECT_NE(d["severity"], "error") << d.dump();
  }
  const json& log = result.result["log_tail"];
  ASSERT_TRUE(log.is_array());
  EXPECT_LE(log.size(), 200u);
  const std::string text = log.dump();
  EXPECT_NE(text.find("--build-scripts"), std::string::npos) << text;
  EXPECT_NE(text.find("Scripts compiled"), std::string::npos) << text;

  // The editor's script list was refreshed the way Play does it: the schema
  // file exists and the registry now knows the project's scripts.
  EXPECT_TRUE(fs::exists(fixture.Root() / ".mercury" / "scripts.mschema"));
  EXPECT_NE(TypeRegistry::Instance().FindComponent("Rotator"), nullptr);

  // validate_project against the live registry is clean, with no
  // "unknown component" warnings left.
  const ToolResult validated = fixture.CallTool("validate_project");
  ASSERT_TRUE(validated.ok);
  EXPECT_FALSE(validated.result["has_errors"].get<bool>())
      << validated.result.dump(2);

  // Nothing is left pending: a second build works.
  const ToolResult again = fixture.CallTool("build_scripts");
  EXPECT_TRUE(again.ok) << again.result.dump(2);
}

TEST(McpToolsAssetsBuildTest, BuildScriptsReportsASyntaxErrorWithFileAndLine) {
  SKIP_WITHOUT_PLAYER();
  McpToolFixture fixture;
  UsePump(fixture);
  // Break a script in the fixture's private copy of the project.
  const fs::path script = fixture.Root() / "scripts" / "rotator.as";
  const std::string original = ReadFile(script);
  const std::string source = original + "\nint broken = ;\n";
  // The 1-based line of the injected statement.
  const int broken_line =
      static_cast<int>(std::count(
          source.begin(), source.begin() + source.find("int broken"), '\n')) +
      1;
  WriteFile(script, source);

  const ToolResult result = fixture.CallTool("build_scripts");
  EXPECT_FALSE(result.ok) << "a failed build is ok=false";
  EXPECT_FALSE(result.result["ok"].get<bool>());
  EXPECT_FALSE(result.result["message"].get<std::string>().empty());
  const json& diagnostics = result.result["diagnostics"];
  EXPECT_TRUE(
      HasDiagnostic(diagnostics, "scripts/rotator.as", broken_line, "error"))
      << "expected scripts/rotator.as:" << broken_line << " in "
      << diagnostics.dump(2) << "\nlog: " << result.result["log_tail"].dump(2);
  for (const json& d : diagnostics) {
    if (d["file"] == "scripts/rotator.as" && d["severity"] == "error") {
      EXPECT_GT(d["col"].get<int>(), 0);
      EXPECT_FALSE(d["message"].get<std::string>().empty());
    }
  }

  // Fixing the script makes the next build succeed: the failed one left
  // nothing locked.
  WriteFile(script, original);
  const ToolResult fixed = fixture.CallTool("build_scripts");
  EXPECT_TRUE(fixed.ok) << fixed.result.dump(2);
}

TEST(McpToolsAssetsBuildTest, LastBuildSucceededDrivesTheResult) {
  SKIP_WITHOUT_PLAYER();
  McpToolFixture fixture;
  UsePump(fixture);
  PlaySession& play = fixture.Scene().PlayRef();
  EXPECT_FALSE(play.LastBuildSucceeded()) << "no build has run yet";

  const ToolResult good = fixture.CallTool("build_scripts");
  ASSERT_TRUE(good.ok) << good.result.dump(2);
  EXPECT_TRUE(good.result["ok"].get<bool>());
  EXPECT_TRUE(play.LastBuildSucceeded());
  const fs::path schema = fixture.Root() / ".mercury" / "scripts.mschema";
  ASSERT_TRUE(fs::exists(schema));

  // Make the schema look freshly written. A failed build must still be
  // reported as a failure: success comes from LastBuildSucceeded(), not from
  // the script list's mtime, which can be fresh for an unrelated reason.
  fs::last_write_time(schema,
                      fs::file_time_type::clock::now() + std::chrono::hours(1));
  const fs::path script = fixture.Root() / "scripts" / "rotator.as";
  const std::string original = ReadFile(script);
  WriteFile(script, original + "\nint broken = ;\n");
  const ToolResult bad = fixture.CallTool("build_scripts");
  EXPECT_FALSE(bad.ok) << bad.result.dump(2);
  EXPECT_FALSE(bad.result["ok"].get<bool>());
  EXPECT_FALSE(play.LastBuildSucceeded());
  EXPECT_FALSE(bad.result["message"].get<std::string>().empty());

  // Fixing the script lets the next build succeed; the flag follows it.
  WriteFile(script, original);
  const ToolResult fixed = fixture.CallTool("build_scripts");
  EXPECT_TRUE(fixed.ok) << fixed.result.dump(2);
  EXPECT_TRUE(play.LastBuildSucceeded());
}

TEST(McpToolsAssetsBuildTest, BuildScriptsSurvivesEditorShutdown) {
  SKIP_WITHOUT_PLAYER();
  McpToolFixture fixture;
  fixture.PumpTimeout(std::chrono::seconds(120));
  std::weak_ptr<const void> token = fixture.Scene().LifetimeToken();
  ASSERT_FALSE(token.expired());

  bool destroyed = false;
  fixture.SetPump([&] {
    if (!destroyed) {
      // The build is in flight (ActiveJob armed). Destroying the scene expires
      // its LifetimeToken; the pump must then finish the call without ever
      // dereferencing the now-stale scene pointer.
      fixture.DestroyScene();
      destroyed = true;
      EXPECT_TRUE(token.expired());
    }
    mcp::TickScriptBuild();
  });

  const ToolResult result = fixture.CallTool("build_scripts");
  EXPECT_FALSE(result.ok);
  EXPECT_NE(
      result.result["message"].get<std::string>().find("editor shutting down"),
      std::string::npos)
      << result.result.dump();
}
