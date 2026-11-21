// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The core tool group: the handshake tools every client sees first. The Rust
// side of this group is mercury/mcp/src/tools/core.rs.
//
// Per-tool-group convention (every group follows it, so packages can add
// tools in parallel without touching each other's files):
//
//   - The Rust spec (name, description, mode, args schema) lives in
//     mercury/mcp/src/tools/<group>.rs. Registering it costs one `pub mod`
//     line and one concat line in `all()` in mercury/mcp/src/tools/mod.rs.
//   - The C++ handlers live in editor/mcp/tools/<group>.cc, registered with
//     MERCURY_MCP_TOOL. The CMake glob (CONFIGURE_DEPENDS) of that folder
//     links every file into the editor and into every editor-linked test
//     executable (the mercury_mcp_tools object library), so a new file needs
//     no CMake edit and no registration list. Shared scene helpers are in
//     mcp/tools/scene_json.h.
//   - Tests live in tests/mcp_tools_<group>_test.cc. CMake globs
//     tests/mcp_tools_*_test.cc into the one mcp_tools_test executable (no
//     CMake edit), and tests/mcp_tools_fixture.h opens a copy of a real
//     project in a headless editor and calls tools through the registry.
//   - A tool that must wait (a rendered frame, a child process) registers
//     with MERCURY_MCP_TOOL_ASYNC and calls `done` later on the UI thread
//     (contract in mcp/tool_registry.h); its Rust spec raises the reply
//     timeout with `.with_timeout_secs(n)` (default 30).
//   - Tool names are snake_case and identical on both sides. The args schema
//     comes from a schemars struct in the Rust file whose fields match what
//     the C++ handler reads from `args`.
//   - Errors are returned as ok = false with {"error": "..."} and, when a
//     close alternative exists, {"suggestion": "..."} (introspection::Suggest).

#include "mcp/tool_registry.h"
#include "version.h"

namespace Mercury {

namespace {

mcp::ToolResult PingTool(EditorScene&, const nlohmann::json&) {
  return mcp::ToolResult{
      true,
      nlohmann::json{{"pong", true}, {"editor_version", MERCURY_VERSION_STRING}},
      {}};
}

}  // namespace

MERCURY_MCP_TOOL("ping", kRead, &PingTool);

}  // namespace Mercury
