// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the MCP play-in-editor tools (editor/mcp/tools/play.cc): the idle
// refusal paths and reading play logs through the shared console helpers.
// They never start a real mercury_player: the fixture's PlaySession has no
// player binary to launch, so only the paths that refuse before launch (or
// whose launch refusal is expected) are exercised.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "console.h"
#include "mcp_tools_fixture.h"

using Mercury::Console;
using Mercury::mcp::ToolResult;
using Mercury::mcp::testing::McpToolFixture;
using nlohmann::json;

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

}  // namespace
