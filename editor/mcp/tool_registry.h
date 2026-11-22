// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * tool_registry.h
 *
 * The editor's MCP tool registry. Tools self-register at static
 * initialization with MERCURY_MCP_TOOL and are looked up by name when a call
 * arrives. The registry owns the names, the Read/Write/Exec mode of each tool
 * and the defence-in-depth check of that mode against the editor settings.
 */
#ifndef MERCURY_EDITOR_MCP_TOOL_REGISTRY_H
#define MERCURY_EDITOR_MCP_TOOL_REGISTRY_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Mercury {

class EditorScene;

}  // namespace Mercury

namespace Mercury::mcp {

// What a tool does to the editor. Read tools only look; Write and Exec tools
// change the project, the map or the editor and are rejected unless the
// settings allow edits (checked once when the call is dispatched and again
// here, right before the handler runs).
enum class ToolMode { kRead, kWrite, kExec };

// A PNG image a tool captured for its result; the label names it in the client.
struct Png {
  std::vector<std::uint8_t> data;
  std::string label;
};

struct ToolResult {
  bool ok = true;
  // The JSON value returned to the MCP client. `ok == false` usually carries
  // {"error": "..."}.
  nlohmann::json result;
  std::vector<Png> images;
};

// Runs on the editor's UI thread with the MCP call arguments already parsed.
using ToolHandler =
    std::function<ToolResult(EditorScene& scene, const nlohmann::json& args)>;

// Completes an asynchronous tool call. See AsyncToolHandler.
using ToolDone = std::function<void(ToolResult)>;

// A tool that cannot answer within one UI-thread callback (it needs a
// rendered frame, a child process to finish, ...). The contract:
//
//   - The handler starts on the UI thread, like a ToolHandler, and returns
//     as soon as the work is under way. Arguments and anything else it needs
//     later must be copied into the continuation: `args` does not outlive the
//     call.
//   - `done` must be called exactly once, on the UI thread, with the result.
//     A second call is ignored (and asserts in debug builds, where it is a
//     bug in the tool). Never call it from another thread: hand the result
//     to the UI thread first (wxTheApp->CallAfter).
//   - If the editor shuts down (or the MCP server stops) before `done` is
//     called, the host answers the client with {"error": "editor shutting
//     down"} itself; a `done` called after that is a harmless no-op, but the
//     continuation must not touch the EditorScene once it may be gone.
//     Hold a weak "still alive" flag of your own, or only use scene state
//     from callbacks the editor runs.
//   - The client waits at most the tool's timeout (timeout_secs in its Rust
//     ToolSpec, 30 s by default): give long-running tools a longer one.
using AsyncToolHandler = std::function<void(
    EditorScene& scene, const nlohmann::json& args, ToolDone done)>;

struct ToolEntry {
  std::string name;
  ToolMode mode = ToolMode::kRead;
  // Exactly one of the two is set.
  ToolHandler handler;
  AsyncToolHandler async_handler;

  bool IsAsync() const { return static_cast<bool>(async_handler); }
};

class ToolRegistry {
 public:
  static ToolRegistry& Instance();

  // Called by the MERCURY_MCP_TOOL / MERCURY_MCP_TOOL_ASYNC macros at static
  // initialization. Sync and async tools share one table (one name, one
  // entry; registering a name again replaces it).
  void Register(std::string name, ToolMode mode, ToolHandler handler);
  void RegisterAsync(std::string name, ToolMode mode, AsyncToolHandler handler);

  // nullptr when no tool is registered under this name.
  const ToolEntry* Find(const std::string& name) const;

  // Every registered tool, ordered by name. For catalogue-style listings (the
  // agent guides); the pointers stay valid until a tool is registered again.
  std::vector<const ToolEntry*> Entries() const;

  // Registry lookup, the mode re-check against the editor settings, and the
  // handler itself, for a synchronous tool. An async tool answers with an
  // error here: call InvokeAsync. On the UI thread.
  ToolResult Invoke(EditorScene& scene, const std::string& name,
                    const nlohmann::json& args) const;

  // The same for either kind of tool, which is what the MCP host calls.
  // `done` is called exactly once: before this returns for a sync tool (and
  // for every error: unknown tool, mode check), later for an async one.
  // On the UI thread.
  void InvokeAsync(EditorScene& scene, const std::string& name,
                   const nlohmann::json& args, ToolDone done) const;

 private:
  ToolRegistry() = default;

  // The error for an unknown tool or a failed mode check; nullopt to proceed.
  std::optional<ToolResult> Reject(const EditorScene& scene,
                                   const ToolEntry* entry,
                                   const std::string& name) const;

  std::map<std::string, ToolEntry> tools_;
};

// Tracks the asynchronous calls of one MCP host. Track() wraps the transport's
// "complete the reply" step into the ToolDone handed to the handler, which:
//
//   - completes the call exactly once: a second `done` is ignored (and
//     asserts in debug builds unless SetAssertOnDoubleDone(false));
//   - lets FailAll() complete every call still waiting with an error, which
//     is what answers the client when the host stops (or dies) before a tool
//     called `done`; such a call's late `done` is a silent no-op.
//
// `finish` runs outside the tracker's locks, once. A ToolDone only touches
// its own call state, never the host, so it is safe to call after the host
// is gone. Thread-safe (the host's tests call it from several threads).
class CallTracker {
 public:
  ToolDone Track(std::function<void(ToolResult)> finish);
  // Completes every call that has not completed with {"error": message};
  // returns how many it completed.
  size_t FailAll(const std::string& message);
  // Calls tracked and not yet completed.
  size_t Pending() const;

  // Debug builds assert on a double `done`; the tests of that rule turn the
  // assertion off.
  static void SetAssertOnDoubleDone(bool enable);

 private:
  struct Call {
    std::mutex mutex;
    std::function<void(ToolResult)> finish;
    bool completed = false;
    bool failed_by_shutdown = false;
  };

  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<Call>> calls_;
};

// Static tool self-registration, evaluated once when the program starts.
// `name` is a string literal (it may contain anything, e.g.
// "__resources/list"), `mode` is one of kRead, kWrite, kExec, and `handler`
// is a ToolHandler-compatible callable.
//
// The registration only takes effect when the translation unit holding it is
// linked in: a static library drops object files nothing references, macro
// included. Put tools in editor/mcp/tools/<group>.cc: that folder is built as
// the mercury_mcp_tools object library, whose objects are linked directly into
// the editor and every editor-linked test, so they are never dropped.
#define MERCURY_MCP_TOOL(name, mode, handler)                              \
  namespace {                                                              \
  const bool MERCURY_MCP_TOOL_CAT(k_mcp_tool_registered_, __LINE__) = [] { \
    ::Mercury::mcp::ToolRegistry::Instance().Register(                     \
        name, ::Mercury::mcp::ToolMode::mode, handler);                    \
    return true;                                                           \
  }();                                                                     \
  }

// The same for an AsyncToolHandler (see its contract above): `handler` is
// callable as handler(EditorScene&, const nlohmann::json&, ToolDone).
#define MERCURY_MCP_TOOL_ASYNC(name, mode, handler)                        \
  namespace {                                                              \
  const bool MERCURY_MCP_TOOL_CAT(k_mcp_tool_registered_, __LINE__) = [] { \
    ::Mercury::mcp::ToolRegistry::Instance().RegisterAsync(                \
        name, ::Mercury::mcp::ToolMode::mode, handler);                    \
    return true;                                                           \
  }();                                                                     \
  }

#define MERCURY_MCP_TOOL_CAT(a, b) MERCURY_MCP_TOOL_CAT_INNER(a, b)
#define MERCURY_MCP_TOOL_CAT_INNER(a, b) a##b

}  // namespace Mercury::mcp

#endif  // MERCURY_EDITOR_MCP_TOOL_REGISTRY_H
