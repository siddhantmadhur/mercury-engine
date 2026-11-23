// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the MCP play-in-editor tools (editor/mcp/tools/play.cc): the idle
// refusal paths and reading play logs through the shared console helpers,
// plus inspect_runtime_entity's argument checks. The tests that need a real
// mercury_player (the Play-busy refusal and the end-to-end headless run)
// skip when the binary was not built; `check` depends on it.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "console.h"
#include "mcp/tools/play.h"
#include "mcp_tools_fixture.h"
#include "os/filesystem.h"
#include "os/process.h"
#include "play_session.h"
#include "test_paths.h"

using Mercury::Console;
using Mercury::PlaySession;
using Mercury::mcp::ToolResult;
using Mercury::mcp::testing::McpToolFixture;
using nlohmann::json;

namespace fs = std::filesystem;

namespace {

std::vector<std::string> Texts(const json& result) {
  std::vector<std::string> texts;
  for (const json& entry : result["entries"]) {
    texts.push_back(entry["text"].get<std::string>());
  }
  return texts;
}

TEST(McpToolsPlayTest, StatusIsIdleBeforeAnythingStarts) {
  McpToolFixture fixture;
  const ToolResult result = fixture.CallTool("play_status");
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(result.result["state"], "idle");
  EXPECT_EQ(result.result["last_build_succeeded"], false);
}

TEST(McpToolsPlayTest, StopWhenIdleIsSuccessful) {
  McpToolFixture fixture;
  const ToolResult result = fixture.CallTool("play_stop");
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(result.result["state"], "idle");
  EXPECT_FALSE(fixture.Scene().PlayRef().IsBusy());
}

TEST(McpToolsPlayTest, StartRefusesAMissingMap) {
  McpToolFixture fixture;
  const ToolResult result =
      fixture.CallTool("play_start", {{"map", "res://maps/not_here.mmap"}});
  ASSERT_FALSE(result.ok);
  EXPECT_NE(result.result["error"].get<std::string>().find("map not found"),
            std::string::npos)
      << result.result.dump();
  EXPECT_FALSE(fixture.Scene().PlayRef().IsBusy());

  const ToolResult wrong_kind = fixture.CallTool("play_start", {{"map", 3}});
  EXPECT_FALSE(wrong_kind.ok);
  EXPECT_FALSE(fixture.CallTool("play_start", {{"save", "yes"}}).ok);
}

TEST(McpToolsPlayTest, StartRefusesAnUnsavedMap) {
  McpToolFixture fixture;
  const ToolResult dirty = fixture.CallTool(
      "apply_scene_operations",
      {{"operations",
        json::array({json{{"op", "create"}, {"name", "Dirty"}}})}});
  ASSERT_TRUE(dirty.ok) << dirty.result.dump();
  ASSERT_TRUE(fixture.Scene().IsDirty());

  const ToolResult result = fixture.CallTool("play_start");
  ASSERT_FALSE(result.ok);
  EXPECT_NE(result.result["error"].get<std::string>().find("unsaved changes"),
            std::string::npos)
      << result.result.dump();
  EXPECT_FALSE(fixture.Scene().PlayRef().IsBusy());
}

TEST(McpToolsPlayTest, LogsKeepOnlyBuildAndGameSources) {
  McpToolFixture fixture;
  Console& console = fixture.Scene().ConsoleRef();
  const uint64_t cursor = console.LatestCursor();
  console.Add(Console::Level::kInfo, Console::Source::kEditor, "editor line");
  console.Add(Console::Level::kInfo, Console::Source::kBuild, "build line");
  console.Add(Console::Level::kWarning, Console::Source::kGame, "game warning");
  console.Add(Console::Level::kError, Console::Source::kLore, "lore line");

  const ToolResult result = fixture.CallTool("play_logs", {{"cursor", cursor}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(result.result["state"], "idle");
  EXPECT_EQ(Texts(result.result),
            (std::vector<std::string>{"build line", "game warning"}));
  ASSERT_EQ(result.result["entries"].size(), 2u);
  EXPECT_EQ(result.result["entries"][0]["source"], "build");
  EXPECT_EQ(result.result["entries"][0]["level"], "info");
  EXPECT_EQ(result.result["entries"][1]["source"], "game");
  EXPECT_EQ(result.result["entries"][1]["level"], "warning");
  EXPECT_EQ(result.result["dropped"], false);
  EXPECT_EQ(result.result["truncated"], false);

  const ToolResult filtered = fixture.CallTool(
      "play_logs", {{"cursor", cursor}, {"levels", {"warning"}}});
  ASSERT_TRUE(filtered.ok) << filtered.result.dump();
  EXPECT_EQ(Texts(filtered.result), (std::vector<std::string>{"game warning"}));

  EXPECT_FALSE(fixture.CallTool("play_logs", {{"levels", {"verbose"}}}).ok);
  EXPECT_FALSE(fixture.CallTool("play_logs", {{"levels", "warning"}}).ok);
}

TEST(McpToolsPlayTest, LogCursorReadsOnlyNewLines) {
  McpToolFixture fixture;
  Console& console = fixture.Scene().ConsoleRef();
  const uint64_t cursor = console.LatestCursor();
  console.Add(Console::Level::kInfo, Console::Source::kBuild, "first");

  ToolResult result = fixture.CallTool("play_logs", {{"cursor", cursor}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(Texts(result.result), (std::vector<std::string>{"first"}));
  const uint64_t next = result.result["next_cursor"];

  console.Add(Console::Level::kInfo, Console::Source::kGame, "second");
  console.Add(Console::Level::kInfo, Console::Source::kEditor, "ignored");
  console.Add(Console::Level::kError, Console::Source::kBuild, "third");

  result = fixture.CallTool("play_logs", {{"cursor", next}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(Texts(result.result),
            (std::vector<std::string>{"second", "third"}));
  EXPECT_EQ(result.result["dropped"], false);

  // Without a cursor the tail includes only play's sources, and `limit`
  // counts those lines, not the editor lines between them.
  result = fixture.CallTool("play_logs", {{"limit", 1}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(Texts(result.result), (std::vector<std::string>{"third"}));
  EXPECT_EQ(result.result["truncated"], true);

  EXPECT_FALSE(fixture.CallTool("play_logs", {{"cursor", -1}}).ok);
  EXPECT_FALSE(fixture.CallTool("play_logs", {{"limit", 0}}).ok);
  EXPECT_FALSE(
      fixture.CallTool("play_logs", {{"cursor", console.LatestCursor() + 1}})
          .ok);
}

TEST(McpToolsPlayTest, ReadOnlyServerCanPollButCannotStartOrStop) {
  McpToolFixture fixture;
  EXPECT_FALSE(fixture.CallTool("play_start", json::object(), false).ok);
  EXPECT_FALSE(fixture.CallTool("play_stop", json::object(), false).ok);
  EXPECT_TRUE(fixture.CallTool("play_status", json::object(), false).ok);
  EXPECT_TRUE(fixture.CallTool("play_logs", json::object(), false).ok);
}

// ------------------------------------------------------- inspect_runtime_entity

TEST(McpToolsPlayTest, InspectRejectsFramesOutsideTheRange) {
  McpToolFixture fixture;
  for (const json& frames :
       {json(0), json(3601), json(-1), json("60"), json(1.5)}) {
    const ToolResult result =
        fixture.CallTool("inspect_runtime_entity", {{"frames", frames}});
    EXPECT_FALSE(result.ok) << frames.dump() << ": " << result.result.dump();
  }
}

TEST(McpToolsPlayTest, InspectRejectsDumpFramesOutsideTheRange) {
  McpToolFixture fixture;
  for (const json& dump_at : {json::array({0}), json::array({11}),
                              json::array({10, 11})}) {
    const ToolResult result = fixture.CallTool(
        "inspect_runtime_entity", {{"frames", 10}, {"dump_at", dump_at}});
    EXPECT_FALSE(result.ok) << dump_at.dump() << ": " << result.result.dump();
    EXPECT_NE(result.result["error"].get<std::string>().find("between 1 and 10"),
              std::string::npos)
        << result.result.dump();
  }
}

TEST(McpToolsPlayTest, InspectRejectsNonInputCommands) {
  McpToolFixture fixture;
  for (const char* command : {"capture", "quit", "dump", "log"}) {
    const ToolResult result = fixture.CallTool(
        "inspect_runtime_entity",
        {{"frames", 10},
         {"inputs", json::array({json{{"frame", 1},
                                      {"command", command},
                                      {"args", json::array({"x"})}}})}});
    EXPECT_FALSE(result.ok) << command << ": " << result.result.dump();
    EXPECT_NE(
        result.result["error"].get<std::string>().find("is not allowed"),
        std::string::npos)
        << command << ": " << result.result.dump();
  }
}

TEST(McpToolsPlayTest, InspectRejectsBadInputFramesAndArgs) {
  McpToolFixture fixture;
  for (const json& frame : {json(0), json(11), json(-1), json("3")}) {
    const ToolResult result = fixture.CallTool(
        "inspect_runtime_entity",
        {{"frames", 10},
         {"inputs", json::array({json{{"frame", frame},
                                      {"command", "press"},
                                      {"args", json::array({"w"})}}})}});
    EXPECT_FALSE(result.ok) << frame.dump() << ": " << result.result.dump();
  }
  // press takes exactly one argument.
  const ToolResult no_args = fixture.CallTool(
      "inspect_runtime_entity",
      {{"frames", 10},
       {"inputs", json::array({json{
            {"frame", 1}, {"command", "press"}, {"args", json::array()}}})}});
  EXPECT_FALSE(no_args.ok) << no_args.result.dump();
  EXPECT_NE(no_args.result["error"].get<std::string>().find("argument"),
            std::string::npos)
      << no_args.result.dump();
}

TEST(McpToolsPlayTest, InspectRefusesADirtyMapUnlessSaveIsTrue) {
  McpToolFixture fixture;
  // The space in the name also proves the ';' path syntax end to end when the
  // saved map is dumped below.
  ASSERT_TRUE(fixture
                  .CallTool("apply_scene_operations",
                            {{"operations",
                              json::array({json{{"op", "create"},
                                                {"name", "Dirty Inspect"}}})}})
                  .ok);
  ASSERT_TRUE(fixture.Scene().IsDirty());

  const ToolResult refused = fixture.CallTool(
      "inspect_runtime_entity", {{"map", "res://maps/main.mmap"}});
  ASSERT_FALSE(refused.ok);
  EXPECT_NE(refused.result["error"].get<std::string>().find("unsaved changes"),
            std::string::npos)
      << refused.result.dump();
  EXPECT_TRUE(fixture.Scene().IsDirty()) << "a refusal must not save";

  // With save:true the dirty check passes and the map is written; the tool
  // then runs the player (pumped like the editor's timer) unless this build
  // has no mercury_player.
  const bool has_player = !PlaySession::PlayerPath().empty();
  if (has_player) {
    fixture.PumpTimeout(std::chrono::seconds(180));
    fixture.SetPump([] { Mercury::mcp::TickRuntimeInspect(); });
  }
  const ToolResult saved = fixture.CallTool(
      "inspect_runtime_entity",
      {{"map", "res://maps/main.mmap"},
       {"save", true},
       {"frames", 1},
       {"dump_at", json::array({1})},
       {"nodes", json::array({"Dirty Inspect"})}});
  EXPECT_FALSE(fixture.Scene().IsDirty()) << "save:true must save the map";
  if (has_player) {
    ASSERT_TRUE(saved.ok) << saved.result.dump(2);
    ASSERT_EQ(saved.result["dumps"].size(), 1u) << saved.result.dump(2);
    bool found = false;
    for (const json& node : saved.result["dumps"][0]["nodes"]) {
      if (node["path"] == "Dirty Inspect") found = true;
    }
    EXPECT_TRUE(found) << "a node path with a space must survive the ';' "
                          "syntax: "
                       << saved.result.dump(2);
  } else if (!saved.ok) {
    EXPECT_EQ(saved.result["error"].get<std::string>().find("unsaved changes"),
              std::string::npos)
        << saved.result.dump();
  }
}

TEST(McpToolsPlayTest, InspectRejectsNodePathsWithSemicolonsOrNewlines) {
  McpToolFixture fixture;
  for (const char* path : {"Main;Camera", "Main Camera\nChild"}) {
    const ToolResult result = fixture.CallTool(
        "inspect_runtime_entity", {{"nodes", json::array({path})}});
    EXPECT_FALSE(result.ok) << path;
    EXPECT_NE(result.result["error"].get<std::string>().find("';'"),
              std::string::npos)
        << result.result.dump();
  }
}

TEST(McpToolsPlayTest, InspectFailsWhenARequestedDumpIsNotWritten) {
  if (PlaySession::PlayerPath().empty()) {
    GTEST_SKIP() << "mercury_player was not found next to the test or in the "
                    "build's bin folder (build the `check` target, which "
                    "depends on it)";
  }
  McpToolFixture fixture;
  // A directory at the dump path makes the player's write fail, so the tool
  // finds no readable dump and must report ok = false.
  std::error_code ec;
  std::filesystem::create_directories(
      fixture.Root() / ".mercury" / "tmp" / "dump_1.json", ec);
  ASSERT_FALSE(ec) << ec.message();
  fixture.PumpTimeout(std::chrono::seconds(180));
  fixture.SetPump([] { Mercury::mcp::TickRuntimeInspect(); });

  const ToolResult result = fixture.CallTool(
      "inspect_runtime_entity",
      {{"map", "res://maps/main.mmap"},
       {"frames", 1},
       {"dump_at", json::array({1})},
       {"nodes", json::array({"Player"})}});
  EXPECT_FALSE(result.ok) << result.result.dump(2);
  EXPECT_TRUE(result.result["dumps"].empty()) << result.result.dump(2);
  EXPECT_NE(result.result["error"].get<std::string>().find("dump"),
            std::string::npos)
      << result.result.dump(2);
  bool reported = false;
  for (const json& error : result.result["errors"]) {
    if (error.get<std::string>().find("dump at frame 1") != std::string::npos) {
      reported = true;
    }
  }
  EXPECT_TRUE(reported) << result.result.dump(2);
}

// A relative dump path must land under the project root, whatever the
// player's working directory is. The project copy lives at a path with a
// space in it, which the old absolute-path script form could not survive
// (its file argument is the first whitespace token).
TEST(McpToolsPlayTest, RelativeDumpPathResolvesAgainstTheProjectRoot) {
  if (PlaySession::PlayerPath().empty()) {
    GTEST_SKIP() << "mercury_player was not found next to the test or in the "
                    "build's bin folder (build the `check` target, which "
                    "depends on it)";
  }
  std::error_code ec;
  const fs::path base = Mercury::test::CacheDir() / "inspect space test";
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  ASSERT_FALSE(ec) << ec.message();
  const fs::path project = base / "project";
  fs::copy(fs::path(MERCURY_SOURCE_DIR) / "examples" / "roll_a_ball", project,
           fs::copy_options::recursive, ec);
  ASSERT_FALSE(ec) << ec.message();

  const fs::path script = base / "dumps.input";
  {
    std::ofstream out(script);
    ASSERT_TRUE(out) << "could not write " << script;
    out << "1 dump .mercury/tmp/relative_dump.json Player\n1 quit\n";
  }

  // Run from `base`, not the project root, so a working-directory-relative
  // write would land in the wrong place.
  Mercury::os::Process process;
  std::string error;
  ASSERT_TRUE(process.Start(
      {PlaySession::PlayerPath().string(),
       (project / "roll_a_ball.mproj").string(), "--map",
       "res://maps/main.mmap", "--headless", "--frames", "6"},
      base, &error, {"MERCURY_INPUT_SCRIPT=" + script.string()}))
      << error;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(120);
  while (process.IsRunning() &&
         std::chrono::steady_clock::now() < deadline) {
    process.TakeOutput();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (process.IsRunning()) process.Kill();
  process.TakeOutput();
  ASSERT_TRUE(process.GetExitCode().has_value()) << "the player did not exit";
  EXPECT_EQ(*process.GetExitCode(), 0);

  const fs::path under_root =
      project / ".mercury" / "tmp" / "relative_dump.json";
  EXPECT_TRUE(fs::is_regular_file(under_root, ec)) << under_root;
  EXPECT_FALSE(fs::exists(base / ".mercury" / "tmp" / "relative_dump.json", ec))
      << "the dump must not be written relative to the working directory";
  if (const auto text = Mercury::os::ReadText(under_root)) {
    const json document = json::parse(*text, nullptr, false);
    ASSERT_FALSE(document.is_discarded()) << *text;
    ASSERT_EQ(document["nodes"].size(), 1u) << *text;
    EXPECT_EQ(document["nodes"][0]["path"], "Player");
  } else {
    ADD_FAILURE() << "could not read " << under_root;
  }
  fs::remove_all(base, ec);
}

TEST(McpToolsPlayTest, InspectRefusesWhileAPlaySessionIsBusy) {
  if (PlaySession::PlayerPath().empty()) {
    GTEST_SKIP() << "mercury_player was not found next to the test or in the "
                    "build's bin folder (build the `check` target, which "
                    "depends on it)";
  }
  McpToolFixture fixture;
  const ToolResult started =
      fixture.CallTool("play_start", {{"map", "res://maps/main.mmap"}});
  ASSERT_TRUE(started.ok) << started.result.dump(2);
  ASSERT_EQ(started.result["state"], "building");
  ASSERT_TRUE(fixture.Scene().PlayRef().IsBusy());

  const ToolResult refused = fixture.CallTool(
      "inspect_runtime_entity", {{"map", "res://maps/main.mmap"}});
  EXPECT_FALSE(refused.ok) << refused.result.dump();
  EXPECT_NE(refused.result["error"].get<std::string>().find("already running"),
            std::string::npos)
      << refused.result.dump();

  fixture.CallTool("play_stop");
  EXPECT_FALSE(fixture.Scene().PlayRef().IsBusy());
}

namespace {

const json* PlayerNode(const json& dump) {
  for (const json& node : dump["nodes"]) {
    if (node["path"] == "Player") return &node;
  }
  return nullptr;
}

}  // namespace

// End to end: roll_a_ball's PlayerController pushes the ball toward -Z while
// the Move action holds W (the camera looks down -Z). Two dumps show the
// position and the rigidbody velocity moving that way.
TEST(McpToolsPlayTest, InspectRunsTheRollABallPlayerHeadlessly) {
  if (PlaySession::PlayerPath().empty()) {
    GTEST_SKIP() << "mercury_player was not found next to the test or in the "
                    "build's bin folder (build the `check` target, which "
                    "depends on it)";
  }
  McpToolFixture fixture;
  fixture.PumpTimeout(std::chrono::seconds(180));
  fixture.SetPump([] { Mercury::mcp::TickRuntimeInspect(); });

  const ToolResult result = fixture.CallTool(
      "inspect_runtime_entity",
      {{"map", "res://maps/main.mmap"},
       {"frames", 60},
       {"inputs", json::array({json{{"frame", 5},
                                    {"command", "press"},
                                    {"args", json::array({"w"})}}})},
       {"dump_at", json::array({10, 60})},
       {"nodes", json::array({"Player"})}});
  ASSERT_TRUE(result.ok) << result.result.dump(2);
  EXPECT_EQ(result.result["frames_run"], 60) << result.result.dump(2);

  const json& dumps = result.result["dumps"];
  ASSERT_EQ(dumps.size(), 2u) << result.result.dump(2);
  ASSERT_EQ(dumps[0]["frame"], 10);
  ASSERT_EQ(dumps[1]["frame"], 60);
  const json* early = PlayerNode(dumps[0]);
  const json* late = PlayerNode(dumps[1]);
  ASSERT_NE(early, nullptr) << dumps[0].dump(2);
  ASSERT_NE(late, nullptr) << dumps[1].dump(2);

  // The dump reads the live rigidbody, so velocity is there as well as the
  // post-simulation world transform.
  ASSERT_TRUE(early->contains("velocity")) << early->dump(2);
  ASSERT_TRUE(late->contains("velocity")) << late->dump(2);
  const double early_z = (*early)["transform"]["position"][2];
  const double late_z = (*late)["transform"]["position"][2];
  const double early_vz = (*early)["velocity"][2];
  const double late_vz = (*late)["velocity"][2];
  EXPECT_LT(late_z, early_z) << "W should keep moving the ball toward -Z";
  EXPECT_LT(late_vz, early_vz) << "W should accelerate the ball toward -Z";
}

}  // namespace
