// USS tokenizer, selector and sheet parser, and value conversion.

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>

#include "ui/style_sheet.h"

namespace Mercury::UI {

namespace {

bool IsIdentStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '-' ||
         static_cast<unsigned char>(c) >= 0x80;
}
bool IsIdentChar(char c) {
  return IsIdentStart(c) || std::isdigit(static_cast<unsigned char>(c));
}

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

class ValueTokenizer {
 public:
  explicit ValueTokenizer(std::string_view text) : text_(text) {}

  TokenList ReadList(bool inside_function) {
    TokenList tokens;
    while (true) {
      SkipSpace();
      if (pos_ >= text_.size()) break;
      char c = text_[pos_];
      if (c == ')') {
        if (inside_function) {
          pos_++;
          break;
        }
        pos_++;  // stray ')'
        continue;
      }
      if (c == ',') {
        pos_++;
        tokens.push_back({StyleToken::Type::kComma, ","});
        continue;
      }
      if (c == '"' || c == '\'') {
        tokens.push_back({StyleToken::Type::kString, ReadString(c)});
        continue;
      }
      if (c == '#') {
        pos_++;
        size_t start = pos_;
        while (pos_ < text_.size() && IsIdentChar(text_[pos_])) pos_++;
        tokens.push_back(
            {StyleToken::Type::kHash, std::string(text_.substr(start, pos_ - start))});
        continue;
      }
      if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' ||
          ((c == '-' || c == '+') && pos_ + 1 < text_.size() &&
           (std::isdigit(static_cast<unsigned char>(text_[pos_ + 1])) ||
            text_[pos_ + 1] == '.'))) {
        tokens.push_back(ReadNumber());
        continue;
      }
      if (IsIdentStart(c)) {
        size_t start = pos_;
        while (pos_ < text_.size() && IsIdentChar(text_[pos_])) pos_++;
        std::string ident(text_.substr(start, pos_ - start));
        if (pos_ < text_.size() && text_[pos_] == '(') {
          pos_++;
          StyleToken fn{StyleToken::Type::kFunction, Lower(ident)};
          if (fn.text == "url" || fn.text == "resource") {
            // url(foo.png) may be unquoted.
            SkipSpace();
            if (pos_ < text_.size() && text_[pos_] != '"' && text_[pos_] != '\'') {
              size_t s = pos_;
              while (pos_ < text_.size() && text_[pos_] != ')') pos_++;
              fn.args.push_back({StyleToken::Type::kString,
                                 std::string(Trim(text_.substr(s, pos_ - s)))});
              if (pos_ < text_.size()) pos_++;
              tokens.push_back(std::move(fn));
              continue;
            }
          }
          fn.args = ReadList(true);
          tokens.push_back(std::move(fn));
        } else {
          tokens.push_back({StyleToken::Type::kIdent, ident});
        }
        continue;
      }
      pos_++;  // unknown character (e.g. '/', '!important' bang)
    }
    return tokens;
  }

 private:
  void SkipSpace() {
    while (pos_ < text_.size() &&
           std::isspace(static_cast<unsigned char>(text_[pos_]))) {
      pos_++;
    }
  }

  std::string ReadString(char quote) {
    pos_++;
    std::string out;
    while (pos_ < text_.size() && text_[pos_] != quote) {
      if (text_[pos_] == '\\' && pos_ + 1 < text_.size()) pos_++;
      out += text_[pos_++];
    }
    if (pos_ < text_.size()) pos_++;
    return out;
  }

  StyleToken ReadNumber() {
    size_t start = pos_;
    if (text_[pos_] == '-' || text_[pos_] == '+') pos_++;
    while (pos_ < text_.size() &&
           (std::isdigit(static_cast<unsigned char>(text_[pos_])) ||
            text_[pos_] == '.')) {
      pos_++;
    }
    // Exponents (1e3) are rare in USS; not supported.
    std::string number(text_.substr(start, pos_ - start));
    StyleToken token;
    token.number = std::strtof(number.c_str(), nullptr);
    token.text = number;
    if (pos_ < text_.size() && text_[pos_] == '%') {
      pos_++;
      token.type = StyleToken::Type::kPercentage;
      token.text += "%";
    } else if (pos_ < text_.size() &&
               std::isalpha(static_cast<unsigned char>(text_[pos_]))) {
      size_t u = pos_;
      while (pos_ < text_.size() &&
             std::isalpha(static_cast<unsigned char>(text_[pos_]))) {
        pos_++;
      }
      token.type = StyleToken::Type::kDimension;
      token.unit = Lower(text_.substr(u, pos_ - u));
      token.text += token.unit;
    } else {
      token.type = StyleToken::Type::kNumber;
    }
    return token;
  }

  std::string_view text_;
  size_t pos_ = 0;
};

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
    case StyleToken::Type::kNumber: return Length::Px(t.number);
    case StyleToken::Type::kPercentage: return Length::Percent(t.number);
    case StyleToken::Type::kDimension:
      if (t.unit == "px") return Length::Px(t.number);
      return std::nullopt;
    case StyleToken::Type::kIdent:
      if (t.text == "auto") return Length::Auto();
      if (t.text == "none") return Length::None();
      return std::nullopt;
    default: return std::nullopt;
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
  if (t.type == StyleToken::Type::kString) return ResolveRelativePath(base, t.text);
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
  static const char* kNames[] = {
      "ease",           "ease-in",           "ease-out",
      "ease-in-out",    "linear",            "ease-in-sine",
      "ease-out-sine",  "ease-in-out-sine",  "ease-in-cubic",
      "ease-out-cubic", "ease-in-out-cubic", "ease-in-back",
      "ease-out-back",  "ease-in-out-back",  "ease-in-bounce",
      "ease-out-bounce", "ease-in-out-bounce"};
  if (t.type != StyleToken::Type::kIdent) return std::nullopt;
  for (size_t i = 0; i < std::size(kNames); i++) {
    if (t.text == kNames[i]) return static_cast<EasingMode>(i);
  }
  return std::nullopt;
}

// 1-4 values, CSS box order: top right bottom left.
bool ExpandBox(const TokenList& tokens, StylePropertyId top, StylePropertyId right,
               StylePropertyId bottom, StylePropertyId left,
               std::vector<ExpandedDeclaration>& out) {
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
    {"aliceblue", 0xF0F8FF},     {"antiquewhite", 0xFAEBD7},
    {"aqua", 0x00FFFF},          {"aquamarine", 0x7FFFD4},
    {"azure", 0xF0FFFF},         {"beige", 0xF5F5DC},
    {"bisque", 0xFFE4C4},        {"black", 0x000000},
    {"blanchedalmond", 0xFFEBCD}, {"blue", 0x0000FF},
    {"blueviolet", 0x8A2BE2},    {"brown", 0xA52A2A},
    {"burlywood", 0xDEB887},     {"cadetblue", 0x5F9EA0},
    {"chartreuse", 0x7FFF00},    {"chocolate", 0xD2691E},
    {"coral", 0xFF7F50},         {"cornflowerblue", 0x6495ED},
    {"cornsilk", 0xFFF8DC},      {"crimson", 0xDC143C},
    {"cyan", 0x00FFFF},          {"darkblue", 0x00008B},
    {"darkcyan", 0x008B8B},      {"darkgoldenrod", 0xB8860B},
    {"darkgray", 0xA9A9A9},      {"darkgreen", 0x006400},
    {"darkgrey", 0xA9A9A9},      {"darkkhaki", 0xBDB76B},
    {"darkmagenta", 0x8B008B},   {"darkolivegreen", 0x556B2F},
    {"darkorange", 0xFF8C00},    {"darkorchid", 0x9932CC},
    {"darkred", 0x8B0000},       {"darksalmon", 0xE9967A},
    {"darkseagreen", 0x8FBC8F},  {"darkslateblue", 0x483D8B},
    {"darkslategray", 0x2F4F4F}, {"darkslategrey", 0x2F4F4F},
    {"darkturquoise", 0x00CED1}, {"darkviolet", 0x9400D3},
    {"deeppink", 0xFF1493},      {"deepskyblue", 0x00BFFF},
    {"dimgray", 0x696969},       {"dimgrey", 0x696969},
    {"dodgerblue", 0x1E90FF},    {"firebrick", 0xB22222},
    {"floralwhite", 0xFFFAF0},   {"forestgreen", 0x228B22},
    {"fuchsia", 0xFF00FF},       {"gainsboro", 0xDCDCDC},
    {"ghostwhite", 0xF8F8FF},    {"gold", 0xFFD700},
    {"goldenrod", 0xDAA520},     {"gray", 0x808080},
    {"green", 0x008000},         {"greenyellow", 0xADFF2F},
    {"grey", 0x808080},          {"honeydew", 0xF0FFF0},
    {"hotpink", 0xFF69B4},       {"indianred", 0xCD5C5C},
    {"indigo", 0x4B0082},        {"ivory", 0xFFFFF0},
    {"khaki", 0xF0E68C},         {"lavender", 0xE6E6FA},
    {"lavenderblush", 0xFFF0F5}, {"lawngreen", 0x7CFC00},
    {"lemonchiffon", 0xFFFACD},  {"lightblue", 0xADD8E6},
    {"lightcoral", 0xF08080},    {"lightcyan", 0xE0FFFF},
    {"lightgoldenrodyellow", 0xFAFAD2}, {"lightgray", 0xD3D3D3},
    {"lightgreen", 0x90EE90},    {"lightgrey", 0xD3D3D3},
    {"lightpink", 0xFFB6C1},     {"lightsalmon", 0xFFA07A},
    {"lightseagreen", 0x20B2AA}, {"lightskyblue", 0x87CEFA},
    {"lightslategray", 0x778899}, {"lightslategrey", 0x778899},
    {"lightsteelblue", 0xB0C4DE}, {"lightyellow", 0xFFFFE0},
    {"lime", 0x00FF00},          {"limegreen", 0x32CD32},
    {"linen", 0xFAF0E6},         {"magenta", 0xFF00FF},
    {"maroon", 0x800000},        {"mediumaquamarine", 0x66CDAA},
    {"mediumblue", 0x0000CD},    {"mediumorchid", 0xBA55D3},
    {"mediumpurple", 0x9370DB},  {"mediumseagreen", 0x3CB371},
    {"mediumslateblue", 0x7B68EE}, {"mediumspringgreen", 0x00FA9A},
    {"mediumturquoise", 0x48D1CC}, {"mediumvioletred", 0xC71585},
    {"midnightblue", 0x191970},  {"mintcream", 0xF5FFFA},
    {"mistyrose", 0xFFE4E1},     {"moccasin", 0xFFE4B5},
    {"navajowhite", 0xFFDEAD},   {"navy", 0x000080},
    {"oldlace", 0xFDF5E6},       {"olive", 0x808000},
    {"olivedrab", 0x6B8E23},     {"orange", 0xFFA500},
    {"orangered", 0xFF4500},     {"orchid", 0xDA70D6},
    {"palegoldenrod", 0xEEE8AA}, {"palegreen", 0x98FB98},
    {"paleturquoise", 0xAFEEEE}, {"palevioletred", 0xDB7093},
    {"papayawhip", 0xFFEFD5},    {"peachpuff", 0xFFDAB9},
    {"peru", 0xCD853F},          {"pink", 0xFFC0CB},
    {"plum", 0xDDA0DD},          {"powderblue", 0xB0E0E6},
    {"purple", 0x800080},        {"rebeccapurple", 0x663399},
    {"red", 0xFF0000},           {"rosybrown", 0xBC8F8F},
    {"royalblue", 0x4169E1},     {"saddlebrown", 0x8B4513},
    {"salmon", 0xFA8072},        {"sandybrown", 0xF4A460},
    {"seagreen", 0x2E8B57},      {"seashell", 0xFFF5EE},
    {"sienna", 0xA0522D},        {"silver", 0xC0C0C0},
    {"skyblue", 0x87CEEB},       {"slateblue", 0x6A5ACD},
    {"slategray", 0x708090},     {"slategrey", 0x708090},
    {"snow", 0xFFFAFA},          {"springgreen", 0x00FF7F},
    {"steelblue", 0x4682B4},     {"tan", 0xD2B48C},
    {"teal", 0x008080},          {"thistle", 0xD8BFD8},
    {"tomato", 0xFF6347},        {"turquoise", 0x40E0D0},
    {"violet", 0xEE82EE},        {"wheat", 0xF5DEB3},
    {"white", 0xFFFFFF},         {"whitesmoke", 0xF5F5F5},
    {"yellow", 0xFFFF00},        {"yellowgreen", 0x9ACD32},
};

int CountLines(std::string_view text, size_t end) {
  return static_cast<int>(std::count(text.begin(), text.begin() + end, '\n')) +
         1;
}

}  // namespace

TokenList TokenizeStyleValue(std::string_view text) {
  return ValueTokenizer(text).ReadList(false);
}

std::string TokensToString(const TokenList& tokens) {
  std::string out;
  for (const auto& t : tokens) {
    if (t.type == StyleToken::Type::kComma) {
      out += ",";
      continue;
    }
    if (!out.empty() && out.back() != '(') out += " ";
    switch (t.type) {
      case StyleToken::Type::kHash: out += "#" + t.text; break;
      case StyleToken::Type::kString: out += "\"" + t.text + "\""; break;
      case StyleToken::Type::kFunction:
        out += t.text + "(" + TokensToString(t.args) + ")";
        break;
      default: out += t.text; break;
    }
  }
  return out;
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
  const bool absolute_dir = !base.starts_with("res://") && base.starts_with("/");
  return prefix + (absolute_dir ? "/" : "") + joined;
}

std::optional<ComplexSelector> ParseSelector(std::string_view text,
                                             std::string* error) {
  ComplexSelector selector;
  selector.text = std::string(Trim(text));
  std::string_view s = selector.text;
  size_t i = 0;
  auto fail = [&](std::string message) -> std::optional<ComplexSelector> {
    if (error) *error = std::move(message);
    return std::nullopt;
  };
  auto read_ident = [&]() {
    size_t start = i;
    while (i < s.size() && IsIdentChar(s[i])) i++;
    return std::string(s.substr(start, i - start));
  };

  bool pending_child = false;
  while (i < s.size()) {
    // Whitespace and '>' between parts.
    bool had_space = false;
    while (i < s.size() &&
           (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == '>')) {
      if (s[i] == '>') pending_child = true;
      had_space = true;
      i++;
    }
    if (i >= s.size()) break;
    if (!selector.parts.empty()) {
      if (!had_space && !pending_child) return fail("unexpected character");
      selector.combinators.push_back(pending_child
                                         ? ComplexSelector::Combinator::kChild
                                         : ComplexSelector::Combinator::kDescendant);
    }
    pending_child = false;

    SimpleSelector part;
    bool any = false;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) &&
           s[i] != '>') {
      const char c = s[i];
      if (c == '*') {
        part.type = "*";
        i++;
      } else if (c == '.') {
        i++;
        std::string cls = read_ident();
        if (cls.empty()) return fail("expected a class name after '.'");
        part.classes.push_back(cls);
      } else if (c == '#') {
        i++;
        part.name = read_ident();
        if (part.name.empty()) return fail("expected a name after '#'");
      } else if (c == ':') {
        i++;
        std::string pseudo = Lower(read_ident());
        if (pseudo == "hover") part.pseudo |= kPseudoHover;
        else if (pseudo == "active") part.pseudo |= kPseudoActive;
        else if (pseudo == "inactive") part.not_pseudo |= kPseudoActive;
        else if (pseudo == "focus") part.pseudo |= kPseudoFocus;
        else if (pseudo == "checked") part.pseudo |= kPseudoChecked;
        else if (pseudo == "disabled") part.pseudo |= kPseudoDisabled;
        else if (pseudo == "enabled") part.not_pseudo |= kPseudoDisabled;
        else if (pseudo == "root") part.pseudo |= kPseudoRoot;
        else return fail("unknown pseudo-class :" + pseudo);
      } else if (IsIdentStart(c)) {
        part.type = read_ident();
      } else {
        return fail(std::format("unexpected '{}' in selector", c));
      }
      any = true;
    }
    if (!any) return fail("empty selector");
    selector.parts.push_back(std::move(part));
  }
  if (selector.parts.empty()) return fail("empty selector");
  if (selector.combinators.size() + 1 != selector.parts.size()) {
    return fail("selector ends with a combinator");
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
  std::erase_if(declarations,
                [&](const StyleDeclaration& d) { return d.property == property; });
}

std::vector<StyleDeclaration> ParseInlineStyle(std::string_view text) {
  std::vector<StyleDeclaration> out;
  size_t start = 0;
  int depth = 0;
  char quote = 0;
  auto flush = [&](size_t end) {
    std::string_view decl = Trim(text.substr(start, end - start));
    const size_t colon = decl.find(':');
    if (!decl.empty() && colon != std::string_view::npos) {
      std::string_view value = Trim(decl.substr(colon + 1));
      if (value.ends_with("!important")) {
        value = Trim(value.substr(0, value.size() - 10));
      }
      out.push_back({Lower(Trim(decl.substr(0, colon))), std::string(value),
                     TokenizeStyleValue(value), 0});
      if (out.back().property.starts_with("--")) {
        // Custom property names are case-sensitive.
        out.back().property = std::string(Trim(decl.substr(0, colon)));
      }
    }
    start = end + 1;
  };
  for (size_t i = 0; i < text.size(); i++) {
    const char c = text[i];
    if (quote) {
      if (c == quote) quote = 0;
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '(') {
      depth++;
    } else if (c == ')') {
      depth--;
    } else if (c == ';' && depth <= 0) {
      flush(i);
    }
  }
  flush(text.size());
  return out;
}

std::shared_ptr<StyleSheet> StyleSheet::Parse(std::string_view text,
                                              std::string path,
                                              std::vector<StyleParseError>* errors) {
  auto sheet = std::make_shared<StyleSheet>();
  sheet->path = std::move(path);
  auto error = [&](size_t at, std::string message) {
    if (errors) errors->push_back({CountLines(text, at), std::move(message)});
  };

  // Strip comments first, keeping newlines so line numbers stay right.
  std::string clean(text);
  for (size_t i = 0; i + 1 < clean.size(); i++) {
    if (clean[i] == '"' || clean[i] == '\'') {
      const char q = clean[i];
      for (i++; i < clean.size() && clean[i] != q; i++) {
      }
      continue;
    }
    if (clean[i] == '/' && clean[i + 1] == '*') {
      size_t end = clean.find("*/", i + 2);
      if (end == std::string::npos) end = clean.size() - 2;
      for (size_t j = i; j < end + 2; j++) {
        if (clean[j] != '\n') clean[j] = ' ';
      }
      i = end + 1;
    }
  }
  std::string_view s = clean;

  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    if (i >= s.size()) break;

    if (s[i] == '@') {
      const size_t end = s.find(';', i);
      const size_t stop = end == std::string_view::npos ? s.size() : end;
      std::string_view rule = Trim(s.substr(i + 1, stop - i - 1));
      if (rule.starts_with("import")) {
        auto tokens = TokenizeStyleValue(rule.substr(6));
        if (!tokens.empty()) {
          if (auto p = ToPath(tokens[0], sheet->path)) {
            sheet->imports.push_back(*p);
          } else {
            error(i, "@import needs url(\"...\") or a string");
          }
        }
      } else {
        error(i, "unsupported at-rule @" + std::string(rule.substr(0, rule.find(' '))));
      }
      i = stop + 1;
      continue;
    }

    const size_t open = s.find('{', i);
    if (open == std::string_view::npos) {
      error(i, "expected '{'");
      break;
    }
    const size_t close = s.find('}', open);
    if (close == std::string_view::npos) {
      error(open, "missing '}'");
      break;
    }

    StyleRule rule;
    rule.line = CountLines(s, i);
    std::string_view selector_text = s.substr(i, open - i);
    size_t sel_start = 0;
    while (sel_start <= selector_text.size()) {
      size_t comma = selector_text.find(',', sel_start);
      if (comma == std::string_view::npos) comma = selector_text.size();
      std::string message;
      auto selector =
          ParseSelector(selector_text.substr(sel_start, comma - sel_start), &message);
      if (selector) {
        rule.selectors.push_back(std::move(*selector));
      } else {
        error(i, "bad selector \"" +
                     std::string(Trim(selector_text.substr(sel_start, comma - sel_start))) +
                     "\": " + message);
      }
      sel_start = comma + 1;
    }

    std::string_view body = s.substr(open + 1, close - open - 1);
    auto declarations = ParseInlineStyle(body);
    // Line numbers per declaration, found by searching the body in order.
    size_t search = 0;
    for (auto& d : declarations) {
      size_t at = body.find(d.property, search);
      if (at == std::string_view::npos) at = search;
      d.line = CountLines(s, open + 1 + at);
      search = at + d.property.size();
      if (!d.property.starts_with("--") && !FindStyleProperty(d.property) &&
          ExpandDeclaration(d.property, d.tokens).empty()) {
        error(open + 1 + at, "unknown property \"" + d.property + "\"");
      }
    }
    rule.declarations = std::move(declarations);
    if (!rule.selectors.empty()) sheet->rules.push_back(std::move(rule));
    i = close + 1;
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
  if (tokens.size() == 1 && (tokens[0].IsIdent("initial") ||
                             tokens[0].IsIdent("unset"))) {
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
          if (tokens.size() > 1 && tokens[1].type == StyleToken::Type::kNumber) {
            y = tokens[1].number;
          }
          return Scale{glm::vec2(first.number, y)};
        } else if constexpr (std::is_same_v<T, Rotate>) {
          if (first.IsIdent("none")) return Rotate{};
          if (auto d = ToDegrees(first)) return Rotate{*d};
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, TransformOrigin>) {
          auto keyword = [](const StyleToken& t) -> std::optional<Length> {
            if (t.IsIdent("left") || t.IsIdent("top")) return Length::Percent(0);
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

bool SubstituteVariables(
    const TokenList& tokens,
    const std::unordered_map<std::string, TokenList>& variables,
    TokenList& out) {
  for (const auto& t : tokens) {
    if (t.type == StyleToken::Type::kFunction && t.text == "var" &&
        !t.args.empty() && t.args[0].type == StyleToken::Type::kIdent) {
      auto it = variables.find(t.args[0].text);
      if (it != variables.end()) {
        if (!SubstituteVariables(it->second, variables, out)) return false;
        continue;
      }
      // Fallback: everything after the first comma.
      auto comma = std::find_if(t.args.begin(), t.args.end(), [](const auto& a) {
        return a.type == StyleToken::Type::kComma;
      });
      if (comma == t.args.end()) return false;
      TokenList fallback(comma + 1, t.args.end());
      if (!SubstituteVariables(fallback, variables, out)) return false;
      continue;
    }
    if (t.type == StyleToken::Type::kFunction) {
      StyleToken copy = t;
      copy.args.clear();
      if (!SubstituteVariables(t.args, variables, copy.args)) return false;
      out.push_back(std::move(copy));
      continue;
    }
    out.push_back(t);
  }
  return true;
}

}  // namespace Mercury::UI
