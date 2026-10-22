/*
 * style.h
 *
 * The USS style properties an element can have, modeled on Unity's IStyle /
 * IResolvedStyle. Every property is listed once in MERCURY_UI_STYLE_PROPERTIES
 * and that list generates:
 *   - StylePropertyId, the property's id,
 *   - ComputedStyle, the resolved value of every property (resolvedStyle),
 *   - InlineStyle, the optional values set from code or a style="" attribute,
 *   - the name/inheritance table used by the cascade and the UI Builder.
 *
 *   element->style().width = Length::Percent(50);
 *   element->style().backgroundColor = Color(1, 0, 0);
 *   float w = element->resolvedStyle().width.value;
 */
#ifndef MERCURY_UI_STYLE_H
#define MERCURY_UI_STYLE_H

#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Mercury::UI {

// A size or offset: pixels, a percentage of the parent, or a keyword.
struct Length {
  enum class Unit : uint8_t { kPixel, kPercent, kAuto, kNone };
  float value = 0.0f;
  Unit unit = Unit::kPixel;

  constexpr Length() = default;
  constexpr Length(float px) : value(px) {}
  constexpr Length(float v, Unit u) : value(v), unit(u) {}
  static constexpr Length Px(float px) { return {px, Unit::kPixel}; }
  static constexpr Length Percent(float p) { return {p, Unit::kPercent}; }
  static constexpr Length Auto() { return {0.0f, Unit::kAuto}; }
  static constexpr Length None() { return {0.0f, Unit::kNone}; }

  bool IsAuto() const { return unit == Unit::kAuto; }
  bool IsNone() const { return unit == Unit::kNone; }
  bool IsPercent() const { return unit == Unit::kPercent; }
  // Pixels, resolving percentages against `reference`.
  float Resolve(float reference) const {
    if (unit == Unit::kPercent) return value * reference / 100.0f;
    if (unit == Unit::kPixel) return value;
    return 0.0f;
  }
  bool operator==(const Length&) const = default;
};

// Linear RGBA, 0..1. Kept distinct from glm::vec4 so StyleValue can tell
// them apart.
struct Color {
  glm::vec4 rgba = glm::vec4(0.0f);
  constexpr Color() = default;
  constexpr Color(float r, float g, float b, float a = 1.0f) : rgba(r, g, b, a) {}
  explicit constexpr Color(glm::vec4 v) : rgba(v) {}
  static constexpr Color Clear() { return {0, 0, 0, 0}; }
  static constexpr Color White() { return {1, 1, 1, 1}; }
  static constexpr Color Black() { return {0, 0, 0, 1}; }
  bool operator==(const Color&) const = default;
};

// An image path (res://, absolute, or relative to the style sheet) or a
// texture made in code.
struct Background {
  std::string path;
  uint32_t texture_id = 0;  // UITexture id; wins over path when set
  bool IsSet() const { return texture_id != 0 || !path.empty(); }
  bool operator==(const Background&) const = default;
};

struct FontDefinition {
  std::string path;  // a .ttf; empty uses the default font
  bool operator==(const FontDefinition&) const = default;
};

struct Translate {
  Length x, y;
  bool operator==(const Translate&) const = default;
};

struct Scale {
  glm::vec2 value = glm::vec2(1.0f);
  bool operator==(const Scale&) const = default;
};

struct Rotate {
  float degrees = 0.0f;
  bool operator==(const Rotate&) const = default;
};

struct TransformOrigin {
  Length x = Length::Percent(50), y = Length::Percent(50);
  bool operator==(const TransformOrigin&) const = default;
};

struct TextShadow {
  glm::vec2 offset = glm::vec2(0.0f);
  float blur_radius = 0.0f;
  Color color = Color::Clear();
  bool operator==(const TextShadow&) const = default;
};

enum class EasingMode : uint8_t {
  kEase,
  kEaseIn,
  kEaseOut,
  kEaseInOut,
  kLinear,
  kEaseInSine,
  kEaseOutSine,
  kEaseInOutSine,
  kEaseInCubic,
  kEaseOutCubic,
  kEaseInOutCubic,
  kEaseInBack,
  kEaseOutBack,
  kEaseInOutBack,
  kEaseInBounce,
  kEaseOutBounce,
  kEaseInOutBounce,
};

// transition-property / -duration / -timing-function / -delay values.
struct PropertyNameList {
  std::vector<std::string> names{"all"};
  bool operator==(const PropertyNameList&) const = default;
};
struct TimeList {
  std::vector<float> seconds{0.0f};
  bool operator==(const TimeList&) const = default;
};
struct EasingList {
  std::vector<EasingMode> modes{EasingMode::kEase};
  bool operator==(const EasingList&) const = default;
};

enum class FlexDirection : uint8_t { kColumn, kColumnReverse, kRow, kRowReverse };
enum class Wrap : uint8_t { kNoWrap, kWrap, kWrapReverse };
enum class Align : uint8_t {
  kAuto,
  kFlexStart,
  kCenter,
  kFlexEnd,
  kStretch,
  kSpaceBetween,
  kSpaceAround,
};
enum class Justify : uint8_t {
  kFlexStart,
  kCenter,
  kFlexEnd,
  kSpaceBetween,
  kSpaceAround,
  kSpaceEvenly,
};
enum class Position : uint8_t { kRelative, kAbsolute };
enum class DisplayStyle : uint8_t { kFlex, kNone };
enum class Visibility : uint8_t { kVisible, kHidden };
enum class Overflow : uint8_t { kVisible, kHidden };
// -unity-text-align, Unity's TextAnchor.
enum class TextAnchor : uint8_t {
  kUpperLeft,
  kUpperCenter,
  kUpperRight,
  kMiddleLeft,
  kMiddleCenter,
  kMiddleRight,
  kLowerLeft,
  kLowerCenter,
  kLowerRight,
};
enum class FontStyle : uint8_t { kNormal, kBold, kItalic, kBoldAndItalic };
enum class WhiteSpace : uint8_t { kNormal, kNoWrap, kPre, kPreWrap };
enum class TextOverflow : uint8_t { kClip, kEllipsis };
enum class ScaleMode : uint8_t { kStretchToFill, kScaleAndCrop, kScaleToFit };

// X(Id, "uss-name", Type, member, default, inherited)
#define MERCURY_UI_STYLE_PROPERTIES(X)                                        \
  X(Width, "width", Length, width, Length::Auto(), false)                     \
  X(Height, "height", Length, height, Length::Auto(), false)                  \
  X(MinWidth, "min-width", Length, minWidth, Length::Auto(), false)           \
  X(MinHeight, "min-height", Length, minHeight, Length::Auto(), false)        \
  X(MaxWidth, "max-width", Length, maxWidth, Length::None(), false)           \
  X(MaxHeight, "max-height", Length, maxHeight, Length::None(), false)        \
  X(FlexDirection, "flex-direction", FlexDirection, flexDirection,            \
    FlexDirection::kColumn, false)                                            \
  X(FlexWrap, "flex-wrap", Wrap, flexWrap, Wrap::kNoWrap, false)              \
  X(FlexGrow, "flex-grow", float, flexGrow, 0.0f, false)                      \
  X(FlexShrink, "flex-shrink", float, flexShrink, 1.0f, false)                \
  X(FlexBasis, "flex-basis", Length, flexBasis, Length::Auto(), false)        \
  X(AlignItems, "align-items", Align, alignItems, Align::kStretch, false)     \
  X(AlignContent, "align-content", Align, alignContent, Align::kFlexStart,    \
    false)                                                                    \
  X(AlignSelf, "align-self", Align, alignSelf, Align::kAuto, false)           \
  X(JustifyContent, "justify-content", Justify, justifyContent,               \
    Justify::kFlexStart, false)                                               \
  X(Position, "position", Position, position, Position::kRelative, false)     \
  X(Left, "left", Length, left, Length::Auto(), false)                        \
  X(Top, "top", Length, top, Length::Auto(), false)                           \
  X(Right, "right", Length, right, Length::Auto(), false)                     \
  X(Bottom, "bottom", Length, bottom, Length::Auto(), false)                  \
  X(MarginLeft, "margin-left", Length, marginLeft, Length(), false)           \
  X(MarginTop, "margin-top", Length, marginTop, Length(), false)              \
  X(MarginRight, "margin-right", Length, marginRight, Length(), false)        \
  X(MarginBottom, "margin-bottom", Length, marginBottom, Length(), false)     \
  X(PaddingLeft, "padding-left", Length, paddingLeft, Length(), false)        \
  X(PaddingTop, "padding-top", Length, paddingTop, Length(), false)           \
  X(PaddingRight, "padding-right", Length, paddingRight, Length(), false)     \
  X(PaddingBottom, "padding-bottom", Length, paddingBottom, Length(), false)  \
  X(BorderLeftWidth, "border-left-width", float, borderLeftWidth, 0.0f,       \
    false)                                                                    \
  X(BorderTopWidth, "border-top-width", float, borderTopWidth, 0.0f, false)   \
  X(BorderRightWidth, "border-right-width", float, borderRightWidth, 0.0f,    \
    false)                                                                    \
  X(BorderBottomWidth, "border-bottom-width", float, borderBottomWidth, 0.0f, \
    false)                                                                    \
  X(BorderLeftColor, "border-left-color", Color, borderLeftColor,             \
    Color::Clear(), false)                                                    \
  X(BorderTopColor, "border-top-color", Color, borderTopColor,                \
    Color::Clear(), false)                                                    \
  X(BorderRightColor, "border-right-color", Color, borderRightColor,          \
    Color::Clear(), false)                                                    \
  X(BorderBottomColor, "border-bottom-color", Color, borderBottomColor,       \
    Color::Clear(), false)                                                    \
  X(BorderTopLeftRadius, "border-top-left-radius", Length,                    \
    borderTopLeftRadius, Length(), false)                                     \
  X(BorderTopRightRadius, "border-top-right-radius", Length,                  \
    borderTopRightRadius, Length(), false)                                    \
  X(BorderBottomRightRadius, "border-bottom-right-radius", Length,            \
    borderBottomRightRadius, Length(), false)                                 \
  X(BorderBottomLeftRadius, "border-bottom-left-radius", Length,              \
    borderBottomLeftRadius, Length(), false)                                  \
  X(BackgroundColor, "background-color", Color, backgroundColor,              \
    Color::Clear(), false)                                                    \
  X(BackgroundImage, "background-image", Background, backgroundImage,         \
    Background(), false)                                                      \
  X(BackgroundImageTintColor, "-unity-background-image-tint-color", Color,    \
    unityBackgroundImageTintColor, Color::White(), false)                     \
  X(BackgroundScaleMode, "-unity-background-scale-mode", ScaleMode,           \
    unityBackgroundScaleMode, ScaleMode::kStretchToFill, false)               \
  X(SliceLeft, "-unity-slice-left", float, unitySliceLeft, 0.0f, false)       \
  X(SliceTop, "-unity-slice-top", float, unitySliceTop, 0.0f, false)          \
  X(SliceRight, "-unity-slice-right", float, unitySliceRight, 0.0f, false)    \
  X(SliceBottom, "-unity-slice-bottom", float, unitySliceBottom, 0.0f, false) \
  X(SliceScale, "-unity-slice-scale", float, unitySliceScale, 1.0f, false)    \
  X(Color, "color", Color, color, Color::Black(), true)                       \
  X(FontSize, "font-size", float, fontSize, 12.0f, true)                      \
  X(Font, "-unity-font-definition", FontDefinition, unityFontDefinition,      \
    FontDefinition(), true)                                                   \
  X(FontStyle, "-unity-font-style", FontStyle, unityFontStyleAndWeight,       \
    FontStyle::kNormal, true)                                                 \
  X(TextAlign, "-unity-text-align", TextAnchor, unityTextAlign,               \
    TextAnchor::kUpperLeft, true)                                             \
  X(WhiteSpace, "white-space", WhiteSpace, whiteSpace, WhiteSpace::kNormal,   \
    true)                                                                     \
  X(TextOverflow, "text-overflow", TextOverflow, textOverflow,                \
    TextOverflow::kClip, false)                                               \
  X(LetterSpacing, "letter-spacing", float, letterSpacing, 0.0f, true)        \
  X(WordSpacing, "word-spacing", float, wordSpacing, 0.0f, true)              \
  X(ParagraphSpacing, "-unity-paragraph-spacing", float,                      \
    unityParagraphSpacing, 0.0f, true)                                        \
  X(TextOutlineWidth, "-unity-text-outline-width", float,                     \
    unityTextOutlineWidth, 0.0f, true)                                        \
  X(TextOutlineColor, "-unity-text-outline-color", Color,                     \
    unityTextOutlineColor, Color::Clear(), true)                              \
  X(TextShadow, "text-shadow", TextShadow, textShadow, TextShadow(), true)    \
  X(Opacity, "opacity", float, opacity, 1.0f, false)                          \
  X(Display, "display", DisplayStyle, display, DisplayStyle::kFlex, false)    \
  X(Visibility, "visibility", Visibility, visibility, Visibility::kVisible,   \
    true)                                                                     \
  X(Overflow, "overflow", Overflow, overflow, Overflow::kVisible, false)      \
  X(Translate, "translate", Translate, translate, Translate(), false)         \
  X(Scale, "scale", Scale, scale, Scale(), false)                             \
  X(Rotate, "rotate", Rotate, rotate, Rotate(), false)                        \
  X(TransformOrigin, "transform-origin", TransformOrigin, transformOrigin,    \
    TransformOrigin(), false)                                                 \
  X(TransitionProperty, "transition-property", PropertyNameList,              \
    transitionProperty, PropertyNameList(), false)                            \
  X(TransitionDuration, "transition-duration", TimeList, transitionDuration,  \
    TimeList(), false)                                                        \
  X(TransitionTimingFunction, "transition-timing-function", EasingList,       \
    transitionTimingFunction, EasingList(), false)                            \
  X(TransitionDelay, "transition-delay", TimeList, transitionDelay,           \
    TimeList(), false)

enum class StylePropertyId : uint8_t {
#define MERCURY_UI_X(id, name, type, member, def, inherited) k##id,
  MERCURY_UI_STYLE_PROPERTIES(MERCURY_UI_X)
#undef MERCURY_UI_X
      kCount
};
constexpr size_t kStylePropertyCount =
    static_cast<size_t>(StylePropertyId::kCount);

// Any one property's value. Enums are stored as their underlying int.
using StyleValue =
    std::variant<std::monostate, Length, float, Color, int, Background,
                 FontDefinition, Translate, Scale, Rotate, TransformOrigin,
                 TextShadow, PropertyNameList, TimeList, EasingList>;

/**
 * @brief The resolved value of every style property (Unity's resolvedStyle)
 */
struct ComputedStyle {
#define MERCURY_UI_X(id, name, type, member, def, inherited) type member = def;
  MERCURY_UI_STYLE_PROPERTIES(MERCURY_UI_X)
#undef MERCURY_UI_X

  StyleValue Get(StylePropertyId id) const;
  void Set(StylePropertyId id, const StyleValue& value);
  // Copy the inherited properties from a parent's style.
  void InheritFrom(const ComputedStyle& parent);
  bool operator==(const ComputedStyle&) const = default;
};

/**
 * @brief Values set on one element from code or a UXML style="" attribute;
 * they win over every style sheet (Unity's element.style)
 */
struct InlineStyle {
#define MERCURY_UI_X(id, name, type, member, def, inherited) \
  std::optional<type> member;
  MERCURY_UI_STYLE_PROPERTIES(MERCURY_UI_X)
#undef MERCURY_UI_X

  // Shorthand setters, like the USS shorthands.
  void SetMargin(Length all);
  void SetPadding(Length all);
  void SetBorderWidth(float all);
  void SetBorderColor(Color all);
  void SetBorderRadius(Length all);

  bool Has(StylePropertyId id) const;
  void Clear(StylePropertyId id);
  void Set(StylePropertyId id, const StyleValue& value);
  StyleValue Get(StylePropertyId id) const;  // monostate when unset
  void ApplyTo(ComputedStyle& style) const;
};

struct StylePropertyInfo {
  StylePropertyId id;
  const char* name;
  bool inherited;
  bool animatable;
};

const StylePropertyInfo& GetStylePropertyInfo(StylePropertyId id);
std::optional<StylePropertyId> FindStyleProperty(std::string_view uss_name);

// The default value (as in a fresh ComputedStyle).
StyleValue DefaultStyleValue(StylePropertyId id);

// Value interpolation for transitions; returns `to` when not animatable.
StyleValue Interpolate(StylePropertyId id, const StyleValue& from,
                       const StyleValue& to, float t);
float Ease(EasingMode mode, float t);

// Formats a value back to USS text, e.g. "10px", "rgba(255, 0, 0, 0.5)".
std::string FormatStyleValue(StylePropertyId id, const StyleValue& value);

// Keyword names of an enum-valued property, in enum order; empty otherwise.
const std::vector<std::string>& StyleKeywords(StylePropertyId id);

}  // namespace Mercury::UI

#endif  // MERCURY_UI_STYLE_H
