#include "reflection.h"

#include <format>
#include <optional>

namespace Mercury {

const char* FieldTypeName(FieldType type) {
  switch (type) {
    case FieldType::kBool: return "bool";
    case FieldType::kInt: return "int";
    case FieldType::kFloat: return "float";
    case FieldType::kString: return "string";
    case FieldType::kVector2: return "Vector2";
    case FieldType::kVector3: return "Vector3";
    case FieldType::kColor: return "Color";
    case FieldType::kNodeRef: return "NodePath";
    case FieldType::kAsset: return "Resource";
    case FieldType::kEnum: return "enum";
  }
  return "unknown";
}

mtext::Variant EmptyValueFor(FieldType type) {
  switch (type) {
    case FieldType::kBool: return false;
    case FieldType::kInt: return 0;
    case FieldType::kFloat: return 0.0;
    case FieldType::kString: return std::string();
    case FieldType::kEnum: return std::string();
    case FieldType::kVector2: return glm::vec2(0.0f);
    case FieldType::kVector3: return glm::vec3(0.0f);
    case FieldType::kColor: return mtext::Color{};
    case FieldType::kNodeRef: return mtext::NodePath{};
    case FieldType::kAsset: return mtext::Resource{};
  }
  return {};
}

FieldType FieldTypeOf(const mtext::Variant& value) {
  using T = mtext::Variant::Type;
  switch (value.GetType()) {
    case T::kBool: return FieldType::kBool;
    case T::kInt: return FieldType::kInt;
    case T::kFloat: return FieldType::kFloat;
    case T::kString: return FieldType::kString;
    case T::kVector2: return FieldType::kVector2;
    case T::kVector3: return FieldType::kVector3;
    case T::kColor: return FieldType::kColor;
    case T::kNodePath: return FieldType::kNodeRef;
    case T::kResource:
    case T::kExtResource: return FieldType::kAsset;
    default: return FieldType::kString;
  }
}

template <typename T, typename ToVariant, typename FromVariant>
Reflector& Reflector::Visit(const char* name, FieldType type, T& member,
                            ToVariant to_variant, FromVariant from_variant) {
  switch (mode_) {
    case Mode::kDescribe:
      fields_.push_back(FieldInfo{name, type, to_variant(member)});
      break;
    case Mode::kWrite:
      properties_->Set(name, to_variant(member));
      break;
    case Mode::kRead:
      if (auto value = properties_->Get(name)) {
        std::optional<T> converted = from_variant(*value);
        if (converted) {
          member = std::move(*converted);
        } else {
          warnings_.push_back(std::format("\"{}\" should be a {}, not {}", name,
                                          FieldTypeName(type),
                                          mtext::TypeName(value->GetType())));
        }
      }
      break;
  }
  return *this;
}

Reflector& Reflector::Field(const char* name, bool& value) {
  return Visit(
      name, FieldType::kBool, value,
      [](bool v) { return mtext::Variant(v); },
      [](const mtext::Variant& v) { return v.ToBool(); });
}

Reflector& Reflector::Field(const char* name, int& value) {
  return Visit(
      name, FieldType::kInt, value, [](int v) { return mtext::Variant(v); },
      [](const mtext::Variant& v) -> std::optional<int> {
        if (auto i = v.ToInt()) return static_cast<int>(*i);
        return std::nullopt;
      });
}

Reflector& Reflector::Field(const char* name, float& value) {
  return Visit(
      name, FieldType::kFloat, value,
      [](float v) { return mtext::Variant(v); },
      [](const mtext::Variant& v) -> std::optional<float> {
        if (auto f = v.ToFloat()) return static_cast<float>(*f);
        return std::nullopt;
      });
}

Reflector& Reflector::Field(const char* name, std::string& value) {
  return Visit(
      name, FieldType::kString, value,
      [](const std::string& v) { return mtext::Variant(v); },
      [](const mtext::Variant& v) { return v.ToString(); });
}

Reflector& Reflector::Field(const char* name, glm::vec2& value) {
  return Visit(
      name, FieldType::kVector2, value,
      [](glm::vec2 v) { return mtext::Variant(v); },
      [](const mtext::Variant& v) -> std::optional<glm::vec2> {
        if (auto p = v.Get<glm::vec2>()) return *p;
        return std::nullopt;
      });
}

Reflector& Reflector::Field(const char* name, glm::vec3& value) {
  return Visit(
      name, FieldType::kVector3, value,
      [](glm::vec3 v) { return mtext::Variant(v); },
      [](const mtext::Variant& v) -> std::optional<glm::vec3> {
        if (auto p = v.Get<glm::vec3>()) return *p;
        // A Vector2 is accepted with z = 0, handy for 2D games.
        if (auto p = v.Get<glm::vec2>()) return glm::vec3(*p, 0.0f);
        return std::nullopt;
      });
}

Reflector& Reflector::Field(const char* name, glm::vec4& value) {
  return Visit(
      name, FieldType::kColor, value,
      [](glm::vec4 v) { return mtext::Variant(mtext::Color{v}); },
      [](const mtext::Variant& v) -> std::optional<glm::vec4> {
        if (auto p = v.Get<mtext::Color>()) return p->rgba;
        return std::nullopt;
      });
}

Reflector& Reflector::Field(const char* name, NodeRef& value) {
  return Visit(
      name, FieldType::kNodeRef, value,
      [](const NodeRef& v) { return mtext::Variant(mtext::NodePath{v.path}); },
      [](const mtext::Variant& v) -> std::optional<NodeRef> {
        if (auto p = v.Get<mtext::NodePath>()) return NodeRef{p->path};
        if (auto s = v.Get<std::string>()) return NodeRef{*s};
        return std::nullopt;
      });
}

Reflector& Reflector::Field(const char* name, AssetRef& value,
                            const char* asset_type) {
  Visit(
      name, FieldType::kAsset, value,
      [](const AssetRef& v) { return mtext::Variant(mtext::Resource{v.path}); },
      [](const mtext::Variant& v) -> std::optional<AssetRef> {
        if (auto p = v.Get<mtext::Resource>()) return AssetRef{p->path};
        if (auto s = v.Get<std::string>()) return AssetRef{*s};
        return std::nullopt;
      });
  if (mode_ == Mode::kDescribe) fields_.back().asset_type = asset_type;
  return *this;
}

void Reflector::EnumImpl(const char* name, int& index,
                         std::initializer_list<const char*> names) {
  std::vector<std::string> values(names.begin(), names.end());
  auto name_of = [&](int i) -> std::string {
    return i >= 0 && i < static_cast<int>(values.size()) ? values[i] : "";
  };
  switch (mode_) {
    case Mode::kDescribe: {
      FieldInfo info{name, FieldType::kEnum, name_of(index)};
      info.enum_values = values;
      fields_.push_back(std::move(info));
      break;
    }
    case Mode::kWrite:
      properties_->Set(name, name_of(index));
      break;
    case Mode::kRead:
      if (auto value = properties_->Get(name)) {
        auto text = value->ToString();
        bool found = false;
        for (size_t i = 0; text && i < values.size(); i++) {
          if (values[i] == *text) {
            index = static_cast<int>(i);
            found = true;
          }
        }
        if (!found) {
          std::string options;
          for (const auto& v : values) {
            options += (options.empty() ? "\"" : ", \"") + v + "\"";
          }
          warnings_.push_back(
              std::format("\"{}\" must be one of {}", name, options));
        }
      }
      break;
  }
}

Reflector& Reflector::Tooltip(const char* text) {
  if (mode_ == Mode::kDescribe && !fields_.empty()) {
    fields_.back().tooltip = text;
  }
  return *this;
}

}  // namespace Mercury
