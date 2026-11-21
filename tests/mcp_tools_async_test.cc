// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the asynchronous tool contract (MERCURY_MCP_TOOL_ASYNC): sync and
// async tools in one registry, completion through the test fixture's pump,
// and the CallTracker the MCP host uses to complete each call exactly once
// and to answer calls that are still open when the host stops. The same
// contract over real HTTP is in mcp_host_test.cc.

#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mcp/tool_registry.h"
#include "mcp_tools_fixture.h"

using namespace Mercury;
using Mercury::mcp::AsyncToolHandler;
using Mercury::mcp::CallTracker;
using Mercury::mcp::ToolDone;
using Mercury::mcp::ToolRegistry;
using Mercury::mcp::ToolResult;
using Mercury::mcp::testing::McpToolFixture;

namespace {

// Parked `done` callbacks of the test tools below, completed by the pump.
std::vector<ToolDone>& Parked() {
  static std::vector<ToolDone> parked;
  return parked;
}
int g_async_handler_runs = 0;
int g_write_handler_runs = 0;

void AsyncLater(EditorScene&, const nlohmann::json& args, ToolDone done) {
  g_async_handler_runs++;
  // Copy what the continuation needs out of `args`: it does not outlive the
  // call.
  const std::string text = args.value("text", std::string());
  Parked().push_back([done = std::move(done), text](ToolResult) {
    done(ToolResult{true, nlohmann::json{{"echo", text}}, {}});
  });
}

void AsyncNever(EditorScene&, const nlohmann::json&, ToolDone) {}

void AsyncWrite(EditorScene&, const nlohmann::json&, ToolDone done) {
  g_write_handler_runs++;
  done(ToolResult{true, nlohmann::json{{"wrote", true}}, {}});
}

ToolResult SyncEcho(EditorScene&, const nlohmann::json& args) {
  return ToolResult{true, nlohmann::json{{"echo", args}}, {}};
}

}  // namespace

MERCURY_MCP_TOOL_ASYNC("mcp_test_async_later", kRead, &AsyncLater);
MERCURY_MCP_TOOL_ASYNC("mcp_test_async_never", kRead, &AsyncNever);
MERCURY_MCP_TOOL_ASYNC("mcp_test_async_write", kWrite, &AsyncWrite);
MERCURY_MCP_TOOL("mcp_test_sync_echo", kRead, &SyncEcho);

TEST(McpToolsAsyncTest, SyncAndAsyncToolsShareOneTable) {
  const mcp::ToolEntry* sync = ToolRegistry::Instance().Find("mcp_test_sync_echo");
  const mcp::ToolEntry* async =
      ToolRegistry::Instance().Find("mcp_test_async_later");
  ASSERT_NE(sync, nullptr);
  ASSERT_NE(async, nullptr);
  EXPECT_FALSE(sync->IsAsync());
  EXPECT_TRUE(async->IsAsync());
  // ping lives in the object library (tools/core.cc) and is still sync.
  const mcp::ToolEntry* ping = ToolRegistry::Instance().Find("ping");
  ASSERT_NE(ping, nullptr);
  EXPECT_FALSE(ping->IsAsync());
}

TEST(McpToolsAsyncTest, FixtureCallsSyncToolsAtOnce) {
  McpToolFixture fixture;
  const ToolResult result =
      fixture.CallTool("mcp_test_sync_echo", {{"a", 1}});
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.result["echo"]["a"], 1);

  const ToolResult unknown = fixture.CallTool("no_such_tool");
  EXPECT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.result["error"], "unknown tool no_such_tool");
}

TEST(McpToolsAsyncTest, FixturePumpsAnAsyncToolToCompletion) {
  McpToolFixture fixture;
  Parked().clear();
  // The tool parks its `done`; the second pump pass completes it, the way a
  // rendered frame or a finished process would.
  int pass = 0;
  fixture.SetPump([&] {
    if (++pass == 2) {
      ASSERT_EQ(Parked().size(), 1u);
      Parked().front()(ToolResult{});
      Parked().clear();
    }
  });
  const int before = g_async_handler_runs;
  const ToolResult result =
      fixture.CallTool("mcp_test_async_later", {{"text", "hello"}});
  EXPECT_EQ(g_async_handler_runs, before + 1);
  EXPECT_GE(pass, 2);
  EXPECT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(result.result["echo"], "hello");
}

TEST(McpToolsAsyncTest, FixtureFailsClearlyWhenDoneNeverComes) {
  McpToolFixture fixture;
  fixture.PumpTimeout(std::chrono::milliseconds(50));
  ToolResult result;
  EXPECT_NONFATAL_FAILURE(
      result = fixture.CallTool("mcp_test_async_never"),
      "did not call done() within 50 ms");
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.result["error"].get<std::string>().find("mcp_test_async_never"),
            std::string::npos);
}

TEST(McpToolsAsyncTest, TheModeCheckAppliesToAsyncToolsToo) {
  McpToolFixture fixture;
  g_write_handler_runs = 0;
  const ToolResult rejected =
      fixture.CallTool("mcp_test_async_write", {}, /*allow_edits=*/false);
  EXPECT_FALSE(rejected.ok);
  EXPECT_NE(rejected.result["error"].get<std::string>().find("edits are not allowed"),
            std::string::npos);
  EXPECT_EQ(g_write_handler_runs, 0) << "a rejected tool must not run";

  const ToolResult allowed = fixture.CallTool("mcp_test_async_write");
  EXPECT_TRUE(allowed.ok);
  EXPECT_EQ(g_write_handler_runs, 1);
}

TEST(McpToolsAsyncTest, TheSyncEntryPointRefusesAnAsyncTool) {
  McpToolFixture fixture;
  const ToolResult result = ToolRegistry::Instance().Invoke(
      fixture.Scene(), "mcp_test_async_later", nlohmann::json::object());
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.result["error"].get<std::string>().find("asynchronous"),
            std::string::npos);
}

// ------------------------------------------------------------ CallTracker --

TEST(McpToolsAsyncTest, TrackerCompletesEachCallExactlyOnce) {
  CallTracker tracker;
  int finished = 0;
  ToolResult last;
  ToolDone done = tracker.Track([&](ToolResult result) {
    finished++;
    last = std::move(result);
  });
  EXPECT_EQ(tracker.Pending(), 1u);
  EXPECT_EQ(finished, 0);

  done(ToolResult{true, nlohmann::json{{"n", 1}}, {}});
  EXPECT_EQ(finished, 1);
  EXPECT_EQ(tracker.Pending(), 0u);

  // A second done() is ignored (it asserts in debug builds, where the
  // assertion is a feature; this test turns it off to check the release
  // behaviour).
  CallTracker::SetAssertOnDoubleDone(false);
  done(ToolResult{false, nlohmann::json{{"n", 2}}, {}});
  CallTracker::SetAssertOnDoubleDone(true);
  EXPECT_EQ(finished, 1);
  EXPECT_TRUE(last.ok);
  EXPECT_EQ(last.result["n"], 1);
}

TEST(McpToolsAsyncTest, TrackerFailAllAnswersOpenCallsOnly) {
  CallTracker tracker;
  std::vector<ToolResult> results(3);
  std::vector<int> finished(3, 0);
  std::vector<ToolDone> dones;
  for (int i = 0; i < 3; i++) {
    dones.push_back(tracker.Track([&, i](ToolResult result) {
      finished[i]++;
      results[i] = std::move(result);
    }));
  }
  dones[1](ToolResult{true, nlohmann::json{{"mine", true}}, {}});
  EXPECT_EQ(tracker.Pending(), 2u);

  tracker.FailAll("editor shutting down");
  EXPECT_EQ(tracker.Pending(), 0u);
  EXPECT_EQ(finished, (std::vector<int>{1, 1, 1}));
  EXPECT_FALSE(results[0].ok);
  EXPECT_EQ(results[0].result["error"], "editor shutting down");
  EXPECT_TRUE(results[1].ok);  // already answered: left alone
  EXPECT_FALSE(results[2].ok);

  // A done() that arrives after the shutdown answer is a silent no-op, even
  // with the double-done assertion on: the host answered, not the tool.
  dones[0](ToolResult{true, nlohmann::json{{"late", true}}, {}});
  dones[2](ToolResult{true, nlohmann::json{{"late", true}}, {}});
  EXPECT_EQ(finished, (std::vector<int>{1, 1, 1}));
  EXPECT_FALSE(results[0].ok);

  // Failing again changes nothing.
  tracker.FailAll("again");
  EXPECT_EQ(finished, (std::vector<int>{1, 1, 1}));
}

TEST(McpToolsAsyncTest, DoneOutlivesTheTracker) {
  ToolDone done;
  int finished = 0;
  {
    CallTracker tracker;
    done = tracker.Track([&](ToolResult) { finished++; });
    tracker.FailAll("editor shutting down");
  }
  EXPECT_EQ(finished, 1);
  done(ToolResult{});  // the tracker is gone; must not touch it
  EXPECT_EQ(finished, 1);
}
