#include "mtext.h"

#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>

namespace Mercury::mtext {

// ---------------------------------------------------------------- Variant ---

std::optional<bool> Variant::ToBool() const {
  if (auto v = Get<bool>()) return *v;
  if (auto v = Get<int64_t>()) return *v != 0;
  return std::nullopt;
}

std::optional<int64_t> Variant::ToInt() const {
  if (auto v = Get<int64_t>()) return *v;
  if (auto v = Get<double>()) return static_cast<int64_t>(std::llround(*v));
  if (auto v = Get<bool>()) return *v ? 1 : 0;
  return std::nullopt;
}

std::optional<double> Variant::ToFloat() const {
  if (auto v = Get<double>()) return *v;
  if (auto v = Get<int64_t>()) return static_cast<double>(*v);
  return std::nullopt;
}

std::optional<std::string> Variant::ToString() const {
  if (auto v = Get<std::string>()) return *v;
  return std::nullopt;
}

const char* TypeName(Variant::Type type) {
  switch (type) {
    case Variant::Type::kNil: return "null";
    case Variant::Type::kBool: return "bool";
    case Variant::Type::kInt: return "int";
    case Variant::Type::kFloat: return "float";
    case Variant::Type::kString: return "string";
    case Variant::Type::kVector2: return "Vector2";
    case Variant::Type::kVector3: return "Vector3";
    case Variant::Type::kColor: return "Color";
    case Variant::Type::kNodePath: return "NodePath";
    case Variant::Type::kExtResource: return "ExtResource";
    case Variant::Type::kResource: return "Resource";
    case Variant::Type::kArray: return "Array";
  }
  return "unknown";
}

// ----------------------------------------------------------- PropertyList ---

const Variant* PropertyList::Get(std::string_view key) const {
  for (const auto& item : items_) {
    if (item.key == key) return &item.value;
  }
  return nullptr;
}

Variant* PropertyList::Get(std::string_view key) {
  for (auto& item : items_) {
    if (item.key == key) return &item.value;
  }
  return nullptr;
}

void PropertyList::Set(std::string_view key, Variant value) {
  if (auto existing = Get(key)) {
    *existing = std::move(value);
    return;
  }
  items_.push_back(Property{std::string(key), std::move(value)});
}

bool PropertyList::Remove(std::string_view key) {
  for (auto it = items_.begin(); it != items_.end(); ++it) {
    if (it->key == key) {
      items_.erase(it);
      return true;
    }
  }
  return false;
}

std::string PropertyList::GetString(std::string_view key,
                                    std::string_view fallback) const {
  if (auto v = Get(key)) {
    if (auto s = v->Get<std::string>()) return *s;
  }
  return std::string(fallback);
}

int64_t PropertyList::GetInt(std::string_view key, int64_t fallback) const {
  if (auto v = Get(key)) return v->ToInt().value_or(fallback);
  return fallback;
}

double PropertyList::GetFloat(std::string_view key, double fallback) const {
  if (auto v = Get(key)) return v->ToFloat().value_or(fallback);
  return fallback;
}

bool PropertyList::GetBool(std::string_view key, bool fallback) const {
  if (auto v = Get(key)) return v->ToBool().value_or(fallback);
  return fallback;
}

// --------------------------------------------------------------- Document ---

Section& Document::Add(std::string tag) {
  sections.push_back(Section{std::move(tag)});
  return sections.back();
}

const Section* Document::Find(std::string_view tag) const {
  for (const auto& section : sections) {
    if (section.tag == tag) return &section;
  }
  return nullptr;
}

Section* Document::Find(std::string_view tag) {
  for (auto& section : sections) {
    if (section.tag == tag) return &section;
  }
  return nullptr;
}

std::vector<const Section*> Document::FindAll(std::string_view tag) const {
  std::vector<const Section*> found;
  for (const auto& section : sections) {
    if (section.tag == tag) found.push_back(&section);
  }
  return found;
}

std::string ParseError::ToString() const {
  return std::format("{}:{}: {}", line, column, message);
}

// ----------------------------------------------------------------- Parser ---

namespace {

bool IsIdentStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool IsIdentChar(char c) {
  return IsIdentStart(c) || (c >= '0' && c <= '9') || c == '/' || c == '.' ||
         c == '-';
}

class Parser {
 public:
  explicit Parser(std::string_view source) : src_(source) {}

  bool ParseDocument(Document& out) {
    SkipSpace();
    while (!AtEnd()) {
      if (Peek() == '[') {
        Section section;
        if (!ParseHeader(section)) return false;
        out.sections.push_back(std::move(section));
      } else if (IsIdentStart(Peek())) {
        if (out.sections.empty()) {
          return Fail("property found before the first [section]");
        }
        Property property;
        if (!ParseAssignment(property)) return false;
        out.sections.back().properties.Set(property.key,
                                           std::move(property.value));
      } else {
        return Fail(std::format("unexpected character '{}'", Peek()));
      }
      SkipSpace();
    }
    return true;
  }

  bool ParseSingleValue(Variant& out) {
    SkipSpace();
    if (!ParseValue(out)) return false;
    SkipSpace();
    if (!AtEnd()) return Fail("trailing characters after value");
    return true;
  }

  const ParseError& Error() const { return error_; }

 private:
  bool AtEnd() const { return pos_ >= src_.size(); }
  char Peek() const { return AtEnd() ? '\0' : src_[pos_]; }

  char Advance() {
    char c = src_[pos_++];
    if (c == '\n') {
      line_++;
      column_ = 1;
    } else {
      column_++;
    }
    return c;
  }

  // Skips whitespace (including newlines) and ';' comments.
  void SkipSpace() {
    while (!AtEnd()) {
      char c = Peek();
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        Advance();
      } else if (c == ';') {
        while (!AtEnd() && Peek() != '\n') Advance();
      } else {
        break;
      }
    }
  }

  // Skips spaces on the current line only.
  void SkipInlineSpace() {
    while (!AtEnd() && (Peek() == ' ' || Peek() == '\t' || Peek() == '\r')) {
      Advance();
    }
  }

  bool Fail(std::string message) {
    if (error_.message.empty()) {
      error_ = ParseError{line_, column_, std::move(message)};
    }
    return false;
  }

  bool Expect(char c) {
    if (Peek() != c) {
      return Fail(AtEnd() ? std::format("expected '{}' but reached the end", c)
                          : std::format("expected '{}' but found '{}'", c,
                                        Peek()));
    }
    Advance();
    return true;
  }

  bool ParseIdent(std::string& out) {
    if (!IsIdentStart(Peek())) return Fail("expected a name");
    size_t start = pos_;
    while (!AtEnd() && IsIdentChar(Peek())) Advance();
    out = std::string(src_.substr(start, pos_ - start));
    return true;
  }

  bool ParseHeader(Section& section) {
    section.line = line_;
    Advance();  // '['
    SkipInlineSpace();
    if (!ParseIdent(section.tag)) return false;
    while (true) {
      SkipInlineSpace();
      if (Peek() == ']') {
        Advance();
        break;
      }
      if (AtEnd() || Peek() == '\n') return Fail("unterminated section header");
      Property attribute;
      if (!ParseIdent(attribute.key)) return false;
      SkipInlineSpace();
      if (!Expect('=')) return false;
      SkipInlineSpace();
      if (!ParseValue(attribute.value)) return false;
      section.attributes.Set(attribute.key, std::move(attribute.value));
    }
    // Nothing but a comment may follow the header on its line.
    SkipInlineSpace();
    if (!AtEnd() && Peek() != '\n' && Peek() != ';') {
      return Fail("unexpected text after section header");
    }
    return true;
  }

  bool ParseAssignment(Property& property) {
    if (!ParseIdent(property.key)) return false;
    SkipInlineSpace();
    if (!Expect('=')) return false;
    SkipInlineSpace();
    if (!ParseValue(property.value)) return false;
    SkipInlineSpace();
    if (!AtEnd() && Peek() != '\n' && Peek() != ';') {
      return Fail("unexpected text after value");
    }
    return true;
  }

  bool ParseValue(Variant& out) {
    char c = Peek();
    if (c == '"') {
      std::string s;
      if (!ParseString(s)) return false;
      out = Variant(std::move(s));
      return true;
    }
    if (c == '[') return ParseArray(out);
    if (c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9')) {
      return ParseNumber(out);
    }
    if (IsIdentStart(c)) return ParseWord(out);
    if (AtEnd()) return Fail("expected a value but reached the end");
    return Fail(std::format("unexpected character '{}' in value", c));
  }

  bool ParseString(std::string& out) {
    Advance();  // opening quote
    while (true) {
      if (AtEnd()) return Fail("unterminated string");
      char c = Advance();
      if (c == '"') return true;
      if (c == '\\') {
        if (AtEnd()) return Fail("unterminated string");
        char e = Advance();
        switch (e) {
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case 'r': out.push_back('\r'); break;
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          default: return Fail(std::format("unknown escape '\\{}'", e));
        }
      } else {
        out.push_back(c);
      }
    }
  }

  bool ParseNumberText(std::string_view& text, bool& is_float) {
    size_t start = pos_;
    is_float = false;
    if (Peek() == '-' || Peek() == '+') Advance();
    while (!AtEnd()) {
      char c = Peek();
      if (c >= '0' && c <= '9') {
        Advance();
      } else if (c == '.' || c == 'e' || c == 'E') {
        is_float = true;
        Advance();
        if ((c == 'e' || c == 'E') && (Peek() == '-' || Peek() == '+')) {
          Advance();
        }
      } else {
        break;
      }
    }
    text = src_.substr(start, pos_ - start);
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    if (text.empty() || text == "-") return Fail("malformed number");
    return true;
  }

  bool ParseNumber(Variant& out) {
    std::string_view text;
    bool is_float = false;
    if (!ParseNumberText(text, is_float)) return false;
    const char* end = text.data() + text.size();
    if (is_float) {
      double value = 0.0;
      auto [ptr, ec] = std::from_chars(text.data(), end, value);
      if (ec != std::errc() || ptr != end) {
        return Fail(std::format("malformed number '{}'", text));
      }
      out = Variant(value);
    } else {
      int64_t value = 0;
      auto [ptr, ec] = std::from_chars(text.data(), end, value);
      if (ec != std::errc() || ptr != end) {
        return Fail(std::format("malformed number '{}'", text));
      }
      out = Variant(value);
    }
    return true;
  }

  // Parses "(n, n, ...)" of exactly `count` numbers.
  bool ParseFloatArgs(int count, float* values) {
    SkipSpace();
    if (!Expect('(')) return false;
    for (int i = 0; i < count; i++) {
      SkipSpace();
      Variant number;
      if (!ParseNumber(number)) return false;
      values[i] = static_cast<float>(*number.ToFloat());
      SkipSpace();
      if (i + 1 < count && !Expect(',')) return false;
    }
    SkipSpace();
    return Expect(')');
  }

  bool ParseStringArg(std::string& out) {
    SkipSpace();
    if (!Expect('(')) return false;
    SkipSpace();
    if (Peek() != '"') return Fail("expected a string argument");
    if (!ParseString(out)) return false;
    SkipSpace();
    return Expect(')');
  }

  bool ParseWord(Variant& out) {
    std::string word;
    if (!ParseIdent(word)) return false;
    if (word == "true") {
      out = Variant(true);
    } else if (word == "false") {
      out = Variant(false);
    } else if (word == "null") {
      out = Variant();
    } else if (word == "inf") {
      out = Variant(INFINITY);
    } else if (word == "Vector2") {
      glm::vec2 v;
      if (!ParseFloatArgs(2, &v.x)) return false;
      out = Variant(v);
    } else if (word == "Vector3") {
      glm::vec3 v;
      if (!ParseFloatArgs(3, &v.x)) return false;
      out = Variant(v);
    } else if (word == "Color") {
      // Color(r, g, b) or Color(r, g, b, a)
      SkipSpace();
      if (!Expect('(')) return false;
      float c[4] = {0.0f, 0.0f, 0.0f, 1.0f};
      int i = 0;
      for (; i < 4; i++) {
        SkipSpace();
        Variant number;
        if (!ParseNumber(number)) return false;
        c[i] = static_cast<float>(*number.ToFloat());
        SkipSpace();
        if (Peek() == ')') break;
        if (!Expect(',')) return false;
      }
      if (i < 2) return Fail("Color needs 3 or 4 components");
      if (!Expect(')')) return false;
      out = Variant(Color{glm::vec4(c[0], c[1], c[2], c[3])});
    } else if (word == "NodePath") {
      std::string s;
      if (!ParseStringArg(s)) return false;
      out = Variant(NodePath{std::move(s)});
    } else if (word == "ExtResource") {
      std::string s;
      if (!ParseStringArg(s)) return false;
      out = Variant(ExtResource{std::move(s)});
    } else if (word == "Resource") {
      std::string s;
      if (!ParseStringArg(s)) return false;
      out = Variant(Resource{std::move(s)});
    } else {
      return Fail(std::format("unknown value '{}'", word));
    }
    return true;
  }

  bool ParseArray(Variant& out) {
    Advance();  // '['
    Array items;
    SkipSpace();
    while (Peek() != ']') {
      Variant item;
      if (!ParseValue(item)) return false;
      items.push_back(std::move(item));
      SkipSpace();
      if (Peek() == ',') {
        Advance();
        SkipSpace();
      } else if (Peek() != ']') {
        return Fail("expected ',' or ']' in array");
      }
    }
    Advance();  // ']'
    out = Variant(std::move(items));
    return true;
  }

  std::string_view src_;
  size_t pos_ = 0;
  int line_ = 1;
  int column_ = 1;
  ParseError error_;
};

// Shortest text that reads back as the same float. Values that came from a
// float field are printed at float precision, so 0.45f stays "0.45".
std::string FormatNumber(double value) {
  if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
  if (std::isnan(value)) return "0.0";
  if (value == 0.0) value = 0.0;  // "-0" would read back as the integer 0
  char buffer[64];
  std::to_chars_result result;
  const float as_float = static_cast<float>(value);
  if (static_cast<double>(as_float) == value) {
    result = std::to_chars(buffer, buffer + sizeof(buffer), as_float);
  } else {
    result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  }
  return std::string(buffer, result.ptr);
}

std::string FormatFloat(double value) {
  std::string text = FormatNumber(value);
  // Keep a decimal point so the value reads back as a float, not an int.
  if (text.find_first_of(".einf") == std::string::npos) text += ".0";
  return text;
}

std::string Quote(std::string_view s) {
  std::string out = "\"";
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      default: out.push_back(c);
    }
  }
  out += "\"";
  return out;
}

}  // namespace

bool Parse(std::string_view source, Document& out, ParseError* error) {
  Parser parser(source);
  Document document;
  if (!parser.ParseDocument(document)) {
    if (error) *error = parser.Error();
    return false;
  }
  out = std::move(document);
  return true;
}

std::optional<Variant> ParseValue(std::string_view source) {
  Parser parser(source);
  Variant value;
  if (!parser.ParseSingleValue(value)) return std::nullopt;
  return value;
}

// ----------------------------------------------------------------- Writer ---

std::string WriteValue(const Variant& value) {
  switch (value.GetType()) {
    case Variant::Type::kNil:
      return "null";
    case Variant::Type::kBool:
      return *value.Get<bool>() ? "true" : "false";
    case Variant::Type::kInt:
      return std::to_string(*value.Get<int64_t>());
    case Variant::Type::kFloat:
      return FormatFloat(*value.Get<double>());
    case Variant::Type::kString:
      return Quote(*value.Get<std::string>());
    case Variant::Type::kVector2: {
      auto v = *value.Get<glm::vec2>();
      return std::format("Vector2({}, {})", FormatNumber(v.x),
                         FormatNumber(v.y));
    }
    case Variant::Type::kVector3: {
      auto v = *value.Get<glm::vec3>();
      return std::format("Vector3({}, {}, {})", FormatNumber(v.x),
                         FormatNumber(v.y), FormatNumber(v.z));
    }
    case Variant::Type::kColor: {
      auto c = value.Get<Color>()->rgba;
      return std::format("Color({}, {}, {}, {})", FormatNumber(c.r),
                         FormatNumber(c.g), FormatNumber(c.b),
                         FormatNumber(c.a));
    }
    case Variant::Type::kNodePath:
      return std::format("NodePath({})", Quote(value.Get<NodePath>()->path));
    case Variant::Type::kExtResource:
      return std::format("ExtResource({})",
                         Quote(value.Get<ExtResource>()->id));
    case Variant::Type::kResource:
      return std::format("Resource({})", Quote(value.Get<Resource>()->path));
    case Variant::Type::kArray: {
      std::string out = "[";
      const auto& items = *value.Get<Array>();
      for (size_t i = 0; i < items.size(); i++) {
        if (i > 0) out += ", ";
        out += WriteValue(items[i]);
      }
      return out + "]";
    }
  }
  return "null";
}

std::string Write(const Document& document) {
  std::string out;
  for (size_t i = 0; i < document.sections.size(); i++) {
    const Section& section = document.sections[i];
    // A blank line separates sections, except within a run of [ext_resource]
    // lines, which read best as one block (as in Godot files).
    if (i > 0) {
      const Section& previous = document.sections[i - 1];
      const bool resource_run = section.tag == "ext_resource" &&
                                previous.tag == "ext_resource" &&
                                previous.properties.empty();
      if (!resource_run) out += "\n";
    }
    out += "[" + section.tag;
    for (const auto& attribute : section.attributes) {
      out += " " + attribute.key + "=" + WriteValue(attribute.value);
    }
    out += "]\n";
    for (const auto& property : section.properties) {
      out += property.key + " = " + WriteValue(property.value) + "\n";
    }
  }
  return out;
}

bool LoadFile(const std::filesystem::path& path, Document& out,
              ParseError* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (error) *error = ParseError{0, 0, "could not open " + path.string()};
    return false;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return Parse(buffer.str(), out, error);
}

bool SaveFile(const std::filesystem::path& path, const Document& document) {
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  auto temp = path;
  temp += ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << Write(document);
    if (!file) return false;
  }
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
}

}  // namespace Mercury::mtext
