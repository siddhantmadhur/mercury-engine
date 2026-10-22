#include "style.h"

#include <cmath>
#include <format>
#include <numbers>
#include <unordered_map>

namespace Mercury::UI {

namespace {

// Keyword spellings of each enum, in enum order.
template <typename E>
const std::vector<std::string>& KeywordsOf() {
  static const std::vector<std::string> kNone;
  return kNone;
}
template <>
const std::vector<std::string>& KeywordsOf<FlexDirection>() {
  static const std::vector<std::string> k{"column", "column-reverse", "row",
                                          "row-reverse"};
  return k;
}
template <>
const std::vector<std::string>& KeywordsOf<Wrap>() {
  static const std::vector<std::string> k{"nowrap", "wrap", "wrap-reverse"};
  return k;
}
template <>
const std::vector<std::string>& KeywordsOf<Align>() {
  static const std::vector<std::string> k{"auto",    "flex-start",
                                          "center",  "flex-end",
                                          "stretch", "space-between",
                                          "space-around"};
  return k;
}
template <>
const std::vector<std::string>& KeywordsOf<Justify>() {
  static const std::vector<std::string> k{"flex-start",    "center",
                                          "flex-end",      "space-between",
                                          "space-around",  "space-evenly"};
  return k;
}
template <>
const std::vector<std::string>& KeywordsOf<Position>() {
  static const std::vector<std::string> k{"relative", "absolute"};
  return k;
}
template <>
const std::vector<std::string>& KeywordsOf<DisplayStyle>() {
  static const std::vector<std::string> k{"flex", "none"};
  return k;
}
template <>
const std::vector<std::string>& KeywordsOf<Visibility>() {
  static const std::vector<std::string> k{"visible", "hidden"};
  return k;
}
template <>
const std::vector<std::string>& KeywordsOf<Overflow>() {
  static const std::vector<std::string> k{"visible", "hidden"};
  return k;
}
template <>
const std::vector<std::string>& KeywordsOf<TextAnchor>() {
  static const std::vector<std::string> k{
      "upper-left",  "upper-center",  "upper-right",
      "middle-left", "middle-center", "middle-right",
      "lower-left",  "lower-center",  "lower-right"};
  return k;
}
template <>
const std::vector<std::string>& KeywordsOf<FontStyle>() {
  static const std::vector<std::string> k{"normal", "bold", "italic",
                                          "bold-and-italic"};
  return k;
}
template <>
const std::vector<std::string>& KeywordsOf<WhiteSpace>() {
  static const std::vector<std::string> k{"normal", "nowrap", "pre",
                                          "pre-wrap"};
  return k;
}
template <>
const std::vector<std::string>& KeywordsOf<TextOverflow>() {
  static const std::vector<std::string> k{"clip", "ellipsis"};
  return k;
}
template <>
const std::vector<std::string>& KeywordsOf<ScaleMode>() {
  static const std::vector<std::string> k{"stretch-to-fill", "scale-and-crop",
                                          "scale-to-fit"};
  return k;
}

template <typename T>
StyleValue ToValue(const T& v) {
  if constexpr (std::is_enum_v<T>) {
    return static_cast<int>(v);
  } else {
    return v;
  }
}

template <typename T>
bool FromValue(const StyleValue& value, T& out) {
  if constexpr (std::is_enum_v<T>) {
    if (auto p = std::get_if<int>(&value)) {
      out = static_cast<T>(*p);
      return true;
    }
    return false;
  } else if constexpr (std::is_same_v<T, float>) {
    if (auto p = std::get_if<float>(&value)) {
      out = *p;
      return true;
    }
    if (auto p = std::get_if<Length>(&value)) {
      out = p->value;
      return true;
    }
    return false;
  } else if constexpr (std::is_same_v<T, Length>) {
    if (auto p = std::get_if<Length>(&value)) {
      out = *p;
      return true;
    }
    if (auto p = std::get_if<float>(&value)) {
      out = Length(*p);
      return true;
    }
    return false;
  } else {
    if (auto p = std::get_if<T>(&value)) {
      out = *p;
      return true;
    }
    return false;
  }
}

constexpr bool IsAnimatableType(std::string_view type) {
  return type == "Length" || type == "float" || type == "Color" ||
         type == "Translate" || type == "Scale" || type == "Rotate" ||
         type == "TransformOrigin" || type == "TextShadow";
}

const StylePropertyInfo kInfos[] = {
#define MERCURY_UI_X(id, name, type, member, def, inherited) \
  {StylePropertyId::k##id, name, inherited, IsAnimatableType(#type)},
    MERCURY_UI_STYLE_PROPERTIES(MERCURY_UI_X)
#undef MERCURY_UI_X
};
static_assert(std::size(kInfos) == kStylePropertyCount);

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

Length LerpLength(const Length& a, const Length& b, float t) {
  if (a.unit != b.unit) return t < 1.0f ? a : b;
  return Length(Lerp(a.value, b.value, t), a.unit);
}

Color LerpColor(const Color& a, const Color& b, float t) {
  return Color(glm::mix(a.rgba, b.rgba, t));
}

std::string FormatFloat(float v) {
  if (std::abs(v - std::round(v)) < 1e-4f) {
    return std::format("{}", static_cast<int>(std::round(v)));
  }
  std::string s = std::format("{:.3f}", v);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

std::string FormatLength(const Length& l) {
  switch (l.unit) {
    case Length::Unit::kAuto: return "auto";
    case Length::Unit::kNone: return "none";
    case Length::Unit::kPercent: return FormatFloat(l.value) + "%";
    case Length::Unit::kPixel: return FormatFloat(l.value) + "px";
  }
  return "0";
}

std::string FormatColor(const Color& c) {
  auto byte = [](float f) {
    return static_cast<int>(std::round(std::clamp(f, 0.0f, 1.0f) * 255.0f));
  };
  if (c.rgba.a >= 1.0f) {
    return std::format("rgb({}, {}, {})", byte(c.rgba.r), byte(c.rgba.g),
                       byte(c.rgba.b));
  }
  return std::format("rgba({}, {}, {}, {})", byte(c.rgba.r), byte(c.rgba.g),
                     byte(c.rgba.b), FormatFloat(c.rgba.a));
}

const char* EasingName(EasingMode mode) {
  static const char* kNames[] = {
      "ease",          "ease-in",          "ease-out",
      "ease-in-out",   "linear",           "ease-in-sine",
      "ease-out-sine", "ease-in-out-sine", "ease-in-cubic",
      "ease-out-cubic", "ease-in-out-cubic", "ease-in-back",
      "ease-out-back", "ease-in-out-back", "ease-in-bounce",
      "ease-out-bounce", "ease-in-out-bounce"};
  return kNames[static_cast<int>(mode)];
}

float BounceOut(float t) {
  const float n1 = 7.5625f, d1 = 2.75f;
  if (t < 1 / d1) return n1 * t * t;
  if (t < 2 / d1) {
    t -= 1.5f / d1;
    return n1 * t * t + 0.75f;
  }
  if (t < 2.5f / d1) {
    t -= 2.25f / d1;
    return n1 * t * t + 0.9375f;
  }
  t -= 2.625f / d1;
  return n1 * t * t + 0.984375f;
}

// CSS cubic-bezier(x1, y1, x2, y2) evaluated at x = t.
float CubicBezier(float x1, float y1, float x2, float y2, float t) {
  auto bez = [](float a, float b, float s) {
    return 3 * a * s * (1 - s) * (1 - s) + 3 * b * s * s * (1 - s) + s * s * s;
  };
  float lo = 0.0f, hi = 1.0f, s = t;
  for (int i = 0; i < 24; i++) {
    s = (lo + hi) / 2;
    if (bez(x1, x2, s) < t) {
      lo = s;
    } else {
      hi = s;
    }
  }
  return bez(y1, y2, s);
}

}  // namespace

StyleValue ComputedStyle::Get(StylePropertyId id) const {
  switch (id) {
#define MERCURY_UI_X(id, name, type, member, def, inherited) \
  case StylePropertyId::k##id:                                \
    return ToValue(member);
    MERCURY_UI_STYLE_PROPERTIES(MERCURY_UI_X)
#undef MERCURY_UI_X
    default: return {};
  }
}

void ComputedStyle::Set(StylePropertyId id, const StyleValue& value) {
  switch (id) {
#define MERCURY_UI_X(id, name, type, member, def, inherited) \
  case StylePropertyId::k##id:                                \
    FromValue(value, member);                                 \
    break;
    MERCURY_UI_STYLE_PROPERTIES(MERCURY_UI_X)
#undef MERCURY_UI_X
    default: break;
  }
}

void ComputedStyle::InheritFrom(const ComputedStyle& parent) {
#define MERCURY_UI_X(id, name, type, member, def, inherited) \
  if constexpr (inherited) member = parent.member;
  MERCURY_UI_STYLE_PROPERTIES(MERCURY_UI_X)
#undef MERCURY_UI_X
}

void InlineStyle::SetMargin(Length all) {
  marginLeft = marginTop = marginRight = marginBottom = all;
}
void InlineStyle::SetPadding(Length all) {
  paddingLeft = paddingTop = paddingRight = paddingBottom = all;
}
void InlineStyle::SetBorderWidth(float all) {
  borderLeftWidth = borderTopWidth = borderRightWidth = borderBottomWidth = all;
}
void InlineStyle::SetBorderColor(Color all) {
  borderLeftColor = borderTopColor = borderRightColor = borderBottomColor = all;
}
void InlineStyle::SetBorderRadius(Length all) {
  borderTopLeftRadius = borderTopRightRadius = borderBottomRightRadius =
      borderBottomLeftRadius = all;
}

bool InlineStyle::Has(StylePropertyId id) const {
  switch (id) {
#define MERCURY_UI_X(id, name, type, member, def, inherited) \
  case StylePropertyId::k##id:                                \
    return member.has_value();
    MERCURY_UI_STYLE_PROPERTIES(MERCURY_UI_X)
#undef MERCURY_UI_X
    default: return false;
  }
}

void InlineStyle::Clear(StylePropertyId id) {
  switch (id) {
#define MERCURY_UI_X(id, name, type, member, def, inherited) \
  case StylePropertyId::k##id:                                \
    member.reset();                                           \
    break;
    MERCURY_UI_STYLE_PROPERTIES(MERCURY_UI_X)
#undef MERCURY_UI_X
    default: break;
  }
}

void InlineStyle::Set(StylePropertyId id, const StyleValue& value) {
  switch (id) {
#define MERCURY_UI_X(id, name, type, member, def, inherited) \
  case StylePropertyId::k##id: {                              \
    type v = def;                                             \
    if (FromValue(value, v)) member = v;                      \
    break;                                                    \
  }
    MERCURY_UI_STYLE_PROPERTIES(MERCURY_UI_X)
#undef MERCURY_UI_X
    default: break;
  }
}

StyleValue InlineStyle::Get(StylePropertyId id) const {
  switch (id) {
#define MERCURY_UI_X(id, name, type, member, def, inherited) \
  case StylePropertyId::k##id:                                \
    return member ? ToValue(*member) : StyleValue{};
    MERCURY_UI_STYLE_PROPERTIES(MERCURY_UI_X)
#undef MERCURY_UI_X
    default: return {};
  }
}

void InlineStyle::ApplyTo(ComputedStyle& style) const {
#define MERCURY_UI_X(id, name, type, member, def, inherited) \
  if (member) style.member = *member;
  MERCURY_UI_STYLE_PROPERTIES(MERCURY_UI_X)
#undef MERCURY_UI_X
}

const StylePropertyInfo& GetStylePropertyInfo(StylePropertyId id) {
  return kInfos[static_cast<size_t>(id)];
}

std::optional<StylePropertyId> FindStyleProperty(std::string_view uss_name) {
  static const auto kByName = [] {
    std::unordered_map<std::string_view, StylePropertyId> map;
    for (const auto& info : kInfos) map[info.name] = info.id;
    // Unity's older name for the font property.
    map["-unity-font"] = StylePropertyId::kFont;
    return map;
  }();
  auto it = kByName.find(uss_name);
  if (it == kByName.end()) return std::nullopt;
  return it->second;
}

StyleValue DefaultStyleValue(StylePropertyId id) {
  static const ComputedStyle kDefault;
  return kDefault.Get(id);
}

float Ease(EasingMode mode, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  constexpr float kPi = std::numbers::pi_v<float>;
  constexpr float c1 = 1.70158f, c2 = c1 * 1.525f, c3 = c1 + 1.0f;
  switch (mode) {
    case EasingMode::kEase: return CubicBezier(0.25f, 0.1f, 0.25f, 1.0f, t);
    case EasingMode::kEaseIn: return CubicBezier(0.42f, 0.0f, 1.0f, 1.0f, t);
    case EasingMode::kEaseOut: return CubicBezier(0.0f, 0.0f, 0.58f, 1.0f, t);
    case EasingMode::kEaseInOut:
      return CubicBezier(0.42f, 0.0f, 0.58f, 1.0f, t);
    case EasingMode::kLinear: return t;
    case EasingMode::kEaseInSine: return 1 - std::cos(t * kPi / 2);
    case EasingMode::kEaseOutSine: return std::sin(t * kPi / 2);
    case EasingMode::kEaseInOutSine: return -(std::cos(kPi * t) - 1) / 2;
    case EasingMode::kEaseInCubic: return t * t * t;
    case EasingMode::kEaseOutCubic: return 1 - std::pow(1 - t, 3.0f);
    case EasingMode::kEaseInOutCubic:
      return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2;
    case EasingMode::kEaseInBack: return c3 * t * t * t - c1 * t * t;
    case EasingMode::kEaseOutBack:
      return 1 + c3 * std::pow(t - 1, 3.0f) + c1 * std::pow(t - 1, 2.0f);
    case EasingMode::kEaseInOutBack:
      return t < 0.5f
                 ? (std::pow(2 * t, 2.0f) * ((c2 + 1) * 2 * t - c2)) / 2
                 : (std::pow(2 * t - 2, 2.0f) * ((c2 + 1) * (t * 2 - 2) + c2) +
                    2) / 2;
    case EasingMode::kEaseInBounce: return 1 - BounceOut(1 - t);
    case EasingMode::kEaseOutBounce: return BounceOut(t);
    case EasingMode::kEaseInOutBounce:
      return t < 0.5f ? (1 - BounceOut(1 - 2 * t)) / 2
                      : (1 + BounceOut(2 * t - 1)) / 2;
  }
  return t;
}

StyleValue Interpolate(StylePropertyId id, const StyleValue& from,
                       const StyleValue& to, float t) {
  if (from.index() != to.index()) return t < 1.0f ? from : to;
  return std::visit(
      [&](const auto& a) -> StyleValue {
        using T = std::decay_t<decltype(a)>;
        const T& b = std::get<T>(to);
        if constexpr (std::is_same_v<T, float>) {
          return Lerp(a, b, t);
        } else if constexpr (std::is_same_v<T, Length>) {
          return LerpLength(a, b, t);
        } else if constexpr (std::is_same_v<T, Color>) {
          return LerpColor(a, b, t);
        } else if constexpr (std::is_same_v<T, Translate>) {
          return Translate{LerpLength(a.x, b.x, t), LerpLength(a.y, b.y, t)};
        } else if constexpr (std::is_same_v<T, Scale>) {
          return Scale{glm::mix(a.value, b.value, t)};
        } else if constexpr (std::is_same_v<T, Rotate>) {
          return Rotate{Lerp(a.degrees, b.degrees, t)};
        } else if constexpr (std::is_same_v<T, TransformOrigin>) {
          return TransformOrigin{LerpLength(a.x, b.x, t),
                                 LerpLength(a.y, b.y, t)};
        } else if constexpr (std::is_same_v<T, TextShadow>) {
          return TextShadow{glm::mix(a.offset, b.offset, t),
                            Lerp(a.blur_radius, b.blur_radius, t),
                            LerpColor(a.color, b.color, t)};
        } else {
          return t < 1.0f ? StyleValue(a) : StyleValue(b);
        }
      },
      from);
}

const std::vector<std::string>& StyleKeywords(StylePropertyId id) {
  switch (id) {
#define MERCURY_UI_X(id, name, type, member, def, inherited) \
  case StylePropertyId::k##id:                                \
    return KeywordsOf<type>();
    MERCURY_UI_STYLE_PROPERTIES(MERCURY_UI_X)
#undef MERCURY_UI_X
    default: return KeywordsOf<float>();
  }
}

std::string FormatStyleValue(StylePropertyId id, const StyleValue& value) {
  return std::visit(
      [&](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return "initial";
        } else if constexpr (std::is_same_v<T, float>) {
          // Border widths and font sizes read better with a unit.
          if (id == StylePropertyId::kFontSize ||
              id == StylePropertyId::kLetterSpacing ||
              id == StylePropertyId::kWordSpacing ||
              id == StylePropertyId::kParagraphSpacing ||
              id == StylePropertyId::kTextOutlineWidth ||
              (id >= StylePropertyId::kBorderLeftWidth &&
               id <= StylePropertyId::kBorderBottomWidth)) {
            return FormatFloat(v) + "px";
          }
          return FormatFloat(v);
        } else if constexpr (std::is_same_v<T, Length>) {
          return FormatLength(v);
        } else if constexpr (std::is_same_v<T, Color>) {
          return FormatColor(v);
        } else if constexpr (std::is_same_v<T, int>) {
          const auto& keywords = StyleKeywords(id);
          if (v >= 0 && v < static_cast<int>(keywords.size())) {
            return keywords[v];
          }
          return std::to_string(v);
        } else if constexpr (std::is_same_v<T, Background>) {
          if (v.path.empty()) return "none";
          return std::format("url(\"{}\")", v.path);
        } else if constexpr (std::is_same_v<T, FontDefinition>) {
          if (v.path.empty()) return "initial";
          return std::format("url(\"{}\")", v.path);
        } else if constexpr (std::is_same_v<T, Translate>) {
          return FormatLength(v.x) + " " + FormatLength(v.y);
        } else if constexpr (std::is_same_v<T, Scale>) {
          return FormatFloat(v.value.x) + " " + FormatFloat(v.value.y);
        } else if constexpr (std::is_same_v<T, Rotate>) {
          return FormatFloat(v.degrees) + "deg";
        } else if constexpr (std::is_same_v<T, TransformOrigin>) {
          return FormatLength(v.x) + " " + FormatLength(v.y);
        } else if constexpr (std::is_same_v<T, TextShadow>) {
          return std::format("{}px {}px {}px {}", FormatFloat(v.offset.x),
                             FormatFloat(v.offset.y),
                             FormatFloat(v.blur_radius), FormatColor(v.color));
        } else if constexpr (std::is_same_v<T, PropertyNameList>) {
          std::string out;
          for (const auto& n : v.names) out += (out.empty() ? "" : ", ") + n;
          return out;
        } else if constexpr (std::is_same_v<T, TimeList>) {
          std::string out;
          for (float s : v.seconds) {
            out += (out.empty() ? "" : ", ") + FormatFloat(s) + "s";
          }
          return out;
        } else if constexpr (std::is_same_v<T, EasingList>) {
          std::string out;
          for (EasingMode m : v.modes) {
            out += std::string(out.empty() ? "" : ", ") + EasingName(m);
          }
          return out;
        }
      },
      value);
}

}  // namespace Mercury::UI
