/*
 * script_runtime.h
 *
 * The embedded AngelScript engine that runs game scripts. One engine, one
 * module ("game") and a short-lived context per call. Scripts are plain .as
 * files under <project>/scripts; they are compiled with CScriptBuilder and an
 * engine-provided prelude, so there is no external bundler and no source map.
 *
 * Rules:
 *   - Script classes are registered with metadata ([script], [field], ...) and
 *     are instantiated through their factory.
 *   - A script only ever sees opaque handles (GameObject wraps a scene pointer
 *     plus an entity id, UI elements are integer handles). Every native checks
 *     them before using them.
 *   - Nothing static holds an asIScriptObject: ScriptRuntime owns the classes
 *     and drops them in Shutdown, before the module is discarded.
 */
#ifndef MERCURY_SCRIPTING_AS_SCRIPT_RUNTIME_H
#define MERCURY_SCRIPTING_AS_SCRIPT_RUNTIME_H

#include <angelscript.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "scripting/reflection.h"

class CScriptBuilder;

namespace Mercury {

class MapScene;
class ScriptBehaviour;

// Which callbacks a script class defines (a bit per Behaviour virtual).
enum ScriptCallback : uint32_t {
  kCbAwake = 1u << 0,
  kCbOnEnable = 1u << 1,
  kCbOnDisable = 1u << 2,
  kCbOnStart = 1u << 3,
  kCbOnFrame = 1u << 4,
  kCbOnLateFrame = 1u << 5,
  kCbOnTick = 1u << 6,
  kCbOnCollisionEnter = 1u << 7,
  kCbOnCollisionStay = 1u << 8,
  kCbOnCollisionLeave = 1u << 9,
  kCbOnTriggerEnter = 1u << 10,
  kCbOnTriggerStay = 1u << 11,
  kCbOnTriggerExit = 1u << 12,
  kCbOnDestroy = 1u << 13,
};

constexpr int kScriptCallbackCount = 14;

struct ScriptField {
  std::string name;
  FieldType type = FieldType::kFloat;
  FieldType element_type = FieldType::kString;  // kList
  std::vector<std::string> enum_values;
  std::string asset_type;
  std::string tooltip;
  int property_index = -1;
  int type_id = 0;
};

struct ScriptCallable {
  std::string name;
  std::string arg;  // "", "bool", "int", "float" or "string"
  asIScriptFunction* function = nullptr;
};

/**
 * @brief An AngelScript class registered with [script] or [dataAsset]
 */
struct ScriptClass {
  std::string name;
  std::string location;  // "scripts/ball.as:12"
  asITypeInfo* type = nullptr;
  asIScriptFunction* factory = nullptr;
  std::vector<ScriptField> fields;
  std::vector<std::string> bindables;
  std::vector<ScriptCallable> callables;
  uint32_t callbacks = 0;
  bool data_asset = false;
  asIScriptFunction* callbacks_by_slot[kScriptCallbackCount] = {};
  // 0 = no argument, 1 = float, 2 = double, 3 = GameObject
  int callback_arg_types[kScriptCallbackCount] = {};
};

// A typed argument or return value for ScriptRuntime::Call.
struct ScriptArg {
  enum class Kind {
    kNone,
    kBool,
    kInt,
    kFloat,
    kDouble,
    kString,
    kObject
  } kind = Kind::kNone;
  bool b = false;
  int i = 0;
  float f = 0.0f;
  double d = 0.0;
  std::string s;
  void* object = nullptr;
  int type_id = 0;  // for kObject

  static ScriptArg Bool(bool value) {
    ScriptArg a;
    a.kind = Kind::kBool;
    a.b = value;
    return a;
  }
  static ScriptArg Int(int value) {
    ScriptArg a;
    a.kind = Kind::kInt;
    a.i = value;
    return a;
  }
  static ScriptArg Float(float value) {
    ScriptArg a;
    a.kind = Kind::kFloat;
    a.f = value;
    return a;
  }
  static ScriptArg Double(double value) {
    ScriptArg a;
    a.kind = Kind::kDouble;
    a.d = value;
    return a;
  }
  static ScriptArg String(std::string value) {
    ScriptArg a;
    a.kind = Kind::kString;
    a.s = std::move(value);
    return a;
  }
  static ScriptArg Object(void* value, int type_id) {
    ScriptArg a;
    a.kind = Kind::kObject;
    a.object = value;
    a.type_id = type_id;
    return a;
  }
};

// Empties the table of UI element handles (bindings_ui.cc); old handles then
// throw.
void InvalidateUiHandles();

class ScriptRuntime {
 public:
  static ScriptRuntime& Instance();

  /**
   * @brief Starts AngelScript and installs the native API. Call once per
   * process.
   */
  bool Initialize();
  // Drops every script object and the engine. Destroy scenes first.
  void Shutdown();
  bool IsInitialized() const { return engine_ != nullptr; }

  asIScriptEngine* GetEngine() const { return engine_; }
  asIScriptModule* GetModule() const { return module_; }

  /**
   * @brief Compiles every .as under <project>/scripts (plus the API prelude)
   *
   * Classes register themselves while the module is built. Afterwards every
   * registered script is described and added to the TypeRegistry.
   */
  bool LoadScripts(const std::filesystem::path& project_root);

  /**
   * @brief Compiles one module from the given files (tests and tools)
   * @param register_types adds [script] classes to the TypeRegistry
   */
  bool LoadFiles(const std::vector<std::filesystem::path>& files,
                 const std::string& module_name, bool register_types);

  // Evaluates source text and returns the string returned by `string main()`.
  bool RunString(std::string_view source, std::string_view name = "<eval>",
                 std::string* result = nullptr);

  /**
   * @brief The only way C++ calls into AngelScript
   *
   * Creates a context, sets the arguments, executes and logs exceptions. A
   * call longer than the timeout is aborted by the line callback.
   * @param label shown in error messages, e.g. "Ball.onTick"
   * @return false if the call threw
   */
  bool Call(asIScriptFunction* function, asIScriptObject* object,
            std::span<const ScriptArg> args, std::string_view label,
            ScriptArg* result = nullptr);

  // Seconds a call may run before it is aborted; 0 turns the guard off.
  // Defaults to MERCURY_SCRIPT_TIMEOUT or 10.
  void SetTimeoutSeconds(double seconds) { timeout_seconds_ = seconds; }

  // "scripts/ball.as:42:5: Error: message" for the last call on this context.
  std::string FormatException(asIScriptContext* context) const;

  // --- scenes -------------------------------------------------------------
  static uint32_t NextSceneSerial();
  void RegisterScene(uint32_t serial, MapScene* scene);
  void UnregisterScene(uint32_t serial);
  MapScene* FindScene(uint32_t serial) const;
  uint32_t CurrentScene() const { return current_scene_; }
  void SetCurrentScene(uint32_t serial) { current_scene_ = serial; }

  // --- behaviours ---------------------------------------------------------
  int RegisterBehaviour(ScriptBehaviour* behaviour);
  void UnregisterBehaviour(int id);
  ScriptBehaviour* FindBehaviour(int id) const;
  // Keeps a data-asset instance alive for the lifetime of the runtime.
  void OwnDataAsset(std::unique_ptr<ScriptBehaviour> asset);

  // --- script classes -----------------------------------------------------
  bool AddScriptClass(std::unique_ptr<ScriptClass> script_class,
                      std::string* error);
  const ScriptClass* FindScriptClass(std::string_view name) const;
  const std::vector<std::unique_ptr<ScriptClass>>& ScriptClasses() const {
    return classes_;
  }

  // --- timers -------------------------------------------------------------
  struct Timer {
    int behaviour = 0;
    std::string method;
    double remaining = 0.0;
    double interval = 0.0;
    bool repeat = false;
  };
  // Runs timers due in game time; the scene calls it every fixed tick.
  void PumpTimers();
  int AddTimer(int behaviour, std::string method, double seconds, bool repeat);
  void CancelTimer(int id) { timers_.erase(id); }

  // --- callbacks ----------------------------------------------------------
  // Calls a method by name on a script object (used by UI/timers/events).
  bool CallMethod(asIScriptObject* object, std::string_view method,
                  std::span<const ScriptArg> args, std::string_view label);

  // --- helpers for the native bindings ------------------------------------
  // The ScriptBehaviour that owns a script object, or null.
  ScriptBehaviour* BehaviourOf(asIScriptObject* object) const;
  // Throws an AngelScript exception on the active context (if any).
  static void Throw(const char* message);

 private:
  ScriptRuntime() = default;
  ~ScriptRuntime();

  bool BuildFromSections(
      const std::vector<std::pair<std::string, std::string>>& sections,
      const std::string& module_name, bool register_types);
  void RegisterClasses(asIScriptModule* module, CScriptBuilder& builder,
                       bool register_types);
  void DiscardModule();
  static void MessageCallback(const asSMessageInfo* msg, void* param);
  static void LineCallback(asIScriptContext* context, void* param);

  asIScriptEngine* engine_ = nullptr;
  asIScriptModule* module_ = nullptr;
  std::string module_name_ = "game";
  std::string loaded_project_;
  std::vector<std::unique_ptr<ScriptClass>> classes_;

  std::unordered_map<uint32_t, MapScene*> scenes_;
  std::unordered_map<int, ScriptBehaviour*> behaviours_;
  int next_behaviour_id_ = 1;
  uint32_t current_scene_ = 0;
  std::vector<std::unique_ptr<ScriptBehaviour>> data_assets_;

  std::unordered_map<int, Timer> timers_;
  int next_timer_id_ = 1;

  double timeout_seconds_ = 10.0;
  bool timed_out_ = false;
  bool compile_failed_ = false;
  std::chrono::steady_clock::time_point call_start_;

  friend class ScriptBuilder;
};

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_AS_SCRIPT_RUNTIME_H
