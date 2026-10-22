/*
 * style_sheet.h
 *
 * USS style sheets: a CSS subset, as in Unity UI Toolkit.
 *
 *   @import url("common.uss");
 *   :root { --accent: #ffcc33; }
 *   .hud-label { font-size: 24px; color: var(--accent); }
 *   #score > Label:hover { scale: 1.1 1.1; transition: scale 0.2s ease-out; }
 *
 * Selectors: Type, .class, #name, *, :hover :active :inactive :focus
 * :checked :disabled :enabled :root, descendant (A B) and child (A > B)
 * combinators, and comma lists. Values may use var(--name, fallback).
 */
#ifndef MERCURY_UI_STYLE_SHEET_H
#define MERCURY_UI_STYLE_SHEET_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ui/style.h"

namespace Mercury::UI {

class VisualElement;

struct StyleToken {
  enum class Type {
    kIdent,       // auto, flex-start, --my-var
    kNumber,      // 10
    kDimension,   // 10px, 2s, 45deg (unit in `unit`)
    kPercentage,  // 50%
    kHash,        // #ff0000 (text without the #)
    kString,      // "quoted"
    kFunction,    // name(args...), args in `args`
    kComma,
  };
  Type type = Type::kIdent;
  std::string text;
  float number = 0.0f;
  std::string unit;
  std::vector<StyleToken> args;

  bool IsIdent(std::string_view s) const {
    return type == Type::kIdent && text == s;
  }
};

using TokenList = std::vector<StyleToken>;

// Tokenizes a declaration's value text.
TokenList TokenizeStyleValue(std::string_view text);
std::string TokensToString(const TokenList& tokens);

enum PseudoState : uint32_t {
  kPseudoNone = 0,
  kPseudoHover = 1 << 0,
  kPseudoActive = 1 << 1,
  kPseudoFocus = 1 << 2,
  kPseudoChecked = 1 << 3,
  kPseudoDisabled = 1 << 4,
  kPseudoRoot = 1 << 5,
};

struct SimpleSelector {
  std::string type;  // empty or "*" matches any type
  std::string name;  // #name
  std::vector<std::string> classes;
  uint32_t pseudo = 0;      // states that must be set
  uint32_t not_pseudo = 0;  // states that must be clear (:enabled, :inactive)
};

struct ComplexSelector {
  enum class Combinator { kDescendant, kChild };
  // Left to right; combinators[i] joins parts[i] and parts[i + 1].
  std::vector<SimpleSelector> parts;
  std::vector<Combinator> combinators;
  int specificity = 0;
  std::string text;

  bool Matches(const VisualElement& element) const;
  // Like Matches, but ignores pseudo states on the rightmost part (used to
  // know which rules may apply once the element is hovered, etc.).
  bool MatchesIgnoringState(const VisualElement& element) const;
};

std::optional<ComplexSelector> ParseSelector(std::string_view text,
                                             std::string* error = nullptr);

struct StyleDeclaration {
  std::string property;  // as written, e.g. "margin" or "--accent"
  std::string value;     // as written
  TokenList tokens;
  int line = 0;
};

struct StyleRule {
  std::vector<ComplexSelector> selectors;
  std::vector<StyleDeclaration> declarations;
  int line = 0;

  std::string SelectorText() const;
  const StyleDeclaration* Find(std::string_view property) const;
  void SetDeclaration(std::string_view property, std::string_view value);
  void RemoveDeclaration(std::string_view property);
};

struct StyleParseError {
  int line = 0;
  std::string message;
};

class StyleSheet {
 public:
  // Where the sheet came from; relative url()s resolve against it.
  std::string path;
  std::vector<std::string> imports;  // @import targets, resolved
  std::vector<StyleRule> rules;

  static std::shared_ptr<StyleSheet> Parse(
      std::string_view text, std::string path = "",
      std::vector<StyleParseError>* errors = nullptr);

  std::string Write() const;

  // The first rule whose selector text is exactly `selector`.
  StyleRule* FindRule(std::string_view selector);
  StyleRule& AddRule(std::string_view selector);
};

// A property set by one declaration, after shorthand expansion.
struct ExpandedDeclaration {
  StylePropertyId id;
  TokenList tokens;
};

/**
 * @brief Expands shorthands (margin, padding, border-width, border-color,
 * border-radius, flex, transition, -unity-text-outline) into properties
 *
 * Unknown properties return nothing; custom properties are not handled here.
 */
std::vector<ExpandedDeclaration> ExpandDeclaration(std::string_view property,
                                                   const TokenList& tokens);

/**
 * @brief Converts a value to a property's type
 * @param base_path path of the sheet or UXML file, for relative url()s
 * @return nullopt if the value is invalid; "initial" gives the default value
 */
std::optional<StyleValue> ParseStyleValue(StylePropertyId id,
                                          const TokenList& tokens,
                                          std::string_view base_path = "");

// Replaces var(--name, fallback) using `variables`; false if a variable is
// missing and has no fallback.
bool SubstituteVariables(
    const TokenList& tokens,
    const std::unordered_map<std::string, TokenList>& variables,
    TokenList& out);

// Parses "#fff", "rgba(...)", named colors.
std::optional<Color> ParseColor(std::string_view text);

// Joins a relative path onto the directory of `base` ("res://ui/hud.uss" +
// "../img/a.png" = "res://img/a.png"). Absolute and res:// paths pass through.
std::string ResolveRelativePath(std::string_view base, std::string_view path);

// Parses a style="a: b; c: d" attribute into declarations.
std::vector<StyleDeclaration> ParseInlineStyle(std::string_view text);

}  // namespace Mercury::UI

#endif  // MERCURY_UI_STYLE_SHEET_H
