// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * type_registry.h
 *
 * Every component and asset type the engine knows about, with its fields.
 *
 * Built-in components register at startup (RegisterBuiltinTypes). Game
 * scripts register from the game executable through MERCURY_SCRIPT, and
 * AngelScript classes register when the player loads the script bundle
 * (ScriptRuntime). The editor never runs game code, so it learns about scripts
 * from the schema file that the player writes with `--dump-schema`.
 */
#ifndef MERCURY_SCRIPTING_TYPE_REGISTRY_H
#define MERCURY_SCRIPTING_TYPE_REGISTRY_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
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
  // Registered by MERCURY_SCRIPT in this executable. Native scripts survive
  // ScriptRuntime::Shutdown (which drops AngelScript classes); the script
  // runtime must not remove code that was linked into the game.
  bool native_script = false;
  // Declared by game code (MERCURY_SCRIPT, or a data component registered by
  // the game itself), so SaveSchema exports it for the editor.
  bool from_game = false;

  const FieldInfo* FindField(std::string_view field) const;
};

struct AssetTypeInfo {
  std::string name;
  std::string description;
  std::vector<FieldInfo> fields;
  // Declared by game code (@dataAsset), so saved in the schema.
  bool from_game = false;
  // Registered by MERCURY_DATA_ASSET in this executable, so it outlives the
  // script runtime like a native component does.
  bool native_asset = false;

  const FieldInfo* FindField(std::string_view field) const;
};

/**
 * @brief Options for applying a component snapshot
 */
struct ComponentDeserializeOptions {
  // Reject NaN/infinite floats and vectors (network-facing byte snapshots use
  // this by default).
  bool reject_non_finite = true;
  // Only apply fields marked runtime_writable, like the script API. Pass false
  // for a trusted local snapshot (save games, undo) that restores every
  // reflected field.
  bool require_runtime_writable = true;
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
  // Removes AngelScript scripts and data assets registered by ScriptRuntime.
  // Types registered by MERCURY_SCRIPT / MERCURY_DATA_ASSET stay: they are
  // code linked into this executable, not data to reload.
  void ClearGameTypes();

  // --- component snapshots (replication, save games, undo) ----------------
  //
  // Serializing writes every field the component's Reflect method describes,
  // through the ComponentInfo's get/set functions. It works for components
  // whose data lives in the entity's registry (the built-ins, and game
  // components registered with get_field/set_field); scripts are Behaviour
  // objects and have no field access here.
  //
  // Deserializing sets the fields present in `in`/`bytes` and leaves the rest
  // alone, so a delta snapshot only needs the fields it changes.

  // Fills `out` with the component's current values. False if the type is
  // unknown or cannot read its fields.
  bool SerializeComponent(MapScene& scene, kEntityId entity,
                          std::string_view type,
                          mtext::PropertyList& out) const;
  // The same in the engine's text format, as a UTF-8 byte buffer.
  bool SerializeComponent(MapScene& scene, kEntityId entity,
                          std::string_view type,
                          std::vector<uint8_t>& out) const;
  // Applies values from `in`, skipping fields that are unknown, read-only at
  // run time (unless `options.require_runtime_writable` is false), or
  // non-finite (when `options.reject_non_finite` is true). Returns false when
  // no field was applied, including an unknown/unsettable type.
  bool DeserializeComponent(MapScene& scene, kEntityId entity,
                            std::string_view type,
                            const mtext::PropertyList& in,
                            const ComponentDeserializeOptions& options =
                                ComponentDeserializeOptions{}) const;
  // The same in the engine's text format, as a UTF-8 byte buffer. This is the
  // network-facing form, so it uses the safe defaults above.
  bool DeserializeComponent(MapScene& scene, kEntityId entity,
                            std::string_view type,
                            std::span<const uint8_t> in) const;

  /**
   * @brief Write every component (scripts and data types) and its fields
   *
   * Every field is one [[field]] entry carrying its name, default value and
   * details; file order is declaration order (the inspector's order).
   *
   *   [mschema]
   *   format = 2
   *
   *   [[script]]
   *   name = "Ball"
   *
   *   [[field]]
   *   script = "Ball"
   *   name = "speed"
   *   [field.props]
   *   default = 60.0
   *
   *   [[field]]
   *   script = "Goal"
   *   name = "scorer"
   *   [field.props]
   *   default = { nil = true }
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
