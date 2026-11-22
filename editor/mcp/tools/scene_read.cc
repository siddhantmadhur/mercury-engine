// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The scene-read tool group: get_editor_state, get_component_schema,
// find_nodes, get_node and read_console. Every tool only looks. The Rust side
// of this group (names, descriptions, argument schemas) is
// mercury/mcp/src/tools/scene_read.rs; the field names read from `args` here
// match the structs there.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "editor_scene.h"
#include "introspection/introspection.h"
#include "mcp/tool_registry.h"
#include "mcp/tools/scene_json.h"
#include "project/project.h"
#include "scripting/type_registry.h"

namespace Mercury {

namespace {

using mcp::ToolResult;
using nlohmann::json;

constexpr int kDefaultLimit = 100;
constexpr int kDefaultConsoleLimit = 200;
constexpr int kMaxLimit = 1000;

ToolResult Ok(json result) { return ToolResult{true, std::move(result), {}}; }

ToolResult Fail(const std::string& error, const std::string& suggestion = {}) {
  json result{{"error", error}};
  if (!suggestion.empty()) result["suggestion"] = suggestion;
  return ToolResult{false, std::move(result), {}};
}

// The best did-you-mean candidate, "" when none is close.
std::string Closest(const std::string& word,
                    const std::vector<std::string>& candidates) {
  const auto close = introspection::Suggest(word, candidates, 1);
  return close.empty() ? std::string() : close.front();
}

std::string DidYouMean(const std::string& suggestion) {
  return suggestion.empty() ? std::string()
                            : std::format(" (did you mean \"{}\"?)", suggestion);
}

// An optional string argument. False with `error` when present but not a
// string.
bool OptionalString(const json& args, const char* key,
                    std::optional<std::string>* out, std::string* error) {
  out->reset();
  if (!args.contains(key) || args.at(key).is_null()) return true;
  if (!args.at(key).is_string()) {
    *error = std::format("\"{}\" must be a string", key);
    return false;
  }
  *out = args.at(key).get<std::string>();
  return true;
}

bool OptionalBool(const json& args, const char* key, bool fallback, bool* out,
                  std::string* error) {
  *out = fallback;
  if (!args.contains(key) || args.at(key).is_null()) return true;
  if (!args.at(key).is_boolean()) {
    *error = std::format("\"{}\" must be true or false", key);
    return false;
  }
  *out = args.at(key).get<bool>();
  return true;
}

// `limit`: an integer from 1; above kMaxLimit it is clamped to kMaxLimit.
bool ReadLimit(const json& args, int fallback, size_t* out,
               std::string* error) {
  *out = static_cast<size_t>(fallback);
  if (!args.contains("limit") || args.at("limit").is_null()) return true;
  const json& limit = args.at("limit");
  if (!limit.is_number_integer() || limit.get<int64_t>() < 1) {
    *error = "\"limit\" must be an integer from 1 to 1000";
    return false;
  }
  *out = static_cast<size_t>(std::min<int64_t>(limit.get<int64_t>(), kMaxLimit));
  return true;
}

char Lower(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// `*` matches any run of characters (including none), `?` exactly one;
// ASCII case-insensitive.
bool GlobMatch(const std::string& pattern, const std::string& text) {
  size_t p = 0, t = 0;
  size_t star = std::string::npos, star_text = 0;
  while (t < text.size()) {
    if (p < pattern.size() &&
        (pattern[p] == '?' || Lower(pattern[p]) == Lower(text[t]))) {
      p++;
      t++;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      star_text = t;
    } else if (star != std::string::npos) {
      p = star + 1;
      t = ++star_text;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') p++;
  return p == pattern.size();
}

json Vec3Json(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

// ------------------------------------------------------------- get_editor_state

ToolResult GetEditorState(EditorScene& scene, const json&) {
  json out;

  if (auto project = scene.GetProject()) {
    out["project"] = {{"name", project->GetName()},
                      {"root", project->GetRoot().generic_string()},
                      {"main_map", project->GetMainMap()}};
  } else {
    out["project"] = nullptr;
  }
  out["open_map"] = {{"path", scene.GetMapPath()}, {"dirty", scene.IsDirty()}};

  json selection;
  const MapNode* selected = scene.GetSelectedNode();
  selection["node"] =
      selected ? mcp::NodeToJson(scene, *selected, false) : json(nullptr);
  selection["asset"] = scene.GetSelectedAsset().empty()
                           ? json(nullptr)
                           : json(scene.GetSelectedAsset());
  out["selection"] = std::move(selection);

  // The 2D view looks straight along -Z whatever yaw and pitch it kept.
  const bool view_2d = scene.IsView2D();
  const glm::vec3 target = scene.GetCameraTarget();
  const glm::vec3 position = scene.GetCameraPosition();
  out["camera"] = {{"target", Vec3Json(target)},
                   {"position", Vec3Json(position)},
                   {"yaw", view_2d ? 0.0f : scene.GetCameraYaw()},
                   {"pitch", view_2d ? 0.0f : scene.GetCameraPitch()},
                   {"distance", scene.GetCameraDistance()},
                   {"view_2d", view_2d}};

  out["play_session_running"] = scene.PlayRef().IsRunning();
  out["mcp"] = {{"read_only", !scene.Settings().mcp_allow_edits}};

  const Console::Counts counts = scene.ConsoleRef().CountLevels();
  out["console"] = {{"errors", counts.errors},
                    {"warnings", counts.warnings},
                    {"latest_cursor", counts.latest_cursor}};
  return Ok(std::move(out));
}

// ------------------------------------------------------- get_component_schema

ToolResult GetComponentSchema(EditorScene&, const json& args) {
  std::string error;
  std::optional<std::string> type;
  if (!OptionalString(args, "type", &type, &error)) return Fail(error);

  const json schema = json::parse(introspection::EngineSchemaJson());
  const json& components = schema.at("components");
  const json& asset_types = schema.at("asset_types");

  if (!type) {
    json component_list = json::array();
    for (const json& component : components) {
      component_list.push_back({{"name", component.at("name")},
                                {"category", component.at("category")},
                                {"is_script", component.at("is_script")}});
    }
    json asset_list = json::array();
    for (const json& asset_type : asset_types) {
      asset_list.push_back({{"name", asset_type.at("name")},
                            {"description", asset_type.at("description")}});
    }
    return Ok({{"components", std::move(component_list)},
               {"asset_types", std::move(asset_list)}});
  }

  std::vector<std::string> names;
  for (const json& component : components) {
    if (component.at("name") == *type) {
      json entry = component;
      entry["kind"] = "component";
      return Ok(std::move(entry));
    }
    names.push_back(component.at("name").get<std::string>());
  }
  for (const json& asset_type : asset_types) {
    if (asset_type.at("name") == *type) {
      json entry = asset_type;
      entry["kind"] = "asset_type";
      return Ok(std::move(entry));
    }
    names.push_back(asset_type.at("name").get<std::string>());
  }
  const std::string best = Closest(*type, names);
  return Fail(std::format("unknown component or asset type \"{}\"{}", *type,
                          DidYouMean(best)),
              best);
}

// ------------------------------------------------------------------ find_nodes

ToolResult FindNodes(EditorScene& scene, const json& args) {
  std::string error;
  std::optional<std::string> name_glob, component, tag, path_prefix;
  if (!OptionalString(args, "name_glob", &name_glob, &error) ||
      !OptionalString(args, "component", &component, &error) ||
      !OptionalString(args, "tag", &tag, &error) ||
      !OptionalString(args, "path_prefix", &path_prefix, &error)) {
    return Fail(error);
  }
  size_t limit = 0;
  if (!ReadLimit(args, kDefaultLimit, &limit, &error)) return Fail(error);

  json nodes = json::array();
  bool truncated = false;
  size_t matches = 0;
  const Map& map = scene.GetMap();
  map.Visit([&](const MapNode& node) {
    if (truncated) return;
    if (name_glob && !GlobMatch(*name_glob, node.name)) return;
    if (tag && node.tag != *tag) return;
    if (component && !node.FindComponent(*component)) return;
    if (path_prefix && !mcp::FullNodePath(node).starts_with(*path_prefix)) {
      return;
    }
    matches++;
    if (nodes.size() >= limit) {
      truncated = true;
      return;
    }
    nodes.push_back(mcp::NodeToJson(scene, node, false));
  });

  // A filter value no node has and nothing in the project defines is almost
  // certainly a typo: say so instead of returning an empty list.
  if (matches == 0) {
    if (component &&
        !TypeRegistry::Instance().FindComponent(*component)) {
      std::vector<std::string> names;
      for (const ComponentInfo* info : TypeRegistry::Instance().Components()) {
        names.push_back(info->name);
      }
      const std::string best = Closest(*component, names);
      return Fail(std::format("unknown component type \"{}\"{}", *component,
                              DidYouMean(best)),
                  best);
    }
    if (tag) {
      std::vector<std::string> tags;
      if (auto project = scene.GetProject()) tags = project->GetTags();
      if (std::find(tags.begin(), tags.end(), *tag) == tags.end()) {
        const std::string best = Closest(*tag, tags);
        return Fail(std::format("no node has the tag \"{}\", and the project "
                                "does not define it{}",
                                *tag, DidYouMean(best)),
                    best);
      }
    }
  }
  return Ok({{"nodes", std::move(nodes)}, {"truncated", truncated}});
}

// -------------------------------------------------------------------- get_node

ToolResult GetNode(EditorScene& scene, const json& args) {
  if (!args.contains("node")) {
    return Fail("missing \"node\": give {\"uid\": <number or string>} or "
                "{\"path\": \"Root/Child\"}");
  }
  std::string error;
  bool include_components = true, include_children = false;
  if (!OptionalBool(args, "include_components", true, &include_components,
                    &error) ||
      !OptionalBool(args, "include_children", false, &include_children,
                    &error)) {
    return Fail(error);
  }
  std::string suggestion;
  const MapNode* node =
      mcp::ResolveNode(scene, args.at("node"), &error, &suggestion);
  if (!node) return Fail(error, suggestion);

  json out = mcp::NodeToJson(scene, *node, include_components);
  if (include_children) {
    json children = json::array();
    for (const auto& child : node->children) {
      children.push_back({{"name", child->name}, {"uid", child->uid}});
    }
    out["children"] = std::move(children);
  }
  return Ok(std::move(out));
}

// ---------------------------------------------------------------- read_console

const char* LevelName(Console::Level level) {
  switch (level) {
    case Console::Level::kInfo:
      return "info";
    case Console::Level::kWarning:
      return "warning";
    case Console::Level::kError:
      return "error";
  }
  return "info";
}

const char* SourceName(Console::Source source) {
  switch (source) {
    case Console::Source::kEditor:
      return "editor";
    case Console::Source::kBuild:
      return "build";
    case Console::Source::kGame:
      return "game";
    case Console::Source::kLore:
      return "lore";
  }
  return "editor";
}

ToolResult ReadConsole(EditorScene& scene, const json& args) {
  std::string error;
  size_t limit = 0;
  if (!ReadLimit(args, kDefaultConsoleLimit, &limit, &error)) {
    return Fail(error);
  }

  Console::Level min_level = Console::Level::kInfo;
  std::optional<std::string> level_name;
  if (!OptionalString(args, "min_level", &level_name, &error)) {
    return Fail(error);
  }
  if (level_name) {
    if (*level_name == "info") {
      min_level = Console::Level::kInfo;
    } else if (*level_name == "warning") {
      min_level = Console::Level::kWarning;
    } else if (*level_name == "error") {
      min_level = Console::Level::kError;
    } else {
      const std::string best =
          Closest(*level_name, {"info", "warning", "error"});
      return Fail(std::format("\"min_level\" must be \"info\", \"warning\" or "
                              "\"error\", not \"{}\"{}",
                              *level_name, DidYouMean(best)),
                  best);
    }
  }

  Console& console = scene.ConsoleRef();
  std::optional<uint64_t> cursor;
  if (args.contains("cursor") && !args.at("cursor").is_null()) {
    const json& value = args.at("cursor");
    if (!value.is_number_integer() || value.get<int64_t>() < 0) {
      return Fail("\"cursor\" must be a non-negative integer: the "
                  "next_cursor of a previous read_console call");
    }
    cursor = value.get<uint64_t>();
    if (*cursor > console.LatestCursor()) {
      return Fail(std::format(
          "cursor {} is ahead of the console (its latest cursor is {}); it "
          "probably came from an earlier editor session. Omit \"cursor\" to "
          "read the most recent entries",
          *cursor, console.LatestCursor()));
    }
  }

  const Console::Page page = console.ReadFrom(cursor, min_level, limit);
  json entries = json::array();
  for (const Console::Entry& entry : page.entries) {
    entries.push_back({{"seq", entry.seq},
                       {"level", LevelName(entry.line.level)},
                       {"source", SourceName(entry.line.source)},
                       {"text", entry.line.text}});
  }
  return Ok({{"entries", std::move(entries)},
             {"next_cursor", page.next_cursor},
             {"truncated", page.truncated},
             {"dropped", page.dropped}});
}

}  // namespace

MERCURY_MCP_TOOL("get_editor_state", kRead, &GetEditorState);
MERCURY_MCP_TOOL("get_component_schema", kRead, &GetComponentSchema);
MERCURY_MCP_TOOL("find_nodes", kRead, &FindNodes);
MERCURY_MCP_TOOL("get_node", kRead, &GetNode);
MERCURY_MCP_TOOL("read_console", kRead, &ReadConsole);

}  // namespace Mercury
