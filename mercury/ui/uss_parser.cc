// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// USS parser front end. Tokenizing, rules, declarations and selectors are
// parsed in Rust with the cssparser crate (mercury/uss/uss.rs) and cross the
// cxx bridge as flat structs; this file rebuilds the engine's
// StyleRule/ComplexSelector and StyleToken types from them, filters
// declarations against the style property whitelist and keeps the parts that
// are not parsing: value conversion (ParseStyleValue, ExpandDeclaration),
// color parsing (ParseColor) and serialization (Write).

#include <algorithm>
#include <bit>
#include <cctype>
#include <format>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ui/style_sheet.h"
#include "uss/uss.rs.h"

namespace Mercury::UI {

namespace {

std::string_view Trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return s;
}

std::string Lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return out;
}

// Shorthand names ExpandDeclaration knows. A known shorthand with a value
// that fails to expand is a value error, not an unknown property.
bool IsShorthand(std::string_view property) {
  static const char* kShorthands[] = {
      "margin",        "padding", "border-width", "border-color",
      "border-radius", "flex",    "transition",   "-unity-text-outline"};
  return std::find(std::begin(kShorthands), std::end(kShorthands), property) !=
         std::end(kShorthands);
}

// Token kinds cross the bridge in StyleToken::Type order (mercury/uss/uss.rs).
StyleToken ToStyleToken(const Mercury::uss::UssToken& raw) {
  StyleToken token;
  switch (raw.kind) {
    case 1:
      token.type = StyleToken::Type::kNumber;
      break;
    case 2:
      token.type = StyleToken::Type::kDimension;
      break;
    case 3:
      token.type = StyleToken::Type::kPercentage;
      break;
    case 4:
      token.type = StyleToken::Type::kHash;
      break;
    case 5:
      token.type = StyleToken::Type::kString;
      break;
    case 6:
      token.type = StyleToken::Type::kFunction;
      break;
    case 7:
      token.type = StyleToken::Type::kComma;
      break;
    default:
      token.type = StyleToken::Type::kIdent;
      break;
  }
  token.text = std::string(raw.text);
  token.number = raw.number;
  token.unit = std::string(raw.unit);
  return token;
}

// Function arguments arrive flattened in DFS preorder with a depth per
// token: a token's arguments follow it with depth + 1 until a token of the
// same or lower depth ends them.
TokenList ConvertTokens(const rust::Vec<Mercury::uss::UssToken>& flat) {
  std::vector<TokenList> stack;
  for (const auto& raw : flat) {
    while (stack.size() > static_cast<size_t>(raw.depth) + 1) {
      TokenList args = std::move(stack.back());
      stack.pop_back();
      if (!stack.empty() && !stack.back().empty()) {
        stack.back().back().args = std::move(args);
      }
    }
    while (stack.size() <= static_cast<size_t>(raw.depth)) {
      stack.emplace_back();
    }
    stack.back().push_back(ToStyleToken(raw));
  }
  while (stack.size() > 1) {
    TokenList args = std::move(stack.back());
    stack.pop_back();
    if (!stack.empty() && !stack.back().empty()) {
      stack.back().back().args = std::move(args);
    }
  }
  return stack.empty() ? TokenList{} : std::move(stack.front());
}

// The Rust parser takes raw bytes: a sheet saved with an encoding other
// than UTF-8 must not throw when cxx would validate a rust::Str.
rust::Slice<const std::uint8_t> AsRustBytes(std::string_view text) {
  return rust::Slice<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

ComplexSelector MapSelector(const Mercury::uss::UssSelector& raw) {
  ComplexSelector selector;
  selector.text = std::string(raw.text);
  for (const auto& raw_part : raw.parts) {
    SimpleSelector part;
    part.type = std::string(raw_part.type_name);
    part.name = std::string(raw_part.name);
    for (const auto& class_name : raw_part.classes) {
      part.classes.push_back(std::string(class_name));
    }
    part.pseudo = raw_part.pseudo;
    part.not_pseudo = raw_part.not_pseudo;
    selector.parts.push_back(std::move(part));
  }
  for (const auto& combinator : raw.combinators) {
    selector.combinators.push_back(
        combinator == 1 ? ComplexSelector::Combinator::kChild
                        : ComplexSelector::Combinator::kDescendant);
  }
  // Unity's specificity: names count most, then classes and pseudo-classes,
  // then types.
  int ids = 0, classes = 0, types = 0;
  for (const auto& part : selector.parts) {
    if (!part.name.empty()) ids++;
    classes += static_cast<int>(part.classes.size()) +
               std::popcount(part.pseudo) + std::popcount(part.not_pseudo);
    if (!part.type.empty() && part.type != "*") types++;
  }
  selector.specificity = ids * 10000 + classes * 100 + types;
  return selector;
}

// Splits a token list at top-level commas.
std::vector<TokenList> SplitCommas(const TokenList& tokens) {
  std::vector<TokenList> parts(1);
  for (const auto& t : tokens) {
    if (t.type == StyleToken::Type::kComma) {
      parts.emplace_back();
    } else {
      parts.back().push_back(t);
    }
  }
  return parts;
}

bool IsNumeric(const StyleToken& t) {
  return t.type == StyleToken::Type::kNumber ||
         t.type == StyleToken::Type::kDimension ||
         t.type == StyleToken::Type::kPercentage;
}

std::optional<Length> ToLength(const StyleToken& t) {
  switch (t.type) {
    case StyleToken::Type::kNumber:
      return Length::Px(t.number);
    case StyleToken::Type::kPercentage:
      return Length::Percent(t.number);
    case StyleToken::Type::kDimension:
      if (t.unit == "px") return Length::Px(t.number);
      return std::nullopt;
    case StyleToken::Type::kIdent:
      if (t.text == "auto") return Length::Auto();
      if (t.text == "none") return Length::None();
      return std::nullopt;
    default:
      return std::nullopt;
  }
}

std::optional<float> ToFloat(const StyleToken& t) {
  if (t.type == StyleToken::Type::kNumber) return t.number;
  if (t.type == StyleToken::Type::kDimension && t.unit == "px") return t.number;
  return std::nullopt;
}

std::optional<float> ToSeconds(const StyleToken& t) {
  if (t.type == StyleToken::Type::kNumber) return t.number;
  if (t.type != StyleToken::Type::kDimension) return std::nullopt;
  if (t.unit == "s") return t.number;
  if (t.unit == "ms") return t.number / 1000.0f;
  return std::nullopt;
}

std::optional<float> ToDegrees(const StyleToken& t) {
  constexpr float kPi = std::numbers::pi_v<float>;
  if (t.type == StyleToken::Type::kNumber) return t.number;
  if (t.type != StyleToken::Type::kDimension) return std::nullopt;
  if (t.unit == "deg") return t.number;
  if (t.unit == "rad") return t.number * 180.0f / kPi;
  if (t.unit == "grad") return t.number * 0.9f;
  if (t.unit == "turn") return t.number * 360.0f;
  return std::nullopt;
}

std::optional<Color> ToColor(const StyleToken& t) {
  if (t.type == StyleToken::Type::kHash) return ParseColor("#" + t.text);
  if (t.type == StyleToken::Type::kIdent) return ParseColor(t.text);
  if (t.type == StyleToken::Type::kFunction &&
      (t.text == "rgb" || t.text == "rgba")) {
    std::vector<float> values;
    for (const auto& a : t.args) {
      if (a.type == StyleToken::Type::kComma) continue;
      if (a.type == StyleToken::Type::kPercentage) {
        values.push_back(a.number / 100.0f * 255.0f);
      } else if (IsNumeric(a)) {
        values.push_back(a.number);
      } else {
        return std::nullopt;
      }
    }
    if (values.size() < 3) return std::nullopt;
    float alpha = values.size() >= 4 ? values[3] : 1.0f;
    return Color(values[0] / 255.0f, values[1] / 255.0f, values[2] / 255.0f,
                 alpha);
  }
  return std::nullopt;
}

std::optional<std::string> ToPath(const StyleToken& t, std::string_view base) {
  if (t.type == StyleToken::Type::kFunction &&
      (t.text == "url" || t.text == "resource") && !t.args.empty()) {
    const std::string& raw = t.args[0].text;
    if (t.text == "resource") {
      // resource("x") loads from the project root, like Unity's Resources.
      if (raw.starts_with("res://")) return raw;
      return "res://" + raw;
    }
    return ResolveRelativePath(base, raw);
  }
  if (t.type == StyleToken::Type::kString)
    return ResolveRelativePath(base, t.text);
  return std::nullopt;
}

template <typename E>
std::optional<int> ToKeyword(StylePropertyId id, const StyleToken& t) {
  if (t.type != StyleToken::Type::kIdent) return std::nullopt;
  const auto& keywords = StyleKeywords(id);
  for (size_t i = 0; i < keywords.size(); i++) {
    if (keywords[i] == t.text) return static_cast<int>(i);
  }
  return std::nullopt;
}

std::optional<EasingMode> ToEasing(const StyleToken& t) {
  static const char* kNames[] = {"ease",
                                 "ease-in",
                                 "ease-out",
                                 "ease-in-out",
                                 "linear",
                                 "ease-in-sine",
                                 "ease-out-sine",
                                 "ease-in-out-sine",
                                 "ease-in-cubic",
                                 "ease-out-cubic",
                                 "ease-in-out-cubic",
                                 "ease-in-back",
                                 "ease-out-back",
                                 "ease-in-out-back",
                                 "ease-in-bounce",
                                 "ease-out-bounce",
                                 "ease-in-out-bounce"};
  if (t.type != StyleToken::Type::kIdent) return std::nullopt;
  for (size_t i = 0; i < std::size(kNames); i++) {
    if (t.text == kNames[i]) return static_cast<EasingMode>(i);
  }
  return std::nullopt;
}

// 1-4 values, CSS box order: top right bottom left.
bool ExpandBox(const TokenList& tokens, StylePropertyId top,
               StylePropertyId right, StylePropertyId bottom,
               StylePropertyId left, std::vector<ExpandedDeclaration>& out) {
  if (tokens.empty() || tokens.size() > 4) return false;
  const size_t n = tokens.size();
  const StyleToken& t = tokens[0];
  const StyleToken& r = n > 1 ? tokens[1] : t;
  const StyleToken& b = n > 2 ? tokens[2] : t;
  const StyleToken& l = n > 3 ? tokens[3] : r;
  out.push_back({top, {t}});
  out.push_back({right, {r}});
  out.push_back({bottom, {b}});
  out.push_back({left, {l}});
  return true;
}

struct NamedColor {
  const char* name;
  uint32_t rgb;
};

constexpr NamedColor kNamedColors[] = {
    {"aliceblue", 0xF0F8FF},
    {"antiquewhite", 0xFAEBD7},
    {"aqua", 0x00FFFF},
    {"aquamarine", 0x7FFFD4},
    {"azure", 0xF0FFFF},
    {"beige", 0xF5F5DC},
    {"bisque", 0xFFE4C4},
    {"black", 0x000000},
    {"blanchedalmond", 0xFFEBCD},
    {"blue", 0x0000FF},
    {"blueviolet", 0x8A2BE2},
    {"brown", 0xA52A2A},
    {"burlywood", 0xDEB887},
    {"cadetblue", 0x5F9EA0},
    {"chartreuse", 0x7FFF00},
    {"chocolate", 0xD2691E},
    {"coral", 0xFF7F50},
    {"cornflowerblue", 0x6495ED},
    {"cornsilk", 0xFFF8DC},
    {"crimson", 0xDC143C},
    {"cyan", 0x00FFFF},
    {"darkblue", 0x00008B},
    {"darkcyan", 0x008B8B},
    {"darkgoldenrod", 0xB8860B},
    {"darkgray", 0xA9A9A9},
    {"darkgreen", 0x006400},
    {"darkgrey", 0xA9A9A9},
    {"darkkhaki", 0xBDB76B},
    {"darkmagenta", 0x8B008B},
    {"darkolivegreen", 0x556B2F},
    {"darkorange", 0xFF8C00},
    {"darkorchid", 0x9932CC},
    {"darkred", 0x8B0000},
    {"darksalmon", 0xE9967A},
    {"darkseagreen", 0x8FBC8F},
    {"darkslateblue", 0x483D8B},
    {"darkslategray", 0x2F4F4F},
    {"darkslategrey", 0x2F4F4F},
    {"darkturquoise", 0x00CED1},
    {"darkviolet", 0x9400D3},
    {"deeppink", 0xFF1493},
    {"deepskyblue", 0x00BFFF},
    {"dimgray", 0x696969},
    {"dimgrey", 0x696969},
    {"dodgerblue", 0x1E90FF},
    {"firebrick", 0xB22222},
    {"floralwhite", 0xFFFAF0},
    {"forestgreen", 0x228B22},
    {"fuchsia", 0xFF00FF},
    {"gainsboro", 0xDCDCDC},
    {"ghostwhite", 0xF8F8FF},
    {"gold", 0xFFD700},
    {"goldenrod", 0xDAA520},
    {"gray", 0x808080},
    {"green", 0x008000},
    {"greenyellow", 0xADFF2F},
    {"grey", 0x808080},
    {"honeydew", 0xF0FFF0},
    {"hotpink", 0xFF69B4},
    {"indianred", 0xCD5C5C},
    {"indigo", 0x4B0082},
    {"ivory", 0xFFFFF0},
    {"khaki", 0xF0E68C},
    {"lavender", 0xE6E6FA},
    {"lavenderblush", 0xFFF0F5},
    {"lawngreen", 0x7CFC00},
    {"lemonchiffon", 0xFFFACD},
    {"lightblue", 0xADD8E6},
    {"lightcoral", 0xF08080},
    {"lightcyan", 0xE0FFFF},
    {"lightgoldenrodyellow", 0xFAFAD2},
    {"lightgray", 0xD3D3D3},
    {"lightgreen", 0x90EE90},
    {"lightgrey", 0xD3D3D3},
    {"lightpink", 0xFFB6C1},
    {"lightsalmon", 0xFFA07A},
    {"lightseagreen", 0x20B2AA},
    {"lightskyblue", 0x87CEFA},
    {"lightslategray", 0x778899},
    {"lightslategrey", 0x778899},
    {"lightsteelblue", 0xB0C4DE},
    {"lightyellow", 0xFFFFE0},
    {"lime", 0x00FF00},
    {"limegreen", 0x32CD32},
    {"linen", 0xFAF0E6},
    {"magenta", 0xFF00FF},
    {"maroon", 0x800000},
    {"mediumaquamarine", 0x66CDAA},
    {"mediumblue", 0x0000CD},
    {"mediumorchid", 0xBA55D3},
    {"mediumpurple", 0x9370DB},
    {"mediumseagreen", 0x3CB371},
    {"mediumslateblue", 0x7B68EE},
    {"mediumspringgreen", 0x00FA9A},
    {"mediumturquoise", 0x48D1CC},
    {"mediumvioletred", 0xC71585},
    {"midnightblue", 0x191970},
    {"mintcream", 0xF5FFFA},
    {"mistyrose", 0xFFE4E1},
    {"moccasin", 0xFFE4B5},
    {"navajowhite", 0xFFDEAD},
    {"navy", 0x000080},
    {"oldlace", 0xFDF5E6},
    {"olive", 0x808000},
    {"olivedrab", 0x6B8E23},
    {"orange", 0xFFA500},
    {"orangered", 0xFF4500},
    {"orchid", 0xDA70D6},
    {"palegoldenrod", 0xEEE8AA},
    {"palegreen", 0x98FB98},
    {"paleturquoise", 0xAFEEEE},
    {"palevioletred", 0xDB7093},
    {"papayawhip", 0xFFEFD5},
    {"peachpuff", 0xFFDAB9},
    {"peru", 0xCD853F},
    {"pink", 0xFFC0CB},
    {"plum", 0xDDA0DD},
    {"powderblue", 0xB0E0E6},
    {"purple", 0x800080},
    {"rebeccapurple", 0x663399},
    {"red", 0xFF0000},
    {"rosybrown", 0xBC8F8F},
    {"royalblue", 0x4169E1},
    {"saddlebrown", 0x8B4513},
    {"salmon", 0xFA8072},
    {"sandybrown", 0xF4A460},
    {"seagreen", 0x2E8B57},
    {"seashell", 0xFFF5EE},
    {"sienna", 0xA0522D},
    {"silver", 0xC0C0C0},
    {"skyblue", 0x87CEEB},
    {"slateblue", 0x6A5ACD},
    {"slategray", 0x708090},
    {"slategrey", 0x708090},
    {"snow", 0xFFFAFA},
    {"springgreen", 0x00FF7F},
    {"steelblue", 0x4682B4},
    {"tan", 0xD2B48C},
    {"teal", 0x008080},
    {"thistle", 0xD8BFD8},
    {"tomato", 0xFF6347},
    {"turquoise", 0x40E0D0},
    {"violet", 0xEE82EE},
    {"wheat", 0xF5DEB3},
    {"white", 0xFFFFFF},
    {"whitesmoke", 0xF5F5F5},
    {"yellow", 0xFFFF00},
    {"yellowgreen", 0x9ACD32},
};

std::string TokensToStringImpl(const TokenList& tokens, int depth) {
  if (depth >= 32) return "";
  std::string out;
  for (const auto& t : tokens) {
    if (t.type == StyleToken::Type::kComma) {
      out += ",";
      continue;
    }
    if (!out.empty() && out.back() != '(') out += " ";
    switch (t.type) {
      case StyleToken::Type::kHash:
        out += "#" + t.text;
        break;
      case StyleToken::Type::kString:
        out += "\"" + t.text + "\"";
        break;
      case StyleToken::Type::kFunction:
        out += t.text + "(" + TokensToStringImpl(t.args, depth + 1) + ")";
        break;
      default:
        out += t.text;
        break;
    }
  }
  return out;
}

}  // namespace

TokenList TokenizeStyleValue(std::string_view text) {
  return ConvertTokens(Mercury::uss::TokenizeValue(AsRustBytes(text)));
}

std::string TokensToString(const TokenList& tokens) {
  return TokensToStringImpl(tokens, 0);
}

std::optional<Color> ParseColor(std::string_view text) {
  text = Trim(text);
  if (text.starts_with('#')) {
    std::string hex(text.substr(1));
    auto nibble = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      return -1;
    };
    for (char c : hex) {
      if (nibble(c) < 0) return std::nullopt;
    }
    std::vector<float> channels;
    if (hex.size() == 3 || hex.size() == 4) {
      for (char c : hex) channels.push_back(nibble(c) * 17 / 255.0f);
    } else if (hex.size() == 6 || hex.size() == 8) {
      for (size_t i = 0; i < hex.size(); i += 2) {
        channels.push_back((nibble(hex[i]) * 16 + nibble(hex[i + 1])) / 255.0f);
      }
    } else {
      return std::nullopt;
    }
    if (channels.size() == 3) channels.push_back(1.0f);
    return Color(channels[0], channels[1], channels[2], channels[3]);
  }
  if (text.starts_with("rgb")) {
    auto tokens = TokenizeStyleValue(text);
    if (tokens.size() == 1) return ToColor(tokens[0]);
    return std::nullopt;
  }
  const std::string name = Lower(text);
  if (name == "transparent" || name == "clear") return Color::Clear();
  for (const auto& named : kNamedColors) {
    if (name == named.name) {
      return Color(((named.rgb >> 16) & 0xFF) / 255.0f,
                   ((named.rgb >> 8) & 0xFF) / 255.0f,
                   (named.rgb & 0xFF) / 255.0f, 1.0f);
    }
  }
  return std::nullopt;
}

std::string ResolveRelativePath(std::string_view base, std::string_view path) {
  if (path.empty()) return "";
  if (path.starts_with("res://") || path.starts_with("/") ||
      path.find(":/") != std::string_view::npos ||
      path.find(":\\") != std::string_view::npos) {
    return std::string(path);
  }
  // Unity's project://database/Assets/... form maps to the project root.
  if (path.starts_with("project://database/")) {
    return "res://" + std::string(path.substr(19));
  }
  std::string prefix;
  std::string dir(base);
  if (dir.starts_with("res://")) {
    prefix = "res://";
    dir = dir.substr(6);
  }
  const size_t slash = dir.find_last_of("/\\");
  dir = slash == std::string::npos ? "" : dir.substr(0, slash);

  // Normalize "a/b/../c" without touching the filesystem.
  std::string joined;
  {
    std::vector<std::string> out;
    auto add = [&out](std::string_view s) {
      size_t start = 0;
      while (start <= s.size()) {
        size_t end = s.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = s.size();
        std::string_view part = s.substr(start, end - start);
        if (part == "..") {
          if (!out.empty() && out.back() != "..") {
            out.pop_back();
          } else {
            out.emplace_back("..");
          }
        } else if (!part.empty() && part != ".") {
          out.emplace_back(part);
        }
        start = end + 1;
      }
    };
    add(dir);
    add(path);
    for (const auto& p : out) joined += (joined.empty() ? "" : "/") + p;
  }
  const bool absolute_dir =
      !base.starts_with("res://") && base.starts_with("/");
  return prefix + (absolute_dir ? "/" : "") + joined;
}

std::optional<ComplexSelector> ParseSelector(std::string_view text,
                                             std::string* error) {
  const Mercury::uss::UssSelectorResult result =
      Mercury::uss::ParseSelectorText(AsRustBytes(text));
  if (!result.ok) {
    if (error) *error = std::string(result.error);
    return std::nullopt;
  }
  return MapSelector(result.selector);
}

std::string StyleRule::SelectorText() const {
  std::string out;
  for (const auto& s : selectors) out += (out.empty() ? "" : ", ") + s.text;
  return out;
}

const StyleDeclaration* StyleRule::Find(std::string_view property) const {
  for (const auto& d : declarations) {
    if (d.property == property) return &d;
  }
  return nullptr;
}

void StyleRule::SetDeclaration(std::string_view property,
                               std::string_view value) {
  for (auto& d : declarations) {
    if (d.property == property) {
      d.value = std::string(value);
      d.tokens = TokenizeStyleValue(value);
      return;
    }
  }
  declarations.push_back({std::string(property), std::string(value),
                          TokenizeStyleValue(value), 0});
}

void StyleRule::RemoveDeclaration(std::string_view property) {
  std::erase_if(declarations, [&](const StyleDeclaration& d) {
    return d.property == property;
  });
}

std::vector<StyleDeclaration> ParseInlineStyle(std::string_view text) {
  const rust::Vec<Mercury::uss::UssDeclaration> parsed =
      Mercury::uss::ParseDeclarations(AsRustBytes(text));
  std::vector<StyleDeclaration> out;
  for (const auto& raw : parsed) {
    out.push_back({std::string(raw.name), std::string(raw.value),
                   ConvertTokens(raw.tokens), 0});
    // Property names are lowercased; custom property names are
    // case-sensitive.
    if (!out.back().property.starts_with("--")) {
      out.back().property = Lower(out.back().property);
    }
  }
  return out;
}

std::shared_ptr<StyleSheet> StyleSheet::Parse(
    std::string_view text, std::string path,
    std::vector<StyleParseError>* errors) {
  auto sheet = std::make_shared<StyleSheet>();
  sheet->path = std::move(path);
  const Mercury::uss::UssSheet data =
      Mercury::uss::ParseSheet(AsRustBytes(text));

  // Errors raised here (bad imports, unknown properties) are merged with
  // the parser's own by line, so the list stays in source order.
  std::vector<StyleParseError> local_errors;
  for (const auto& import : data.imports) {
    const std::string import_text = std::string(import.text);
    const TokenList tokens = TokenizeStyleValue(import_text);
    if (tokens.empty()) continue;
    if (auto resolved = ToPath(tokens[0], sheet->path)) {
      sheet->imports.push_back(std::move(*resolved));
    } else {
      local_errors.push_back({static_cast<int>(import.line),
                              "@import needs url(\"...\") or a string"});
    }
  }
  for (const auto& raw : data.rules) {
    StyleRule rule;
    rule.line = static_cast<int>(raw.line);
    for (const auto& selector : raw.selectors) {
      rule.selectors.push_back(MapSelector(selector));
    }
    for (const auto& raw_declaration : raw.declarations) {
      StyleDeclaration declaration;
      declaration.property = std::string(raw_declaration.name);
      declaration.value = std::string(raw_declaration.value);
      declaration.tokens = ConvertTokens(raw_declaration.tokens);
      declaration.line = static_cast<int>(raw_declaration.line);
      // Custom property names are case-sensitive; the rest are lowercased.
      if (!declaration.property.starts_with("--")) {
        declaration.property = Lower(declaration.property);
      }
      // Unknown properties are dropped and recorded; parsing continues. A
      // known shorthand whose value does not expand is kept, like the old
      // parser did, but reported as a value error.
      const bool custom = declaration.property.starts_with("--");
      const bool shorthand = IsShorthand(declaration.property);
      if (custom || FindStyleProperty(declaration.property) ||
          (shorthand &&
           !ExpandDeclaration(declaration.property, declaration.tokens)
                .empty())) {
        rule.declarations.push_back(std::move(declaration));
      } else if (shorthand) {
        const std::string property = declaration.property;
        const int line = declaration.line;
        rule.declarations.push_back(std::move(declaration));
        local_errors.push_back(
            {line, "invalid value for \"" + property + "\""});
      } else {
        local_errors.push_back(
            {declaration.line,
             "unknown property \"" + declaration.property + "\""});
      }
    }
    if (!rule.selectors.empty()) sheet->rules.push_back(std::move(rule));
  }
  if (errors) {
    size_t rust_at = 0;
    size_t local_at = 0;
    while (rust_at < data.errors.size() || local_at < local_errors.size()) {
      const bool rust_first = local_at >= local_errors.size() ||
                              (rust_at < data.errors.size() &&
                               static_cast<int>(data.errors[rust_at].line) <=
                                   local_errors[local_at].line);
      if (rust_first) {
        errors->push_back({static_cast<int>(data.errors[rust_at].line),
                           std::string(data.errors[rust_at].message)});
        rust_at++;
      } else {
        errors->push_back(std::move(local_errors[local_at++]));
      }
    }
  }
  return sheet;
}

std::string StyleSheet::Write() const {
  std::string out;
  for (const auto& import : imports) {
    out += std::format("@import url(\"{}\");\n", import);
  }
  if (!imports.empty()) out += "\n";
  for (size_t r = 0; r < rules.size(); r++) {
    const auto& rule = rules[r];
    out += rule.SelectorText() + " {\n";
    for (const auto& d : rule.declarations) {
      out += "    " + d.property + ": " + d.value + ";\n";
    }
    out += "}\n";
    if (r + 1 < rules.size()) out += "\n";
  }
  return out;
}

StyleRule* StyleSheet::FindRule(std::string_view selector) {
  for (auto& rule : rules) {
    if (rule.SelectorText() == Trim(selector)) return &rule;
  }
  return nullptr;
}

StyleRule& StyleSheet::AddRule(std::string_view selector) {
  StyleRule rule;
  size_t start = 0;
  while (start <= selector.size()) {
    size_t comma = selector.find(',', start);
    if (comma == std::string_view::npos) comma = selector.size();
    if (auto parsed = ParseSelector(selector.substr(start, comma - start))) {
      rule.selectors.push_back(std::move(*parsed));
    }
    start = comma + 1;
  }
  rules.push_back(std::move(rule));
  return rules.back();
}

std::vector<ExpandedDeclaration> ExpandDeclaration(std::string_view property,
                                                   const TokenList& tokens) {
  std::vector<ExpandedDeclaration> out;
  using P = StylePropertyId;
  if (auto id = FindStyleProperty(property)) {
    out.push_back({*id, tokens});
    return out;
  }
  // "initial" on a shorthand resets every longhand.
  const bool initial = tokens.size() == 1 && tokens[0].IsIdent("initial");
  auto box = [&](P top, P right, P bottom, P left) {
    if (initial) {
      for (P p : {top, right, bottom, left}) out.push_back({p, tokens});
    } else {
      ExpandBox(tokens, top, right, bottom, left, out);
    }
  };
  if (property == "margin") {
    box(P::kMarginTop, P::kMarginRight, P::kMarginBottom, P::kMarginLeft);
  } else if (property == "padding") {
    box(P::kPaddingTop, P::kPaddingRight, P::kPaddingBottom, P::kPaddingLeft);
  } else if (property == "border-width") {
    box(P::kBorderTopWidth, P::kBorderRightWidth, P::kBorderBottomWidth,
        P::kBorderLeftWidth);
  } else if (property == "border-color") {
    box(P::kBorderTopColor, P::kBorderRightColor, P::kBorderBottomColor,
        P::kBorderLeftColor);
  } else if (property == "border-radius") {
    // Corner order: top-left, top-right, bottom-right, bottom-left.
    box(P::kBorderTopLeftRadius, P::kBorderTopRightRadius,
        P::kBorderBottomRightRadius, P::kBorderBottomLeftRadius);
  } else if (property == "flex") {
    if (initial) {
      for (P p : {P::kFlexGrow, P::kFlexShrink, P::kFlexBasis}) {
        out.push_back({p, tokens});
      }
    } else if (tokens.size() == 1 && tokens[0].IsIdent("none")) {
      out.push_back({P::kFlexGrow, TokenizeStyleValue("0")});
      out.push_back({P::kFlexShrink, TokenizeStyleValue("0")});
      out.push_back({P::kFlexBasis, TokenizeStyleValue("auto")});
    } else if (tokens.size() == 1 && tokens[0].IsIdent("auto")) {
      out.push_back({P::kFlexGrow, TokenizeStyleValue("1")});
      out.push_back({P::kFlexShrink, TokenizeStyleValue("1")});
      out.push_back({P::kFlexBasis, TokenizeStyleValue("auto")});
    } else if (!tokens.empty()) {
      // grow [shrink] [basis]; a lone length is the basis.
      size_t i = 0;
      if (tokens[0].type == StyleToken::Type::kNumber) {
        out.push_back({P::kFlexGrow, {tokens[0]}});
        i = 1;
        if (i < tokens.size() && tokens[i].type == StyleToken::Type::kNumber) {
          out.push_back({P::kFlexShrink, {tokens[i]}});
          i++;
        } else {
          out.push_back({P::kFlexShrink, TokenizeStyleValue("1")});
        }
      }
      if (i < tokens.size()) {
        out.push_back({P::kFlexBasis, {tokens[i]}});
      } else {
        out.push_back({P::kFlexBasis, TokenizeStyleValue("0px")});
      }
    }
  } else if (property == "transition") {
    TokenList props, durations, easings, delays;
    for (const auto& part : SplitCommas(tokens)) {
      std::string name = "all";
      std::optional<float> duration, delay;
      std::string easing = "ease";
      for (const auto& t : part) {
        if (auto seconds = ToSeconds(t); seconds && IsNumeric(t)) {
          if (!duration) {
            duration = seconds;
          } else {
            delay = seconds;
          }
        } else if (ToEasing(t)) {
          easing = t.text;
        } else if (t.type == StyleToken::Type::kIdent) {
          name = t.text;
        }
      }
      auto append = [](TokenList& list, const std::string& text) {
        if (!list.empty()) list.push_back({StyleToken::Type::kComma, ","});
        for (auto& t : TokenizeStyleValue(text)) list.push_back(t);
      };
      append(props, name);
      append(durations, std::format("{}s", duration.value_or(0.0f)));
      append(easings, easing);
      append(delays, std::format("{}s", delay.value_or(0.0f)));
    }
    out.push_back({P::kTransitionProperty, props});
    out.push_back({P::kTransitionDuration, durations});
    out.push_back({P::kTransitionTimingFunction, easings});
    out.push_back({P::kTransitionDelay, delays});
  } else if (property == "-unity-text-outline") {
    for (const auto& t : tokens) {
      if (ToColor(t)) {
        out.push_back({P::kTextOutlineColor, {t}});
      } else {
        out.push_back({P::kTextOutlineWidth, {t}});
      }
    }
  }
  return out;
}

std::optional<StyleValue> ParseStyleValue(StylePropertyId id,
                                          const TokenList& tokens,
                                          std::string_view base_path) {
  if (tokens.empty()) return std::nullopt;
  if (tokens.size() == 1 &&
      (tokens[0].IsIdent("initial") || tokens[0].IsIdent("unset"))) {
    return DefaultStyleValue(id);
  }
  const StyleValue sample = DefaultStyleValue(id);
  const StyleToken& first = tokens[0];

  return std::visit(
      [&](const auto& def) -> std::optional<StyleValue> {
        using T = std::decay_t<decltype(def)>;
        if constexpr (std::is_same_v<T, Length>) {
          if (auto l = ToLength(first)) return *l;
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, float>) {
          if (auto f = ToFloat(first)) return *f;
          if (first.type == StyleToken::Type::kPercentage &&
              id == StylePropertyId::kOpacity) {
            return first.number / 100.0f;
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, Color>) {
          if (auto c = ToColor(first)) return *c;
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, int>) {
          if (auto k = ToKeyword<int>(id, first)) return *k;
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, Background>) {
          if (first.IsIdent("none")) return Background{};
          if (auto p = ToPath(first, base_path)) return Background{*p};
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, FontDefinition>) {
          if (auto p = ToPath(first, base_path)) return FontDefinition{*p};
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, Translate>) {
          if (first.IsIdent("none")) return Translate{};
          auto x = ToLength(first);
          auto y = tokens.size() > 1 ? ToLength(tokens[1]) : Length::Px(0);
          if (!x || !y) return std::nullopt;
          return Translate{*x, *y};
        } else if constexpr (std::is_same_v<T, Scale>) {
          if (first.IsIdent("none")) return Scale{};
          if (first.type != StyleToken::Type::kNumber) return std::nullopt;
          float y = first.number;
          if (tokens.size() > 1 &&
              tokens[1].type == StyleToken::Type::kNumber) {
            y = tokens[1].number;
          }
          return Scale{glm::vec2(first.number, y)};
        } else if constexpr (std::is_same_v<T, Rotate>) {
          if (first.IsIdent("none")) return Rotate{};
          if (auto d = ToDegrees(first)) return Rotate{*d};
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, TransformOrigin>) {
          auto keyword = [](const StyleToken& t) -> std::optional<Length> {
            if (t.IsIdent("left") || t.IsIdent("top"))
              return Length::Percent(0);
            if (t.IsIdent("center")) return Length::Percent(50);
            if (t.IsIdent("right") || t.IsIdent("bottom")) {
              return Length::Percent(100);
            }
            return ToLength(t);
          };
          auto x = keyword(first);
          auto y = tokens.size() > 1 ? keyword(tokens[1]) : Length::Percent(50);
          if (!x || !y) return std::nullopt;
          // "top left" style ordering: swap if the first is vertical.
          if (first.IsIdent("top") || first.IsIdent("bottom")) std::swap(x, y);
          return TransformOrigin{*x, *y};
        } else if constexpr (std::is_same_v<T, TextShadow>) {
          TextShadow shadow;
          std::vector<float> lengths;
          for (const auto& t : tokens) {
            if (auto c = ToColor(t)) {
              shadow.color = *c;
            } else if (auto f = ToFloat(t)) {
              lengths.push_back(*f);
            } else if (t.IsIdent("none")) {
              return TextShadow{};
            } else {
              return std::nullopt;
            }
          }
          if (lengths.size() >= 2) shadow.offset = {lengths[0], lengths[1]};
          if (lengths.size() >= 3) shadow.blur_radius = lengths[2];
          if (shadow.color == Color::Clear() && lengths.size() >= 2) {
            shadow.color = Color::Black();
          }
          return shadow;
        } else if constexpr (std::is_same_v<T, PropertyNameList>) {
          PropertyNameList list;
          list.names.clear();
          for (const auto& part : SplitCommas(tokens)) {
            if (part.size() != 1 || part[0].type != StyleToken::Type::kIdent) {
              return std::nullopt;
            }
            list.names.push_back(part[0].text);
          }
          return list;
        } else if constexpr (std::is_same_v<T, TimeList>) {
          TimeList list;
          list.seconds.clear();
          for (const auto& part : SplitCommas(tokens)) {
            if (part.size() != 1) return std::nullopt;
            auto s = ToSeconds(part[0]);
            if (!s) return std::nullopt;
            list.seconds.push_back(*s);
          }
          return list;
        } else if constexpr (std::is_same_v<T, EasingList>) {
          EasingList list;
          list.modes.clear();
          for (const auto& part : SplitCommas(tokens)) {
            if (part.size() != 1) return std::nullopt;
            auto e = ToEasing(part[0]);
            if (!e) return std::nullopt;
            list.modes.push_back(*e);
          }
          return list;
        } else {
          return std::nullopt;
        }
      },
      sample);
}

namespace {

bool SubstituteVariablesImpl(
    const TokenList& tokens,
    const std::unordered_map<std::string, TokenList>& variables, TokenList& out,
    int depth) {
  // var() chains and self-referencing custom properties would otherwise
  // recurse without bound.
  if (depth >= 32) return false;
  for (const auto& t : tokens) {
    if (t.type == StyleToken::Type::kFunction && t.text == "var" &&
        !t.args.empty() && t.args[0].type == StyleToken::Type::kIdent) {
      auto it = variables.find(t.args[0].text);
      if (it != variables.end()) {
        if (!SubstituteVariablesImpl(it->second, variables, out, depth + 1))
          return false;
        continue;
      }
      // Fallback: everything after the first comma.
      auto comma = std::find_if(
          t.args.begin(), t.args.end(),
          [](const auto& a) { return a.type == StyleToken::Type::kComma; });
      if (comma == t.args.end()) return false;
      TokenList fallback(comma + 1, t.args.end());
      if (!SubstituteVariablesImpl(fallback, variables, out, depth + 1))
        return false;
      continue;
    }
    if (t.type == StyleToken::Type::kFunction) {
      StyleToken copy = t;
      copy.args.clear();
      if (!SubstituteVariablesImpl(t.args, variables, copy.args, depth + 1))
        return false;
      out.push_back(std::move(copy));
      continue;
    }
    out.push_back(t);
  }
  return true;
}

}  // namespace

bool SubstituteVariables(
    const TokenList& tokens,
    const std::unordered_map<std::string, TokenList>& variables,
    TokenList& out) {
  return SubstituteVariablesImpl(tokens, variables, out, 0);
}

}  // namespace Mercury::UI
