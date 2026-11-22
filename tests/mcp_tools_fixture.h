// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * mcp_tools_fixture.h
 *
 * Header-only test fixture for the MCP tool groups (tests/mcp_tools_<group>_test.cc,
 * all built into the mcp_tools_test executable by a CMake glob).
 *
 *   #include "mcp_tools_fixture.h"
 *   using Mercury::mcp::ToolResult;
 *   using Mercury::mcp::testing::McpToolFixture;
 *
 *   TEST(McpToolsMyGroupTest, DoesTheThing) {
 *     McpToolFixture fixture;                     // roll_a_ball, opened
 *     ToolResult r = fixture.CallTool("my_tool", {{"path", "Root/Player"}});
 *     ASSERT_TRUE(r.ok) << r.result.dump();
 *     // fixture.Scene() is the open EditorScene; fixture.Root() the project.
 *   }
 *
 * What it gives you:
 *
 *   - A copy of examples/roll_a_ball in a fresh temp folder (so a tool may
 *     write files) opened in a headless EditorScene, with the editor
 *     settings redirected to a throwaway config dir (XDG_CONFIG_HOME), so
 *     nothing touches the developer's editor.cfg. Everything is removed when
 *     the fixture goes out of scope. Pass another examples/ folder name to
 *     the constructor to use a different project.
 *   - CallTool(name, args, allow_edits = true): the call goes through
 *     ToolRegistry::InvokeAsync exactly like an MCP call does, including the
 *     Read/Write/Exec check against the editor's "allow edits" setting
 *     (allow_edits = false is the read-only server: a Write or Exec tool must
 *     answer ok = false). Synchronous tools return at once. An asynchronous
 *     tool (MERCURY_MCP_TOOL_ASYNC) is pumped to completion: the fixture
 *     runs the pump until the tool calls `done`, and fails the test clearly
 *     (a gtest failure plus an {"error": ...} result) when it has not within
 *     PumpTimeout (10 s by default).
 *   - The pump: wxTheApp->ProcessPendingEvents() when a wxApp exists (so
 *     CallAfter work runs), then the hook set with SetPump (step a frame,
 *     advance a fake clock, run a child process's reader, ...), then a 1 ms
 *     sleep. Tests of tools that need the editor's frame loop install the
 *     hook that steps it.
 *
 * Not on offer: a real UI thread or a rendering device. The fixture is the
 * UI thread: handlers and `done` run on the test's thread.
 */
#ifndef MERCURY_TESTS_MCP_TOOLS_FIXTURE_H
#define MERCURY_TESTS_MCP_TOOLS_FIXTURE_H

#include <gtest/gtest.h>

#include <wx/app.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>

#include <nlohmann/json.hpp>

#include "editor_scene.h"
#include "editor_settings.h"
#include "mcp/tool_registry.h"

namespace Mercury::mcp::testing {

class McpToolFixture {
 public:
  explicit McpToolFixture(const std::string& example = "roll_a_ball") {
    namespace fs = std::filesystem;
    root_ = fs::temp_directory_path() /
            ("mcp_tools_" + example + "_" +
             std::to_string(std::random_device{}()));
    fs::create_directories(root_ / "config");
    const char* previous = std::getenv("XDG_CONFIG_HOME");
    had_config_home_ = previous != nullptr;
    if (previous) previous_config_home_ = previous;
    setenv("XDG_CONFIG_HOME", (root_ / "config").string().c_str(), 1);

    const fs::path source = fs::path(MERCURY_SOURCE_DIR) / "examples" / example;
    project_ = root_ / "project";
    std::error_code ec;
    fs::copy(source, project_, fs::copy_options::recursive, ec);
    EXPECT_FALSE(ec) << "copying " << source << ": " << ec.message();

    scene_ = std::make_unique<EditorScene>();
    std::filesystem::path mproj;
    for (const auto& entry : fs::directory_iterator(project_, ec)) {
      if (entry.path().extension() == ".mproj") mproj = entry.path();
    }
    EXPECT_TRUE(scene_->OpenProject(mproj)) << "opening " << mproj;
    // The editor calls this every frame; it runs SyncFromMap so the map nodes
    // have live entities and components for the tools to query.
    scene_->OnFrame(0.0);
  }

  ~McpToolFixture() {
    scene_.reset();  // before the config dir and the project go away
    if (had_config_home_) {
      setenv("XDG_CONFIG_HOME", previous_config_home_.c_str(), 1);
    } else {
      unsetenv("XDG_CONFIG_HOME");
    }
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  McpToolFixture(const McpToolFixture&) = delete;
  McpToolFixture& operator=(const McpToolFixture&) = delete;

  EditorScene& Scene() { return *scene_; }
  // Destroys the scene now (expiring its LifetimeToken), e.g. to test that an
  // in-flight async tool does not touch a scene that is going away.
  void DestroyScene() { scene_.reset(); }
  // The project's folder: a private copy, safe to write to.
  const std::filesystem::path& Root() const { return project_; }

  // How long CallTool waits for an async tool to call `done`.
  void PumpTimeout(std::chrono::milliseconds timeout) {
    pump_timeout_ = timeout;
  }
  // Extra work to run on every pump pass while an async tool is pending.
  void SetPump(std::function<void()> pump) { pump_ = std::move(pump); }

  ToolResult CallTool(std::string_view name,
                      const nlohmann::json& args = nlohmann::json::object(),
                      bool allow_edits = true) {
    scene_->Settings().mcp_allow_edits = allow_edits;

    struct Completion {
      std::optional<ToolResult> result;
    };
    auto completion = std::make_shared<Completion>();
    ToolRegistry::Instance().InvokeAsync(
        *scene_, std::string(name), args,
        [completion](ToolResult result) {
          // A second done() is the tool's bug; the first answer stands.
          if (!completion->result) completion->result = std::move(result);
        });

    const auto deadline = std::chrono::steady_clock::now() + pump_timeout_;
    while (!completion->result) {
      if (std::chrono::steady_clock::now() >= deadline) {
        const std::string message =
            "async tool '" + std::string(name) +
            "' did not call done() within " +
            std::to_string(pump_timeout_.count()) + " ms";
        ADD_FAILURE() << message;
        return ToolResult{false, nlohmann::json{{"error", message}}, {}};
      }
      if (wxTheApp) wxTheApp->ProcessPendingEvents();
      if (pump_) pump_();
      if (completion->result) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::move(*completion->result);
  }

 private:
  std::filesystem::path root_;
  std::filesystem::path project_;
  bool had_config_home_ = false;
  std::string previous_config_home_;
  std::chrono::milliseconds pump_timeout_{10000};
  std::function<void()> pump_;
  std::unique_ptr<EditorScene> scene_;
};

}  // namespace Mercury::mcp::testing

#endif  // MERCURY_TESTS_MCP_TOOLS_FIXTURE_H
