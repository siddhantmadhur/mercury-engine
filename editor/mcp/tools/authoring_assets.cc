// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The authoring tool group: scaffold_script and create_or_update_asset.
// The Rust side (specs and argument schemas) is
// mercury/mcp/src/tools/authoring_assets.rs.
//
// Both tools use the editor's own authoring paths: scaffold_script calls
// EditorScene::CreateScript (what the New Script dialog calls), and
// create_or_update_asset writes a .masset with SaveAssetFile and refreshes the
// editor's AssetDatabase and asset selection. With "build": true the script
// tool runs the very same build build_scripts runs, through the shared
// TryStartScriptBuild helper, so there is only one kind of script build.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "editor_scene.h"
#include "introspection/introspection.h"
#include "mcp/tool_registry.h"
#include "mcp/tools/assets_build.h"
#include "mcp/tools/scene_json.h"
#include "project/asset_database.h"
#include "project/project.h"
#include "scripting/type_registry.h"
#include "templates.h"

namespace Mercury::mcp {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

ToolResult Error(std::string message, std::string suggestion = {}) {
  json result = {{"error", std::move(message)}};
  if (!suggestion.empty()) result["suggestion"] = std::move(suggestion);
  return ToolResult{false, std::move(result), {}};
}

std::string DidYouMean(const std::string& suggestion) {
  return suggestion.empty()
             ? std::string()
             : std::format(" (did you mean \"{}\"?)", suggestion);
}

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return text;
}

// Exactly the four spellings Templates::ScriptKindFromName recognises. It
// silently maps anything else to AngelScript, so the tool checks the name
// first instead of creating a different kind than the caller asked for.
constexpr std::string_view kScriptKinds[] = {"angel_script", "cpp_behaviour",
                                             "cpp_system", "cpp_editor_system"};

bool IsScriptKind(std::string_view name) {
  return std::find(std::begin(kScriptKinds), std::end(kScriptKinds), name) !=
         std::end(kScriptKinds);
}

// --------------------------------------------------------- scaffold_script --

void ScaffoldScriptTool(EditorScene& scene, const json& args, ToolDone done) {
  auto project = scene.GetProject();
  if (!project) {
    done(Error("no project is open"));
    return;
  }
  if (!args.contains("name") || !args["name"].is_string()) {
    done(Error("name is required and must be a string"));
    return;
  }
  if (!args.contains("kind") || !args["kind"].is_string()) {
    done(Error("kind is required and must be a string"));
    return;
  }
  const std::string name = args["name"].get<std::string>();
  const std::string kind = args["kind"].get<std::string>();
  if (!IsScriptKind(kind)) {
    done(
        Error(std::format("unknown script kind '{}'; expected one of "
                          "angel_script, cpp_behaviour, cpp_system, "
                          "cpp_editor_system",
                          kind)));
    return;
  }
  bool build = false;
  if (args.contains("build") && !args["build"].is_null()) {
    if (!args["build"].is_boolean()) {
      done(Error("build must be true or false"));
      return;
    }
    build = args["build"].get<bool>();
  }

  const Templates::ScriptKind script_kind = Templates::ScriptKindFromName(kind);
  // The New Script dialog uses the same validator; give its message back so
  // an agent sees why the name is unusable, not just "invalid name".
  const std::string reason = Templates::ValidateScriptName(name, script_kind);
  if (!reason.empty()) {
    done(Error(std::format("invalid script name: {}", reason)));
    return;
  }

  const Templates::ScriptPaths paths =
      Templates::GetScriptPaths(project->GetRoot(), script_kind, name);
  std::vector<fs::path> targets;
  if (!paths.header.empty()) targets.push_back(paths.header);
  targets.push_back(paths.source);
  std::error_code ec;
  for (const fs::path& target : targets) {
    // The same symlink-aware check Templates::CreateScript uses before it
    // writes anything: an existing target is never overwritten.
    if (fs::symlink_status(target, ec).type() != fs::file_type::not_found) {
      done(Error(std::format("{} already exists; refusing to overwrite it",
                             project->ToResPath(target))));
      return;
    }
  }

  // The dialog calls the same CreateScript with open_after_create false:
  // opening an editor is a UI concern, not an MCP tool's job.
  if (!scene.CreateScript(script_kind, name, /*open_after_create=*/false)) {
    done(Error(std::format("could not create script '{}'", name)));
    return;
  }
  json created = json::array();
  for (const fs::path& target : targets) {
    created.push_back(project->ToResPath(target));
  }
  json result = {
      {"created", std::move(created)}, {"class_name", name}, {"kind", kind}};
  if (!build) {
    done(ToolResult{true, std::move(result), {}});
    return;
  }

  // The build may take minutes. TryStartScriptBuild calls `done` when it
  // finishes; fold its ok/diagnostics into this call's answer. On a refusal
  // it returns the error instead, which goes through the same fold.
  ToolDone merged = [result = std::move(result),
                     done = std::move(done)](ToolResult build_result) mutable {
    const bool ok = build_result.ok;
    result["ok"] = ok;
    result["build"] = {{"ok", ok},
                       {"diagnostics", build_result.result.value(
                                           "diagnostics", json::array())}};
    if (!ok) {
      result["error"] = build_result.result.value(
          "error", build_result.result.value("message",
                                             std::string("the build failed")));
      if (build_result.result.contains("suggestion")) {
        result["suggestion"] = build_result.result["suggestion"];
      }
    }
    done(ToolResult{ok, std::move(result), {}});
  };
  if (auto error = TryStartScriptBuild(scene, merged)) {
    merged(std::move(*error));
  }
}

// ------------------------------------------------- create_or_update_asset --

ToolResult CreateOrUpdateAssetTool(EditorScene& scene, const json& args) {
  auto project = scene.GetProject();
  if (!project) return Error("no project is open");
  if (!args.contains("path") || !args["path"].is_string()) {
    return Error("path is required and must be a string");
  }
  const std::string res_path = args["path"].get<std::string>();
  if (!IsResPath(res_path)) {
    return Error("path must start with \"res://\": " + res_path);
  }
  if (fs::path(res_path.substr(kResPrefix.size())).extension() !=
      kAssetExtension) {
    return Error("path must end with .masset: " + res_path);
  }
  const fs::path file = project->Resolve(res_path);
  if (file.empty()) {
    return Error("path escapes the project root: " + res_path);
  }
  if (!args.contains("fields") || !args["fields"].is_object()) {
    return Error("fields is required and must be an object");
  }
  const json& fields = args["fields"];
  bool create_only = false;
  if (args.contains("create_only") && !args["create_only"].is_null()) {
    if (!args["create_only"].is_boolean()) {
      return Error("create_only must be true or false");
    }
    create_only = args["create_only"].get<bool>();
  }
  std::string wanted_type;
  if (args.contains("type") && !args["type"].is_null()) {
    if (!args["type"].is_string()) return Error("type must be a string");
    wanted_type = args["type"].get<std::string>();
  }

  std::error_code ec;
  const bool exists = fs::exists(file, ec);
  const TypeRegistry& registry = TypeRegistry::Instance();
  std::vector<std::string> known_types;
  for (const AssetTypeInfo* info : registry.AssetTypes()) {
    known_types.push_back(info->name);
  }
  const AssetTypeInfo* info = nullptr;
  Asset asset;
  if (exists) {
    if (create_only) {
      return Error("asset already exists: " + res_path);
    }
    // The AssetDatabase is the editor's loader and cache; its Load reloads a
    // file that changed on disk, so this is the same snapshot the Inspector
    // would edit.
    const std::shared_ptr<const Asset> loaded = scene.Assets().Load(res_path);
    if (!loaded) return Error("could not load asset: " + res_path);
    asset = *loaded;
    if (!wanted_type.empty() && wanted_type != asset.type) {
      return Error(std::format("asset {} has type '{}', not '{}'", res_path,
                               asset.type, wanted_type));
    }
    info = registry.FindAssetType(asset.type);
  } else {
    if (wanted_type.empty()) {
      return Error("type is required when creating a new asset");
    }
    info = registry.FindAssetType(wanted_type);
    if (!info) {
      // Case-insensitive match first (the same convenience
      // list_project_assets offers), then the usual did-you-mean.
      for (const AssetTypeInfo* candidate : registry.AssetTypes()) {
        if (Lower(candidate->name) == Lower(wanted_type)) {
          info = candidate;
          break;
        }
      }
    }
    if (!info) {
      const auto close = introspection::Suggest(wanted_type, known_types, 1);
      const std::string best = close.empty() ? std::string() : close.front();
      return Error(std::format("unknown asset type '{}'{}", wanted_type,
                               DidYouMean(best)),
                   best);
    }
    asset.type = info->name;
    asset.path = res_path;
    // The same defaults Templates::CreateAsset writes for a new .masset.
    for (const FieldInfo& field : info->fields) {
      asset.properties.Set(field.name, field.default_value);
    }
  }
  if (!info) {
    const auto close = introspection::Suggest(asset.type, known_types, 1);
    const std::string best = close.empty() ? std::string() : close.front();
    return Error(std::format("asset type '{}' is not registered{}", asset.type,
                             DidYouMean(best)),
                 best);
  }

  std::vector<std::string> field_names;
  for (const FieldInfo& field : info->fields) field_names.push_back(field.name);
  std::vector<std::string> written;
  for (auto it = fields.begin(); it != fields.end(); ++it) {
    const std::string& key = it.key();
    const FieldInfo* field = info->FindField(key);
    if (!field) {
      const auto close = introspection::Suggest(key, field_names, 1);
      const std::string best = close.empty() ? std::string() : close.front();
      return Error(std::format("unknown field '{}' for asset type '{}'{}", key,
                               asset.type, DidYouMean(best)),
                   best);
    }
    mtext::Variant value;
    std::string field_error;
    // The scene tools' converter checks the value (including res:// field
    // targets) against the same reflected FieldInfo.
    if (!mcp::JsonToPropertyValue(*field, it.value(), value, &field_error,
                                  project.get())) {
      return Error(field_error);
    }
    asset.properties.Set(field->name, std::move(value));
    written.push_back(field->name);
  }

  std::error_code dir_error;
  fs::create_directories(file.parent_path(), dir_error);
  if (!SaveAssetFile(file, asset)) {
    return Error("could not save asset: " + res_path);
  }
  // ProjectBrowserPanel::HandleCreateAsset selects a new asset so the editor
  // notices it; Forget drops any cached snapshot of the old file. Both work
  // headlessly (they only change revisions) so this tool needs no UI.
  scene.Assets().Forget(res_path);
  scene.SelectAsset(res_path);
  return ToolResult{true,
                    json{{"path", res_path},
                         {"type", asset.type},
                         {"created", !exists},
                         {"fields_written", std::move(written)}},
                    {}};
}

}  // namespace

MERCURY_MCP_TOOL_ASYNC("scaffold_script", kWrite, &ScaffoldScriptTool);
MERCURY_MCP_TOOL("create_or_update_asset", kWrite, &CreateOrUpdateAssetTool);

}  // namespace Mercury::mcp
