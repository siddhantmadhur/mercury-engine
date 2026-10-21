/*
 * type_registry.h
 *
 * Every component and asset type the engine knows about, with its fields.
 *
 * Built-in components register at startup (RegisterBuiltinTypes). Game
 * scripts register from the game executable through MERCURY_SCRIPT. The
 * editor never loads game code, so it learns about scripts from the schema
 * file that the game writes with `--dump-schema`.
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

struct ComponentInfo {
  std::string name;
  std::string category;  // groups the editor's Add Component menu
  std::string description;
  bool is_script = false;
  bool removable = true;  // Transform is always there
  std::vector<FieldInfo> fields;
  // Null for scripts known only from a schema file (inside the editor).
  AttachFn attach;

  const FieldInfo* FindField(std::string_view field) const;
};

struct AssetTypeInfo {
  std::string name;
  std::string description;
  std::vector<FieldInfo> fields;

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

void LogReflectionWarnings(const Reflector& reflector, std::string_view context);

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
