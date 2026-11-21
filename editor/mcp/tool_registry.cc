// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "mcp/tool_registry.h"

#include <utility>

#include "editor_scene.h"
#include "editor_settings.h"

namespace Mercury::mcp {

ToolRegistry& ToolRegistry::Instance() {
  static ToolRegistry registry;
  return registry;
}

void ToolRegistry::Register(std::string name, ToolMode mode,
                            ToolHandler handler) {
  // Build the entry first, then key the map on a copy of the name: moving
  // the same string into both would leave one of them empty.
  ToolEntry entry{std::move(name), mode, std::move(handler)};
  tools_.insert_or_assign(entry.name, std::move(entry));
}

const ToolEntry* ToolRegistry::Find(const std::string& name) const {
  const auto it = tools_.find(name);
  return it == tools_.end() ? nullptr : &it->second;
}

ToolResult ToolRegistry::Invoke(EditorScene& scene, const std::string& name,
                                const nlohmann::json& args) const {
  const ToolEntry* entry = Find(name);
  if (entry == nullptr) {
    return ToolResult{
        false, nlohmann::json{{"error", "unknown tool " + name}}, {}};
  }
  // Defence in depth: the Rust transport already got the mode as its
  // read_only flag, but the settings are checked again here, on the UI
  // thread, before any handler runs.
  if (entry->mode != ToolMode::kRead && !scene.Settings().mcp_allow_edits) {
    return ToolResult{
        false,
        nlohmann::json{
            {"error", "tool '" + name +
                          "' changes the editor and edits are not allowed"}},
        {}};
  }
  return entry->handler(scene, args);
}

}  // namespace Mercury::mcp
