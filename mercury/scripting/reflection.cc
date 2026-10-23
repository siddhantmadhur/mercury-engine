#include "reflection.h"

#include "scripting/event.h"

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
    case FieldType::kList: return "list";
    case FieldType::kEvent: return "event";
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
    case FieldType::kList:
    case FieldType::kEvent: return mtext::Array{};
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
    case T::kArray: return FieldType::kList;
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
    case Mode::kGet:
      if (!found_ && lookup_ == name) {
        *out_ = to_variant(member);
        found_ = true;
      }
      break;
    case Mode::kSet:
      if (!found_ && lookup_ == name) {
        found_ = true;
        if (std::optional<T> converted = from_variant(*in_)) member = std::move(*converted);
      }
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
    case Mode::kEvents:
      break;
  }
  return *this;
}

template <typename T, typename ToVariant, typename FromVariant>
Reflector& Reflector::VisitList(const char* name, FieldType element,
                                std::vector<T>& member, ToVariant to_variant,
                                FromVariant from_variant) {
  auto to_array = [&](const std::vector<T>& items) {
    mtext::Array array;
    array.reserve(items.size());
    for (const auto& item : items) array.push_back(to_variant(item));
    return mtext::Variant(std::move(array));
  };
  auto from_array =
      [&](const mtext::Variant& value) -> std::optional<std::vector<T>> {
    const auto* array = value.Get<mtext::Array>();
    if (!array) return std::nullopt;
    std::vector<T> items;
    items.reserve(array->size());
    for (const auto& item : *array) {
      std::optional<T> converted = from_variant(item);
      if (!converted) return std::nullopt;
      items.push_back(std::move(*converted));
    }
    return items;
  };
  Visit(name, FieldType::kList, member, to_array, from_array);
  if (mode_ == Mode::kDescribe) fields_.back().element_type = element;
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

namespace {
std::optional<int> ToIntValue(const mtext::Variant& v) {
  if (auto i = v.ToInt()) return static_cast<int>(*i);
  return std::nullopt;
}
std::optional<float> ToFloatValue(const mtext::Variant& v) {
  if (auto f = v.ToFloat()) return static_cast<float>(*f);
  return std::nullopt;
}
std::optional<NodeRef> ToNodeRef(const mtext::Variant& v) {
  if (auto p = v.Get<mtext::NodePath>()) return NodeRef{p->path};
  if (auto s = v.Get<std::string>()) return NodeRef{*s};
  return std::nullopt;
}
std::optional<AssetRef> ToAssetRef(const mtext::Variant& v) {
  if (auto p = v.Get<mtext::Resource>()) return AssetRef{p->path};
  if (auto s = v.Get<std::string>()) return AssetRef{*s};
  if (v.IsNil()) return AssetRef{};
  return std::nullopt;
}
}  // namespace

Reflector& Reflector::Field(const char* name, std::vector<int>& value) {
  return VisitList(
      name, FieldType::kInt, value, [](int v) { return mtext::Variant(v); },
      ToIntValue);
}

Reflector& Reflector::Field(const char* name, std::vector<float>& value) {
  return VisitList(
      name, FieldType::kFloat, value, [](float v) { return mtext::Variant(v); },
      ToFloatValue);
}

Reflector& Reflector::Field(const char* name, std::vector<std::string>& value) {
  return VisitList(
      name, FieldType::kString, value,
      [](const std::string& v) { return mtext::Variant(v); },
      [](const mtext::Variant& v) { return v.ToString(); });
}

Reflector& Reflector::Field(const char* name, std::vector<NodeRef>& value) {
  return VisitList(
      name, FieldType::kNodeRef, value,
      [](const NodeRef& v) { return mtext::Variant(mtext::NodePath{v.path}); },
      ToNodeRef);
}

Reflector& Reflector::Field(const char* name, std::vector<AssetRef>& value,
                            const char* asset_type) {
  VisitList(
      name, FieldType::kAsset, value,
      [](const AssetRef& v) { return mtext::Variant(mtext::Resource{v.path}); },
      ToAssetRef);
  if (mode_ == Mode::kDescribe) fields_.back().asset_type = asset_type;
  return *this;
}

Reflector& Reflector::Field(const char* name, Event& value) {
  if (mode_ == Mode::kEvents) {
    if (event_fn_) event_fn_(value);
    return *this;
  }
  Visit(name, FieldType::kEvent, value.calls, Event::ToVariant,
        Event::FromVariant);
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
    case Mode::kGet:
      if (!found_ && lookup_ == name) {
        *out_ = name_of(index);
        found_ = true;
      }
      break;
    case Mode::kSet:
      if (!found_ && lookup_ == name) {
        found_ = true;
        if (auto text = in_->ToString()) {
          for (size_t i = 0; i < values.size(); i++) {
            if (values[i] == *text) index = static_cast<int>(i);
          }
        } else if (auto i = in_->ToInt()) {
          index = static_cast<int>(*i);
        }
      }
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
    case Mode::kEvents:
      break;
  }
}

Reflector& Reflector::Property(const char* name,
                               const std::function<mtext::Variant()>& get,
                               const std::function<void(const mtext::Variant&)>& set) {
  if (found_ || lookup_ != name) return *this;
  if (mode_ == Mode::kGet && get) {
    *out_ = get();
    found_ = true;
  } else if (mode_ == Mode::kSet && set) {
    set(*in_);
    found_ = true;
  }
  return *this;
}

Reflector& Reflector::Tooltip(const char* text) {
  if (mode_ == Mode::kDescribe && !fields_.empty()) {
    fields_.back().tooltip = text;
  }
  return *this;
}

}  // namespace Mercury
