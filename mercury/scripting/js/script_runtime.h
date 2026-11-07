/*
 * script_runtime.h
 *
 * The embedded JavaScript engine (V8) that runs TypeScript game scripts. There is one isolate and
 * one context for the process. Scripts are compiled by esbuild into a single bundle (game.js) that
 * registers its classes when it runs.
 *
 * Rules:
 *   - C++ calls into JS only through ScriptRuntime::Call. It logs exceptions (with TypeScript
 *     positions from the source map) and never lets them stop the game.
 *   - No raw pointer ever reaches JS. JS holds numbers (a scene serial, an entity id, a behaviour
 *     id) and every native function checks them first.
 *   - Nothing in the TypeRegistry or any other static holds a v8 handle: ScriptRuntime owns the
 *     script classes and drops them in Shutdown, before the isolate is disposed.
 */
#ifndef MERCURY_SCRIPTING_JS_SCRIPT_RUNTIME_H
#define MERCURY_SCRIPTING_JS_SCRIPT_RUNTIME_H

#include <v8.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "scripting/js/source_map.h"
#include "scripting/reflection.h"

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

struct ScriptField {
  std::string name;
  FieldType type = FieldType::kFloat;
  FieldType element_type = FieldType::kString;  // kList
  std::vector<std::string> enum_values;
  std::string asset_type;
  std::string tooltip;
};

struct ScriptCallable {
  std::string name;
  std::string arg;  // "", "bool", "int", "float" or "string"
};

/**
 * @brief A TypeScript class registered with @script (or @dataAsset)
 */
struct ScriptClass {
  std::string name;
  std::string location;  // "scripts/ball.ts:12", for duplicate-name errors
  v8::Global<v8::Function> constructor;
  std::vector<ScriptField> fields;
  std::vector<std::string> bindables;
  std::vector<ScriptCallable> callables;
  uint32_t callbacks = 0;
  bool data_asset = false;
};

// Empties the table of UI element handles (bindings_ui.cc); old handles then throw.
void InvalidateUiHandles();

class ScriptRuntime {
 public:
  static ScriptRuntime& Instance();

  /**
   * @brief Starts V8 and installs the native API. Call once per process.
   */
  bool Initialize();
  // Drops every script object and disposes V8. Destroy scenes first.
  void Shutdown();
  bool IsInitialized() const { return isolate_ != nullptr; }

  v8::Isolate* GetIsolate() const { return isolate_; }
  v8::Local<v8::Context> GetContext() const { return context_.Get(isolate_); }

  /**
   * @brief Reads a bundle (through the VFS), runs it and loads its source map
   *
   * Classes register themselves while it runs. Afterwards every registered script is described and
   * added to the TypeRegistry.
   */
  bool LoadBundle(const std::string& path, const std::string& map_path);
  // Evaluates source text (tests, tools). `name` shows in stack traces.
  bool RunString(std::string_view source, std::string_view name = "<eval>",
                 std::string* result = nullptr);

  /**
   * @brief The only way C++ calls into JS
   *
   * Opens its own scopes, catches exceptions (logging them with TypeScript positions) and runs the
   * microtask checkpoint afterwards. A call longer than the timeout is terminated.
   * @param label shown in error messages, e.g. "Ball.onTick"
   * @return false if the call threw
   */
  bool Call(v8::Local<v8::Function> function, v8::Local<v8::Value> receiver,
            std::span<v8::Local<v8::Value>> args, std::string_view label,
            v8::Local<v8::Value>* result = nullptr);

  // Seconds a call may run before it is terminated; 0 turns the watchdog off.
  // Defaults to MERCURY_SCRIPT_TIMEOUT or 10.
  void SetTimeoutSeconds(double seconds) { timeout_seconds_ = seconds; }

  // Converts a position in the bundle into TypeScript terms.
  const SourceMap& GetSourceMap() const { return source_map_; }
  // "Error: message\n  at onTick (scripts/ball.ts:42:5)"
  std::string FormatException(v8::Local<v8::Context> context, v8::TryCatch& try_catch);
  // Rewrites the bundle positions in a JS stack string as TypeScript ones.
  std::string RemapStack(const std::string& stack) const;

  // --- scenes -------------------------------------------------------------
  // Scenes register under a serial that scripts hold instead of a pointer.
  static uint32_t NextSceneSerial();
  void RegisterScene(uint32_t serial, MapScene* scene);
  void UnregisterScene(uint32_t serial);
  MapScene* FindScene(uint32_t serial) const;
  // The scene whose script is running now (set around every call into JS), so the static
  // `Scene`, `Input` and `Physics` of the TS API know which scene they mean.
  uint32_t CurrentScene() const { return current_scene_; }
  void SetCurrentScene(uint32_t serial) { current_scene_ = serial; }
  // Runs timers due in game time; the scene calls it every fixed tick.
  void PumpTimers();

  // --- behaviours ---------------------------------------------------------
  int RegisterBehaviour(ScriptBehaviour* behaviour);
  void UnregisterBehaviour(int id);
  ScriptBehaviour* FindBehaviour(int id) const;

  // --- script classes -----------------------------------------------------
  // Called by __native.registerScript while the bundle runs. False (and a JS error) on duplicates.
  bool AddScriptClass(std::unique_ptr<ScriptClass> script_class, std::string* error);
  const ScriptClass* FindScriptClass(std::string_view name) const;
  const std::vector<std::unique_ptr<ScriptClass>>& ScriptClasses() const { return classes_; }

  // JS helpers the TS runtime hands over (makeGameObject, factories for vectors, ...).
  v8::Local<v8::Function> Helper(std::string_view name) const;
  void SetHelper(std::string name, v8::Local<v8::Function> function);

  // The shared Float64Array(16) natives use to return vectors without allocating.
  double* Scratch() const { return scratch_; }

 private:
  ScriptRuntime() = default;
  void InstallGlobals(v8::Local<v8::Context> context);
  void RegisterClassesInTypeRegistry();
  void WatchdogLoop();

  std::unique_ptr<v8::Platform> platform_;
  std::unique_ptr<v8::ArrayBuffer::Allocator> allocator_;
  v8::Isolate* isolate_ = nullptr;
  v8::Global<v8::Context> context_;
  v8::Global<v8::Object> native_;
  v8::Global<v8::ArrayBuffer> scratch_buffer_;
  v8::Global<v8::Float64Array> scratch_array_;
  double* scratch_ = nullptr;
  std::unordered_map<std::string, v8::Global<v8::Function>> helpers_;
  std::vector<std::unique_ptr<ScriptClass>> classes_;

  SourceMap source_map_;
  std::string bundle_path_;

  std::unordered_map<uint32_t, MapScene*> scenes_;
  std::unordered_map<int, ScriptBehaviour*> behaviours_;
  int next_behaviour_id_ = 1;
  uint32_t current_scene_ = 0;

  // Watchdog. `call_generation_` changes on every top-level call, so a timer that fires just as
  // a call returns cannot terminate the next one.
  double timeout_seconds_ = 10.0;
  std::thread watchdog_;
  std::mutex watchdog_mutex_;
  std::condition_variable watchdog_cv_;
  bool watchdog_stop_ = false;
  uint64_t call_generation_ = 0;  // guarded by watchdog_mutex_
  bool call_active_ = false;      // guarded by watchdog_mutex_
  std::chrono::steady_clock::time_point call_start_;
  int call_depth_ = 0;
  std::atomic<bool> timed_out_{false};
};

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_JS_SCRIPT_RUNTIME_H
