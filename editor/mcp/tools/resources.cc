// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The MCP resources, served through the transport's two reserved tool names.
// The Rust transport (mercury/mcp/src/bridge.rs) maps resources/list and
// resources/templates/list to __resources/list and resources/read to
// __resources/read; they are not in the tool catalogue, so a client cannot
// reach them through tools/call. This file is their only owner.
//
//   __resources/list   {} -> {"resources": [{uri, name, mimeType,
//                       description}], "templates": [{uriTemplate, name,
//                       mimeType}]}
//   __resources/read   {"uri": ...} -> {"contents": [{uri, mimeType, text}]},
//                       or ok = false with {"error": ..., "suggestion"?: ...}
//                       (the transport turns that into an MCP error).
//
// Resources:
//   mercury://guides/AGENTS.md | CLAUDE.md | GEMINI.md   text/markdown
//   mercury://project/structure                          application/json
//   mercury://project/schema                             application/json
//   mercury://asset/{+path}  (template)                  application/json

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "editor_scene.h"
#include "introspection/introspection.h"
#include "mcp/tool_registry.h"
#include "mcp/tools/guides.h"
#include "project/project.h"

namespace Mercury::mcp {

namespace {

constexpr std::string_view kGuidePrefix = "mercury://guides/";
constexpr std::string_view kStructureUri = "mercury://project/structure";
constexpr std::string_view kSchemaUri = "mercury://project/schema";
constexpr std::string_view kAssetPrefix = "mercury://asset/";

ToolResult Error(std::string message, const std::string& suggestion = {}) {
  nlohmann::json result{{"error", std::move(message)}};
  if (!suggestion.empty()) result["suggestion"] = suggestion;
  return ToolResult{false, std::move(result), {}};
}

// Decodes %XX escapes (a URI template path may be percent-encoded). nullopt
// for a malformed escape or one that decodes to a control character.
std::optional<std::string> PercentDecode(std::string_view text) {
  std::string out;
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] != '%') {
      out += text[i];
      continue;
    }
    if (i + 2 >= text.size()) return std::nullopt;
    const auto hex = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    const int high = hex(text[i + 1]);
    const int low = hex(text[i + 2]);
    if (high < 0 || low < 0) return std::nullopt;
    const int value = high * 16 + low;
    if (value < 0x20 || value == 0x7f) return std::nullopt;
    out += static_cast<char>(value);
    i += 2;
  }
  return out;
}

nlohmann::json Content(std::string_view uri, std::string_view mime,
                       std::string text) {
  return nlohmann::json{
      {"contents", nlohmann::json::array({nlohmann::json{
                       {"uri", uri}, {"mimeType", mime}, {"text", text}}})}};
}

std::vector<std::string> FixedUris() {
  std::vector<std::string> uris;
  for (const std::string& name : GuideNames()) {
    uris.push_back(std::string(kGuidePrefix) + name);
  }
  uris.emplace_back(kStructureUri);
  uris.emplace_back(kSchemaUri);
  return uris;
}

ToolResult ListResourcesTool(EditorScene&, const nlohmann::json&) {
  nlohmann::json resources = nlohmann::json::array();
  for (const std::string& name : GuideNames()) {
    resources.push_back(
        {{"uri", std::string(kGuidePrefix) + name},
         {"name", name},
         {"mimeType", "text/markdown"},
         {"description",
          "The Mercury guide for AI agents (" + name +
              "), generated from the open project: engine conventions, "
              "workflow, tool catalogue and project facts."}});
  }
  resources.push_back(
      {{"uri", std::string(kStructureUri)},
       {"name", "project-structure"},
       {"mimeType", "application/json"},
       {"description",
        "The open project's files grouped by kind (project file, maps, "
        "prefabs, scripts, ui, input, animators, data_assets, models, "
        "textures, audio) as res:// paths, at most 2000 in all; `truncated` "
        "says when some were left out and `counts` has the true sizes."}});
  resources.push_back(
      {{"uri", std::string(kSchemaUri)},
       {"name", "project-schema"},
       {"mimeType", "application/json"},
       {"description",
        "Every component, script and asset type the engine knows, with their "
        "fields, types, defaults and enum values."}});

  nlohmann::json templates = nlohmann::json::array();
  templates.push_back({{"uriTemplate", std::string(kAssetPrefix) + "{+path}"},
                       {"name", "asset"},
                       {"mimeType", "application/json"}});
  return ToolResult{true,
                    nlohmann::json{{"resources", std::move(resources)},
                                   {"templates", std::move(templates)}},
                    {}};
}

ToolResult ReadAsset(EditorScene& scene, std::string_view encoded_path,
                     std::string_view uri) {
  const auto project = scene.GetProject();
  if (!project) return Error("no project is open in the editor");

  const std::optional<std::string> decoded = PercentDecode(encoded_path);
  if (!decoded || decoded->empty()) {
    return Error("bad asset URI " + std::string(uri) +
                 ": expected mercury://asset/<path inside the project>");
  }
  // mercury://asset/assets/player.masset names res://assets/player.masset; the
  // res:// form is accepted too.
  const std::string res_path =
      IsResPath(*decoded) ? *decoded : std::string(kResPrefix) + *decoded;

  std::string json;
  std::string error;
  if (!introspection::InspectAsset(*project, res_path, &json, &error)) {
    std::string suggestion;
    std::vector<std::string> candidates;
    for (const std::string& file : ListStructureFiles(scene)) {
      candidates.push_back(file.substr(kResPrefix.size()));
    }
    const std::string wanted = res_path.substr(kResPrefix.size());
    const auto suggestions = introspection::Suggest(wanted, candidates, 1);
    if (!suggestions.empty() && suggestions[0] != wanted) {
      suggestion = std::string(kAssetPrefix) + suggestions[0];
    }
    return Error(error, suggestion);
  }
  return ToolResult{
      true, Content(uri, "application/json", std::move(json)), {}};
}

ToolResult ReadResourceTool(EditorScene& scene, const nlohmann::json& args) {
  if (!args.is_object() || !args.contains("uri") || !args["uri"].is_string()) {
    return Error("uri is required: the resource URI to read");
  }
  const std::string uri = args["uri"].get<std::string>();

  if (uri.starts_with(kGuidePrefix)) {
    if (auto guide = BuildGuide(scene, uri.substr(kGuidePrefix.size()))) {
      return ToolResult{
          true, Content(uri, "text/markdown", std::move(*guide)), {}};
    }
  } else if (uri == kStructureUri) {
    if (!scene.GetProject()) return Error("no project is open in the editor");
    return ToolResult{
        true,
        Content(uri, "application/json", ProjectStructureJson(scene).dump()),
        {}};
  } else if (uri == kSchemaUri) {
    return ToolResult{
        true,
        Content(uri, "application/json", introspection::EngineSchemaJson()),
        {}};
  } else if (uri.starts_with(kAssetPrefix)) {
    return ReadAsset(scene, std::string_view(uri).substr(kAssetPrefix.size()),
                     uri);
  }

  const auto suggestions = introspection::Suggest(uri, FixedUris(), 1);
  return Error("resource not found: " + uri,
               suggestions.empty() ? std::string() : suggestions[0]);
}

}  // namespace

MERCURY_MCP_TOOL("__resources/list", kRead, &ListResourcesTool);
MERCURY_MCP_TOOL("__resources/read", kRead, &ReadResourceTool);

}  // namespace Mercury::mcp
