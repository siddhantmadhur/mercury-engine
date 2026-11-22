// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "mcp/mcp_host.h"

#include <wx/app.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <random>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#include "console.h"
#include "editor_scene.h"
#include "editor_settings.h"
#include "mcp/json_util.h"
#include "mcp/tool_host.h"
#include "mcp/tool_registry.h"
#include "mercury/mcp/bridge.rs.h"
#include "os/process.h"
#include "version.h"

namespace Mercury {

namespace mcp {

// The queue-onto-UI-thread step, replaceable by tests. Production never sets
// it: the editor dispatches with wxTheApp->CallAfter.
McpHost::Dispatcher& DispatcherSlot() {
  static McpHost::Dispatcher dispatcher;
  return dispatcher;
}

// Completes a reply with {"error": message}. Every queued call completes
// exactly once, on every path, or the MCP client would wait forever.
void CompleteError(rust::Box<PendingReply> reply, std::string_view message) {
  nlohmann::json error{{"error", message}};
  CompleteReply(std::move(reply), false, error.dump(), {});
}

// Converts a tool result into the Rust image list.
rust::Vec<ImagePng> ToRustImages(const std::vector<Png>& images) {
  rust::Vec<ImagePng> out;
  for (const auto& image : images) {
    ImagePng converted;
    for (std::uint8_t byte : image.data) converted.data.push_back(byte);
    converted.label = image.label;
    out.push_back(std::move(converted));
  }
  return out;
}

// ---------------------------------------------------------------------------
// The C++ tool host the Rust transport calls into
//
// Invoke runs on the transport's tokio worker threads. It must be thread-safe
// and must not touch the editor: it copies the arguments out, then queues the
// work for the UI thread through the dispatcher, guarded by the alive token
// the host clears when it dies. The tool work itself always runs on the UI
// thread.
// ---------------------------------------------------------------------------

class ToolHostImpl final : public ToolHost,
                           public std::enable_shared_from_this<ToolHostImpl> {
 public:
  ToolHostImpl(EditorScene& scene, std::shared_ptr<bool> alive,
               std::shared_ptr<CallTracker> tracker)
      : scene_(scene), alive_(std::move(alive)), tracker_(std::move(tracker)) {}

  void Invoke(rust::Str tool, rust::Str args_json,
              rust::Box<PendingReply> reply) const override {
    // Worker thread: copy everything out of the Rust side first. `pending`
    // sits in a shared_ptr because the queued work must stay copyable for
    // wxWidgets' CallAfter while the reply itself is move-only.
    auto self = shared_from_this();
    auto pending =
        std::make_shared<rust::Box<PendingReply>>(std::move(reply));
    const std::string name(tool);
    const std::string args(args_json);

    const auto run = [self, pending, name, args]() {
      if (!*self->alive_) {
        // The editor is gone; the reply still completes exactly once.
        CompleteError(std::move(*pending), "editor shutting down");
        return;
      }
      nlohmann::json parsed;
      if (!ParseJson(args, parsed) ||
          (!parsed.is_null() && !parsed.is_object())) {
        CompleteError(std::move(*pending),
                      "the tool arguments are not a JSON object");
        return;
      }
      // UI thread: the alive token guarantees the scene is still alive (the
      // host clears the token before the scene goes away, on this thread).
      //
      // The reply is completed by `done`, exactly once: a sync tool calls it
      // before InvokeAsync returns, an async tool whenever it finishes. If the
      // host stops first, CallTracker::FailAll answers "editor shutting down"
      // (the destructor clears the alive token, then stops, which fails every
      // call still waiting) and a later `done` is a no-op. `done` only touches
      // its own call state and the reply, so it is safe after the host is
      // gone; the alive check also keeps a result computed against a dying
      // scene from being sent.
      ToolDone done = self->tracker_->Track(
          [self, pending](ToolResult result) {
            if (!*self->alive_) {
              CompleteError(std::move(*pending), "editor shutting down");
              return;
            }
            CompleteReply(std::move(*pending), result.ok,
                          result.result.dump(), ToRustImages(result.images));
          });
      ToolRegistry::Instance().InvokeAsync(self->scene_, name, parsed,
                                           std::move(done));
    };

    if (McpHost::Dispatcher dispatcher = DispatcherSlot()) {
      dispatcher(run);
      return;
    }
    if (wxTheApp) {
      wxTheApp->CallAfter([run]() { run(); });
      return;
    }
    // No UI thread to hand the work to (a hostless build): fail closed
    // instead of running editor code on a background thread.
    CompleteError(std::move(*pending), "the editor is not available");
  }

 private:
  EditorScene& scene_;
  std::shared_ptr<bool> alive_;
  std::shared_ptr<CallTracker> tracker_;
};

}  // namespace mcp

// ----------------------------------------------------------------- Impl ----

// Everything that touches the Rust side or the tool host: the installed
// mcp_host.h stays plain C++ (editor extensions include editor_scene.h).
struct McpHost::Impl {
  EditorScene& scene;
  Console& console;
  // Held by pending UI-thread callbacks; cleared before the scene dies, so a
  // queued call answers "editor shutting down" instead of touching it.
  std::shared_ptr<bool> alive = std::make_shared<bool>(true);
  std::shared_ptr<mcp::ToolHost> host_impl;
  // Asynchronous tool calls still waiting for their `done`; failed when the
  // server stops, so no client waits for a reply that can no longer come.
  std::shared_ptr<mcp::CallTracker> tracker =
      std::make_shared<mcp::CallTracker>();
  std::optional<rust::Box<mcp::Server>> server;

  bool running = false;
  std::string url;
  std::string last_error;
  // Snapshot of the settings the running server was started with.
  std::uint16_t port = 0;
  bool ephemeral = false;
  bool allow_edits = false;
  std::string token;
  // True while we own the .mcp.json entry we wrote, with the root it went to:
  // removal always resolves that recorded path, never the currently open
  // project (which may already be the next one).
  bool wrote_config = false;
  std::filesystem::path written_root;
  std::string warned_root;

  // 32 random bytes as hex: a bearer token only the local editor knows.
  std::string NewToken() const {
    std::array<std::uint8_t, 32> bytes{};
    std::random_device device;
    for (auto& byte : bytes) {
      byte = static_cast<std::uint8_t>(device() & 0xff);
    }
    std::string out;
    for (const std::uint8_t byte : bytes) {
      out += std::format("{:02x}", byte);
    }
    return out;
  }

  void ApplySettings() {
    EditorSettings& settings = scene.Settings();
    if (!settings.mcp_enabled) {
      Stop();
      return;
    }
    // First enable: a token no other process has seen, persisted so the
    // editor keeps it across runs and .mcp.json stays valid.
    if (settings.mcp_token.empty()) {
      settings.mcp_token = NewToken();
      settings.Save();
    }
    const bool restart =
        !running || ephemeral != settings.mcp_ephemeral ||
        allow_edits != settings.mcp_allow_edits ||
        token != settings.mcp_token ||
        (!settings.mcp_ephemeral &&
         port != static_cast<std::uint16_t>(settings.mcp_port));
    if (!restart) {
      WriteMcpJson();
      return;
    }
    Stop();
    Start(settings);
  }

  void RegenerateToken() {
    EditorSettings& settings = scene.Settings();
    settings.mcp_token = NewToken();
    settings.Save();
    // Restarts when running, so clients must re-authenticate and .mcp.json
    // points at the new token.
    ApplySettings();
  }

  bool Start(EditorSettings& settings) {
    mcp::ServerConfig config;
    config.port = settings.mcp_ephemeral
                      ? 0
                      : static_cast<std::uint16_t>(settings.mcp_port);
    config.read_only = !settings.mcp_allow_edits;
    config.token = settings.mcp_token;
    config.editor_version = MERCURY_VERSION_STRING;

    host_impl = std::make_shared<mcp::ToolHostImpl>(scene, alive, tracker);
    try {
      server.emplace(mcp::StartServer(config, host_impl));
    } catch (const std::exception& error) {
      host_impl.reset();
      running = false;
      url.clear();
      last_error = error.what();
      console.Error("MCP: could not start the server: " + last_error);
      return false;
    }

    running = true;
    url = std::string((*server)->url());
    port = config.port;
    ephemeral = settings.mcp_ephemeral;
    allow_edits = settings.mcp_allow_edits;
    token = settings.mcp_token;
    last_error.clear();
    console.Info("MCP server listening at " + url);
    WriteMcpJson();
    return true;
  }

  void Stop() {
    // Dropping the Rust server shuts the transport down and joins its
    // threads. In-flight calls hold the tool host alive and answer through
    // the dead alive token. This runs in the McpHost destructor too, so it
    // never touches the scene: the .mcp.json removal uses the recorded root.
    //
    // Calls an async tool has not completed yet are answered first, so the
    // clients still connected get the error rather than a dropped connection.
    if (tracker->FailAll(*alive ? "the MCP server stopped"
                                : "editor shutting down") > 0) {
      // The transport drops its tasks the moment the server is dropped, so
      // give the answers a moment to reach their connections. Bounded, and
      // only when a call was still open.
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    server.reset();
    host_impl.reset();
    running = false;
    url.clear();
    RemoveMcpJson();
  }

  void WriteMcpJson() {
    if (!scene.Settings().mcp_write_config) {
      // The user turned the config writing off while the server runs: our
      // entry must go, too.
      RemoveMcpJson();
      return;
    }
    auto project = scene.GetProject();
    if (!project || !running) return;
    std::string error;
    if (McpHost::WriteProjectMcpJson(project->GetRoot(), url, token, &error)) {
      wrote_config = true;
      written_root = project->GetRoot();
      WarnIfNotGitIgnored(project->GetRoot());
    } else {
      console.Error("MCP: could not update .mcp.json: " + error);
    }
  }

  void RemoveMcpJson() {
    if (!wrote_config) return;
    // Remove from the root we wrote to, never from the currently open
    // project.
    const std::filesystem::path root = written_root;
    wrote_config = false;
    written_root.clear();
    std::string error;
    if (!McpHost::RemoveProjectMcpJson(root, &error)) {
      console.Error("MCP: could not remove the .mcp.json entry: " + error);
    }
  }

  // Logs one warning when .mcp.json would be committed to git. Never edits
  // .gitignore; runs at most once per project.
  void WarnIfNotGitIgnored(const std::filesystem::path& root) {
    // One warning per project per session, whatever git says: the check
    // spawns a process and should not repeat on every settings change.
    if (warned_root == root.string()) return;
    warned_root = root.string();

    os::Process git;
    std::string error;
    if (!git.Start(
            {"git", "-C", root.string(), "check-ignore", "-q", ".mcp.json"},
            root, &error)) {
      return;  // no git to ask; stay quiet
    }
    // check-ignore -q prints nothing and exits in milliseconds; wait briefly
    // and give up rather than block the UI thread on a stuck child.
    for (int waited = 0; git.IsRunning() && waited < 100; ++waited) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (git.IsRunning()) {
      git.Kill();
      return;
    }
    // 0: ignored, 1: tracked/untracked but not ignored, 128: not a repo.
    if (git.GetExitCode() == 1) {
      console.Warn(
          ".mcp.json contains this editor's MCP bearer token; add it to "
          ".gitignore so the token is not committed.");
    }
  }
};

McpHost::McpHost(EditorScene& scene, Console& console)
    : impl_(std::make_unique<Impl>(Impl{scene, console})) {}

McpHost::~McpHost() {
  // First thing, so a queued call answers with an error instead of running
  // against the dying scene.
  *impl_->alive = false;
  Stop();
}

std::string McpHost::Url() const { return impl_->url; }

bool McpHost::Running() const { return impl_->running; }

std::string McpHost::LastError() const { return impl_->last_error; }

void McpHost::SetDispatcherForTest(Dispatcher dispatcher) {
  mcp::DispatcherSlot() = std::move(dispatcher);
}

void McpHost::ApplySettings() { impl_->ApplySettings(); }

void McpHost::RegenerateToken() { impl_->RegenerateToken(); }

void McpHost::Stop() { impl_->Stop(); }

// ------------------------------------------------------------- .mcp.json ---

namespace {

std::filesystem::path McpJsonPath(const std::filesystem::path& root) {
  return root / ".mcp.json";
}

nlohmann::json MercuryServerEntry(const std::string& url,
                                  const std::string& token) {
  return nlohmann::json{
      {"type", "http"},
      {"url", url},
      {"headers", nlohmann::json{{"Authorization", "Bearer " + token}}},
  };
}

}  // namespace

bool McpHost::WriteProjectMcpJson(const std::filesystem::path& root,
                                  const std::string& url,
                                  const std::string& token,
                                  std::string* error) {
  nlohmann::json document;
  std::string read_error;
  if (!mcp::ReadJsonFile(McpJsonPath(root), document, &read_error)) {
    // Unreadable existing content must not be silently overwritten.
    if (error) *error = read_error;
    return false;
  }
  if (!document.is_object()) {
    if (error) *error = McpJsonPath(root).string() + " is not a JSON object";
    return false;
  }
  if (!document.contains("mcpServers") || !document["mcpServers"].is_object()) {
    document["mcpServers"] = nlohmann::json::object();
  }
  document["mcpServers"]["mercury"] = MercuryServerEntry(url, token);
  return mcp::WriteJsonFile(McpJsonPath(root), document, error);
}

bool McpHost::RemoveProjectMcpJson(const std::filesystem::path& root,
                                   std::string* error) {
  const std::filesystem::path path = McpJsonPath(root);
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return true;

  nlohmann::json document;
  std::string read_error;
  if (!mcp::ReadJsonFile(path, document, &read_error)) {
    // Not ours to fix; leave it alone.
    if (error) *error = read_error;
    return false;
  }
  if (!document.is_object()) {
    if (error) *error = path.string() + " is not a JSON object";
    return false;
  }
  auto servers = document.find("mcpServers");
  if (servers == document.end() || !servers->is_object()) return true;
  if (servers->find("mercury") == servers->end()) return true;

  servers->erase("mercury");
  if (servers->empty()) document.erase("mcpServers");
  if (document.empty()) {
    std::filesystem::remove(path, ec);
    if (ec) {
      if (error) *error = ec.message();
      return false;
    }
    return true;
  }
  return mcp::WriteJsonFile(path, document, error);
}

}  // namespace Mercury
