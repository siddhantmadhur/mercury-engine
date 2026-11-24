// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The project authoring tool group: manage_input_actions and
// manage_maps_and_settings. The Rust side (specs and argument schemas) is
// mercury/mcp/src/tools/authoring_project.rs.
//
// Both tools go through the editor's own project APIs rather than inventing
// new file formats: input actions are parsed/serialized with
// input/input_actions.h and their AngelScript header uses the same generated
// code + WriteIfChanged path as the Input Actions window; maps use
// Templates::NewMapText and EditorScene::OpenMap; settings use the same
// Project setters the editor's dialogs use. A batch of input ops is applied
// to a copy of the desc and written only when every op succeeded, like
// apply_scene_operations does for the map.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "editor_scene.h"
#include "input/input_actions.h"
#include "input_actions_codegen_util.h"
#include "introspection/introspection.h"
#include "mcp/tool_registry.h"
#include "os/filesystem.h"
#include "project/map.h"
#include "project/project.h"
#include "templates.h"

namespace Mercury::mcp {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr size_t kMaxOperations = 200;

struct Failure {
  std::string message;
  std::string suggestion;
};

bool Fail(Failure* failure, std::string message, std::string suggestion = {}) {
  failure->message = std::move(message);
  failure->suggestion = std::move(suggestion);
  return false;
}

ToolResult Ok(json result) { return ToolResult{true, std::move(result), {}}; }

ToolResult Error(std::string message, std::string suggestion = {}) {
  json result = {{"error", std::move(message)}};
  if (!suggestion.empty()) result["suggestion"] = std::move(suggestion);
  return ToolResult{false, std::move(result), {}};
}

std::string Closest(const std::string& word,
                    const std::vector<std::string>& candidates) {
  const auto close = introspection::Suggest(word, candidates, 1);
  return close.empty() ? std::string() : close.front();
}

std::string DidYouMean(const std::string& suggestion) {
  return suggestion.empty()
             ? std::string()
             : std::format(" (did you mean \"{}\"?)", suggestion);
}

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

bool CheckKeys(const json& object, const std::vector<std::string>& allowed,
               const std::string& what, Failure* failure) {
  for (const auto& [key, value] : object.items()) {
    if (std::find(allowed.begin(), allowed.end(), key) != allowed.end())
      continue;
    const std::string best = Closest(key, allowed);
    std::string list;
    for (const std::string& name : allowed) {
      list += (list.empty() ? "" : ", ") + name;
    }
    return Fail(failure,
                std::format("unknown field \"{}\" in {}{}; the fields are: {}",
                            key, what, DidYouMean(best), list),
                best);
  }
  return true;
}

bool StringField(const json& object, const char* key, std::string* out,
                 Failure* failure) {
  auto it = object.find(key);
  if (it == object.end())
    return Fail(failure, std::format("missing \"{}\"", key));
  if (!it->is_string())
    return Fail(failure, std::format("\"{}\" must be a string", key));
  *out = it->get<std::string>();
  if (out->empty())
    return Fail(failure, std::format("\"{}\" must not be empty", key));
  return true;
}

bool OptionalBoolField(const json& object, const char* key, bool fallback,
                       bool* out, Failure* failure) {
  *out = fallback;
  auto it = object.find(key);
  if (it == object.end() || it->is_null()) return true;
  if (!it->is_boolean())
    return Fail(failure, std::format("\"{}\" must be true or false", key));
  *out = it->get<bool>();
  return true;
}

bool StringArrayField(const json& object, const char* key,
                      std::vector<std::string>* out, Failure* failure) {
  auto it = object.find(key);
  if (it == object.end())
    return Fail(failure, std::format("missing \"{}\"", key));
  if (!it->is_array())
    return Fail(failure,
                std::format("\"{}\" must be an array of strings", key));
  out->clear();
  for (const auto& value : *it) {
    if (!value.is_string())
      return Fail(failure,
                  std::format("\"{}\" must be an array of strings", key));
    out->push_back(value.get<std::string>());
  }
  return true;
}

bool IsResFile(const Project& project, const std::string& path,
               std::string_view extension) {
  return IsResPath(path) && path.ends_with(extension) &&
         !project.Resolve(path).empty();
}

// --------------------------------------------------------- input action ops --

constexpr std::string_view kActionTypes[] = {"button", "value", "pass_through"};

const std::vector<std::string>& InputOpKinds() {
  static const std::vector<std::string> kinds = {
      "create_file",    "add_map",       "remove_map",
      "add_action",     "remove_action", "add_binding",
      "remove_binding", "add_composite", "set_header"};
  return kinds;
}

enum class OpKind {
  kCreateFile,
  kAddMap,
  kRemoveMap,
  kAddAction,
  kRemoveAction,
  kAddBinding,
  kRemoveBinding,
  kAddComposite,
  kSetHeader,
};

struct Operation {
  OpKind kind = OpKind::kAddMap;
  std::string kind_name;
  std::string path;  // create_file (res://), add/remove_binding (control)
  std::string map;
  std::string name;
  std::string type;        // add_action
  std::string header;      // set_header
  std::string class_name;  // set_header
  bool vector2 = false;
  std::vector<std::string> up, down, left, right;
};

std::string TypeName(InputAction::Type type) {
  switch (type) {
    case InputAction::Type::kButton:
      return "button";
    case InputAction::Type::kValue:
      return "value";
    case InputAction::Type::kPassThrough:
      return "pass_through";
  }
  return "button";
}

InputAction::Type ParseType(const std::string& name) {
  if (name == "value") return InputAction::Type::kValue;
  if (name == "pass_through") return InputAction::Type::kPassThrough;
  return InputAction::Type::kButton;
}

// KnownControlPaths is the picker's list in the Input Actions panel. Accept
// the other device spellings ReadControl understands (<Pointer>,
// <XInputController>, <DualShockGamepad>) as aliases of their canonical path.
std::string NormalizeControlPath(const std::string& path) {
  std::string normalized = Lower(path);
  struct Alias {
    std::string_view from, to;
  };
  constexpr Alias kAliases[] = {{"<pointer>/", "<mouse>/"},
                                {"<xinputcontroller>/", "<gamepad>/"},
                                {"<dualshockgamepad>/", "<gamepad>/"}};
  for (const auto& alias : kAliases) {
    if (normalized.starts_with(alias.from)) {
      normalized = std::string(alias.to) + normalized.substr(alias.from.size());
      break;
    }
  }
  return normalized;
}

std::vector<std::string> AllControlPaths() {
  static const std::vector<std::string> paths = [] {
    std::vector<std::string> out;
    for (std::string_view path : KnownControlPaths()) out.emplace_back(path);
    out.insert(out.end(), {"<Pointer>/delta", "<Pointer>/delta/x",
                           "<Pointer>/delta/y", "<Pointer>/scroll",
                           "<Pointer>/scroll/y", "<Pointer>/position"});
    return out;
  }();
  return paths;
}

bool ValidateControlPath(const std::string& path, Failure* failure) {
  const std::string normalized = NormalizeControlPath(path);
  for (std::string_view known : KnownControlPaths()) {
    if (normalized == Lower(std::string(known))) return true;
  }
  const std::string best = Closest(path, AllControlPaths());
  return Fail(
      failure,
      std::format("unknown control path \"{}\"{}", path, DidYouMean(best)),
      best);
}

const std::vector<std::string>& OpFields(OpKind kind) {
  static const std::vector<std::string> kCreate = {"op", "path"};
  static const std::vector<std::string> kMap = {"op", "map"};
  static const std::vector<std::string> kNamed = {"op", "map", "name"};
  static const std::vector<std::string> kAction = {"op", "map", "name", "type",
                                                   "vector2"};
  static const std::vector<std::string> kBinding = {"op", "map", "name",
                                                    "path"};
  static const std::vector<std::string> kComposite = {
      "op", "map", "name", "up", "down", "left", "right"};
  static const std::vector<std::string> kHeader = {"op", "header",
                                                   "class_name"};
  switch (kind) {
    case OpKind::kCreateFile:
      return kCreate;
    case OpKind::kAddMap:
    case OpKind::kRemoveMap:
      return kMap;
    case OpKind::kRemoveAction:
      return kNamed;
    case OpKind::kAddAction:
      return kAction;
    case OpKind::kAddBinding:
    case OpKind::kRemoveBinding:
      return kBinding;
    case OpKind::kAddComposite:
      return kComposite;
    case OpKind::kSetHeader:
      return kHeader;
  }
  return kMap;
}

bool ParseOperation(const json& entry, size_t index, Operation* op,
                    Failure* failure) {
  const std::string where = std::format("ops[{}]", index);
  if (!entry.is_object()) return Fail(failure, "must be an object");
  std::string kind;
  if (!StringField(entry, "op", &kind, failure)) return false;
  auto kind_it = std::find(InputOpKinds().begin(), InputOpKinds().end(), kind);
  if (kind_it == InputOpKinds().end()) {
    const std::string best = Closest(kind, InputOpKinds());
    return Fail(failure, "unknown op \"" + kind + "\"" + DidYouMean(best),
                best);
  }
  op->kind_name = kind;
  const size_t pos = kind_it - InputOpKinds().begin();
  op->kind = static_cast<OpKind>(pos);
  if (!CheckKeys(entry, OpFields(op->kind), where, failure)) return false;

  auto field = [&](const char* key, std::string* out) {
    return StringField(entry, key, out, failure);
  };
  switch (op->kind) {
    case OpKind::kCreateFile: {
      auto it = entry.find("path");
      if (it != entry.end() && !it->is_null()) {
        if (!it->is_string()) return Fail(failure, "\"path\" must be a string");
        op->path = it->get<std::string>();
        if (op->path.empty())
          return Fail(failure, "\"path\" must not be empty");
      }
      return true;
    }
    case OpKind::kAddMap:
    case OpKind::kRemoveMap:
      return field("map", &op->map);
    case OpKind::kRemoveAction:
      return field("map", &op->map) && field("name", &op->name);
    case OpKind::kAddAction: {
      if (!field("map", &op->map) || !field("name", &op->name) ||
          !field("type", &op->type))
        return false;
      auto type_it =
          std::find(std::begin(kActionTypes), std::end(kActionTypes), op->type);
      if (type_it == std::end(kActionTypes)) {
        const std::string best =
            Closest(op->type, std::vector<std::string>(std::begin(kActionTypes),
                                                       std::end(kActionTypes)));
        return Fail(
            failure,
            "unknown action type \"" + op->type + "\"" + DidYouMean(best),
            best);
      }
      return OptionalBoolField(entry, "vector2", false, &op->vector2, failure);
    }
    case OpKind::kAddBinding:
    case OpKind::kRemoveBinding:
      if (!field("map", &op->map) || !field("name", &op->name) ||
          !field("path", &op->path))
        return false;
      // remove_binding may name a path that is not in the picker any more;
      // existing bindings are checked against the desc when it is applied.
      return op->kind == OpKind::kRemoveBinding ||
             ValidateControlPath(op->path, failure);
    case OpKind::kAddComposite:
      if (!field("map", &op->map) || !field("name", &op->name)) return false;
      for (auto [key, part] :
           {std::pair<const char*, std::vector<std::string>*>{"up", &op->up},
            {"down", &op->down},
            {"left", &op->left},
            {"right", &op->right}}) {
        if (!StringArrayField(entry, key, part, failure)) return false;
        for (const std::string& path : *part) {
          if (!ValidateControlPath(path, failure)) {
            failure->message = std::format("{}: {}", key, failure->message);
            return false;
          }
        }
      }
      return true;
    case OpKind::kSetHeader:
      if (!field("header", &op->header) ||
          !field("class_name", &op->class_name))
        return false;
      // Resolve() is checked against the real project by the tool; validate
      // the shape here so a bad path fails before anything is written.
      if (!IsResPath(op->header) || !op->header.ends_with(".as"))
        return Fail(failure, "header must be a res:// path to an .as file");
      if (!Templates::IsValidIdentifier(op->class_name))
        return Fail(failure, "invalid class name \"" + op->class_name + "\"");
      return true;
  }
  return true;
}

int MapIndex(const InputActionsDesc& desc, const std::string& map) {
  for (size_t i = 0; i < desc.maps.size(); ++i) {
    if (desc.maps[i] == map) return static_cast<int>(i);
  }
  return -1;
}

std::vector<std::string> ActionNames(const InputActionsDesc& desc,
                                     const std::string& map) {
  std::vector<std::string> names;
  for (const auto& action : desc.actions) {
    if (action.map == map) names.push_back(action.name);
  }
  return names;
}

int ActionIndex(const InputActionsDesc& desc, const std::string& map,
                const std::string& name) {
  for (size_t i = 0; i < desc.actions.size(); ++i) {
    if (desc.actions[i].map == map && desc.actions[i].name == name)
      return static_cast<int>(i);
  }
  return -1;
}

bool FindMap(const InputActionsDesc& desc, const std::string& map,
             Failure* failure) {
  if (MapIndex(desc, map) >= 0) return true;
  const std::string best = Closest(map, desc.maps);
  return Fail(failure,
              std::format("unknown map \"{}\"{}", map, DidYouMean(best)), best);
}

int FindAction(const InputActionsDesc& desc, const std::string& map,
               const std::string& name, Failure* failure) {
  if (!FindMap(desc, map, failure)) return -1;
  const int index = ActionIndex(desc, map, name);
  if (index >= 0) return index;
  const std::string best = Closest(name, ActionNames(desc, map));
  Fail(failure,
       std::format("unknown action \"{}/{}\"{}", map, name, DidYouMean(best)),
       best);
  return -1;
}

bool ApplyOperation(const Operation& op, InputActionsDesc* desc,
                    Failure* failure) {
  switch (op.kind) {
    case OpKind::kCreateFile:
      return Fail(failure, "create_file must be the first operation");
    case OpKind::kAddMap:
      if (MapIndex(*desc, op.map) >= 0)
        return Fail(failure, std::format("map \"{}\" already exists", op.map));
      desc->maps.push_back(op.map);
      return true;
    case OpKind::kRemoveMap: {
      if (!FindMap(*desc, op.map, failure)) return false;
      std::erase_if(desc->actions,
                    [&](const InputActionDesc& a) { return a.map == op.map; });
      desc->maps.erase(desc->maps.begin() + MapIndex(*desc, op.map));
      return true;
    }
    case OpKind::kAddAction:
      if (!FindMap(*desc, op.map, failure)) return false;
      if (ActionIndex(*desc, op.map, op.name) >= 0)
        return Fail(failure, std::format("action \"{}/{}\" already exists",
                                         op.map, op.name));
      desc->actions.push_back(
          {op.map, op.name, ParseType(op.type), op.vector2, {}, {}});
      return true;
    case OpKind::kRemoveAction: {
      const int index = FindAction(*desc, op.map, op.name, failure);
      if (index < 0) return false;
      desc->actions.erase(desc->actions.begin() + index);
      return true;
    }
    case OpKind::kAddBinding: {
      const int index = FindAction(*desc, op.map, op.name, failure);
      if (index < 0) return false;
      auto& bindings = desc->actions[index].bindings;
      if (std::find(bindings.begin(), bindings.end(), op.path) !=
          bindings.end())
        return Fail(failure, std::format("binding \"{}\" already exists on "
                                         "\"{}/{}\"",
                                         op.path, op.map, op.name));
      bindings.push_back(op.path);
      return true;
    }
    case OpKind::kRemoveBinding: {
      const int index = FindAction(*desc, op.map, op.name, failure);
      if (index < 0) return false;
      auto& bindings = desc->actions[index].bindings;
      auto it = std::find(bindings.begin(), bindings.end(), op.path);
      if (it == bindings.end()) {
        const std::string best = Closest(op.path, bindings);
        return Fail(failure,
                    std::format("binding \"{}\" does not exist on \"{}/{}\"{}",
                                op.path, op.map, op.name, DidYouMean(best)),
                    best);
      }
      bindings.erase(it);
      return true;
    }
    case OpKind::kAddComposite: {
      const int index = FindAction(*desc, op.map, op.name, failure);
      if (index < 0) return false;
      auto& composites = desc->actions[index].composites;
      InputActionDesc::Composite2D composite{op.up, op.down, op.left, op.right};
      if (std::find(composites.begin(), composites.end(), composite) !=
          composites.end())
        return Fail(failure, std::format("that composite already exists on "
                                         "\"{}/{}\"",
                                         op.map, op.name));
      composites.push_back(std::move(composite));
      return true;
    }
    case OpKind::kSetHeader:
      desc->header = op.header;
      desc->class_name = op.class_name;
      return true;
  }
  return true;
}

json CompactActions(const InputActionsDesc& desc) {
  json actions = json::array();
  for (const auto& action : desc.actions) {
    json entry = {{"map", action.map},
                  {"name", action.name},
                  {"type", TypeName(action.type)},
                  {"bindings", action.bindings}};
    if (action.vector2) entry["vector2"] = true;
    if (!action.composites.empty()) {
      json composites = json::array();
      for (const auto& c : action.composites) {
        composites.push_back(json{{"up", c.up},
                                  {"down", c.down},
                                  {"left", c.left},
                                  {"right", c.right}});
      }
      entry["composites"] = std::move(composites);
    }
    actions.push_back(std::move(entry));
  }
  return actions;
}

json ActionsResult(const std::string& path, const InputActionsDesc& desc,
                   bool header_written,
                   const std::vector<std::string>& errors) {
  json header_errors = json::array();
  for (const auto& error : errors) header_errors.push_back(error);
  return json{{"path", path},
              {"maps", desc.maps},
              {"actions", CompactActions(desc)},
              {"header", desc.header},
              {"class_name", desc.class_name},
              {"header_written", header_written},
              {"header_errors", std::move(header_errors)}};
}

// ----------------------------------------------------- manage_input_actions --

std::string ChooseActionsPath(const Project& project,
                              const std::string& requested) {
  if (!requested.empty()) return requested;
  constexpr const char* kDefault = "res://input/actions.minput";
  if (!fs::exists(project.Resolve(kDefault))) return kDefault;
  for (int i = 2;; ++i) {
    const std::string candidate =
        std::format("res://input/actions{}.minput", i);
    if (!fs::exists(project.Resolve(candidate))) return candidate;
  }
}

ToolResult ManageInputActions(EditorScene& scene, const json& args) {
  if (!args.is_object()) return Error("arguments must be an object");
  Failure top;
  if (!CheckKeys(args, {"ops"}, "the arguments", &top))
    return Error(top.message, top.suggestion);
  auto project = scene.GetProject();
  if (!project) return Error("no project is open");

  std::vector<Operation> ops;
  if (args.contains("ops") && !args.at("ops").is_null()) {
    if (!args.at("ops").is_array())
      return Error("\"ops\" must be an array of operations");
    const auto& list = args.at("ops");
    if (list.size() > kMaxOperations) {
      return Error(std::format("too many operations ({}); send at most {}",
                               list.size(), kMaxOperations));
    }
    for (size_t i = 0; i < list.size(); ++i) {
      Operation op;
      Failure failure;
      if (!ParseOperation(list[i], i, &op, &failure)) {
        json result = {
            {"error", std::format("ops[{}]: {}", i, failure.message)}};
        if (!failure.suggestion.empty())
          result["suggestion"] = failure.suggestion;
        result["operation"] = i;
        return ToolResult{false, std::move(result), {}};
      }
      ops.push_back(std::move(op));
    }
  }

  const bool creating = !ops.empty() && ops.front().kind == OpKind::kCreateFile;
  if (creating && ops.size() > 1 &&
      std::any_of(ops.begin() + 1, ops.end(), [](const Operation& op) {
        return op.kind == OpKind::kCreateFile;
      })) {
    return Error("create_file may appear only once");
  }
  if (!creating && std::any_of(ops.begin(), ops.end(), [](const Operation& op) {
        return op.kind == OpKind::kCreateFile;
      })) {
    return Error("create_file must be the first operation");
  }

  std::string res_path = project->GetInputActionsPath();
  if (creating) {
    if (!res_path.empty())
      return Error("the project already has input actions at " + res_path);
    if (!ops.front().path.empty()) {
      if (!IsResFile(*project, ops.front().path, kInputActionsExtension)) {
        return Error(
            "create_file path must be a res:// path ending in .minput "
            "inside the project");
      }
      res_path = ops.front().path;
    } else {
      res_path = ChooseActionsPath(*project, {});
    }
    if (fs::exists(project->Resolve(res_path)))
      return Error(res_path + " already exists; refusing to overwrite it");
  } else if (res_path.empty()) {
    return Error(
        "the project has no input actions file; use ops [{\"op\": "
        "\"create_file\"}] first");
  }
  const fs::path file = project->Resolve(res_path);
  if (file.empty()) return Error("path escapes the project root: " + res_path);

  // Read the base desc, staging the template in a temp file when create_file
  // is requested: the target must not be written until every op succeeded.
  InputActionsDesc desc;
  fs::path staged;
  if (creating) {
    staged = fs::temp_directory_path() /
             ("mercury_mcp_actions_" +
              std::to_string(std::hash<std::string>{}(res_path)) + "_" +
              std::to_string(reinterpret_cast<uintptr_t>(&scene)) + ".minput");
    if (!Templates::CreateInputActions(staged)) {
      return Error("could not stage the input actions template");
    }
  }
  auto load = [&](const fs::path& source, InputActionsDesc* out) {
    auto text = os::ReadText(source);
    if (!text) return std::string("could not read " + res_path);
    std::string error;
    if (!ParseInputActions(*text, out, &error)) return error;
    return std::string();
  };
  std::string read_error = load(creating ? staged : file, &desc);
  if (creating) {
    std::error_code ignored;
    fs::remove(staged, ignored);
  }
  if (!read_error.empty()) return Error(read_error);

  // Parse-stage errors already covered shape, enum values and control paths;
  // this pass checks this particular desc (maps, actions, duplicates).
  InputActionsDesc working = desc;
  const size_t first = creating ? 1u : 0u;
  for (size_t i = first; i < ops.size(); ++i) {
    Failure failure;
    if (!ApplyOperation(ops[i], &working, &failure)) {
      json result = {{"error", std::format("ops[{}] ({}): {}", i,
                                           ops[i].kind_name, failure.message)}};
      if (!failure.suggestion.empty())
        result["suggestion"] = failure.suggestion;
      result["operation"] = i;
      result["applied"] = 0;
      return ToolResult{false, std::move(result), {}};
    }
  }
  if (ops.empty()) {
    // Read-only: return the parsed desc, without writing anything.
    return Ok(ActionsResult(res_path, working, false, {}));
  }

  // Commit: only now, after every op succeeded, touch the project files.
  if (creating) {
    if (!Templates::CreateInputActions(file)) {
      return Error("could not create " + res_path);
    }
    project->SetInputActionsPath(res_path);
    if (!project->Save()) {
      std::error_code ignored;
      fs::remove(file, ignored);
      return Error("could not save the project file");
    }
  }
  if (!SaveInputActions(file, working))
    return Error("could not write " + res_path);

  const auto header = WriteInputActionsHeader(*project, working, res_path);
  json result = ActionsResult(res_path, working, header.written, header.errors);
  if (!header.errors.empty()) {
    result["error"] =
        "the input actions were saved, but the generated header was not "
        "written; fix the reported name collisions";
    return ToolResult{false, std::move(result), {}};
  }
  const auto& window = scene.InputActionsRef();
  if (window.IsOpen() && window.path() == res_path) {
    result["warning"] = "reopen the Input Actions window";
  }
  return Ok(std::move(result));
}

// ------------------------------------------------ manage_maps_and_settings --

const std::vector<std::string>& MapActions() {
  static const std::vector<std::string> actions = {
      "create_map", "open_map", "set_main_map", "update_settings"};
  return actions;
}

const std::vector<std::string>& UnsupportedSettingKeys() {
  static const std::vector<std::string> keys = {"physics",
                                                "physics.gravity",
                                                "physics.tick_rate",
                                                "physics.queries_hit_triggers",
                                                "layers",
                                                "layers.names",
                                                "tags",
                                                "tags.names"};
  return keys;
}

const std::vector<std::string>& SupportedSettingKeys() {
  static const std::vector<std::string> keys = {
      "name",         "render.anti_aliasing",
      "render.fsr",   "render.fsr_quality",
      "render.scale", "render.fsr_sharpness",
      "input_actions"};
  return keys;
}

bool MapPath(const Project& project, const json& args, std::string* path,
             Failure* failure) {
  if (!StringField(args, "path", path, failure)) return false;
  if (!IsResFile(project, *path, kMapExtension))
    return Fail(failure,
                "path must be a res:// path ending in .mmap inside "
                "the project");
  return true;
}

std::optional<double> NumberField(const json& value, double min, double max,
                                  const std::string& what, Failure* failure) {
  if (!value.is_number()) {
    Fail(failure, what + " must be a number");
    return std::nullopt;
  }
  const double number = value.get<double>();
  if (!std::isfinite(number) || number < min || number > max) {
    Fail(failure, std::format("{} must be between {} and {}", what, min, max));
    return std::nullopt;
  }
  return number;
}

ToolResult CreateMap(EditorScene& scene, const Project& project,
                     const json& args) {
  Failure failure;
  if (!CheckKeys(args, {"action", "path", "template", "open", "discard"},
                 "create_map arguments", &failure))
    return Error(failure.message, failure.suggestion);
  std::string res_path;
  if (!MapPath(project, args, &res_path, &failure))
    return Error(failure.message, failure.suggestion);
  std::string kind;
  if (!StringField(args, "template", &kind, &failure))
    return Error(failure.message, failure.suggestion);
  if (kind != "2d" && kind != "3d") {
    const std::string best = Closest(kind, {"2d", "3d"});
    return Error("template must be \"2d\" or \"3d\"" + DidYouMean(best), best);
  }
  bool open = false, discard = false;
  if (!OptionalBoolField(args, "open", false, &open, &failure) ||
      !OptionalBoolField(args, "discard", false, &discard, &failure))
    return Error(failure.message, failure.suggestion);

  const fs::path file = project.Resolve(res_path);
  if (fs::exists(file))
    return Error(res_path + " already exists; refusing to overwrite it");
  if (open && scene.IsDirty() && !discard)
    return Error("unsaved changes; call save_map first",
                 "call save_map first or pass \"discard\": true");

  const std::string text =
      Templates::NewMapText(file.stem().string(), kind == "3d");
  Map parsed;
  std::string error;
  if (!parsed.LoadFromString(text, &error))
    return Error("the new map template is invalid: " + error);
  if (!os::WriteFileAtomic(file, text))
    return Error("could not write " + res_path);
  json result = {{"path", res_path},
                 {"template", kind},
                 {"root", file.stem().string()},
                 {"opened", false}};
  if (open) {
    if (!scene.OpenMap(res_path)) {
      result["error"] = "the map was created but could not be opened";
      return ToolResult{false, std::move(result), {}};
    }
    result["opened"] = true;
  }
  return Ok(std::move(result));
}

ToolResult OpenMap(EditorScene& scene, const Project& project,
                   const json& args) {
  Failure failure;
  if (!CheckKeys(args, {"action", "path", "discard"}, "open_map arguments",
                 &failure))
    return Error(failure.message, failure.suggestion);
  std::string res_path;
  if (!MapPath(project, args, &res_path, &failure))
    return Error(failure.message, failure.suggestion);
  bool discard = false;
  if (!OptionalBoolField(args, "discard", false, &discard, &failure))
    return Error(failure.message, failure.suggestion);
  if (scene.IsDirty() && !discard)
    return Error("unsaved changes; call save_map first",
                 "call save_map first or pass \"discard\": true");
  const bool was_dirty = scene.IsDirty();
  if (!scene.OpenMap(res_path)) return Error("could not open " + res_path);
  return Ok(json{{"path", res_path}, {"discarded", was_dirty}});
}

ToolResult SetMainMap(EditorScene& scene, Project& project, const json& args) {
  Failure failure;
  if (!CheckKeys(args, {"action", "path"}, "set_main_map arguments", &failure))
    return Error(failure.message, failure.suggestion);
  std::string res_path;
  if (!MapPath(project, args, &res_path, &failure))
    return Error(failure.message, failure.suggestion);
  if (!fs::exists(project.Resolve(res_path)))
    return Error(res_path + " does not exist");
  const std::string previous = project.GetMainMap();
  bool saved = false;
  if (res_path == scene.GetMapPath()) {
    // EditorScene::SetMainMap is the editor wrapper for the open map: it also
    // logs and bumps the map revision. Use it when it applies.
    saved = scene.SetMainMap();
  } else {
    project.SetMainMap(res_path);
    saved = project.Save();
  }
  if (!saved) return Error("could not save the project file");
  return Ok(json{{"path", res_path}, {"previous", previous}});
}

ToolResult UpdateSettings(Project& project, const json& args) {
  Failure failure;
  if (!CheckKeys(args, {"action", "settings"}, "update_settings arguments",
                 &failure))
    return Error(failure.message, failure.suggestion);
  if (!args.contains("settings") || !args.at("settings").is_object())
    return Error("\"settings\" must be an object");

  // Expand the nested render object into the dotted keys the setters use.
  std::vector<std::pair<std::string, json>> values;
  for (const auto& [key, value] : args.at("settings").items()) {
    if (key == "render" && value.is_object()) {
      for (const auto& [child, child_value] : value.items()) {
        values.emplace_back("render." + child, child_value);
      }
    } else {
      values.emplace_back(key, value);
    }
  }

  const auto& supported = SupportedSettingKeys();
  const auto& unsupported = UnsupportedSettingKeys();
  std::vector<std::string> all_keys = supported;
  all_keys.insert(all_keys.end(), unsupported.begin(), unsupported.end());

  // Validate everything first: no setter runs unless the whole object is OK.
  struct Pending {
    std::string key;
    std::function<void()> apply;
  };
  std::vector<Pending> pending;
  for (const auto& [key, value] : values) {
    if (std::find(unsupported.begin(), unsupported.end(), key) !=
        unsupported.end()) {
      return Error(key +
                       " is not supported yet; there is no editor writing "
                       "path for [physics], [layers] or [tags] settings",
                   "unsupported settings: physics.*, layers.*, tags.*");
    }
    if (std::find(supported.begin(), supported.end(), key) == supported.end()) {
      const std::string best = Closest(key, all_keys);
      return Error("unknown setting \"" + key + "\"" + DidYouMean(best), best);
    }
    const std::string what = "\"" + key + "\"";
    if (key == "name") {
      if (!value.is_string() || value.get<std::string>().empty())
        return Error(what + " must be a non-empty string");
      const std::string name = value.get<std::string>();
      pending.push_back({key, [&project, name] { project.SetName(name); }});
    } else if (key == "render.anti_aliasing") {
      if (!value.is_string())
        return Error(what + " must be \"none\", \"fxaa\" or \"taa\"");
      const std::string mode = value.get<std::string>();
      if (mode != "none" && mode != "fxaa" && mode != "taa") {
        const std::string best = Closest(mode, {"none", "fxaa", "taa"});
        return Error(
            "unknown anti_aliasing mode \"" + mode + "\"" + DidYouMean(best),
            best);
      }
      pending.push_back(
          {key, [&project, mode] { project.SetRenderAntiAliasing(mode); }});
    } else if (key == "render.fsr_quality") {
      if (!value.is_string())
        return Error(what + " must be \"high\" or \"ultra\"");
      const std::string quality = value.get<std::string>();
      if (quality != "high" && quality != "ultra") {
        const std::string best = Closest(quality, {"high", "ultra"});
        return Error(
            "unknown fsr_quality \"" + quality + "\"" + DidYouMean(best), best);
      }
      pending.push_back(
          {key, [&project, quality] { project.SetRenderFsrQuality(quality); }});
    } else if (key == "render.fsr") {
      if (!value.is_boolean()) return Error(what + " must be true or false");
      const bool enabled = value.get<bool>();
      pending.push_back(
          {key, [&project, enabled] { project.SetRenderFsr(enabled); }});
    } else if (key == "render.scale" || key == "render.fsr_sharpness") {
      const bool scale = key == "render.scale";
      const auto number =
          NumberField(value, scale ? 0.5 : 0.0, 1.0, what, &failure);
      if (!number) return Error(failure.message);
      const float number_value = static_cast<float>(*number);
      pending.push_back({key, [&project, scale, number_value] {
                           if (scale)
                             project.SetRenderScale(number_value);
                           else
                             project.SetRenderFsrSharpness(number_value);
                         }});
    } else if (key == "input_actions") {
      if (!value.is_string())
        return Error(what + " must be a res:// path to a .minput file");
      const std::string path = value.get<std::string>();
      if (!IsResFile(project, path, kInputActionsExtension))
        return Error(what +
                     " must be a res:// path ending in .minput inside "
                     "the project");
      pending.push_back(
          {key, [&project, path] { project.SetInputActionsPath(path); }});
    }
  }
  if (pending.empty()) return Error("no settings were given");

  json written = json::array();
  for (const auto& entry : pending) {
    entry.apply();
    written.push_back(entry.key);
  }
  if (!project.Save()) return Error("could not save the project file");
  return Ok(json{{"settings_written", std::move(written)}});
}

ToolResult ManageMapsAndSettings(EditorScene& scene, const json& args) {
  if (!args.is_object()) return Error("arguments must be an object");
  auto project = scene.GetProject();
  if (!project) return Error("no project is open");
  std::string action;
  Failure failure;
  if (!StringField(args, "action", &action, &failure)) {
    const std::string best = Closest(action, MapActions());
    return Error(failure.message + DidYouMean(best), best);
  }
  auto it = std::find(MapActions().begin(), MapActions().end(), action);
  if (it == MapActions().end()) {
    const std::string best = Closest(action, MapActions());
    return Error("unknown action \"" + action + "\"" + DidYouMean(best), best);
  }
  if (action == "create_map") return CreateMap(scene, *project, args);
  if (action == "open_map") return OpenMap(scene, *project, args);
  if (action == "set_main_map") return SetMainMap(scene, *project, args);
  return UpdateSettings(*project, args);
}

}  // namespace

MERCURY_MCP_TOOL("manage_input_actions", kWrite, &ManageInputActions);
MERCURY_MCP_TOOL("manage_maps_and_settings", kWrite, &ManageMapsAndSettings);

}  // namespace Mercury::mcp
