// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "mtext.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>

#include <toml++/toml.hpp>

#include "os/filesystem.h"
#include "version.h"

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
    case Variant::Type::kNil:
      return "null";
    case Variant::Type::kBool:
      return "bool";
    case Variant::Type::kInt:
      return "int";
    case Variant::Type::kFloat:
      return "float";
    case Variant::Type::kString:
      return "string";
    case Variant::Type::kVector2:
      return "Vector2";
    case Variant::Type::kVector3:
      return "Vector3";
    case Variant::Type::kColor:
      return "Color";
    case Variant::Type::kNodePath:
      return "NodePath";
    case Variant::Type::kExtResource:
      return "ExtResource";
    case Variant::Type::kResource:
      return "Resource";
    case Variant::Type::kArray:
      return "Array";
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

namespace {

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

}  // namespace

// ------------------------------------------------------------------- TOML ---
//
// Format 2 support. toml++ does the parsing; the mapping onto Document and
// Variant follows the layout described in mtext.h. The writer emits TOML by
// hand so section order stays stable and floats keep their decimal point.

namespace {

// The format version this reader/writer speaks. Future versions are
// rejected here and migrate in UpgradeDocument once their reader exists.
constexpr int kCurrentFormat = 2;

ParseError TomlError(const toml::source_position& at, std::string message) {
  return ParseError{static_cast<int>(at.line), static_cast<int>(at.column),
                    std::move(message)};
}

ParseError TomlError(const toml::node& node, std::string message) {
  return TomlError(node.source().begin, std::move(message));
}

bool TomlFloat(const toml::node& node, float& out) {
  if (node.is_integer()) {
    out = static_cast<float>(node.as_integer()->get());
    return true;
  }
  if (node.is_floating_point()) {
    out = static_cast<float>(node.as_floating_point()->get());
    return true;
  }
  return false;
}

bool TomlValueToVariant(const toml::node& node, Variant& out,
                        ParseError* error) {
  auto fail = [&](std::string message) {
    if (error && error->message.empty()) {
      *error = TomlError(node, std::move(message));
    }
    return false;
  };
  switch (node.type()) {
    case toml::node_type::boolean:
      out = Variant(node.as_boolean()->get());
      return true;
    case toml::node_type::integer:
      out = Variant(node.as_integer()->get());
      return true;
    case toml::node_type::floating_point:
      out = Variant(node.as_floating_point()->get());
      return true;
    case toml::node_type::string:
      out = Variant(std::string(node.as_string()->get()));
      return true;
    case toml::node_type::array: {
      Array items;
      for (const toml::node& element : *node.as_array()) {
        Variant item;
        if (!TomlValueToVariant(element, item, error)) return false;
        items.push_back(std::move(item));
      }
      out = Variant(std::move(items));
      return true;
    }
    case toml::node_type::table: {
      // A tagged inline table carries exactly one key: { nil = true },
      // { vec2 = [x, y] }, { color = [r, g, b, a] }, { path = "..." }, ...
      const toml::table& table = *node.as_table();
      if (table.empty()) {
        return fail(
            "empty table value; values are tagged inline tables "
            "like { nil = true }");
      }
      if (table.size() != 1) {
        return fail(
            "inline table values carry exactly one tag, "
            "like { vec2 = [x, y] }");
      }
      const auto& [key, value] = *table.begin();
      const std::string_view tag = key.str();
      if (tag == "nil") {
        if (!value.is_boolean() || !value.as_boolean()->get()) {
          return fail("{ nil = true } expected");
        }
        out = Variant();
        return true;
      }
      if (tag == "vec2" || tag == "vec3") {
        const size_t count = tag == "vec2" ? 2 : 3;
        if (!value.is_array() || value.as_array()->size() != count) {
          return fail(tag == "vec2" ? "vec2 needs two numbers"
                                    : "vec3 needs three numbers");
        }
        const toml::array& array = *value.as_array();
        float components[3] = {0.0f, 0.0f, 0.0f};
        for (size_t i = 0; i < count; i++) {
          if (!TomlFloat(array[i], components[i])) {
            return fail(std::string(tag) + " needs numbers");
          }
        }
        if (tag == "vec2") {
          out = Variant(glm::vec2(components[0], components[1]));
        } else {
          out = Variant(glm::vec3(components[0], components[1], components[2]));
        }
        return true;
      }
      if (tag == "color") {
        if (!value.is_array() ||
            (value.as_array()->size() != 3 && value.as_array()->size() != 4)) {
          return fail("color needs three or four numbers");
        }
        const toml::array& array = *value.as_array();
        float components[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        for (size_t i = 0; i < array.size(); i++) {
          if (!TomlFloat(array[i], components[i])) {
            return fail("color needs numbers");
          }
        }
        out = Variant(Color{glm::vec4(components[0], components[1],
                                      components[2], components[3])});
        return true;
      }
      if (tag == "path" || tag == "ext" || tag == "res") {
        if (!value.is_string()) {
          return fail(std::string("{ ") + std::string(tag) +
                      " = \"...\" } expected");
        }
        std::string text(value.as_string()->get());
        if (tag == "path") {
          out = Variant(NodePath{std::move(text)});
        } else if (tag == "ext") {
          out = Variant(ExtResource{std::move(text)});
        } else {
          out = Variant(Resource{std::move(text)});
        }
        return true;
      }
      return fail(std::format("unsupported value tag '{}'", tag));
    }
    default:
      return fail("unsupported value type");
  }
}

bool TomlPropertyList(const toml::table& table, PropertyList& out,
                      ParseError* error) {
  for (auto&& [key, value] : table) {
    Variant item;
    if (!TomlValueToVariant(value, item, error)) return false;
    out.Set(key.str(), std::move(item));
  }
  return true;
}

bool TomlSection(std::string tag, const toml::table& table, Section& section,
                 ParseError* error) {
  section.tag = std::move(tag);
  const auto& begin = table.source().begin;
  if (begin.line > 0) section.line = static_cast<int>(begin.line);
  for (auto&& [key, value] : table) {
    // The [tag.props] sub-table carries the section's properties; a scalar
    // "props" key stays an attribute like any other.
    if (key.str() == "props" && value.is_table()) {
      if (!TomlPropertyList(*value.as_table(), section.properties, error)) {
        return false;
      }
      continue;
    }
    Variant item;
    if (!TomlValueToVariant(value, item, error)) return false;
    section.attributes.Set(key.str(), std::move(item));
  }
  return true;
}

bool ParseTomlDocument(std::string_view source, Document& out,
                       ParseError* error) {
  auto fail = [&](ParseError e) {
    if (error && error->message.empty()) *error = std::move(e);
    return false;
  };
  toml::parse_result result = toml::parse(source);
  if (!result.succeeded()) {
    return fail(TomlError(result.error().source().begin,
                          std::string(result.error().description())));
  }
  const toml::table& root = result.table();

  Document document;
  // The plain [tag] table is the file header; every other section is a
  // [[tag]] array-of-tables entry. The header is kept in front so consumers
  // that expect document.sections[0] to be the header keep working.
  std::optional<Section> header;
  for (auto&& [key, value] : root) {
    const std::string tag(key.str());
    if (value.is_table()) {
      if (header) {
        return fail(TomlError(*value.as_table(),
                              std::format("expected [[{}]] but found a "
                                          "second header table",
                                          tag)));
      }
      header.emplace();
      if (!TomlSection(tag, *value.as_table(), *header, error)) return false;
      continue;
    }
    const toml::array* array = value.as_array();
    if (!array || !array->is_array_of_tables()) {
      return fail(
          TomlError(value, std::format("expected a [[{}]] section", tag)));
    }
    for (const toml::node& element : *array) {
      Section section;
      if (!TomlSection(tag, *element.as_table(), section, error)) return false;
      document.sections.push_back(std::move(section));
    }
  }
  if (!header) {
    return fail(TomlError(root.source().begin,
                          "the file needs a [header] table with format = 2"));
  }
  // Version gate: the reader reads kCurrentFormat and rejects both older
  // files and files from a newer editor.
  const Variant* format = header->attributes.Get("format");
  if (!format || format->GetType() != Variant::Type::kInt) {
    return fail(TomlError(root.source().begin,
                          std::format("the [{}] header needs format = {}",
                                      header->tag, kCurrentFormat)));
  }
  if (format->ToInt().value_or(0) < kCurrentFormat) {
    return fail(TomlError(
        root.source().begin,
        std::format("format {} is not supported; this editor reads format {}",
                    *format->ToInt(), kCurrentFormat)));
  }
  if (format->ToInt().value_or(0) > kCurrentFormat) {
    // An editor_version helps name the tool that wrote the future file.
    const std::string version = header->attributes.GetString("editor_version");
    return fail(TomlError(
        root.source().begin,
        std::format("format {} is newer than this editor, which reads "
                    "format {}{}",
                    *format->ToInt(), kCurrentFormat,
                    version.empty()
                        ? ""
                        : std::format(" (written by editor {})", version))));
  }
  document.sections.insert(document.sections.begin(), std::move(*header));
  out = std::move(document);
  return true;
}

std::string TomlQuote(std::string_view s) {
  std::string out = "\"";
  for (char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\r':
        out += "\\r";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20 ||
            static_cast<unsigned char>(c) == 0x7f) {
          out +=
              std::format("\\u{:04x}",
                          static_cast<unsigned>(static_cast<unsigned char>(c)));
        } else {
          out.push_back(c);
        }
    }
  }
  out += "\"";
  return out;
}

bool TomlBareKey(std::string_view key) {
  if (key.empty()) return false;
  for (char c : key) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) {
      return false;
    }
  }
  return true;
}

std::string TomlKey(std::string_view key) {
  if (TomlBareKey(key)) return std::string(key);
  return TomlQuote(key);
}

std::string WriteTomlValue(const Variant& value) {
  switch (value.GetType()) {
    case Variant::Type::kNil:
      return "{ nil = true }";
    case Variant::Type::kBool:
      return *value.Get<bool>() ? "true" : "false";
    case Variant::Type::kInt:
      return std::to_string(*value.Get<int64_t>());
    case Variant::Type::kFloat:
      return FormatFloat(*value.Get<double>());
    case Variant::Type::kString:
      return TomlQuote(*value.Get<std::string>());
    case Variant::Type::kVector2: {
      auto v = *value.Get<glm::vec2>();
      return std::format("{{ vec2 = [{}, {}] }}", FormatFloat(v.x),
                         FormatFloat(v.y));
    }
    case Variant::Type::kVector3: {
      auto v = *value.Get<glm::vec3>();
      return std::format("{{ vec3 = [{}, {}, {}] }}", FormatFloat(v.x),
                         FormatFloat(v.y), FormatFloat(v.z));
    }
    case Variant::Type::kColor: {
      auto c = value.Get<Color>()->rgba;
      return std::format("{{ color = [{}, {}, {}, {}] }}", FormatFloat(c.r),
                         FormatFloat(c.g), FormatFloat(c.b), FormatFloat(c.a));
    }
    case Variant::Type::kNodePath:
      return std::format("{{ path = {} }}",
                         TomlQuote(value.Get<NodePath>()->path));
    case Variant::Type::kExtResource:
      return std::format("{{ ext = {} }}",
                         TomlQuote(value.Get<ExtResource>()->id));
    case Variant::Type::kResource:
      return std::format("{{ res = {} }}",
                         TomlQuote(value.Get<Resource>()->path));
    case Variant::Type::kArray: {
      std::string out = "[";
      const auto& items = *value.Get<Array>();
      for (size_t i = 0; i < items.size(); i++) {
        if (i > 0) out += ", ";
        out += WriteTomlValue(items[i]);
      }
      return out + "]";
    }
  }
  return "{ nil = true }";
}

std::string WriteTomlDocument(const Document& document) {
  std::string out;
  if (document.sections.empty()) return out;
  // Canonical output, so write(parse(write(doc))) == write(doc): the header
  // first, then the remaining sections grouped by tag in alphabetical order
  // (matching the reader, which walks a sorted table), each tag's sections in
  // their original order, and keys sorted.
  auto sorted = [](const PropertyList& list) {
    std::vector<const Property*> items;
    for (const auto& item : list) items.push_back(&item);
    std::sort(
        items.begin(), items.end(),
        [](const Property* a, const Property* b) { return a->key < b->key; });
    return items;
  };
  auto write_section = [&](const Section& section, bool header) {
    out += header ? "[" + TomlKey(section.tag) + "]\n"
                  : "[[" + TomlKey(section.tag) + "]]\n";
    if (header) {
      // Every file records its format version and the editor that wrote it.
      out += "format = 2\n";
      out += "editor_version = " + TomlQuote(MERCURY_VERSION_STRING) + "\n";
    }
    for (const Property* attribute : sorted(section.attributes)) {
      if (header &&
          (attribute->key == "format" || attribute->key == "editor_version")) {
        continue;  // rewritten above with the current values
      }
      out += TomlKey(attribute->key) + " = " +
             WriteTomlValue(attribute->value) + "\n";
    }
    if (!section.properties.empty()) {
      out += "[" + TomlKey(section.tag) + ".props]\n";
      for (const Property* property : sorted(section.properties)) {
        out += TomlKey(property->key) + " = " +
               WriteTomlValue(property->value) + "\n";
      }
    }
  };
  // The first section is the header ([tag]); the rest become [[tag]] entries.
  write_section(document.sections.front(), true);
  std::vector<const Section*> remaining;
  for (size_t i = 1; i < document.sections.size(); i++) {
    remaining.push_back(&document.sections[i]);
  }
  std::stable_sort(
      remaining.begin(), remaining.end(),
      [](const Section* a, const Section* b) { return a->tag < b->tag; });
  for (const Section* section : remaining) {
    out += "\n";
    write_section(*section, false);
  }
  return out;
}

}  // namespace

bool Parse(std::string_view source, Document& out, ParseError* error) {
  if (error) *error = ParseError{};  // the caller may reuse the struct
  Document document;
  if (!ParseTomlDocument(source, document, error)) return false;
  UpgradeDocument(document);
  out = std::move(document);
  return true;
}

std::optional<Variant> ParseValue(std::string_view source) {
  // Format 2 literals are TOML values; parse them as a one-key document.
  // The text comes from the inspector, so anything that adds keys or tables
  // after the value ("1\nx = 2", "1\n[t]") is not a single value.
  std::string text = "v = ";
  text.append(source);
  toml::parse_result result = toml::parse(text);
  if (!result.succeeded()) return std::nullopt;
  const toml::table& root = result.table();
  if (root.size() != 1 || root.begin()->first.str() != "v") {
    return std::nullopt;
  }
  Variant out;
  if (!TomlValueToVariant(root.begin()->second, out, nullptr)) {
    return std::nullopt;
  }
  return out;
}

// ----------------------------------------------------------------- Writer ---

std::string WriteValue(const Variant& value) { return WriteTomlValue(value); }

std::string Write(const Document& document) {
  return WriteTomlDocument(document);
}

bool LoadFile(const std::filesystem::path& path, Document& out,
              ParseError* error) {
  auto text = os::ReadText(path);
  if (!text) {
    if (error) *error = ParseError{0, 0, "could not open " + path.string()};
    return false;
  }
  return Parse(*text, out, error);
}

bool SaveFile(const std::filesystem::path& path, const Document& document) {
  return os::WriteFileAtomic(path, WriteTomlDocument(document));
}

bool UpgradeDocument(Document& document) {
  const auto format =
      document.sections.empty()
          ? int64_t{0}
          : document.sections.front().attributes.GetInt("format", 0);
  switch (format) {
    // Migrations for future format versions go here.
    default:
      return true;
  }
}

}  // namespace Mercury::mtext
