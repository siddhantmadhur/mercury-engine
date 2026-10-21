/*
 * mtext.h
 *
 * Mercury's text format, shared by .mproj, .mmap, .masset and editor files.
 * It is modeled on Godot's .tscn/.tres files so it stays easy to read, diff
 * and edit by hand (or by an LLM):
 *
 *   [node name="Ball" parent="."]     <- section header with attributes
 *   speed = 60.0                      <- properties of that section
 *   position = Vector3(0, 0, 0)
 *   ; a comment
 *
 * Values: null, true/false, integers, floats, "strings", Vector2(x, y),
 * Vector3(x, y, z), Color(r, g, b, a), NodePath("A/B"), ExtResource("id"),
 * Resource("res://path") and [arrays, of, values]. Comments are not kept
 * when a document is written back out.
 */
#ifndef MERCURY_SERIALIZATION_MTEXT_H
#define MERCURY_SERIALIZATION_MTEXT_H

#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Mercury::mtext {

struct NodePath {
  std::string path;
  bool operator==(const NodePath&) const = default;
};

// Reference to an [ext_resource] entry of the same file, by id.
struct ExtResource {
  std::string id;
  bool operator==(const ExtResource&) const = default;
};

// Reference to another file by its res:// path.
struct Resource {
  std::string path;
  bool operator==(const Resource&) const = default;
};

struct Color {
  glm::vec4 rgba = glm::vec4(1.0f);
  bool operator==(const Color&) const = default;
};

class Variant;
using Array = std::vector<Variant>;

class Variant {
 public:
  enum class Type {
    kNil,
    kBool,
    kInt,
    kFloat,
    kString,
    kVector2,
    kVector3,
    kColor,
    kNodePath,
    kExtResource,
    kResource,
    kArray,
  };

  Variant() = default;
  Variant(bool v) : value_(v) {}
  Variant(int v) : value_(static_cast<int64_t>(v)) {}
  Variant(int64_t v) : value_(v) {}
  Variant(float v) : value_(static_cast<double>(v)) {}
  Variant(double v) : value_(v) {}
  Variant(const char* v) : value_(std::string(v)) {}
  Variant(std::string v) : value_(std::move(v)) {}
  Variant(glm::vec2 v) : value_(v) {}
  Variant(glm::vec3 v) : value_(v) {}
  Variant(Color v) : value_(v) {}
  Variant(NodePath v) : value_(std::move(v)) {}
  Variant(ExtResource v) : value_(std::move(v)) {}
  Variant(Resource v) : value_(std::move(v)) {}
  Variant(Array v) : value_(std::move(v)) {}

  Type GetType() const { return static_cast<Type>(value_.index()); }
  bool IsNil() const { return GetType() == Type::kNil; }

  template <typename T>
  const T* Get() const {
    return std::get_if<T>(&value_);
  }
  template <typename T>
  T* Get() {
    return std::get_if<T>(&value_);
  }

  // Lenient conversions: ints convert to floats and back.
  std::optional<bool> ToBool() const;
  std::optional<int64_t> ToInt() const;
  std::optional<double> ToFloat() const;
  std::optional<std::string> ToString() const;  // strings only

  bool operator==(const Variant&) const = default;

 private:
  std::variant<std::monostate, bool, int64_t, double, std::string, glm::vec2,
               glm::vec3, Color, NodePath, ExtResource, Resource, Array>
      value_;
};

const char* TypeName(Variant::Type type);

struct Property {
  std::string key;
  Variant value;
  bool operator==(const Property&) const = default;
};

/**
 * @brief Ordered key/value list used for both header attributes and body
 * properties. Order is kept so files round-trip without reshuffling.
 */
class PropertyList {
 public:
  const Variant* Get(std::string_view key) const;
  Variant* Get(std::string_view key);
  void Set(std::string_view key, Variant value);  // replaces or appends
  bool Remove(std::string_view key);
  bool Has(std::string_view key) const { return Get(key) != nullptr; }

  std::string GetString(std::string_view key,
                        std::string_view fallback = "") const;
  int64_t GetInt(std::string_view key, int64_t fallback = 0) const;
  double GetFloat(std::string_view key, double fallback = 0.0) const;
  bool GetBool(std::string_view key, bool fallback = false) const;

  auto begin() const { return items_.begin(); }
  auto end() const { return items_.end(); }
  auto begin() { return items_.begin(); }
  auto end() { return items_.end(); }
  size_t size() const { return items_.size(); }
  bool empty() const { return items_.empty(); }
  void clear() { items_.clear(); }

  bool operator==(const PropertyList&) const = default;

 private:
  std::vector<Property> items_;
};

struct Section {
  std::string tag;
  PropertyList attributes;
  PropertyList properties;
  int line = 0;  // 1-based line of the header in the parsed source
  bool operator==(const Section& other) const {
    return tag == other.tag && attributes == other.attributes &&
           properties == other.properties;
  }
};

struct Document {
  std::vector<Section> sections;

  Section& Add(std::string tag);
  const Section* Find(std::string_view tag) const;
  Section* Find(std::string_view tag);
  std::vector<const Section*> FindAll(std::string_view tag) const;

  bool operator==(const Document&) const = default;
};

struct ParseError {
  int line = 0;
  int column = 0;
  std::string message;

  std::string ToString() const;
};

bool Parse(std::string_view source, Document& out, ParseError* error = nullptr);

// Parses a single value literal, e.g. "Vector3(1, 2, 3)".
std::optional<Variant> ParseValue(std::string_view source);

std::string Write(const Document& document);
std::string WriteValue(const Variant& value);

bool LoadFile(const std::filesystem::path& path, Document& out,
              ParseError* error = nullptr);

// Writes through a temporary file and renames it over `path`, so a crash
// mid-save never leaves a truncated file behind.
bool SaveFile(const std::filesystem::path& path, const Document& document);

}  // namespace Mercury::mtext

#endif  // MERCURY_SERIALIZATION_MTEXT_H
