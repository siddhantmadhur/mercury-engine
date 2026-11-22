// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "mcp/tools/scene_json.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>
#include <vector>

#include "components/components.h"
#include "editor_scene.h"
#include "introspection/introspection.h"
#include "introspection/variant_json.h"
#include "os/filesystem.h"
#include "project/project.h"
#include "properties/transform.h"

namespace Mercury::mcp {

namespace {

constexpr int kMaxSuggestions = 1;

void SetError(std::string* error, std::string message) {
  if (error) *error = std::move(message);
}

// ---------------------------------------------------------------- Node refs --

constexpr std::string_view kRefForms =
    "a node ref is {\"uid\": <number or string>} or "
    "{\"path\": \"Root/Child/Grandchild\"}";

// Parses a uid given as a JSON number or a decimal string.
bool ParseUid(const nlohmann::json& value, uint64_t* out) {
  if (value.is_number_integer()) {
    if (value.is_number_unsigned()) {
      *out = value.get<uint64_t>();
      return true;
    }
    const int64_t signed_value = value.get<int64_t>();
    if (signed_value < 0) return false;
    *out = static_cast<uint64_t>(signed_value);
    return true;
  }
  if (value.is_string()) {
    const std::string& text = value.get_ref<const std::string&>();
    if (text.empty()) return false;
    uint64_t parsed = 0;
    const char* end = text.data() + text.size();
    auto [stop, ec] = std::from_chars(text.data(), end, parsed);
    if (ec != std::errc() || stop != end) return false;
    *out = parsed;
    return true;
  }
  return false;
}

std::string DidYouMean(const std::string& suggestion) {
  return suggestion.empty()
             ? std::string()
             : std::format(" (did you mean \"{}\"?)", suggestion);
}

std::vector<std::string> AllNodePaths(const Map& map) {
  std::vector<std::string> paths;
  map.Visit([&](const MapNode& node) { paths.push_back(FullNodePath(node)); });
  return paths;
}

std::string Describe(const nlohmann::json& ref) { return ref.dump(); }

// ------------------------------------------------------------------ Values --

// The error text for a bad value, naming the field.
bool Fail(const FieldInfo& field, std::string* error,
          const std::string& message) {
  SetError(error, std::format("field \"{}\": {}", field.name, message));
  return false;
}

bool ParseNumber(const nlohmann::json& json, double* out) {
  if (!json.is_number()) return false;
  *out = json.get<double>();
  return std::isfinite(*out);
}

// An array of `count` finite numbers, or the same inside {"<tag>": [..]}.
bool ParseNumbers(const nlohmann::json& json, const char* tag, size_t count,
                  float* out) {
  const nlohmann::json* array = &json;
  if (json.is_object()) {
    if (json.size() != 1 || !json.contains(tag)) return false;
    array = &json.at(tag);
  }
  if (!array->is_array() || array->size() != count) return false;
  for (size_t i = 0; i < count; i++) {
    double value = 0.0;
    if (!ParseNumber((*array)[i], &value)) return false;
    out[i] = static_cast<float>(value);
  }
  return true;
}

// The string of a {"<tag>": "..."} object or a bare string. Null is "".
bool ParseReference(const nlohmann::json& json, const char* tag,
                    std::string* out) {
  if (json.is_null()) {
    out->clear();
    return true;
  }
  if (json.is_string()) {
    *out = json.get<std::string>();
    return true;
  }
  if (json.is_object() && json.size() == 1 && json.contains(tag) &&
      json.at(tag).is_string()) {
    *out = json.at(tag).get<std::string>();
    return true;
  }
  return false;
}

bool ResFileExists(const Project& project, const std::string& path) {
  const std::filesystem::path file = project.Resolve(path);
  if (file.empty()) return false;  // escapes the project root
  if (os::IsPacked(file)) return true;
  std::error_code ec;
  return std::filesystem::exists(file, ec);
}

// Converts an event call's optional argument, which is a plain scalar.
bool ParseScalar(const nlohmann::json& json, mtext::Variant* out) {
  if (json.is_boolean()) {
    *out = json.get<bool>();
  } else if (json.is_number_integer()) {
    *out = json.get<int64_t>();
  } else if (json.is_number_float()) {
    double value = 0.0;
    if (!ParseNumber(json, &value)) return false;
    *out = value;
  } else if (json.is_string()) {
    *out = json.get<std::string>();
  } else {
    return false;
  }
  return true;
}

bool ParseEventCall(const nlohmann::json& json, mtext::Variant* out) {
  if (!json.is_array() || json.size() < 3 || json.size() > 4) return false;
  std::string target;
  if (!ParseReference(json[0], "path", &target)) return false;
  if (!json[1].is_string() || !json[2].is_string()) return false;
  mtext::Array call{mtext::Variant(mtext::NodePath{target}),
                    mtext::Variant(json[1].get<std::string>()),
                    mtext::Variant(json[2].get<std::string>())};
  if (json.size() == 4) {
    mtext::Variant argument;
    if (!ParseScalar(json[3], &argument)) return false;
    call.push_back(std::move(argument));
  }
  *out = mtext::Variant(std::move(call));
  return true;
}

}  // namespace

// ------------------------------------------------------------ NodeSelector ---

bool NodeSelector::Parse(const nlohmann::json& ref, NodeSelector* out,
                         std::string* error) {
  if (!ref.is_object()) {
    SetError(error,
             std::format("invalid node ref {}: {}", Describe(ref), kRefForms));
    return false;
  }
  const bool has_uid = ref.contains("uid");
  const bool has_path = ref.contains("path");
  if (has_uid == has_path) {
    SetError(error, std::format("invalid node ref {}: give exactly one of "
                                "\"uid\" and \"path\"; {}",
                                Describe(ref), kRefForms));
    return false;
  }
  NodeSelector parsed;
  if (has_uid) {
    uint64_t uid = 0;
    if (!ParseUid(ref.at("uid"), &uid)) {
      SetError(error, std::format("invalid node ref {}: \"uid\" must be a "
                                  "non-negative integer or a decimal string",
                                  Describe(ref)));
      return false;
    }
    parsed.uid = uid;
  } else {
    const nlohmann::json& path = ref.at("path");
    if (!path.is_string() || path.get_ref<const std::string&>().empty()) {
      SetError(error, std::format("invalid node ref {}: \"path\" must be a "
                                  "non-empty string like \"Root/Child\"",
                                  Describe(ref)));
      return false;
    }
    parsed.path = path.get<std::string>();
  }
  if (out) *out = std::move(parsed);
  return true;
}

std::string FullNodePath(const MapNode& node) {
  if (!node.parent) return node.name;
  return FullNodePath(*node.parent) + "/" + node.name;
}

MapNode* ResolveNode(EditorScene& scene, const nlohmann::json& ref,
                     std::string* error, std::string* suggestion) {
  if (suggestion) suggestion->clear();
  NodeSelector parsed;
  if (!NodeSelector::Parse(ref, &parsed, error)) return nullptr;

  Map& map = scene.GetMap();
  if (parsed.uid) {
    if (MapNode* node = map.FindByUid(*parsed.uid)) return node;
    std::vector<std::string> uids;
    map.Visit(
        [&](const MapNode& node) { uids.push_back(std::to_string(node.uid)); });
    const auto close = introspection::Suggest(std::to_string(*parsed.uid), uids,
                                              kMaxSuggestions);
    const std::string best = close.empty() ? std::string() : close.front();
    if (suggestion) *suggestion = best;
    SetError(error,
             std::format("node ref {} not found: no node has uid {}{}",
                         Describe(ref), *parsed.uid,
                         best.empty()
                             ? std::string()
                             : std::format(" (did you mean uid {}?)", best)));
    return nullptr;
  }

  // Full form first ("Root/Child"), then the engine's own ("Child", ".").
  const std::string& path = parsed.path;
  MapNode* root = map.GetRoot();
  if (root) {
    if (path == root->name) return root;
    const std::string prefix = root->name + "/";
    if (path.starts_with(prefix)) {
      if (MapNode* node = map.FindNode(path.substr(prefix.size()))) return node;
    }
  }
  if (path == "." || path.starts_with("./")) {
    if (MapNode* node = map.FindNode(path)) return node;
  } else if (MapNode* node = map.FindNode(path)) {
    return node;
  }

  const auto close =
      introspection::Suggest(path, AllNodePaths(map), kMaxSuggestions);
  const std::string best = close.empty() ? std::string() : close.front();
  if (suggestion) *suggestion = best;
  SetError(error, std::format("node ref {} not found: no node at path \"{}\"{}",
                              Describe(ref), path, DidYouMean(best)));
  return nullptr;
}

// ---------------------------------------------------------------- NodeToJson
// --

nlohmann::json NodeToJson(EditorScene& scene, const MapNode& node,
                          bool include_components) {
  (void)scene;  // reserved: selection and live-entity state may join later
  nlohmann::json out;
  out["name"] = node.name;
  out["uid"] = node.uid;
  out["path"] = FullNodePath(node);
  out["node_path"] = node.GetPath();
  out["parent_uid"] =
      node.parent ? nlohmann::json(node.parent->uid) : nlohmann::json(nullptr);
  out["child_count"] = node.children.size();

  TransformComponent transform;
  if (const ComponentData* data = node.FindComponent("Transform")) {
    Reflector reader = Reflector::Read(data->properties);
    transform.Reflect(reader);
  }
  const glm::quat rotation = Transform::EulerToQuat(transform.rotation);
  out["transform"] = {
      {"position",
       nlohmann::json::array(
           {transform.position.x, transform.position.y, transform.position.z})},
      {"rotation",
       nlohmann::json::array({rotation.x, rotation.y, rotation.z, rotation.w})},
      {"euler_degrees",
       nlohmann::json::array(
           {transform.rotation.x, transform.rotation.y, transform.rotation.z})},
      {"scale", nlohmann::json::array(
                    {transform.scale.x, transform.scale.y, transform.scale.z})},
  };

  if (include_components) {
    nlohmann::json components = nlohmann::json::array();
    for (const ComponentData& component : node.components) {
      nlohmann::json props = nlohmann::json::object();
      for (const mtext::Property& property : component.properties) {
        props[property.key] = introspection::VariantToJson(property.value);
      }
      components.push_back({{"type", component.type}, {"props", props}});
    }
    out["components"] = std::move(components);
  }
  return out;
}

// --------------------------------------------------------- JsonToPropertyValue
// --

bool JsonToPropertyValue(const FieldInfo& field, const nlohmann::json& json,
                         mtext::Variant& out, std::string* error,
                         const Project* project) {
  mtext::Variant value;
  switch (field.type) {
    case FieldType::kBool:
      if (!json.is_boolean())
        return Fail(field, error, "expected true or false");
      value = json.get<bool>();
      break;
    case FieldType::kInt: {
      if (json.is_number_integer()) {
        if (json.is_number_unsigned() &&
            json.get<uint64_t>() >
                static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
          return Fail(field, error, "integer is out of range");
        }
        value = json.get<int64_t>();
        break;
      }
      double number = 0.0;
      // 3.0 is fine, 3.5 is not; the range check keeps the cast defined.
      if (ParseNumber(json, &number) && number == std::floor(number) &&
          std::fabs(number) < 9.0e18) {
        value = static_cast<int64_t>(number);
        break;
      }
      return Fail(field, error, "expected an integer");
    }
    case FieldType::kFloat: {
      double number = 0.0;
      if (!ParseNumber(json, &number)) {
        return Fail(field, error, "expected a finite number");
      }
      value = number;
      break;
    }
    case FieldType::kString:
      if (!json.is_string()) return Fail(field, error, "expected a string");
      value = json.get<std::string>();
      break;
    case FieldType::kEnum: {
      if (!json.is_string()) {
        return Fail(field, error, "expected one of the enum's strings");
      }
      const std::string text = json.get<std::string>();
      if (!field.enum_values.empty() &&
          std::find(field.enum_values.begin(), field.enum_values.end(), text) ==
              field.enum_values.end()) {
        std::string allowed;
        for (const std::string& name : field.enum_values) {
          allowed += (allowed.empty() ? "\"" : ", \"") + name + "\"";
        }
        const auto close =
            introspection::Suggest(text, field.enum_values, kMaxSuggestions);
        return Fail(
            field, error,
            std::format("\"{}\" is not one of {}{}", text, allowed,
                        DidYouMean(close.empty() ? "" : close.front())));
      }
      value = text;
      break;
    }
    case FieldType::kVector2: {
      float v[2];
      if (!ParseNumbers(json, "vec2", 2, v)) {
        return Fail(field, error, "expected [x, y] with finite numbers");
      }
      value = glm::vec2(v[0], v[1]);
      break;
    }
    case FieldType::kVector3: {
      float v[3];
      if (!ParseNumbers(json, "vec3", 3, v)) {
        return Fail(field, error, "expected [x, y, z] with finite numbers");
      }
      value = glm::vec3(v[0], v[1], v[2]);
      break;
    }
    case FieldType::kColor: {
      float v[4];
      if (!ParseNumbers(json, "color", 4, v)) {
        return Fail(field, error, "expected [r, g, b, a] with finite numbers");
      }
      value = mtext::Color{glm::vec4(v[0], v[1], v[2], v[3])};
      break;
    }
    case FieldType::kNodeRef: {
      std::string path;
      if (!ParseReference(json, "path", &path)) {
        return Fail(field, error,
                    "expected {\"path\": \"Child/Grandchild\"} (or a string)");
      }
      value = mtext::NodePath{path};
      break;
    }
    case FieldType::kAsset: {
      std::string path;
      if (!ParseReference(json, "res", &path)) {
        return Fail(field, error,
                    "expected {\"res\": \"res://path\"} (or a string)");
      }
      if (!path.empty()) {
        if (!IsResPath(path)) {
          return Fail(field, error,
                      std::format("\"{}\" is not a res:// path", path));
        }
        if (project && !ResFileExists(*project, path)) {
          return Fail(field, error,
                      std::format("referenced file {} does not exist", path));
        }
      }
      value = mtext::Resource{path};
      break;
    }
    case FieldType::kList: {
      if (!json.is_array()) return Fail(field, error, "expected an array");
      if (field.element_type == FieldType::kList ||
          field.element_type == FieldType::kEvent) {
        return Fail(field, error, "lists of lists or events are not supported");
      }
      FieldInfo element = field;
      element.type = field.element_type;
      mtext::Array items;
      for (size_t i = 0; i < json.size(); i++) {
        mtext::Variant item;
        std::string item_error;
        if (!JsonToPropertyValue(element, json[i], item, &item_error,
                                 project)) {
          // item_error already starts with the field name; add the index.
          SetError(error,
                   std::format("field \"{}\"[{}]:{}", field.name, i,
                               item_error.substr(item_error.find(':') + 1)));
          return false;
        }
        items.push_back(std::move(item));
      }
      value = std::move(items);
      break;
    }
    case FieldType::kEvent: {
      if (!json.is_array())
        return Fail(field, error, "expected an array of calls");
      mtext::Array calls;
      for (size_t i = 0; i < json.size(); i++) {
        mtext::Variant call;
        if (!ParseEventCall(json[i], &call)) {
          return Fail(field, error,
                      std::format("call {} must be [{{\"path\": ..}}, "
                                  "\"Component\", \"method\" [, argument]]",
                                  i));
        }
        calls.push_back(std::move(call));
      }
      value = std::move(calls);
      break;
    }
  }
  out = std::move(value);
  return true;
}

}  // namespace Mercury::mcp
