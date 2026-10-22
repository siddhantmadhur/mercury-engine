#include "binding.h"

#include <cmath>
#include <format>

#include "scripting/reflection.h"
#include "ui/visual_element.h"

namespace Mercury::UI {

namespace {

std::string FormatNumber(double v) {
  if (std::abs(v - std::round(v)) < 1e-6 && std::abs(v) < 1e15) {
    return std::format("{}", static_cast<int64_t>(std::round(v)));
  }
  std::string s = std::format("{:.3f}", v);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

}  // namespace

const char* BindingModeName(BindingMode mode) {
  switch (mode) {
    case BindingMode::kTwoWay: return "TwoWay";
    case BindingMode::kToSource: return "ToSource";
    case BindingMode::kToTarget: return "ToTarget";
    case BindingMode::kToTargetOnce: return "ToTargetOnce";
  }
  return "ToTarget";
}

std::optional<BindingMode> BindingModeFromName(std::string_view name) {
  if (name == "TwoWay") return BindingMode::kTwoWay;
  if (name == "ToSource") return BindingMode::kToSource;
  if (name == "ToTarget") return BindingMode::kToTarget;
  if (name == "ToTargetOnce") return BindingMode::kToTargetOnce;
  return std::nullopt;
}

std::string VariantToString(const mtext::Variant& value) {
  using T = mtext::Variant::Type;
  switch (value.GetType()) {
    case T::kNil: return "";
    case T::kBool: return *value.Get<bool>() ? "true" : "false";
    case T::kInt: return std::to_string(*value.Get<int64_t>());
    case T::kFloat: return FormatNumber(*value.Get<double>());
    case T::kString: return *value.Get<std::string>();
    case T::kVector2: {
      auto v = *value.Get<glm::vec2>();
      return std::format("({}, {})", FormatNumber(v.x), FormatNumber(v.y));
    }
    case T::kVector3: {
      auto v = *value.Get<glm::vec3>();
      return std::format("({}, {}, {})", FormatNumber(v.x), FormatNumber(v.y),
                         FormatNumber(v.z));
    }
    case T::kNodePath: return value.Get<mtext::NodePath>()->path;
    case T::kResource: return value.Get<mtext::Resource>()->path;
    default: return "";
  }
}

std::optional<double> VariantToFloat(const mtext::Variant& value) {
  if (auto f = value.ToFloat()) return f;
  if (auto b = value.Get<bool>()) return *b ? 1.0 : 0.0;
  if (auto s = value.Get<std::string>()) {
    char* end = nullptr;
    const double d = std::strtod(s->c_str(), &end);
    if (end != s->c_str()) return d;
  }
  return std::nullopt;
}

std::optional<int64_t> VariantToInt(const mtext::Variant& value) {
  if (auto f = value.Get<double>()) return static_cast<int64_t>(std::llround(*f));
  if (auto i = value.ToInt()) return i;
  if (auto b = value.Get<bool>()) return *b ? 1 : 0;
  if (auto s = value.Get<std::string>()) {
    char* end = nullptr;
    const long long i = std::strtoll(s->c_str(), &end, 10);
    if (end != s->c_str()) return i;
  }
  return std::nullopt;
}

std::optional<bool> VariantToBool(const mtext::Variant& value) {
  if (auto b = value.Get<bool>()) return *b;
  if (auto i = value.Get<int64_t>()) return *i != 0;
  if (auto f = value.Get<double>()) return *f != 0.0;
  if (auto s = value.Get<std::string>()) {
    if (*s == "true" || *s == "True" || *s == "1") return true;
    if (*s == "false" || *s == "False" || *s == "0") return false;
  }
  return std::nullopt;
}

std::optional<Color> VariantToColor(const mtext::Variant& value) {
  if (auto c = value.Get<mtext::Color>()) return Color(c->rgba);
  if (auto s = value.Get<std::string>()) return ParseColor(*s);
  return std::nullopt;
}

// --------------------------------------------------------- Data sources ---

std::optional<mtext::Variant> UIDataSource::GetValue(std::string_view path) const {
  auto it = values_.find(path);
  if (it == values_.end()) return std::nullopt;
  return it->second;
}

bool UIDataSource::SetValue(std::string_view path, const mtext::Variant& value) {
  values_[std::string(path)] = value;
  return true;
}

ReflectedDataSource::ReflectedDataSource(ReflectFn reflect, std::weak_ptr<void> lifetime)
    : reflect_(std::move(reflect)), lifetime_(std::move(lifetime)) {
  has_lifetime_ = !lifetime_.expired();
}

bool ReflectedDataSource::Alive() const { return !has_lifetime_ || !lifetime_.expired(); }

std::optional<mtext::Variant> ReflectedDataSource::GetValue(std::string_view path) const {
  if (!Alive() || !reflect_) return std::nullopt;
  mtext::Variant out;
  auto reflector = Reflector::Get(path, &out);
  reflect_(reflector);
  if (!reflector.Found()) return std::nullopt;
  return out;
}

bool ReflectedDataSource::SetValue(std::string_view path, const mtext::Variant& value) {
  if (!Alive() || !reflect_) return false;
  auto reflector = Reflector::Set(path, value);
  reflect_(reflector);
  return reflector.Found();
}

// ---------------------------------------------------------------- Update ---

void VisualElement::UpdateBindings() {
  for (auto& [property, binding] : bindings_) {
    DataBinding& b = *binding;
    std::shared_ptr<IDataSource> source = b.dataSource ? b.dataSource : ResolveDataSource();
    if (!source) continue;
    std::string path = ResolveDataSourcePath();
    if (!b.dataSourcePath.empty()) {
      path = path.empty() ? b.dataSourcePath : path + "." + b.dataSourcePath;
    }
    const bool to_ui = b.bindingMode == BindingMode::kToTarget ||
                       b.bindingMode == BindingMode::kTwoWay ||
                       (b.bindingMode == BindingMode::kToTargetOnce && !b.updated_once);
    const bool to_source = b.bindingMode == BindingMode::kTwoWay ||
                           b.bindingMode == BindingMode::kToSource;

    if (to_source && b.last_ui) {
      // Push UI edits first so they aren't overwritten by the old value.
      auto ui = GetProperty(property);
      if (ui && *ui != *b.last_ui) {
        b.last_ui = ui;
        source->SetValue(path, b.uiToSource ? b.uiToSource(*ui) : *ui);
        b.last_source = source->GetValue(path);
      }
    }
    if (to_ui) {
      auto value = source->GetValue(path);
      if (value && (!b.last_source || *b.last_source != *value)) {
        b.last_source = value;
        SetProperty(property, b.sourceToUi ? b.sourceToUi(*value) : *value);
      }
    }
    if (to_source && !b.last_ui && b.bindingMode == BindingMode::kToSource) {
      if (auto ui = GetProperty(property)) {
        source->SetValue(path, b.uiToSource ? b.uiToSource(*ui) : *ui);
      }
    }
    b.last_ui = GetProperty(property);
    b.updated_once = true;
  }
}

}  // namespace Mercury::UI
