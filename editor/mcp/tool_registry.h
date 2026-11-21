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

struct ToolEntry {
  std::string name;
  ToolMode mode = ToolMode::kRead;
  ToolHandler handler;
};

class ToolRegistry {
 public:
  static ToolRegistry& Instance();

  // Called by the MERCURY_MCP_TOOL macro at static initialization.
  void Register(std::string name, ToolMode mode, ToolHandler handler);

  // nullptr when no tool is registered under this name.
  const ToolEntry* Find(const std::string& name) const;

  // Registry lookup, the mode re-check against the editor settings, and the
  // handler itself. Every MCP tool call goes through here, on the UI thread.
  ToolResult Invoke(EditorScene& scene, const std::string& name,
                    const nlohmann::json& args) const;

 private:
  ToolRegistry() = default;

  std::map<std::string, ToolEntry> tools_;
};

// Static tool self-registration, evaluated once when the program starts.
// `name` is a string literal (it may contain anything, e.g.
// "__resources/list"), `mode` is one of kRead, kWrite, kExec, and `handler`
// is a ToolHandler-compatible callable.
//
// The registration only takes effect when the translation unit holding it is
// linked in: a static library drops object files nothing references, macro
// included. Built-in tools live in mcp_host.cc, which editor_scene.cc pulls
// in; keep future tool files referenced by the rest of the editor (or
// force-loaded, as mercury_editor is).
#define MERCURY_MCP_TOOL(name, mode, handler)                                    \
  namespace {                                                                    \
  const bool MERCURY_MCP_TOOL_CAT(k_mcp_tool_registered_, __LINE__) = [] {        \
    ::Mercury::mcp::ToolRegistry::Instance().Register(                            \
        name, ::Mercury::mcp::ToolMode::mode, handler);                           \
    return true;                                                                  \
  }();                                                                            \
  }

#define MERCURY_MCP_TOOL_CAT(a, b) MERCURY_MCP_TOOL_CAT_INNER(a, b)
#define MERCURY_MCP_TOOL_CAT_INNER(a, b) a##b

}  // namespace Mercury::mcp

#endif  // MERCURY_EDITOR_MCP_TOOL_REGISTRY_H
