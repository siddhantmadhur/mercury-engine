// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * mcp_host.h
 *
 * Hosts the MCP server inside the editor. McpHost owns the Rust transport
 * (mercury/mcp: HTTP, bearer token, MCP handshake) and the C++ tool host that
 * forwards every tool call onto the editor's UI thread, through the tool
 * registry, and back.
 *
 * Life cycle: EditorScene owns one McpHost. ApplySettings() starts, stops or
 * restarts the server so it matches the editor settings, and keeps the
 * project's .mcp.json in step. A disabled server leaves no Server object at
 * all: no thread and no socket. Stop() runs on project close and on editor
 * exit.
 *
 * Threading: the transport calls the tool host on its tokio worker threads.
 * The tool host only copies the arguments out and queues the work for the UI
 * thread (wxTheApp->CallAfter in the editor; tests can replace that step with
 * SetDispatcherForTest), guarded by an alive token. If the editor is going
 * away the reply is completed with an error and the scene is never touched.
 *
 * Everything that touches the Rust side (rust::Box, the generated bridge
 * header, the tool host and registry) lives in mcp_host.cc: this header is
 * installed in the SDK (editor_scene.h pulls it in) and must stay plain C++.
 */
#ifndef MERCURY_EDITOR_MCP_HOST_H
#define MERCURY_EDITOR_MCP_HOST_H

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace Mercury {

class Console;
class EditorScene;

class McpHost {
 public:
  McpHost(EditorScene& scene, Console& console);
  // Stops the server and marks the host dead, so calls still in flight are
  // answered with an error instead of touching the dying scene.
  ~McpHost();

  // Starts, stops or restarts the server so it matches the editor settings,
  // and rewrites .mcp.json when the server runs. A generated token is saved
  // back into the settings on first enable.
  void ApplySettings();
  // The server URL (http://127.0.0.1:<port>/mcp); empty when stopped.
  std::string Url() const;
  bool Running() const;
  // The last start failure, if any; cleared by a successful start.
  std::string LastError() const;
  // Generates a fresh token, saves it and restarts the server so clients must
  // re-authenticate.
  void RegenerateToken();
  // Stops the server and removes the .mcp.json entry we wrote. The editor
  // calls this on project close and on exit; ApplySettings also stops
  // internally when the settings turn the server off.
  void Stop();

  // .mcp.json management at a project root: set only mcpServers.mercury,
  // preserving every other key, and remove only our entry (deleting the file
  // when it ends up empty). Both write atomically. Exposed for tests.
  static bool WriteProjectMcpJson(const std::filesystem::path& root,
                                  const std::string& url,
                                  const std::string& token,
                                  std::string* error = nullptr);
  static bool RemoveProjectMcpJson(const std::filesystem::path& root,
                                   std::string* error = nullptr);

  // Test hook: replaces the queue-onto-UI-thread step, which is
  // wxTheApp->CallAfter in the editor. Passing an empty function restores the
  // default.
  using Dispatcher = std::function<void(std::function<void()>)>;
  static void SetDispatcherForTest(Dispatcher dispatcher);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_MCP_HOST_H
