/*
 * script_runtime.cc
 */
#include "scripting/js/script_runtime.h"

#include <libplatform/libplatform.h>

#include <cstdlib>
#include <format>

#include "debugger/debugger.h"
#include "os/filesystem.h"
#include "scripting/js/bindings.h"
#include "scripting/js/script_behaviour.h"
#include "scripting/type_registry.h"

namespace Mercury {

namespace js {
// Defined in bindings_*.cc; each fills `native` with one area of the API.
void InstallCore(v8::Local<v8::Context> context, v8::Local<v8::Object> native);
void InstallAll(v8::Local<v8::Context> context, v8::Local<v8::Object> native);
}  // namespace js

namespace {

std::atomic<uint32_t> g_next_scene_serial{1};
bool g_v8_initialized = false;

std::string Utf8(v8::Isolate* isolate, v8::Local<v8::Value> value) {
  v8::String::Utf8Value text(isolate, value);
  return *text ? std::string(*text, static_cast<size_t>(text.length())) : std::string();
}

void ConsolePrint(const v8::FunctionCallbackInfo<v8::Value>& info, int level) {
  v8::Isolate* isolate = info.GetIsolate();
  std::string line;
  for (int i = 0; i < info.Length(); i++) {
    if (i) line += ' ';
    v8::Local<v8::Value> arg = info[i];
    if (arg->IsObject() && !arg->IsFunction()) {
      // Objects print as JSON when they can (so {a: 1} is not "[object Object]").
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::String> json;
      if (arg.As<v8::Object>()->GetConstructorName()->StringEquals(js::Str(isolate, "Error"))) {
        line += Utf8(isolate, arg);
      } else if (v8::JSON::Stringify(context, arg).ToLocal(&json)) {
        line += Utf8(isolate, json);
      } else {
        line += Utf8(isolate, arg);
      }
    } else {
      line += Utf8(isolate, arg);
    }
  }
  switch (level) {
    case 0: PDebug::log("{}", line); break;
    case 1: PDebug::info("{}", line); break;
    case 2: PDebug::warn("{}", line); break;
    default: PDebug::error("{}", line); break;
  }
}

}  // namespace

ScriptRuntime& ScriptRuntime::Instance() {
  static ScriptRuntime instance;
  return instance;
}

uint32_t ScriptRuntime::NextSceneSerial() { return g_next_scene_serial.fetch_add(1); }

bool ScriptRuntime::Initialize() {
  if (isolate_) return true;
  if (!g_v8_initialized) {
    platform_ = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform_.get());
    v8::V8::Initialize();
    g_v8_initialized = true;
  }
  allocator_.reset(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
  v8::Isolate::CreateParams params;
  params.array_buffer_allocator = allocator_.get();
  isolate_ = v8::Isolate::New(params);
  isolate_->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);

  if (const char* timeout = std::getenv("MERCURY_SCRIPT_TIMEOUT")) {
    timeout_seconds_ = std::atof(timeout);
  }

  {
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    v8::Local<v8::Context> context = v8::Context::New(isolate_);
    context_.Reset(isolate_, context);
    v8::Context::Scope context_scope(context);
    InstallGlobals(context);
  }

  {
    std::lock_guard lock(watchdog_mutex_);
    watchdog_stop_ = false;
  }
  watchdog_ = std::thread([this] { WatchdogLoop(); });
  return true;
}

void ScriptRuntime::InstallGlobals(v8::Local<v8::Context> context) {
  using namespace js;
  v8::Local<v8::Object> global = context->Global();
  global->Set(context, Str(isolate_, "globalThis"), global).Check();

  v8::Local<v8::Object> console = v8::Object::New(isolate_);
  SetFunction(context, console, "log", [](const Info& info) { ConsolePrint(info, 0); });
  SetFunction(context, console, "info", [](const Info& info) { ConsolePrint(info, 1); });
  SetFunction(context, console, "warn", [](const Info& info) { ConsolePrint(info, 2); });
  SetFunction(context, console, "error", [](const Info& info) { ConsolePrint(info, 3); });
  global->Set(context, Str(isolate_, "console"), console).Check();

  v8::Local<v8::Object> native = v8::Object::New(isolate_);
  scratch_buffer_.Reset(isolate_, v8::ArrayBuffer::New(isolate_, 16 * sizeof(double)));
  v8::Local<v8::ArrayBuffer> buffer = scratch_buffer_.Get(isolate_);
  scratch_ = static_cast<double*>(buffer->Data());
  v8::Local<v8::Float64Array> scratch_array = v8::Float64Array::New(buffer, 0, 16);
  scratch_array_.Reset(isolate_, scratch_array);
  native->Set(context, Str(isolate_, "scratch"), scratch_array).Check();

  InstallAll(context, native);
  native_.Reset(isolate_, native);
  global->Set(context, Str(isolate_, "__native"), native).Check();
}

void ScriptRuntime::Shutdown() {
  if (!isolate_) return;
  {
    std::lock_guard lock(watchdog_mutex_);
    watchdog_stop_ = true;
  }
  watchdog_cv_.notify_all();
  if (watchdog_.joinable()) watchdog_.join();

  // Script classes are registered in the TypeRegistry; remove them before their handles go.
  TypeRegistry::Instance().ClearGameTypes();
  {
    v8::Isolate::Scope isolate_scope(isolate_);
    classes_.clear();
    helpers_.clear();
    scratch_array_.Reset();
    scratch_buffer_.Reset();
    native_.Reset();
    context_.Reset();
  }
  scratch_ = nullptr;
  isolate_->Dispose();
  isolate_ = nullptr;
  allocator_.reset();
  source_map_ = SourceMap();
  scenes_.clear();
  behaviours_.clear();
  // V8 itself stays initialised: it cannot be restarted within one process.
}

void ScriptRuntime::WatchdogLoop() {
  std::unique_lock lock(watchdog_mutex_);
  while (!watchdog_stop_) {
    watchdog_cv_.wait_for(lock, std::chrono::milliseconds(50));
    if (watchdog_stop_) break;
    if (!call_active_ || timeout_seconds_ <= 0.0) continue;
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - call_start_).count();
    if (elapsed > timeout_seconds_) {
      // Holding the mutex keeps the call from finishing and starting another in between.
      timed_out_ = true;
      isolate_->TerminateExecution();
      call_active_ = false;
    }
  }
}

std::string ScriptRuntime::FormatException(v8::Local<v8::Context> context,
                                           v8::TryCatch& try_catch) {
  std::string text = Utf8(isolate_, try_catch.Exception());
  v8::Local<v8::Value> stack;
  std::string stack_text;
  if (try_catch.StackTrace(context).ToLocal(&stack) && stack->IsString()) {
    stack_text = Utf8(isolate_, stack);
  }
  if (stack_text.empty()) {
    // A thrown non-Error value, or a syntax error: use the message position.
    v8::Local<v8::Message> message = try_catch.Message();
    if (!message.IsEmpty()) {
      const int line = message->GetLineNumber(context).FromMaybe(0);
      const int column = message->GetStartColumn(context).FromMaybe(0) + 1;
      auto mapped = source_map_.Lookup(line, column);
      if (mapped) {
        text += std::format("\n  at {}:{}:{}", mapped->file, mapped->line, mapped->column);
      } else if (line > 0) {
        text += std::format("\n  at line {}:{}", line, column);
      }
    }
    return text;
  }
  return RemapStack(stack_text);
}

std::string ScriptRuntime::RemapStack(const std::string& stack_text) const {
  // Rewrite "(<bundle>:LINE:COL)" frames as TypeScript positions.
  std::string out;
  size_t pos = 0;
  while (pos < stack_text.size()) {
    size_t eol = stack_text.find('\n', pos);
    if (eol == std::string::npos) eol = stack_text.size();
    std::string line = stack_text.substr(pos, eol - pos);
    pos = eol + 1;
    size_t at = line.find("    at ");
    if (at != std::string::npos && !bundle_path_.empty()) {
      size_t where = line.find(bundle_path_);
      if (where != std::string::npos) {
        int l = 0, c = 0;
        if (std::sscanf(line.c_str() + where + bundle_path_.size(), ":%d:%d", &l, &c) == 2) {
          if (auto mapped = source_map_.Lookup(l, c)) {
            std::string prefix = line.substr(0, where);
            // "at fn (" keeps its "(" and gains ")"; a bare "at path" stays bare.
            const bool paren = !prefix.empty() && prefix.back() == '(';
            line = prefix + std::format("{}:{}:{}", mapped->file, mapped->line, mapped->column) +
                   (paren ? ")" : "");
          }
        }
      }
    }
    if (!out.empty()) out += '\n';
    out += line;
  }
  return out;
}

bool ScriptRuntime::Call(v8::Local<v8::Function> function, v8::Local<v8::Value> receiver,
                         std::span<v8::Local<v8::Value>> args, std::string_view label,
                         v8::Local<v8::Value>* result) {
  v8::Local<v8::Context> context = GetContext();
  v8::TryCatch try_catch(isolate_);
  const bool top_level = call_depth_ == 0;
  uint64_t generation = 0;
  if (top_level) {
    std::lock_guard lock(watchdog_mutex_);
    generation = ++call_generation_;
    call_start_ = std::chrono::steady_clock::now();
    call_active_ = true;
    timed_out_ = false;
  }
  call_depth_++;
  v8::Local<v8::Value> value;
  const bool ok =
      function->Call(context, receiver, static_cast<int>(args.size()), args.data()).ToLocal(&value);
  call_depth_--;

  bool terminated = false;
  if (top_level) {
    std::lock_guard lock(watchdog_mutex_);
    call_active_ = false;
    terminated = timed_out_.exchange(false);
    (void)generation;
  }
  if (terminated || isolate_->IsExecutionTerminating()) {
    isolate_->CancelTerminateExecution();
    PDebug::error("{}: script took too long (infinite loop?)", label);
    return false;
  }
  if (!ok) {
    if (try_catch.HasCaught()) {
      PDebug::error("{}: {}", label, FormatException(context, try_catch));
    } else {
      PDebug::error("{}: call failed", label);
    }
    if (top_level) isolate_->PerformMicrotaskCheckpoint();
    return false;
  }
  if (result) *result = value;
  if (top_level) {
    isolate_->PerformMicrotaskCheckpoint();
    if (try_catch.HasCaught()) {
      PDebug::error("{}: {}", label, FormatException(context, try_catch));
    }
  }
  return true;
}

bool ScriptRuntime::RunString(std::string_view source, std::string_view name,
                              std::string* result) {
  if (!isolate_) return false;
  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);
  v8::Local<v8::Context> context = GetContext();
  v8::Context::Scope context_scope(context);
  v8::TryCatch try_catch(isolate_);

  v8::ScriptOrigin origin(js::Str(isolate_, name));
  v8::Local<v8::Script> script;
  if (!v8::Script::Compile(context, js::Str(isolate_, source), &origin).ToLocal(&script)) {
    PDebug::error("{}: {}", name, FormatException(context, try_catch));
    return false;
  }

  {
    std::lock_guard lock(watchdog_mutex_);
    ++call_generation_;
    call_start_ = std::chrono::steady_clock::now();
    call_active_ = true;
    timed_out_ = false;
  }
  call_depth_++;
  v8::Local<v8::Value> value;
  const bool ok = script->Run(context).ToLocal(&value);
  call_depth_--;
  bool terminated;
  {
    std::lock_guard lock(watchdog_mutex_);
    call_active_ = false;
    terminated = timed_out_.exchange(false);
  }
  if (terminated || isolate_->IsExecutionTerminating()) {
    isolate_->CancelTerminateExecution();
    PDebug::error("{}: script took too long (infinite loop?)", name);
    return false;
  }
  if (!ok) {
    PDebug::error("{}: {}", name, FormatException(context, try_catch));
    return false;
  }
  isolate_->PerformMicrotaskCheckpoint();
  if (result) *result = Utf8(isolate_, value);
  return true;
}

bool ScriptRuntime::LoadBundle(const std::string& path, const std::string& map_path) {
  if (!isolate_) return false;
  auto text = os::ReadText(path);
  if (!text) {
    PDebug::error("Script bundle not found: {}", path);
    return false;
  }
  if (auto map_text = os::ReadText(map_path)) {
    std::string error;
    if (!source_map_.Parse(*map_text, &error)) {
      PDebug::warn("Source map {}: {}", map_path, error);
    }
  }
  bundle_path_ = path;
  if (!RunString(*text, path)) return false;

  // Now that every module has been evaluated, describe each script (throwaway instances can
  // safely reference anything declared later in their file).
  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);
  v8::Context::Scope context_scope(GetContext());
  v8::Local<v8::Function> finalize = Helper("finalizeScripts");
  if (!finalize.IsEmpty()) {
    v8::Local<v8::Value> result;
    if (!Call(finalize, GetContext()->Global(), {}, "finalizeScripts", &result)) return false;
  }
  RegisterClassesInTypeRegistry();
  return true;
}

void ScriptRuntime::RegisterClassesInTypeRegistry() {
  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);
  v8::Context::Scope context_scope(GetContext());
  for (const auto& script_class : classes_) {
    // Describe a throwaway instance: its field initializers are the defaults.
    std::vector<FieldInfo> fields;
    if (auto instance = ScriptBehaviour::Create(*script_class, nullptr, entt::null)) {
      auto reflector = Reflector::Describe();
      instance->Reflect(reflector);
      fields = reflector.GetFields();
    }
    if (script_class->data_asset) {
      TypeRegistry::Instance().RegisterAssetType(
          AssetTypeInfo{script_class->name, "", std::move(fields), true});
      continue;
    }
    ComponentInfo info;
    info.name = script_class->name;
    info.category = "Scripts";
    info.is_script = true;
    info.fields = std::move(fields);
    for (const auto& callable : script_class->callables) {
      info.methods.push_back({callable.name, callable.arg});
    }
    info.attach = [name = script_class->name](MapScene& scene, kEntityId entity,
                                              const mtext::PropertyList& properties) {
      const ScriptClass* found = ScriptRuntime::Instance().FindScriptClass(name);
      if (!found) return;
      v8::Isolate::Scope isolate_scope(ScriptRuntime::Instance().GetIsolate());
      v8::HandleScope handle_scope(ScriptRuntime::Instance().GetIsolate());
      v8::Context::Scope context_scope(ScriptRuntime::Instance().GetContext());
      auto behaviour = ScriptBehaviour::Create(*found, &scene, entity);
      if (!behaviour) return;
      auto reflector = Reflector::Read(properties);
      behaviour->Reflect(reflector);
      const std::string context = scene.GetGameObject(entity).GetPath() + "." + name;
      LogReflectionWarnings(reflector, context);
      // A key the script does not declare is usually a renamed field.
      for (const auto& property : properties) {
        bool known = false;
        for (const auto& field : found->fields) known = known || field.name == property.key;
        if (!known) {
          PDebug::warn("{}: no field named \"{}\" (was it renamed?)", context, property.key);
        }
      }
      scene.AddBehaviour(entity, std::move(behaviour), name);
    };
    TypeRegistry::Instance().RegisterComponent(std::move(info));
  }
}

void ScriptRuntime::SetHelper(std::string name, v8::Local<v8::Function> function) {
  helpers_[std::move(name)].Reset(isolate_, function);
}

v8::Local<v8::Function> ScriptRuntime::Helper(std::string_view name) const {
  auto it = helpers_.find(std::string(name));
  if (it == helpers_.end()) return {};
  return it->second.Get(isolate_);
}

void ScriptRuntime::PumpTimers() {
  if (!isolate_) return;
  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);
  v8::Context::Scope context_scope(GetContext());
  v8::Local<v8::Function> pump = Helper("pumpTimers");
  if (!pump.IsEmpty()) Call(pump, GetContext()->Global(), {}, "timers");
}

void ScriptRuntime::RegisterScene(uint32_t serial, MapScene* scene) { scenes_[serial] = scene; }
void ScriptRuntime::UnregisterScene(uint32_t serial) {
  scenes_.erase(serial);
  if (current_scene_ == serial) current_scene_ = 0;
}
MapScene* ScriptRuntime::FindScene(uint32_t serial) const {
  auto it = scenes_.find(serial);
  return it == scenes_.end() ? nullptr : it->second;
}

int ScriptRuntime::RegisterBehaviour(ScriptBehaviour* behaviour) {
  const int id = next_behaviour_id_++;
  behaviours_[id] = behaviour;
  return id;
}
void ScriptRuntime::UnregisterBehaviour(int id) { behaviours_.erase(id); }
ScriptBehaviour* ScriptRuntime::FindBehaviour(int id) const {
  auto it = behaviours_.find(id);
  return it == behaviours_.end() ? nullptr : it->second;
}

bool ScriptRuntime::AddScriptClass(std::unique_ptr<ScriptClass> script_class, std::string* error) {
  for (const auto& existing : classes_) {
    if (existing->name == script_class->name) {
      if (error) {
        *error = std::format("Two scripts are named \"{}\": {} and {}", script_class->name,
                             existing->location, script_class->location);
      }
      return false;
    }
  }
  classes_.push_back(std::move(script_class));
  return true;
}

const ScriptClass* ScriptRuntime::FindScriptClass(std::string_view name) const {
  for (const auto& c : classes_) {
    if (c->name == name) return c.get();
  }
  return nullptr;
}

}  // namespace Mercury
