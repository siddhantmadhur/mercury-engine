/*
 * reflection.h
 *
 * Minimal field reflection, the C++ stand-in for Unity's [SerializeField].
 * A component lists its editable fields in a Reflect method:
 *
 *   void Reflect(Mercury::Reflector& r) override {
 *     r.Field("speed", speed_).Tooltip("World units per second");
 *     r.Field("target", target_);  // NodeRef
 *   }
 *
 * The same method describes the fields (for the editor and the schema file),
 * reads them from a map's properties and writes them back.
 */
#ifndef MERCURY_SCRIPTING_REFLECTION_H
#define MERCURY_SCRIPTING_REFLECTION_H

#include <glm/glm.hpp>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <vector>

#include "globals.h"
#include "serialization/mtext.h"

namespace Mercury {

/**
 * @brief Reference to another node in the same map, by path from the root
 * ("Ball", "Walls/Left"). Resolve it with Behaviour::Get().
 */
struct NodeRef {
  std::string path;
  bool IsSet() const { return !path.empty(); }
};

/**
 * @brief Reference to a file in the project, as a "res://" path
 */
struct AssetRef {
  std::string path;
  bool IsSet() const { return !path.empty(); }
};

enum class FieldType {
  kBool,
  kInt,
  kFloat,
  kString,
  kVector2,
  kVector3,
  kColor,
  kNodeRef,
  kAsset,
  kEnum,
};

const char* FieldTypeName(FieldType type);

struct FieldInfo {
  std::string name;
  FieldType type = FieldType::kFloat;
  mtext::Variant default_value;
  std::vector<std::string> enum_values;  // kEnum: allowed strings
  std::string asset_type;  // kAsset: "Material", "PhysicsMaterial", "Texture"
  std::string tooltip;
};

// Converts a field's value to the Variant stored in files.
mtext::Variant EmptyValueFor(FieldType type);

// Infers a field type from a value (used when loading a schema file).
FieldType FieldTypeOf(const mtext::Variant& value);

class Reflector {
 public:
  enum class Mode {
    kDescribe,  // record each field and its current value as the default
    kRead,      // assign fields from properties (missing ones are left alone)
    kWrite,     // store every field into properties
  };

  static Reflector Describe() { return Reflector(Mode::kDescribe, nullptr); }
  static Reflector Read(const mtext::PropertyList& properties) {
    return Reflector(Mode::kRead, const_cast<mtext::PropertyList*>(&properties));
  }
  static Reflector Write(mtext::PropertyList& properties) {
    return Reflector(Mode::kWrite, &properties);
  }

  Mode GetMode() const { return mode_; }

  Reflector& Field(const char* name, bool& value);
  Reflector& Field(const char* name, int& value);
  Reflector& Field(const char* name, float& value);
  Reflector& Field(const char* name, std::string& value);
  Reflector& Field(const char* name, glm::vec2& value);
  Reflector& Field(const char* name, glm::vec3& value);
  Reflector& Field(const char* name, glm::vec4& value);  // a Color
  Reflector& Field(const char* name, NodeRef& value);
  Reflector& Field(const char* name, AssetRef& value,
                   const char* asset_type = "");

  /**
   * @brief An enum stored as one of `names` (index 0, 1, 2... in order)
   */
  template <typename E>
    requires std::is_enum_v<E> || std::is_integral_v<E>
  Reflector& Enum(const char* name, E& value,
                  std::initializer_list<const char*> names) {
    int index = static_cast<int>(value);
    EnumImpl(name, index, names);
    value = static_cast<E>(index);
    return *this;
  }

  // Adds a tooltip to the field described just before.
  Reflector& Tooltip(const char* text);

  const std::vector<FieldInfo>& GetFields() const { return fields_; }
  // Problems found while reading, e.g. a property with the wrong type.
  const std::vector<std::string>& GetWarnings() const { return warnings_; }

 private:
  Reflector(Mode mode, mtext::PropertyList* properties)
      : mode_(mode), properties_(properties) {}

  // Shared path for every field type; `get`/`set` convert between the member
  // and its Variant form.
  template <typename T, typename ToVariant, typename FromVariant>
  Reflector& Visit(const char* name, FieldType type, T& member,
                   ToVariant to_variant, FromVariant from_variant);

  void EnumImpl(const char* name, int& index,
                std::initializer_list<const char*> names);

  Mode mode_;
  mtext::PropertyList* properties_;
  std::vector<FieldInfo> fields_;
  std::vector<std::string> warnings_;
};

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_REFLECTION_H
