#include "type_registry.h"

#include <algorithm>
#include <format>

#include "debugger/debugger.h"

namespace Mercury {

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

bool TypeRegistry::SaveSchema(const std::filesystem::path& path) const {
  mtext::Document document;
  document.Add("mschema").attributes.Set("format", 1);
  for (const auto* info : Components()) {
    if (!info->is_script) continue;
    auto& script = document.Add("script");
    script.attributes.Set("name", info->name);
    for (const auto& field : info->fields) {
      script.properties.Set(field.name, field.default_value);
    }
    // Details a default value cannot carry go in one [field] per field.
    for (const auto& field : info->fields) {
      mtext::PropertyList details;
      if (field.type == FieldType::kEnum) {
        mtext::Array values;
        for (const auto& v : field.enum_values) values.emplace_back(v);
        details.Set("enum", values);
      }
      if (field.type == FieldType::kAsset && !field.asset_type.empty()) {
        details.Set("asset_type", field.asset_type);
      }
      if (!field.tooltip.empty()) details.Set("tooltip", field.tooltip);
      if (details.empty()) continue;
      auto& section = document.Add("field");
      section.attributes.Set("script", info->name);
      section.attributes.Set("name", field.name);
      section.properties = std::move(details);
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
  for (const auto& section : document.sections) {
    if (section.tag == "script") {
      auto name = section.attributes.GetString("name");
      // A script compiled into this process wins over the schema.
      if (auto existing = FindComponent(name); existing && existing->attach) {
        continue;
      }
      ComponentInfo info;
      info.name = name;
      info.category = "Scripts";
      info.is_script = true;
      for (const auto& property : section.properties) {
        FieldInfo field;
        field.name = property.key;
        field.type = FieldTypeOf(property.value);
        field.default_value = property.value;
        info.fields.push_back(std::move(field));
      }
      RegisterComponent(std::move(info));
    }
  }
  // Second pass: [field] details refer to scripts declared above.
  for (const auto& section : document.sections) {
    if (section.tag != "field") continue;
    auto script = section.attributes.GetString("script");
    auto field_name = section.attributes.GetString("name");
    auto it = std::find_if(components_.begin(), components_.end(),
                           [&](auto& c) { return c.name == script; });
    if (it == components_.end() || it->attach) continue;
    auto field = std::find_if(it->fields.begin(), it->fields.end(),
                              [&](auto& f) { return f.name == field_name; });
    if (field == it->fields.end()) continue;
    if (auto values = section.properties.Get("enum")) {
      field->type = FieldType::kEnum;
      field->enum_values.clear();
      if (auto array = values->Get<mtext::Array>()) {
        for (const auto& v : *array) {
          if (auto s = v.ToString()) field->enum_values.push_back(*s);
        }
      }
    }
    field->asset_type = section.properties.GetString("asset_type");
    field->tooltip = section.properties.GetString("tooltip");
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
