// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The scene write tool group: apply_scene_operations (a batch of edits as one
// undo step), select_node, save_map, undo and redo. The Rust side (the specs
// and the argument schemas agents read) is mercury/mcp/src/tools/scene_write.rs;
// the field names parsed here are the ones declared there.
//
// How a batch is applied (apply_scene_operations)
//
//   1. Parse and check every operation without touching the map: the shape
//      and unknown fields, component types and fields against the
//      TypeRegistry, values with JsonToPropertyValue (res:// files must
//      exist), prefab files, aliases (an alias must be defined by an earlier
//      operation's "id"), node reference syntax.
//   2. Apply the operations in order to the open map, inside one editor edit
//      (BeginEdit .. MarkChanged .. CommitEdit), after taking a copy of the
//      map. What only the live map can answer (does that path name a node?
//      would that reparent make a cycle? does the node have that component?)
//      fails here; a failure restores the copy, so nothing of the batch
//      remains, and the editor's undo history was never touched. Nothing
//      notifies the editor (MarkChanged) until every operation succeeded.
//   3. On success one MarkChanged records ONE undo step for the whole batch.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "components/components.h"
#include "editor_scene.h"
#include "introspection/introspection.h"
#include "mcp/tool_registry.h"
#include "mcp/tools/scene_json.h"
#include "os/filesystem.h"
#include "project/map.h"
#include "project/prefab_cache.h"
#include "project/project.h"
#include "properties/transform.h"
#include "scripting/reflection.h"
#include "scripting/type_registry.h"

namespace Mercury {

namespace {

using nlohmann::json;
using mcp::ToolResult;

constexpr size_t kMaxOperations = 1000;
// Recomputed transforms (reparent keeping the world transform) are rounded to
// this many steps per unit so float noise does not reach the saved map.
constexpr float kRoundScale = 1.0e5f;
constexpr float kSameEpsilon = 1.0e-4f;

// ------------------------------------------------------------------ Errors --

// What went wrong with one operation, before the index prefix is added.
struct Failure {
  std::string message;
  std::string suggestion;  // the bare did-you-mean candidate, or ""
};

std::string DidYouMean(const std::string& suggestion) {
  return suggestion.empty() ? std::string()
                            : std::format(" (did you mean \"{}\"?)", suggestion);
}

// The best match for `word` among `candidates`, or "".
std::string Closest(const std::string& word,
                    const std::vector<std::string>& candidates) {
  const auto close = introspection::Suggest(word, candidates, 1);
  return close.empty() ? std::string() : close.front();
}

bool Fail(Failure* failure, std::string message, std::string suggestion = {}) {
  failure->message = std::move(message);
  failure->suggestion = std::move(suggestion);
  return false;
}

// An unknown name: "<what> "<name>" ... (did you mean "<best>"?)".
bool FailUnknown(Failure* failure, const std::string& what,
                 const std::string& name,
                 const std::vector<std::string>& candidates) {
  const std::string best = Closest(name, candidates);
  return Fail(failure,
              std::format("unknown {} \"{}\"{}", what, name, DidYouMean(best)),
              best);
}

ToolResult ErrorResult(std::string message, std::string suggestion = {}) {
  json result{{"error", std::move(message)}};
  if (!suggestion.empty()) result["suggestion"] = std::move(suggestion);
  return ToolResult{false, std::move(result), {}};
}

// ----------------------------------------------------------- Parsed model --

enum class OpKind {
  kCreate,
  kInstantiatePrefab,
  kDelete,
  kReparent,
  kSetTransform,
  kSetProperty,
  kAddComponent,
  kRemoveComponent,
};

struct KindName {
  const char* name;
  OpKind kind;
};
constexpr KindName kKinds[] = {
    {"create", OpKind::kCreate},
    {"instantiate_prefab", OpKind::kInstantiatePrefab},
    {"delete", OpKind::kDelete},
    {"reparent", OpKind::kReparent},
    {"set_transform", OpKind::kSetTransform},
    {"set_property", OpKind::kSetProperty},
    {"add_component", OpKind::kAddComponent},
    {"remove_component", OpKind::kRemoveComponent},
};

// The keys each kind accepts (besides "op"); anything else is an error with a
// did-you-mean, so a typo does not silently do nothing.
std::vector<std::string> AllowedKeys(OpKind kind) {
  switch (kind) {
    case OpKind::kCreate:
      return {"name", "parent", "transform", "components", "id"};
    case OpKind::kInstantiatePrefab:
      return {"prefab", "parent", "name", "transform", "id"};
    case OpKind::kDelete:
      return {"node"};
    case OpKind::kReparent:
      return {"node", "new_parent", "keep_world_transform"};
    case OpKind::kSetTransform:
      return {"node", "position", "rotation_euler_degrees", "rotation_quat",
              "scale"};
    case OpKind::kSetProperty:
      return {"node", "component", "field", "value"};
    case OpKind::kAddComponent:
      return {"node", "type", "props"};
    case OpKind::kRemoveComponent:
      return {"node", "type"};
  }
  return {};
}

struct TransformSpec {
  std::optional<glm::vec3> position;
  std::optional<glm::vec3> euler_degrees;
  std::optional<glm::vec3> scale;
  bool Any() const { return position || euler_degrees || scale; }
};

struct PropValue {
  FieldInfo field;
  mtext::Variant value;
};

struct ComponentSpec {
  std::string type;
  std::vector<PropValue> props;
};

struct Operation {
  size_t index = 0;
  OpKind kind = OpKind::kCreate;
  std::string kind_name;
  std::string id;  // alias, "" when none
  json node;       // node references as the client wrote them
  json parent;     // null when absent (the root)
  json new_parent;
  std::string name;
  std::string prefab;
  const Map* prefab_map = nullptr;  // owned by the scene's PrefabCache
  TransformSpec transform;
  std::vector<ComponentSpec> components;  // create
  std::string type;                       // component type for the component ops
  PropValue property;                     // set_property
  bool keep_world_transform = true;
};

// ------------------------------------------------------------ Op parsing ----

bool IsAbsent(const json& object, const char* key) {
  return !object.contains(key) || object.at(key).is_null();
}

bool GetString(const json& object, const char* key, bool required,
               std::string* out, Failure* failure) {
  if (IsAbsent(object, key)) {
    if (required) return Fail(failure, std::format("missing \"{}\"", key));
    return true;
  }
  const json& value = object.at(key);
  if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
    return Fail(failure, std::format("\"{}\" must be a non-empty string", key));
  }
  *out = value.get<std::string>();
  return true;
}

bool CheckKeys(const json& object, const std::vector<std::string>& allowed,
               const std::string& what, Failure* failure) {
  for (const auto& [key, value] : object.items()) {
    if (std::find(allowed.begin(), allowed.end(), key) != allowed.end()) {
      continue;
    }
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

// A node reference: {"uid"}, {"path"} or {"alias"}. `aliases` are the ids
// earlier operations define.
bool CheckRef(const json& ref, const char* field,
              const std::set<std::string>& aliases, Failure* failure) {
  if (ref.is_object() && ref.contains("alias")) {
    if (ref.size() != 1 || !ref.at("alias").is_string() ||
        ref.at("alias").get_ref<const std::string&>().empty()) {
      return Fail(failure,
                  std::format("\"{}\": an alias reference is exactly "
                              "{{\"alias\": \"<id>\"}}, got {}",
                              field, ref.dump()));
    }
    const std::string id = ref.at("alias").get<std::string>();
    if (!aliases.contains(id)) {
      const std::string best =
          Closest(id, std::vector<std::string>(aliases.begin(), aliases.end()));
      return Fail(failure,
                  std::format("\"{}\": alias \"{}\" is not defined by an "
                              "earlier operation (give that operation "
                              "\"id\": \"{}\"){}",
                              field, id, id, DidYouMean(best)),
                  best);
    }
    return true;
  }
  std::string error;
  if (!mcp::NodeSelector::Parse(ref, nullptr, &error)) {
    return Fail(failure, std::format("\"{}\": {}", field, error));
  }
  return true;
}

bool ParseVec3(const json& value, const char* key, glm::vec3* out,
               Failure* failure) {
  FieldInfo field;
  field.name = key;
  field.type = FieldType::kVector3;
  mtext::Variant parsed;
  std::string error;
  if (!mcp::JsonToPropertyValue(field, value, parsed, &error)) {
    return Fail(failure, error);
  }
  *out = *parsed.Get<glm::vec3>();
  return true;
}

// {position, rotation_euler_degrees | rotation_quat, scale}. The quaternion
// is stored as Euler degrees, the way the Transform component holds rotation.
bool ParseTransform(const json& object, TransformSpec* out, Failure* failure) {
  if (!object.is_object()) {
    return Fail(failure, "a transform must be an object like {\"position\": "
                         "[x, y, z], \"rotation_euler_degrees\": [x, y, z], "
                         "\"scale\": [x, y, z]}");
  }
  if (!CheckKeys(object, {"position", "rotation_euler_degrees", "rotation_quat",
                          "scale"},
                 "a transform", failure)) {
    return false;
  }
  TransformSpec spec;
  glm::vec3 vec;
  if (!IsAbsent(object, "position")) {
    if (!ParseVec3(object.at("position"), "position", &vec, failure)) {
      return false;
    }
    spec.position = vec;
  }
  if (!IsAbsent(object, "scale")) {
    if (!ParseVec3(object.at("scale"), "scale", &vec, failure)) return false;
    spec.scale = vec;
  }
  const bool has_euler = !IsAbsent(object, "rotation_euler_degrees");
  const bool has_quat = !IsAbsent(object, "rotation_quat");
  if (has_euler && has_quat) {
    return Fail(failure,
                "give either \"rotation_euler_degrees\" or \"rotation_quat\", "
                "not both");
  }
  if (has_euler) {
    if (!ParseVec3(object.at("rotation_euler_degrees"),
                   "rotation_euler_degrees", &vec, failure)) {
      return false;
    }
    spec.euler_degrees = vec;
  }
  if (has_quat) {
    const json& quat = object.at("rotation_quat");
    bool good = quat.is_array() && quat.size() == 4;
    double v[4] = {0, 0, 0, 0};
    for (size_t i = 0; good && i < 4; i++) {
      good = quat[i].is_number() && std::isfinite(quat[i].get<double>());
      if (good) v[i] = quat[i].get<double>();
    }
    const double length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] +
                                    v[3] * v[3]);
    if (!good) {
      return Fail(failure,
                  "\"rotation_quat\" must be [x, y, z, w] with finite numbers");
    }
    if (length < 1.0e-9) {
      return Fail(failure, "\"rotation_quat\" must not be all zeros");
    }
    // In double precision: near +-90 degrees about Y the conversion amplifies
    // float rounding into visible error. Same convention as
    // Transform::QuatToEuler (glm::eulerAngles, degrees about X, Y, Z).
    const glm::dquat rotation(v[3] / length, v[0] / length, v[1] / length,
                              v[2] / length);
    glm::vec3 degrees(glm::degrees(glm::eulerAngles(rotation)));
    for (int i = 0; i < 3; i++) {
      degrees[i] = std::round(degrees[i] * kRoundScale) / kRoundScale + 0.0f;
    }
    spec.euler_degrees = degrees;
  }
  *out = spec;
  return true;
}

bool FindComponentType(const std::string& type, const ComponentInfo** out,
                       Failure* failure) {
  const ComponentInfo* info = TypeRegistry::Instance().FindComponent(type);
  if (info) {
    *out = info;
    return true;
  }
  std::vector<std::string> names;
  for (const ComponentInfo* component : TypeRegistry::Instance().Components()) {
    names.push_back(component->name);
  }
  return FailUnknown(failure, "component type", type, names);
}

bool FindField(const ComponentInfo& info, const std::string& name,
               const FieldInfo** out, Failure* failure) {
  if (const FieldInfo* field = info.FindField(name)) {
    *out = field;
    return true;
  }
  std::vector<std::string> names;
  for (const FieldInfo& field : info.fields) names.push_back(field.name);
  const std::string best = Closest(name, names);
  std::string list;
  for (const std::string& field : names) {
    list += (list.empty() ? "" : ", ") + field;
  }
  return Fail(failure,
              std::format("component \"{}\" has no field \"{}\"{}{}", info.name,
                          name, DidYouMean(best),
                          list.empty() ? std::string()
                                       : std::format("; its fields are: {}",
                                                     list)),
              best);
}

bool ParseProps(const ComponentInfo& info, const json& props,
                const Project* project, std::vector<PropValue>* out,
                Failure* failure) {
  if (!props.is_object()) {
    return Fail(failure,
                std::format("\"props\" of {} must be an object of field "
                            "values",
                            info.name));
  }
  for (const auto& [key, value] : props.items()) {
    const FieldInfo* field = nullptr;
    if (!FindField(info, key, &field, failure)) return false;
    mtext::Variant parsed;
    std::string error;
    if (!mcp::JsonToPropertyValue(*field, value, parsed, &error, project)) {
      return Fail(failure, std::format("component \"{}\": {}", info.name, error));
    }
    out->push_back(PropValue{*field, std::move(parsed)});
  }
  return true;
}

bool ResExists(const Project& project, const std::string& path) {
  const std::filesystem::path file = project.Resolve(path);
  if (file.empty()) return false;
  if (os::IsPacked(file)) return true;
  std::error_code ec;
  return std::filesystem::exists(file, ec);
}

bool CheckPrefab(EditorScene& scene, const std::string& path, const Map** out,
                 Failure* failure) {
  const std::shared_ptr<Project> project = scene.GetProject();
  if (!project) return Fail(failure, "no project is open");
  if (!IsResPath(path) || !path.ends_with(kPrefabExtension)) {
    return Fail(failure,
                std::format("\"{}\" is not a prefab path like "
                            "\"res://prefabs/crate.mprefab\"",
                            path));
  }
  if (!ResExists(*project, path)) {
    const std::string best = Closest(path, scene.ListProjectFiles({".mprefab"}));
    return Fail(failure,
                std::format("prefab {} does not exist{}", path,
                            DidYouMean(best)),
                best);
  }
  const Map* prefab = scene.GetPrefabCache().Get(path);
  if (!prefab) {
    return Fail(failure, std::format("prefab {} could not be loaded (see the "
                                     "editor console)",
                                     path));
  }
  *out = prefab;
  return true;
}

bool ParseComponents(const json& list, const Project* project,
                     std::vector<ComponentSpec>* out, Failure* failure) {
  if (!list.is_array()) {
    return Fail(failure, "\"components\" must be an array of {\"type\": ..., "
                         "\"props\": {...}}");
  }
  std::set<std::string> seen;
  for (size_t i = 0; i < list.size(); i++) {
    const json& entry = list[i];
    const std::string where = std::format("components[{}]", i);
    if (!entry.is_object()) {
      return Fail(failure, where + " must be an object {\"type\": ...}");
    }
    if (!CheckKeys(entry, {"type", "props"}, where, failure)) return false;
    ComponentSpec spec;
    if (!GetString(entry, "type", true, &spec.type, failure)) {
      failure->message = where + ": " + failure->message;
      return false;
    }
    if (spec.type == "Transform") {
      return Fail(failure,
                  where + ": a node always has its Transform; set it with "
                          "\"transform\" instead");
    }
    if (!seen.insert(spec.type).second) {
      return Fail(failure,
                  std::format("{}: the node would get two \"{}\" components; "
                              "a node holds one of each type",
                              where, spec.type));
    }
    const ComponentInfo* info = nullptr;
    if (!FindComponentType(spec.type, &info, failure)) {
      failure->message = where + ": " + failure->message;
      return false;
    }
    if (!IsAbsent(entry, "props") &&
        !ParseProps(*info, entry.at("props"), project, &spec.props, failure)) {
      failure->message = where + ": " + failure->message;
      return false;
    }
    out->push_back(std::move(spec));
  }
  return true;
}

// Fills `op` from `json_op` (everything that does not need the live map).
bool ParseOperation(EditorScene& scene, const json& json_op, size_t index,
                    std::set<std::string>* aliases,
                    std::map<std::string, size_t>* alias_owner, Operation* op,
                    Failure* failure) {
  const Project* project = scene.GetProject().get();
  op->index = index;
  if (!json_op.is_object()) {
    return Fail(failure, "an operation must be an object like {\"op\": "
                         "\"create\", ...}");
  }
  if (!json_op.contains("op") || !json_op.at("op").is_string()) {
    return Fail(failure, "missing \"op\" (one of create, instantiate_prefab, "
                         "delete, reparent, set_transform, set_property, "
                         "add_component, remove_component)");
  }
  const std::string kind_name = json_op.at("op").get<std::string>();
  op->kind_name = kind_name;
  bool known = false;
  for (const KindName& entry : kKinds) {
    if (kind_name == entry.name) {
      op->kind = entry.kind;
      known = true;
    }
  }
  if (!known) {
    std::vector<std::string> names;
    for (const KindName& entry : kKinds) names.push_back(entry.name);
    return FailUnknown(failure, "operation", kind_name, names);
  }
  std::vector<std::string> allowed = AllowedKeys(op->kind);
  allowed.push_back("op");
  if (!CheckKeys(json_op, allowed, "this operation", failure)) return false;

  auto require_ref = [&](const char* key, json* out) {
    if (IsAbsent(json_op, key)) return Fail(failure, std::format("missing \"{}\"", key));
    if (!CheckRef(json_op.at(key), key, *aliases, failure)) return false;
    *out = json_op.at(key);
    return true;
  };
  auto optional_ref = [&](const char* key, json* out) {
    if (IsAbsent(json_op, key)) return true;
    if (!CheckRef(json_op.at(key), key, *aliases, failure)) return false;
    *out = json_op.at(key);
    return true;
  };
  auto define_alias = [&]() {
    if (!GetString(json_op, "id", false, &op->id, failure)) return false;
    if (op->id.empty()) return true;
    const auto taken = alias_owner->find(op->id);
    if (taken != alias_owner->end()) {
      return Fail(failure,
                  std::format("\"id\" \"{}\" is already used by "
                              "operations[{}]; an alias names one node",
                              op->id, taken->second));
    }
    return true;
  };
  auto publish_alias = [&]() {
    if (op->id.empty()) return;
    aliases->insert(op->id);
    (*alias_owner)[op->id] = index;
  };

  switch (op->kind) {
    case OpKind::kCreate: {
      if (!GetString(json_op, "name", true, &op->name, failure) ||
          !optional_ref("parent", &op->parent) || !define_alias()) {
        return false;
      }
      if (!IsAbsent(json_op, "transform") &&
          !ParseTransform(json_op.at("transform"), &op->transform, failure)) {
        failure->message = "transform: " + failure->message;
        return false;
      }
      if (!IsAbsent(json_op, "components") &&
          !ParseComponents(json_op.at("components"), project, &op->components,
                           failure)) {
        return false;
      }
      publish_alias();
      return true;
    }
    case OpKind::kInstantiatePrefab: {
      if (!GetString(json_op, "prefab", true, &op->prefab, failure) ||
          !GetString(json_op, "name", false, &op->name, failure) ||
          !optional_ref("parent", &op->parent) || !define_alias()) {
        return false;
      }
      if (!CheckPrefab(scene, op->prefab, &op->prefab_map, failure)) return false;
      if (op->name.empty()) {
        op->name = std::filesystem::path(op->prefab).stem().string();
      }
      if (!IsAbsent(json_op, "transform") &&
          !ParseTransform(json_op.at("transform"), &op->transform, failure)) {
        failure->message = "transform: " + failure->message;
        return false;
      }
      publish_alias();
      return true;
    }
    case OpKind::kDelete:
      return require_ref("node", &op->node);
    case OpKind::kReparent: {
      if (!require_ref("node", &op->node) ||
          !require_ref("new_parent", &op->new_parent)) {
        return false;
      }
      if (!IsAbsent(json_op, "keep_world_transform")) {
        const json& keep = json_op.at("keep_world_transform");
        if (!keep.is_boolean()) {
          return Fail(failure, "\"keep_world_transform\" must be true or false");
        }
        op->keep_world_transform = keep.get<bool>();
      }
      return true;
    }
    case OpKind::kSetTransform: {
      if (!require_ref("node", &op->node)) return false;
      json transform = json::object();
      for (const char* key :
           {"position", "rotation_euler_degrees", "rotation_quat", "scale"}) {
        if (!IsAbsent(json_op, key)) transform[key] = json_op.at(key);
      }
      if (!ParseTransform(transform, &op->transform, failure)) return false;
      if (!op->transform.Any()) {
        return Fail(failure,
                    "nothing to change: give at least one of \"position\", "
                    "\"rotation_euler_degrees\", \"rotation_quat\", \"scale\"");
      }
      return true;
    }
    case OpKind::kSetProperty: {
      std::string field_name;
      if (!require_ref("node", &op->node) ||
          !GetString(json_op, "component", true, &op->type, failure) ||
          !GetString(json_op, "field", true, &field_name, failure)) {
        return false;
      }
      if (!json_op.contains("value")) return Fail(failure, "missing \"value\"");
      const ComponentInfo* info = nullptr;
      const FieldInfo* field = nullptr;
      if (!FindComponentType(op->type, &info, failure) ||
          !FindField(*info, field_name, &field, failure)) {
        return false;
      }
      mtext::Variant parsed;
      std::string error;
      if (!mcp::JsonToPropertyValue(*field, json_op.at("value"), parsed, &error,
                                    project)) {
        return Fail(failure, std::format("component \"{}\": {}", info->name,
                                         error));
      }
      op->property = PropValue{*field, std::move(parsed)};
      return true;
    }
    case OpKind::kAddComponent: {
      if (!require_ref("node", &op->node) ||
          !GetString(json_op, "type", true, &op->type, failure)) {
        return false;
      }
      const ComponentInfo* info = nullptr;
      if (!FindComponentType(op->type, &info, failure)) return false;
      ComponentSpec spec;
      spec.type = op->type;
      if (!IsAbsent(json_op, "props") &&
          !ParseProps(*info, json_op.at("props"), project, &spec.props,
                      failure)) {
        return false;
      }
      op->components.push_back(std::move(spec));
      return true;
    }
    case OpKind::kRemoveComponent:
      // The type is checked against the node's own components when applied
      // (a map may carry types the registry does not know, which can still
      // be removed).
      return require_ref("node", &op->node) &&
             GetString(json_op, "type", true, &op->type, failure);
  }
  return true;
}

// -------------------------------------------------------------- Applying ----

// A transform decomposed, for the reparent math below.
struct Trs {
  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 scale{1.0f};
};

Trs LocalTrs(const MapNode& node) {
  TransformComponent transform;
  if (const ComponentData* data = node.FindComponent("Transform")) {
    Reflector reader = Reflector::Read(data->properties);
    transform.Reflect(reader);
  }
  return Trs{transform.position, Transform::EulerToQuat(transform.rotation),
             transform.scale};
}

// The same composition the runtime uses (MapScene::UpdateEntityTransform
// Hierarchy): world position = parent position + parent rotation * (parent
// scale * local position), rotations multiply, scales multiply.
Trs WorldTrs(const MapNode& node) {
  const Trs local = LocalTrs(node);
  if (!node.parent) return local;
  const Trs parent = WorldTrs(*node.parent);
  return Trs{parent.position +
                 parent.rotation * (parent.scale * local.position),
             parent.rotation * local.rotation, parent.scale * local.scale};
}

ComponentData& EnsureTransform(MapNode& node) {
  if (ComponentData* existing = node.FindComponent("Transform")) {
    return *existing;
  }
  node.components.insert(node.components.begin(), ComponentData{"Transform", {}});
  return node.components.front();
}

// Stores a property, dropping it when it equals the field's default (the map
// format leaves fields at their default out; the inspector does the same).
void StoreProperty(ComponentData& component, const std::string& name,
                   const mtext::Variant& value,
                   const mtext::Variant& default_value) {
  if (value == default_value) {
    component.properties.Remove(name);
  } else {
    component.properties.Set(name, value);
  }
}

void StoreVec3(ComponentData& transform, const char* name, glm::vec3 value,
               glm::vec3 default_value) {
  StoreProperty(transform, name, mtext::Variant(value),
                mtext::Variant(default_value));
}

void ApplyTransform(MapNode& node, const TransformSpec& spec) {
  if (!spec.Any()) return;
  ComponentData& transform = EnsureTransform(node);
  if (spec.position) {
    StoreVec3(transform, "position", *spec.position, glm::vec3(0.0f));
  }
  if (spec.euler_degrees) {
    StoreVec3(transform, "rotation", *spec.euler_degrees, glm::vec3(0.0f));
  }
  if (spec.scale) {
    StoreVec3(transform, "scale", *spec.scale, glm::vec3(1.0f));
  }
}

glm::vec3 Round(glm::vec3 value) {
  for (int i = 0; i < 3; i++) {
    value[i] = std::round(value[i] * kRoundScale) / kRoundScale + 0.0f;
  }
  return value;
}

bool Near(glm::vec3 a, glm::vec3 b) {
  return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(kSameEpsilon)));
}

std::string ComponentTypes(const MapNode& node) {
  std::string list;
  for (const ComponentData& component : node.components) {
    list += (list.empty() ? "" : ", ") + component.type;
  }
  return list.empty() ? "none" : list;
}

// One batch being applied to the live map.
class Batch {
 public:
  explicit Batch(EditorScene& scene) : scene_(scene), map_(scene.GetMap()) {}

  bool Apply(const Operation& op, Failure* failure) {
    switch (op.kind) {
      case OpKind::kCreate: return Create(op, failure);
      case OpKind::kInstantiatePrefab: return Instantiate(op, failure);
      case OpKind::kDelete: return Delete(op, failure);
      case OpKind::kReparent: return Reparent(op, failure);
      case OpKind::kSetTransform: return SetTransform(op, failure);
      case OpKind::kSetProperty: return SetProperty(op, failure);
      case OpKind::kAddComponent: return AddComponent(op, failure);
      case OpKind::kRemoveComponent: return RemoveComponent(op, failure);
    }
    return true;
  }

  // The node a reference names now: an alias of this batch, or what
  // ResolveNode finds in the live map (which includes nodes earlier
  // operations created, by uid or path).
  MapNode* Resolve(const json& ref, Failure* failure) {
    if (ref.is_object() && ref.contains("alias")) {
      const std::string id = ref.at("alias").get<std::string>();
      const auto found = aliases_.find(id);
      MapNode* node = found == aliases_.end() ? nullptr
                                              : map_.FindByUid(found->second);
      if (!node) {
        Fail(failure, std::format("alias \"{}\" names a node that an earlier "
                                  "operation deleted",
                                  id));
      }
      return node;
    }
    std::string error;
    std::string suggestion;
    MapNode* node = mcp::ResolveNode(scene_, ref, &error, &suggestion);
    if (!node) Fail(failure, error, suggestion);
    return node;
  }

  // The parent of a create/instantiate: the reference, or the root.
  MapNode* ResolveParent(const json& ref, Failure* failure) {
    return ref.is_null() ? map_.GetRoot() : Resolve(ref, failure);
  }

  json TakeCreated() { return std::move(created_); }
  json TakeWarnings() { return std::move(warnings_); }

 private:
  void Warn(std::string message) { warnings_.push_back(std::move(message)); }

  void NoteCreated(const Operation& op, const MapNode& node) {
    json entry{{"op", op.index},
               {"uid", node.uid},
               {"path", mcp::FullNodePath(node)}};
    if (!op.id.empty()) {
      entry["alias"] = op.id;
      aliases_[op.id] = node.uid;
    }
    created_.push_back(std::move(entry));
    if (node.name != SanitizeNodeName(op.name)) {
      Warn(std::format("operations[{}]: the name \"{}\" was already taken "
                       "under that parent; the node is \"{}\"",
                       op.index, SanitizeNodeName(op.name), node.name));
    } else if (SanitizeNodeName(op.name) != op.name) {
      Warn(std::format("operations[{}]: the name \"{}\" was changed to \"{}\" "
                       "(slashes, quotes and backslashes are removed)",
                       op.index, op.name, node.name));
    }
  }

  void WarnIfFromPrefab(const Operation& op, const MapNode& node,
                        const char* what) {
    if (!node.from_prefab) return;
    const MapNode* root = node.GetPrefabInstanceRoot();
    Warn(std::format(
        "operations[{}]: \"{}\" belongs to the prefab instance \"{}\"; {} is "
        "kept in the open map but a saved map stores a prefab instance's "
        "children from the prefab, so this may not survive a save and reload",
        op.index, mcp::FullNodePath(node),
        root ? mcp::FullNodePath(*root) : std::string("?"), what));
  }

  bool Create(const Operation& op, Failure* failure) {
    MapNode* parent = ResolveParent(op.parent, failure);
    if (!parent) return false;
    MapNode* node = map_.AddNode(parent, op.name);
    node->components.push_back(ComponentData{"Transform", {}});
    ApplyTransform(*node, op.transform);
    for (const ComponentSpec& spec : op.components) {
      ComponentData component{spec.type, {}};
      for (const PropValue& prop : spec.props) {
        StoreProperty(component, prop.field.name, prop.value,
                      prop.field.default_value);
      }
      node->components.push_back(std::move(component));
    }
    NoteCreated(op, *node);
    return true;
  }

  bool Instantiate(const Operation& op, Failure* failure) {
    MapNode* parent = ResolveParent(op.parent, failure);
    if (!parent) return false;
    MapNode* node = map_.AddPrefabInstance(parent, op.name, op.prefab,
                                           *op.prefab_map);
    ApplyTransform(*node, op.transform);
    NoteCreated(op, *node);
    return true;
  }

  bool Delete(const Operation& op, Failure* failure) {
    MapNode* node = Resolve(op.node, failure);
    if (!node) return false;
    if (node->IsRoot()) {
      return Fail(failure, "the root node cannot be deleted");
    }
    WarnIfFromPrefab(op, *node, "the deletion");
    map_.RemoveNode(node);
    return true;
  }

  bool Reparent(const Operation& op, Failure* failure) {
    MapNode* node = Resolve(op.node, failure);
    if (!node) return false;
    MapNode* new_parent = Resolve(op.new_parent, failure);
    if (!new_parent) return false;
    if (node->IsRoot()) return Fail(failure, "the root node cannot be moved");
    if (node == new_parent || node->IsAncestorOf(new_parent)) {
      return Fail(failure,
                  std::format("cannot move \"{}\" under \"{}\": a node cannot "
                              "become its own descendant",
                              mcp::FullNodePath(*node),
                              mcp::FullNodePath(*new_parent)));
    }
    if (node->parent == new_parent) {
      Warn(std::format("operations[{}]: \"{}\" is already a child of \"{}\"; "
                       "nothing changed",
                       op.index, mcp::FullNodePath(*node),
                       mcp::FullNodePath(*new_parent)));
      return true;
    }
    WarnIfFromPrefab(op, *node, "the move");
    const Trs world = WorldTrs(*node);
    const std::string old_name = node->name;
    if (!map_.ReparentNode(node, new_parent)) {
      return Fail(failure, "the node could not be moved");
    }
    if (node->name != old_name) {
      Warn(std::format("operations[{}]: \"{}\" already had a child named "
                       "\"{}\"; the moved node is now \"{}\"",
                       op.index, mcp::FullNodePath(*new_parent), old_name,
                       node->name));
    }
    if (op.keep_world_transform) KeepWorldTransform(*node, world);
    return true;
  }

  // Rewrites the node's local transform so its world transform is `world`
  // under the new parent (the inverse of the runtime's composition, like
  // MapScene::OnWorldTransformChanged).
  void KeepWorldTransform(MapNode& node, const Trs& world) {
    const Trs parent = WorldTrs(*node.parent);
    const glm::vec3 safe(std::abs(parent.scale.x) < 1e-6f ? 1.0f : parent.scale.x,
                         std::abs(parent.scale.y) < 1e-6f ? 1.0f : parent.scale.y,
                         std::abs(parent.scale.z) < 1e-6f ? 1.0f : parent.scale.z);
    const glm::quat inverse = glm::inverse(parent.rotation);
    const glm::vec3 position =
        Round((inverse * (world.position - parent.position)) / safe);
    const glm::quat rotation = inverse * world.rotation;
    const glm::vec3 scale = Round(world.scale / safe);

    const Trs old_local = LocalTrs(node);
    TransformSpec spec;
    if (!Near(position, old_local.position)) spec.position = position;
    if (!Near(scale, old_local.scale)) spec.scale = scale;
    // q and -q are the same rotation.
    if (std::abs(glm::dot(rotation, old_local.rotation)) < 1.0f - 1.0e-6f) {
      spec.euler_degrees = Round(Transform::QuatToEuler(rotation));
    }
    ApplyTransform(node, spec);
  }

  bool SetTransform(const Operation& op, Failure* failure) {
    MapNode* node = Resolve(op.node, failure);
    if (!node) return false;
    ApplyTransform(*node, op.transform);
    return true;
  }

  // The node's component of `type`, or a failure listing what it has.
  ComponentData* FindOnNode(MapNode& node, const std::string& type,
                            Failure* failure) {
    if (ComponentData* component = node.FindComponent(type)) return component;
    std::vector<std::string> types;
    for (const ComponentData& component : node.components) {
      types.push_back(component.type);
    }
    const std::string best = Closest(type, types);
    Fail(failure,
         std::format("node \"{}\" has no \"{}\" component{} (it has: {})",
                     mcp::FullNodePath(node), type, DidYouMean(best),
                     ComponentTypes(node)),
         best);
    return nullptr;
  }

  bool SetProperty(const Operation& op, Failure* failure) {
    MapNode* node = Resolve(op.node, failure);
    if (!node) return false;
    ComponentData* component = FindOnNode(*node, op.type, failure);
    if (!component) {
      failure->message +=
          "; add it first with an add_component operation";
      return false;
    }
    StoreProperty(*component, op.property.field.name, op.property.value,
                  op.property.field.default_value);
    return true;
  }

  bool AddComponent(const Operation& op, Failure* failure) {
    MapNode* node = Resolve(op.node, failure);
    if (!node) return false;
    if (node->FindComponent(op.type)) {
      return Fail(failure,
                  std::format("node \"{}\" already has a \"{}\" component; "
                              "change it with set_property",
                              mcp::FullNodePath(*node), op.type));
    }
    const ComponentSpec& spec = op.components.front();
    ComponentData component{spec.type, {}};
    for (const PropValue& prop : spec.props) {
      StoreProperty(component, prop.field.name, prop.value,
                    prop.field.default_value);
    }
    node->components.push_back(std::move(component));
    return true;
  }

  bool RemoveComponent(const Operation& op, Failure* failure) {
    MapNode* node = Resolve(op.node, failure);
    if (!node) return false;
    ComponentData* component = FindOnNode(*node, op.type, failure);
    if (!component) return false;
    const ComponentInfo* info = TypeRegistry::Instance().FindComponent(op.type);
    if (op.type == "Transform" || (info && !info->removable)) {
      return Fail(failure,
                  std::format("the \"{}\" component cannot be removed", op.type));
    }
    node->components.erase(node->components.begin() +
                           (component - node->components.data()));
    return true;
  }

  EditorScene& scene_;
  Map& map_;
  std::map<std::string, uint64_t> aliases_;
  json created_ = json::array();
  json warnings_ = json::array();
};

ToolResult OperationError(const Operation* op, size_t index,
                          const Failure& failure) {
  const std::string where =
      op ? std::format("operations[{}] ({})", index, op->kind_name)
         : std::format("operations[{}]", index);
  ToolResult result =
      ErrorResult(std::format("{}: {}", where, failure.message),
                  failure.suggestion);
  result.result["operation"] = index;
  result.result["applied"] = 0;
  return result;
}

ToolResult ApplySceneOperations(EditorScene& scene, const json& args) {
  if (!args.is_object()) return ErrorResult("arguments must be an object");
  Failure top;
  if (!CheckKeys(args, {"operations", "select"}, "the arguments", &top)) {
    return ErrorResult(top.message, top.suggestion);
  }
  if (!scene.GetProject()) return ErrorResult("no project is open");
  if (!args.contains("operations") || !args.at("operations").is_array()) {
    return ErrorResult("\"operations\" must be an array of operations");
  }
  const json& list = args.at("operations");
  if (list.empty()) return ErrorResult("\"operations\" is empty: nothing to do");
  if (list.size() > kMaxOperations) {
    return ErrorResult(std::format("too many operations ({}); send at most {} "
                                   "per batch",
                                   list.size(), kMaxOperations));
  }

  // 1. Check every operation, without touching the map.
  std::vector<Operation> operations(list.size());
  std::set<std::string> aliases;
  std::map<std::string, size_t> alias_owner;
  for (size_t i = 0; i < list.size(); i++) {
    Failure failure;
    if (!ParseOperation(scene, list[i], i, &aliases, &alias_owner,
                        &operations[i], &failure)) {
      const bool has_kind = !operations[i].kind_name.empty();
      return OperationError(has_kind ? &operations[i] : nullptr, i, failure);
    }
  }
  json select;
  if (!IsAbsent(args, "select")) {
    Failure failure;
    if (!CheckRef(args.at("select"), "select", aliases, &failure)) {
      return ErrorResult(failure.message, failure.suggestion);
    }
    select = args.at("select");
  }

  // 2. Apply, as one edit. The copy restores the map if the live state makes
  // an operation fail; the editor's history is untouched until the end.
  scene.CommitEdit();  // close any edit still open: the batch is its own step
  scene.BeginEdit();
  const Map snapshot = scene.GetMap();
  Batch batch(scene);
  MapNode* selected = nullptr;
  std::optional<ToolResult> failed;
  try {
    for (const Operation& op : operations) {
      Failure failure;
      if (!batch.Apply(op, &failure)) {
        failed = OperationError(&op, op.index, failure);
        break;
      }
    }
    if (!failed && !select.is_null()) {
      Failure failure;
      selected = batch.Resolve(select, &failure);
      if (!selected) {
        failed = ErrorResult("select: " + failure.message, failure.suggestion);
        failed->result["applied"] = 0;
      }
    }
  } catch (const std::exception& error) {
    failed = ErrorResult(std::string("internal error while applying: ") +
                         error.what());
    failed->result["applied"] = 0;
  }
  if (failed) {
    scene.GetMap() = snapshot;
    scene.CommitEdit();
    return std::move(*failed);
  }

  // 3. One undo step for the whole batch.
  const uint64_t selected_uid = selected ? selected->uid : 0;
  scene.MarkChanged(/*rebuild_scene=*/true);
  scene.CommitEdit();
  scene.NotifyHierarchyChanged();  // the hierarchy panel rebuilds on this
  if (selected_uid != 0) {
    scene.Select(scene.GetMap().FindByUid(selected_uid));
  } else if (scene.GetSelectedUid() != 0 && !scene.GetSelectedNode()) {
    scene.Select(nullptr);  // the selected node was deleted
  }

  json result{{"applied", operations.size()},
              {"created", batch.TakeCreated()},
              {"warnings", batch.TakeWarnings()}};
  if (selected_uid != 0) result["selected"] = scene.GetSelectedUid();
  return ToolResult{true, std::move(result), {}};
}

// ------------------------------------------------------------ The others ----

ToolResult SelectNode(EditorScene& scene, const json& args) {
  if (!args.is_object() || !args.contains("node")) {
    return ErrorResult("missing \"node\": {\"uid\": n} or {\"path\": "
                       "\"Root/Child\"}");
  }
  Failure unknown;
  if (!CheckKeys(args, {"node"}, "the arguments", &unknown)) {
    return ErrorResult(unknown.message, unknown.suggestion);
  }
  std::string error;
  std::string suggestion;
  MapNode* node = mcp::ResolveNode(scene, args.at("node"), &error, &suggestion);
  if (!node) return ErrorResult(error, suggestion);
  scene.Select(node);
  return ToolResult{
      true, json{{"selected", mcp::NodeToJson(scene, *node, false)}}, {}};
}

ToolResult SaveMap(EditorScene& scene, const json&) {
  if (!scene.GetProject()) return ErrorResult("no project is open");
  if (scene.GetMapPath().empty()) {
    return ErrorResult("the open map has no file yet; it cannot be saved "
                       "from here");
  }
  if (!scene.SaveMap()) {
    return ErrorResult("could not save " + scene.GetMapPath() +
                       " (see the editor console)");
  }
  return ToolResult{true, json{{"path", scene.GetMapPath()}}, {}};
}

json HistoryState(const EditorScene& scene) {
  return json{{"can_undo", scene.CanUndo()}, {"can_redo", scene.CanRedo()}};
}

ToolResult UndoTool(EditorScene& scene, const json&) {
  const bool can = scene.CanUndo();
  if (can) scene.Undo();
  json result = HistoryState(scene);
  result["undone"] = can;
  return ToolResult{true, std::move(result), {}};
}

ToolResult RedoTool(EditorScene& scene, const json&) {
  const bool can = scene.CanRedo();
  if (can) scene.Redo();
  json result = HistoryState(scene);
  result["redone"] = can;
  return ToolResult{true, std::move(result), {}};
}

}  // namespace

MERCURY_MCP_TOOL("apply_scene_operations", kWrite, &ApplySceneOperations);
MERCURY_MCP_TOOL("select_node", kRead, &SelectNode);
MERCURY_MCP_TOOL("save_map", kWrite, &SaveMap);
MERCURY_MCP_TOOL("undo", kWrite, &UndoTool);
MERCURY_MCP_TOOL("redo", kWrite, &RedoTool);

}  // namespace Mercury
