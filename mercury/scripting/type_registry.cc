// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "type_registry.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "debugger/debugger.h"
#include "scene/map_scene.h"

namespace Mercury {

namespace {

// A snapshot is the component's properties written as a one-section mtext
// document. That keeps the byte form readable and lets a snapshot be edited
// with the same tools as a map.
bool EncodeProperties(const mtext::PropertyList& properties,
                      std::vector<uint8_t>& out) {
  mtext::Document document;
  document.Add("component").properties = properties;
  const std::string text = mtext::Write(document);
  out.assign(text.begin(), text.end());
  return true;
}

bool DecodeProperties(std::span<const uint8_t> bytes,
                      mtext::PropertyList& out) {
  if (bytes.empty()) return false;
  const std::string text(reinterpret_cast<const char*>(bytes.data()),
                         bytes.size());
  mtext::Document document;
  mtext::ParseError error;
  if (!mtext::Parse(text, document, &error)) return false;
  const mtext::Section* section = document.Find("component");
  if (!section) return false;
  out = section->properties;
  return true;
}

// Rejects NaN and infinity in the numeric field types a snapshot can carry,
// including lists. Vector3(1e300, ...) narrows to inf, and a bare `inf` is a
// valid scalar literal, so both must be checked.
bool IsFiniteValue(const mtext::Variant& value) {
  switch (value.GetType()) {
    case mtext::Variant::Type::kFloat: {
      const double* number = value.Get<double>();
      return number && std::isfinite(*number);
    }
    case mtext::Variant::Type::kVector2: {
      const glm::vec2* vector = value.Get<glm::vec2>();
      return vector && std::isfinite(vector->x) && std::isfinite(vector->y);
    }
    case mtext::Variant::Type::kVector3: {
      const glm::vec3* vector = value.Get<glm::vec3>();
      return vector && std::isfinite(vector->x) && std::isfinite(vector->y) &&
             std::isfinite(vector->z);
    }
    case mtext::Variant::Type::kColor: {
      const mtext::Color* color = value.Get<mtext::Color>();
      if (!color) return true;
      return std::isfinite(color->rgba.r) && std::isfinite(color->rgba.g) &&
             std::isfinite(color->rgba.b) && std::isfinite(color->rgba.a);
    }
    case mtext::Variant::Type::kArray: {
      const mtext::Array* array = value.Get<mtext::Array>();
      if (!array) return true;
      for (const mtext::Variant& item : *array) {
        if (!IsFiniteValue(item)) return false;
      }
      return true;
    }
    default:
      return true;
  }
}

}  // namespace

const FieldInfo* ComponentInfo::FindField(std::string_view field) const {
  for (const auto& info : fields) {
    if (info.name == field) return &info;
  }
  return nullptr;
}

const FieldInfo* AssetTypeInfo::FindField(std::string_view field) const {
  for (const auto& info : fields) {
    if (info.name == field) return &info;
  }
  return nullptr;
}

TypeRegistry& TypeRegistry::Instance() {
  static TypeRegistry registry;
  return registry;
}

void TypeRegistry::RegisterComponent(ComponentInfo info) {
  for (auto& existing : components_) {
    if (existing.name == info.name) {
      existing = std::move(info);
      return;
    }
  }
  components_.push_back(std::move(info));
}

void TypeRegistry::RegisterAssetType(AssetTypeInfo info) {
  for (auto& existing : asset_types_) {
    if (existing.name == info.name) {
      existing = std::move(info);
      return;
    }
  }
  asset_types_.push_back(std::move(info));
}

const ComponentInfo* TypeRegistry::FindComponent(std::string_view name) const {
  for (const auto& info : components_) {
    if (info.name == name) return &info;
  }
  return nullptr;
}

const AssetTypeInfo* TypeRegistry::FindAssetType(std::string_view name) const {
  for (const auto& info : asset_types_) {
    if (info.name == name) return &info;
  }
  return nullptr;
}

std::vector<const ComponentInfo*> TypeRegistry::Components() const {
  std::vector<const ComponentInfo*> list;
  for (const auto& info : components_) list.push_back(&info);
  std::stable_sort(list.begin(), list.end(), [](auto a, auto b) {
    if (a->is_script != b->is_script) return !a->is_script;
    if (a->category != b->category) return a->category < b->category;
    return a->name < b->name;
  });
  return list;
}

std::vector<const AssetTypeInfo*> TypeRegistry::AssetTypes() const {
  std::vector<const AssetTypeInfo*> list;
  for (const auto& info : asset_types_) list.push_back(&info);
  return list;
}

void TypeRegistry::ClearSchemaScripts() {
  std::erase_if(components_, [](const ComponentInfo& info) {
    return info.is_script && !info.attach;
  });
}

void TypeRegistry::ClearGameTypes() {
  // AngelScript classes are rebuilt by the next LoadScripts. Native scripts
  // (MERCURY_SCRIPT) are linked into the executable and must stay.
  std::erase_if(components_, [](const ComponentInfo& info) {
    return info.is_script && info.attach && !info.native_script;
  });
  std::erase_if(asset_types_, [](const AssetTypeInfo& info) {
    return info.from_game && !info.native_asset;
  });
}

bool TypeRegistry::SerializeComponent(MapScene& scene, kEntityId entity,
                                      std::string_view type,
                                      mtext::PropertyList& out) const {
  const ComponentInfo* info = FindComponent(type);
  if (!info || !info->get_field) return false;
  out.clear();
  for (const FieldInfo& field : info->fields) {
    mtext::Variant value;
    if (info->get_field(scene, entity, field.name, &value)) {
      out.Set(field.name, std::move(value));
    }
  }
  return true;
}

bool TypeRegistry::DeserializeComponent(
    MapScene& scene, kEntityId entity, std::string_view type,
    const mtext::PropertyList& in,
    const ComponentDeserializeOptions& options) const {
  const ComponentInfo* info = FindComponent(type);
  if (!info || !info->set_field) return false;

  size_t applied = 0;
  for (const auto& property : in) {
    const FieldInfo* field = info->FindField(property.key);
    if (!field) continue;
    if (options.require_runtime_writable && !field->runtime_writable) continue;
    if (options.reject_non_finite && !IsFiniteValue(property.value)) {
      PDebug::warn("{} . {}: rejected a non-finite value", type, property.key);
      continue;
    }
    if (info->set_field(scene, entity, property.key, property.value)) {
      applied++;
    }
  }
  // A snapshot that changed nothing is a failed apply, not a silent success.
  return applied > 0;
}

bool TypeRegistry::SerializeComponent(MapScene& scene, kEntityId entity,
                                      std::string_view type,
                                      std::vector<uint8_t>& out) const {
  mtext::PropertyList properties;
  if (!SerializeComponent(scene, entity, type, properties)) return false;
  return EncodeProperties(properties, out);
}

bool TypeRegistry::DeserializeComponent(MapScene& scene, kEntityId entity,
                                        std::string_view type,
                                        std::span<const uint8_t> in) const {
  mtext::PropertyList properties;
  if (!DecodeProperties(in, properties)) return false;
  return DeserializeComponent(scene, entity, type, properties,
                              ComponentDeserializeOptions{});
}

bool TypeRegistry::SaveSchema(const std::filesystem::path& path) const {
  mtext::Document document;
  document.Add("mschema").attributes.Set("format", 2);
  for (const auto* info : Components()) {
    // Scripts and game-registered data components alike: the editor never runs
    // game code, so this file is the only way a game's custom types reach it.
    // Engine built-ins are left out; the editor already has them.
    if (!info->is_script && !info->from_game) continue;
    auto& script = document.Add("script");
    script.attributes.Set("name", info->name);
    // Every field is one [[field]] entry carrying its name, default value
    // and details. One representation, and file order is declaration order:
    // the reader sorts table keys, so fields cannot live in [script.props].
    for (const auto& field : info->fields) {
      auto& section = document.Add("field");
      section.attributes.Set("script", info->name);
      section.attributes.Set("name", field.name);
      section.properties.Set("default", field.default_value);
      if (field.type == FieldType::kEnum) {
        mtext::Array values;
        for (const auto& v : field.enum_values) values.emplace_back(v);
        section.properties.Set("enum", values);
      }
      if ((field.type == FieldType::kAsset || field.type == FieldType::kList) &&
          !field.asset_type.empty()) {
        section.properties.Set("asset_type", field.asset_type);
      }
      if (field.type == FieldType::kList) {
        section.properties.Set("element", FieldTypeName(field.element_type));
      }
      if (field.type == FieldType::kEvent)
        section.properties.Set("event", true);
      if (!field.tooltip.empty()) {
        section.properties.Set("tooltip", field.tooltip);
      }
    }
    for (const auto& method : info->methods) {
      auto& section = document.Add("method");
      section.attributes.Set("script", info->name);
      section.attributes.Set("name", method.name);
      section.attributes.Set("arg", method.arg);
    }
  }
  for (const auto& asset : asset_types_) {
    if (!asset.from_game) continue;
    auto& section = document.Add("asset");
    section.attributes.Set("name", asset.name);
    if (!asset.description.empty()) {
      section.attributes.Set("description", asset.description);
    }
    for (const auto& field : asset.fields) {
      section.properties.Set(field.name, field.default_value);
    }
  }
  return mtext::SaveFile(path, document);
}

bool TypeRegistry::LoadSchema(const std::filesystem::path& path,
                              std::string* error) {
  mtext::Document document;
  mtext::ParseError parse_error;
  if (!mtext::LoadFile(path, document, &parse_error)) {
    if (error) *error = path.string() + ":" + parse_error.ToString();
    return false;
  }
  ClearSchemaScripts();
  // First pass: [[script]] declares the component. Its fields are not read
  // here: every field is a [[field]] entry below, in file order, and table
  // keys are sorted so [script.props] could not keep declaration order.
  for (const auto& section : document.sections) {
    if (section.tag != "script") continue;
    auto name = section.attributes.GetString("name");
    // A script compiled into this process wins over the schema.
    if (auto existing = FindComponent(name); existing && existing->attach) {
      continue;
    }
    ComponentInfo info;
    info.name = name;
    info.category = "Scripts";
    info.is_script = true;
    RegisterComponent(std::move(info));
  }
  // Second pass: [[field]] entries, in file order, carry the name, the
  // default value and the details of one field each.
  for (const auto& section : document.sections) {
    if (section.tag != "field") continue;
    auto script = section.attributes.GetString("script");
    auto it = std::find_if(components_.begin(), components_.end(),
                           [&](auto& c) { return c.name == script; });
    if (it == components_.end() || it->attach) continue;
    FieldInfo field;
    field.name = section.attributes.GetString("name");
    if (auto value = section.properties.Get("default")) {
      field.default_value = *value;
    }
    field.type = FieldTypeOf(field.default_value);
    if (auto values = section.properties.Get("enum")) {
      field.type = FieldType::kEnum;
      if (auto array = values->Get<mtext::Array>()) {
        for (const auto& v : *array) {
          if (auto s = v.ToString()) field.enum_values.push_back(*s);
        }
      }
    }
    field.asset_type = section.properties.GetString("asset_type");
    field.tooltip = section.properties.GetString("tooltip");
    if (section.properties.GetBool("event", false))
      field.type = FieldType::kEvent;
    if (auto element = section.properties.Get("element")) {
      field.type = FieldType::kList;
      const std::string name = element->ToString().value_or("");
      for (FieldType t :
           {FieldType::kBool, FieldType::kInt, FieldType::kFloat,
            FieldType::kString, FieldType::kVector2, FieldType::kVector3,
            FieldType::kColor, FieldType::kNodeRef, FieldType::kAsset}) {
        if (name == FieldTypeName(t)) field.element_type = t;
      }
    }
    it->fields.push_back(std::move(field));
  }
  for (const auto& section : document.sections) {
    if (section.tag == "method") {
      auto script = section.attributes.GetString("script");
      auto it = std::find_if(components_.begin(), components_.end(),
                             [&](auto& c) { return c.name == script; });
      if (it == components_.end() || it->attach) continue;
      it->methods.push_back({section.attributes.GetString("name"),
                             section.attributes.GetString("arg")});
    } else if (section.tag == "asset") {
      auto name = section.attributes.GetString("name");
      if (auto existing = FindAssetType(name); existing && !existing->from_game)
        continue;
      AssetTypeInfo info;
      info.name = name;
      info.description = section.attributes.GetString("description");
      info.from_game = true;
      for (const auto& property : section.properties) {
        FieldInfo field;
        field.name = property.key;
        field.type = FieldTypeOf(property.value);
        field.default_value = property.value;
        info.fields.push_back(std::move(field));
      }
      RegisterAssetType(std::move(info));
    }
  }
  return true;
}

void LogReflectionWarnings(const Reflector& reflector,
                           std::string_view context) {
  for (const auto& warning : reflector.GetWarnings()) {
    PDebug::warn("{}: {}", context, warning);
  }
}

}  // namespace Mercury
