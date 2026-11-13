/*
 * type_registry.h
 *
 * Every component and asset type the engine knows about, with its fields.
 *
 * Built-in components register at startup (RegisterBuiltinTypes). Game
 * scripts register when the player loads the script bundle (ScriptRuntime). The
 * editor never runs game code, so it learns about scripts from the schema
 * file that the player writes with `--dump-schema`.
 */
#ifndef MERCURY_SCRIPTING_TYPE_REGISTRY_H
#define MERCURY_SCRIPTING_TYPE_REGISTRY_H

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "globals.h"
#include "scripting/reflection.h"
#include "serialization/mtext.h"

namespace Mercury {

class MapScene;

// Adds the component, built from `properties`, to an entity of a MapScene.
using AttachFn = std::function<void(MapScene& scene, kEntityId entity,
                                    const mtext::PropertyList& properties)>;

// Run-time access to a built-in component's fields, used by the script API.
using HasComponentFn = std::function<bool(MapScene& scene, kEntityId entity)>;
using GetFieldFn =
    std::function<bool(MapScene& scene, kEntityId entity,
                       std::string_view field, mtext::Variant* out)>;
using SetFieldFn =
    std::function<bool(MapScene& scene, kEntityId entity,
                       std::string_view field, const mtext::Variant& value)>;

// A method an Event can call (Behaviour::ReflectMethods).
struct MethodDesc {
  std::string name;
  std::string arg;  // "", "bool", "int", "float" or "string"
};

struct ComponentInfo {
  std::string name;
  std::string category;  // groups the editor's Add Component menu
  std::string description;
  bool is_script = false;
  bool removable = true;  // Transform is always there
  std::vector<FieldInfo> fields;
  std::vector<MethodDesc> methods;
  // Null for scripts known only from a schema file (inside the editor).
  AttachFn attach;
  // Null for scripts and for components that are not stored as a reflected
  // struct.
  HasComponentFn has;
  GetFieldFn get_field;
  SetFieldFn set_field;

  const FieldInfo* FindField(std::string_view field) const;
};

struct AssetTypeInfo {
  std::string name;
  std::string description;
  std::vector<FieldInfo> fields;
  // Declared by game code (@dataAsset), so saved in the schema.
  bool from_game = false;

  const FieldInfo* FindField(std::string_view field) const;
};

class TypeRegistry {
 public:
  static TypeRegistry& Instance();

  // Registering a name again replaces the earlier entry.
  void RegisterComponent(ComponentInfo info);
  void RegisterAssetType(AssetTypeInfo info);

  const ComponentInfo* FindComponent(std::string_view name) const;
  const AssetTypeInfo* FindAssetType(std::string_view name) const;

  // Built-ins first (by category), then scripts, each sorted by name.
  std::vector<const ComponentInfo*> Components() const;
  std::vector<const AssetTypeInfo*> AssetTypes() const;

  // Removes scripts that were loaded from a schema (have no attach function).
  void ClearSchemaScripts();
  // Removes scripts and data assets that game code registered
  // (ScriptRuntime::Shutdown).
  void ClearGameTypes();

  /**
   * @brief Write every script component and its fields
   *
   *   [mschema format=1]
   *
   *   [script name="Ball"]
   *   speed = 60.0
   *
   *   [field script="Goal" name="scorer"]
   *   enum = ["player", "enemy"]
   *   tooltip = "Who gets the point"
   */
  bool SaveSchema(const std::filesystem::path& path) const;
  bool LoadSchema(const std::filesystem::path& path,
                  std::string* error = nullptr);

 private:
  TypeRegistry() = default;
  std::vector<ComponentInfo> components_;
  std::vector<AssetTypeInfo> asset_types_;
};

// Describes the fields of a reflectable type from a default-constructed value.
template <typename T>
std::vector<FieldInfo> DescribeFields() {
  T instance{};
  auto reflector = Reflector::Describe();
  instance.Reflect(reflector);
  return reflector.GetFields();
}

void LogReflectionWarnings(const Reflector& reflector,
                           std::string_view context);

/**
 * @brief Build a reflectable value from properties, logging bad fields
 * @param context Shown in warnings, e.g. "Ball.Rigidbody"
 */
template <typename T>
T ReadFields(const mtext::PropertyList& properties, std::string_view context) {
  T instance{};
  auto reflector = Reflector::Read(properties);
  instance.Reflect(reflector);
  LogReflectionWarnings(reflector, context);
  return instance;
}

// Registers the engine's components (Transform, SpriteRenderer, ...) and
// asset types (Material, PhysicsMaterial). Safe to call more than once.
void RegisterBuiltinTypes();

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_TYPE_REGISTRY_H
