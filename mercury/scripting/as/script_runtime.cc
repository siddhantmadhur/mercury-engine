// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * script_runtime.cc
 */
#include "scripting/as/script_runtime.h"

#include <algorithm>
#include <limits>
#include <cctype>
#include <cstdlib>
#include <format>

#include "debugger/debugger.h"
#include "math/time.h"
#include "os/filesystem.h"
#include "scriptbuilder/scriptbuilder.h"
#include "scripting/as/script_api.h"
#include "scripting/as/script_behaviour.h"
#include "scripting/type_registry.h"

namespace Mercury {

namespace {

std::atomic<uint32_t> g_next_scene_serial{1};

std::string ReadTextFile(const std::filesystem::path& path) {
  // Reads through the mounted .mpack when the game is running from a pack.
  auto text = os::ReadText(path);
  return text ? std::move(*text) : std::string();
}

std::string Trim(std::string_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
    begin++;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
    end--;
  return std::string(text.substr(begin, end - begin));
}

size_t FindMetaKey(const std::string& entry, std::string_view key) {
  for (size_t i = 0; i + key.size() <= entry.size(); i++) {
    if (i > 0 && !std::isspace(static_cast<unsigned char>(entry[i - 1])) &&
        entry[i - 1] != ',') {
      continue;
    }
    if (entry.compare(i, key.size(), key) != 0) continue;
    const size_t after = i + key.size();
    if (after == entry.size() ||
        std::isspace(static_cast<unsigned char>(entry[after])) ||
        entry[after] == '=') {
      return i;
    }
  }
  return std::string::npos;
}

bool MetaIs(const std::string& entry, std::string_view key) {
  return FindMetaKey(entry, key) != std::string::npos;
}

bool HasMeta(const std::vector<std::string>& metadata, std::string_view key) {
  for (const std::string& entry : metadata) {
    if (MetaIs(entry, key)) return true;
  }
  return false;
}

std::string MetaValue(const std::vector<std::string>& metadata,
                      std::string_view key) {
  for (const std::string& entry : metadata) {
    const size_t found = FindMetaKey(entry, key);
    if (found == std::string::npos) continue;
    size_t at = found + key.size();
    while (at < entry.size() &&
           (std::isspace(static_cast<unsigned char>(entry[at])) ||
            entry[at] == '=')) {
      at++;
    }
    if (at < entry.size() && (entry[at] == '"' || entry[at] == '\'')) {
      const char quote = entry[at++];
      std::string value;
      while (at < entry.size() && entry[at] != quote) {
        if (entry[at] == '\\' && at + 1 < entry.size()) at++;
        value += entry[at++];
      }
      return value;
    }
    return Trim(std::string_view(entry).substr(at));
  }
  return {};
}

std::vector<std::string> MetaQuoted(const std::vector<std::string>& metadata,
                                    std::string_view key) {
  std::vector<std::string> values;
  for (const std::string& entry : metadata) {
    const size_t found = FindMetaKey(entry, key);
    if (found == std::string::npos) continue;
    size_t at = found + key.size();
    while (at < entry.size()) {
      if (entry[at] == '"' || entry[at] == '\'') {
        const char quote = entry[at++];
        std::string value;
        while (at < entry.size() && entry[at] != quote) {
          if (entry[at] == '\\' && at + 1 < entry.size()) at++;
          value += entry[at++];
        }
        values.push_back(std::move(value));
        if (at < entry.size()) at++;
      } else {
        at++;
      }
    }
  }
  return values;
}

FieldType PrimitiveFieldType(int type_id) {
  if (type_id == asTYPEID_BOOL) return FieldType::kBool;
  if (type_id == asTYPEID_FLOAT || type_id == asTYPEID_DOUBLE)
    return FieldType::kFloat;
  if (type_id == asTYPEID_INT8 || type_id == asTYPEID_INT16 ||
      type_id == asTYPEID_INT32 || type_id == asTYPEID_INT64 ||
      type_id == asTYPEID_UINT8 || type_id == asTYPEID_UINT16 ||
      type_id == asTYPEID_UINT32 || type_id == asTYPEID_UINT64) {
    return FieldType::kInt;
  }
  return FieldType::kFloat;
}

// FieldType can only store bool, int and float. Reading a double, an int64,
// a handle or any other value through those would touch the wrong number of
// bytes, so [field] accepts only the types below.
bool IsReflectableElementType(asITypeInfo* type) {
  if (!type) return false;
  const std::string name = type->GetName();
  if (name == "string") return true;
  // These element types are not saved, but reading them is skipped rather
  // than reinterpreted, so they are safe to leave in place.
  if (name == "Vector2" || name == "Vector3" || name == "Color" ||
      name == "Event") {
    return true;
  }
  const int type_id = type->GetTypeId();
  return type_id == asTYPEID_BOOL || type_id == asTYPEID_INT32 ||
         type_id == asTYPEID_UINT32 || type_id == asTYPEID_FLOAT;
}

bool IsReflectableFieldType(int type_id, asITypeInfo* type) {
  const std::string name = type ? type->GetName() : "";
  if (name == "Vector2" || name == "Vector3" || name == "Color" ||
      name == "Event" || name == "string") {
    return true;
  }
  if (name == "array") return IsReflectableElementType(type->GetSubType());
  return type_id == asTYPEID_BOOL || type_id == asTYPEID_INT32 ||
         type_id == asTYPEID_UINT32 || type_id == asTYPEID_FLOAT;
}

FieldType ElementFieldType(asITypeInfo* type) {
  if (!type) return FieldType::kString;
  const std::string name = type->GetName();
  if (name == "Vector2") return FieldType::kVector2;
  if (name == "Vector3") return FieldType::kVector3;
  if (name == "Color") return FieldType::kColor;
  if (name == "string") return FieldType::kString;
  return PrimitiveFieldType(type->GetTypeId());
}

std::string CallArgName(int type_id) {
  if (type_id == asTYPEID_BOOL) return "bool";
  if (type_id == asTYPEID_FLOAT || type_id == asTYPEID_DOUBLE) return "float";
  if (type_id == asTYPEID_INT8 || type_id == asTYPEID_INT16 ||
      type_id == asTYPEID_INT32 || type_id == asTYPEID_INT64 ||
      type_id == asTYPEID_UINT8 || type_id == asTYPEID_UINT16 ||
      type_id == asTYPEID_UINT32 || type_id == asTYPEID_UINT64) {
    return "int";
  }
  return "string";
}

}  // namespace

ScriptRuntime& ScriptRuntime::Instance() {
  static ScriptRuntime instance;
  return instance;
}

ScriptRuntime::~ScriptRuntime() { Shutdown(); }

uint32_t ScriptRuntime::NextSceneSerial() {
  return g_next_scene_serial.fetch_add(1);
}

void ScriptRuntime::Throw(const char* message) {
  // The VM runs try/catch itself: the exception must always be raised, even
  // when a catch block is active or one caught an earlier exception.
  if (asIScriptContext* context = asGetActiveContext()) {
    context->SetException(message);
  }
}

bool ScriptRuntime::Initialize() {
  if (engine_) return true;
  engine_ = asCreateScriptEngine();
  if (!engine_) return false;

  engine_->SetMessageCallback(asFUNCTION(MessageCallback), this, asCALL_CDECL);
  engine_->SetEngineProperty(asEP_SCRIPT_SCANNER, 1);  // UTF-8
  engine_->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, 0);
  engine_->SetEngineProperty(asEP_PROPERTY_ACCESSOR_MODE, 3);

  if (const char* timeout = std::getenv("MERCURY_SCRIPT_TIMEOUT")) {
    timeout_seconds_ = std::atof(timeout);
  }

  ConfigureScriptEngine(engine_);

  return engine_ != nullptr;
}

void ScriptRuntime::Shutdown() {
  if (!engine_) return;
  data_assets_.clear();
  replaced_data_assets_.clear();
  behaviours_.clear();
  timers_.clear();
  classes_.clear();
  retired_classes_.clear();
  DiscardModule();
  engine_->ShutDownAndRelease();
  engine_ = nullptr;
  scenes_.clear();
  TypeRegistry::Instance().ClearGameTypes();
  InvalidateUiHandles();
}

void ScriptRuntime::DiscardModule() {
  if (!module_) return;
  module_->Discard();
  module_ = nullptr;
  loaded_project_.clear();
}

void ScriptRuntime::MessageCallback(const asSMessageInfo* msg, void* param) {
  if (!msg) return;
  auto* self = static_cast<ScriptRuntime*>(param);
  const std::string where =
      msg->section && *msg->section
          ? std::format("{}:{}:{}", msg->section, msg->row, msg->col)
          : "<engine>";
  switch (msg->type) {
    case asMSGTYPE_ERROR:
      if (self) self->compile_failed_ = true;
      PDebug::error("{}: {}", where, msg->message ? msg->message : "");
      break;
    case asMSGTYPE_WARNING:
      PDebug::warn("{}: {}", where, msg->message ? msg->message : "");
      break;
    default:
      PDebug::info("{}: {}", where, msg->message ? msg->message : "");
      break;
  }
}

void ScriptRuntime::LineCallback(asIScriptContext* context, void* param) {
  auto* self = static_cast<ScriptRuntime*>(param);
  if (!self || self->timeout_seconds_ <= 0.0) return;
  const auto elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - self->call_start_);
  if (elapsed.count() > self->timeout_seconds_) {
    self->timed_out_ = true;
    context->Abort();
  }
}

bool ScriptRuntime::BuildFromSections(
    const std::vector<std::pair<std::string, std::string>>& sections,
    const std::string& module_name, bool register_types) {
  if (!engine_) return false;
  compile_failed_ = false;

  // A module that will replace the current one is built under a name of its
  // own. StartNewModule would otherwise discard the module that behaviours are
  // using the moment it is called, so a build that then failed would have
  // taken the working scripts with it.
  const std::string build_name =
      register_types ? module_name + "#" + std::to_string(++build_generation_)
                     : module_name;
  CScriptBuilder builder;
  if (builder.StartNewModule(engine_, build_name.c_str()) < 0) {
    PDebug::error("Could not start the script module {}", module_name);
    return false;
  }
  const auto abandon = [&] {
    if (register_types) engine_->DiscardModule(build_name.c_str());
    return false;
  };
  builder.AddSectionFromMemory("<engine>", ApiPreludeSource());
  for (const auto& [name, text] : sections) {
    const int result = builder.AddSectionFromMemory(name.c_str(), text.c_str());
    if (result < 0) {
      PDebug::error("Could not add script section {}", name);
      return abandon();
    }
  }
  if (builder.BuildModule() < 0 || compile_failed_) return abandon();

  asIScriptModule* module = builder.GetModule();
  if (register_types) {
    // Data-asset instances belong to classes of the module being discarded, and
    // releasing them needs the module and the class table still in place.
    data_assets_.clear();
    replaced_data_assets_.clear();
    DiscardModule();
    // Live behaviours point at their ScriptClass; keep the old table around
    // (until Shutdown) rather than freeing it from under them.
    if (!behaviours_.empty()) {
      for (auto& old : classes_) retired_classes_.push_back(std::move(old));
    }
    classes_.clear();
    TypeRegistry::Instance().ClearGameTypes();
    module_ = module;
    module_name_ = build_name;
    RegisterClasses(module, builder, true);
  }
  return true;
}

void ScriptRuntime::RegisterClasses(asIScriptModule* module,
                                    CScriptBuilder& builder,
                                    bool register_types) {
  if (!module) return;
  for (asUINT i = 0; i < module->GetObjectTypeCount(); i++) {
    asITypeInfo* type = module->GetObjectTypeByIndex(i);
    if (!type) continue;
    const std::string type_name = type->GetName();
    if (type_name == "Behaviour" || type_name == "DataAsset") continue;

    const int type_id = type->GetTypeId();
    const auto type_meta = builder.GetMetadataForType(type_id);
    bool is_script = false;
    bool is_data_asset = false;
    for (const auto& entry : type_meta) {
      if (MetaIs(entry, "script")) is_script = true;
      if (MetaIs(entry, "dataAsset")) is_data_asset = true;
    }
    if (!is_script && !is_data_asset) continue;

    auto script_class = std::make_unique<ScriptClass>();
    script_class->name = type_name;
    script_class->type = type;
    script_class->data_asset = is_data_asset;
    if (const std::string custom = MetaValue(type_meta, "script");
        !custom.empty()) {
      script_class->name = custom;
    }
    // Only a constructor with no parameters can be called by the engine.
    for (asUINT f = 0; f < type->GetFactoryCount(); f++) {
      asIScriptFunction* factory = type->GetFactoryByIndex(f);
      if (factory && factory->GetParamCount() == 0) {
        script_class->factory = factory;
        break;
      }
    }

    const char* section = nullptr;
    int row = 0;
    int column = 0;
    if (asIScriptFunction* factory = script_class->factory;
        factory &&
        factory->GetDeclaredAt(&section, &row, &column) == asSUCCESS) {
      script_class->location =
          std::format("{}:{}:{}", section ? section : "", row, column);
    } else if (type->GetMethodCount() > 0) {
      asIScriptFunction* method = type->GetMethodByIndex(0, true);
      if (method &&
          method->GetDeclaredAt(&section, &row, &column) == asSUCCESS) {
        script_class->location =
            std::format("{}:{}:{}", section ? section : "", row, column);
      }
    }

    bool class_supported = true;
    for (asUINT p = 0; p < type->GetPropertyCount(); p++) {
      const auto metadata = builder.GetMetadataForTypeProperty(type_id, p);
      bool is_field = false;
      for (const auto& entry : metadata) {
        if (MetaIs(entry, "field")) is_field = true;
      }
      if (!is_field) continue;

      ScriptField field;
      const char* property_name = nullptr;
      int property_type_id = 0;
      type->GetProperty(p, &property_name, &property_type_id);
      field.name = property_name ? property_name : "";
      field.property_index = static_cast<int>(p);
      field.type_id = property_type_id;
      asITypeInfo* field_type = engine_->GetTypeInfoById(field.type_id);
      const std::string field_type_name =
          field_type ? field_type->GetName() : "";

      if (field_type_name == "Vector2") {
        field.type = FieldType::kVector2;
      } else if (field_type_name == "Vector3") {
        field.type = FieldType::kVector3;
      } else if (field_type_name == "Color") {
        field.type = FieldType::kColor;
      } else if (field_type_name == "Event") {
        field.type = FieldType::kEvent;
      } else if (field_type_name == "string") {
        field.type = FieldType::kString;
        if (MetaValue(metadata, "nodeRef").empty() &&
            HasMeta(metadata, "nodeRef")) {
          field.type = FieldType::kNodeRef;
        }
        if (!MetaValue(metadata, "asset").empty()) {
          field.type = FieldType::kAsset;
          field.asset_type = MetaValue(metadata, "asset");
        }
      } else if (field_type_name == "array") {
        field.type = FieldType::kList;
        field.element_type =
            ElementFieldType(field_type ? field_type->GetSubType() : nullptr);
      } else {
        field.type = PrimitiveFieldType(field.type_id);
      }

      if (!IsReflectableFieldType(field.type_id, field_type)) {
        PDebug::error(
            "{}: [field] {} is a {}; only bool, int, float, string, Vector2, "
            "Vector3, Color, Event and array of those can be saved",
            script_class->name, field.name,
            field_type_name.empty() ? "value type" : field_type_name);
        class_supported = false;
        break;
      }

      const auto enum_values = MetaQuoted(metadata, "enum");
      if (!enum_values.empty()) {
        if (field.type_id != asTYPEID_INT32 &&
            field.type_id != asTYPEID_UINT32) {
          PDebug::error("{}: [field] {} is an enum but not an int",
                        script_class->name, field.name);
          class_supported = false;
          break;
        }
        field.type = FieldType::kEnum;
        field.enum_values = enum_values;
      }
      field.tooltip = MetaValue(metadata, "tooltip");
      script_class->fields.push_back(std::move(field));
    }
    if (!class_supported) continue;

    for (asUINT m = 0; m < type->GetMethodCount(); m++) {
      asIScriptFunction* method = type->GetMethodByIndex(m, true);
      if (!method) continue;
      const auto metadata = builder.GetMetadataForTypeMethod(type_id, method);
      bool callable = false;
      bool bindable = false;
      for (const auto& entry : metadata) {
        if (MetaIs(entry, "callable")) callable = true;
        if (MetaIs(entry, "bindable")) bindable = true;
      }
      if (callable) {
        ScriptCallable entry;
        entry.name = method->GetName();
        if (method->GetParamCount() > 0) {
          int param_type = 0;
          method->GetParam(0, &param_type);
          entry.arg = CallArgName(param_type);
        }
        entry.function = method;
        script_class->callables.push_back(std::move(entry));
      }
      if (bindable) {
        std::string name = method->GetName();
        if (name.starts_with("get_")) name = name.substr(4);
        script_class->bindables.push_back(std::move(name));
      }
    }

    static constexpr const char* kCallbackNames[kScriptCallbackCount] = {
        "awake",           "onEnable",
        "onDisable",       "onStart",
        "onFrame",         "onLateFrame",
        "onTick",          "onCollisionEnter",
        "onCollisionStay", "onCollisionLeave",
        "onTriggerEnter",  "onTriggerStay",
        "onTriggerExit",   "onDestroy"};
    for (int c = 0; c < kScriptCallbackCount; c++) {
      asIScriptFunction* method =
          type->GetMethodByName(kCallbackNames[c], true);
      if (!method) continue;
      script_class->callbacks |= (1u << c);
      script_class->callbacks_by_slot[c] = method;
      if (method->GetParamCount() > 0) {
        int param = 0;
        method->GetParam(0, &param);
        if (param == asTYPEID_FLOAT) {
          script_class->callback_arg_types[c] = 1;
        } else if (param == asTYPEID_DOUBLE) {
          script_class->callback_arg_types[c] = 2;
        } else {
          asITypeInfo* param_type = engine_->GetTypeInfoById(param);
          if (param_type &&
              std::string(param_type->GetName()) == "GameObject") {
            script_class->callback_arg_types[c] = 3;
          }
        }
      }
    }

    std::string error;
    if (!AddScriptClass(std::move(script_class), &error)) {
      PDebug::error("{}", error);
      continue;
    }
  }

  if (!register_types) return;

  for (const auto& script_class : classes_) {
    if (script_class->data_asset) {
      const auto* existing =
          TypeRegistry::Instance().FindAssetType(script_class->name);
      // A script may not take the name of a type the engine itself registers:
      // it would replace the built-in in the TypeRegistry and ClearGameTypes
      // would then remove the built-in when the runtime shuts down.
      if (existing && !existing->from_game) {
        PDebug::error(
            "{}: a built-in asset type has this name; rename the class",
            script_class->name);
        continue;
      }
      // C++ code linked into the executable wins over a script with the same
      // name; the native type is the one the executable was built with.
      if (existing && existing->native_asset) {
        PDebug::warn(
            "{}: a C++ MERCURY_DATA_ASSET with this name is already "
            "registered; the AngelScript class is ignored",
            script_class->name);
        continue;
      }
    } else {
      const auto* existing =
          TypeRegistry::Instance().FindComponent(script_class->name);
      if (existing && !existing->is_script) {
        PDebug::error(
            "{}: a built-in component has this name; rename the class",
            script_class->name);
        continue;
      }
      if (existing && existing->native_script) {
        PDebug::warn(
            "{}: a C++ MERCURY_SCRIPT with this name is already registered; "
            "the AngelScript class is ignored",
            script_class->name);
        continue;
      }
    }
    std::vector<FieldInfo> fields;
    if (auto instance =
            ScriptBehaviour::Create(*script_class, nullptr, entt::null)) {
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
    info.attach = [name = script_class->name](
                      MapScene& scene, kEntityId entity,
                      const mtext::PropertyList& properties) {
      const ScriptClass* found =
          ScriptRuntime::Instance().FindScriptClass(name);
      if (!found) return;
      auto behaviour = ScriptBehaviour::Create(*found, &scene, entity);
      if (!behaviour) return;
      auto reflector = Reflector::Read(properties);
      behaviour->Reflect(reflector);
      const std::string context =
          scene.GetGameObject(entity).GetPath() + "." + name;
      LogReflectionWarnings(reflector, context);
      for (const auto& property : properties) {
        bool known = false;
        for (const auto& field : found->fields)
          known = known || field.name == property.key;
        if (!known) {
          PDebug::warn("{}: no field named \"{}\" (was it renamed?)", context,
                       property.key);
        }
      }
      scene.AddBehaviour(entity, std::move(behaviour), name);
    };
    TypeRegistry::Instance().RegisterComponent(std::move(info));
  }
}

bool ScriptRuntime::LoadScripts(const std::filesystem::path& project_root) {
  if (!engine_) return false;
  const std::string project = project_root.string();
  if (module_ && loaded_project_ == project) return true;
  const std::filesystem::path scripts = project_root / "scripts";
  std::vector<std::filesystem::path> files;
  for (const std::string& relative : os::ListFilesRecursive(scripts)) {
    const std::filesystem::path path(relative);
    if (path.extension() != ".as") continue;
    if (path.filename().string().starts_with(".")) continue;
    files.push_back(scripts / path);
  }
  std::sort(files.begin(), files.end());

  std::vector<std::pair<std::string, std::string>> sections;
  sections.reserve(files.size());
  for (const auto& file : files) {
    std::string text = ReadTextFile(file);
    if (text.empty()) continue;
    const std::string name =
        file.lexically_relative(project_root).generic_string();
    sections.emplace_back(name, std::move(text));
  }
  const bool ok = BuildFromSections(sections, "game", true);
  if (ok) loaded_project_ = project;
  return ok;
}

bool ScriptRuntime::LoadFiles(const std::vector<std::filesystem::path>& files,
                              const std::string& module_name,
                              bool register_types) {
  std::vector<std::pair<std::string, std::string>> sections;
  sections.reserve(files.size());
  for (const auto& file : files) {
    std::string text = ReadTextFile(file);
    if (text.empty()) continue;
    sections.emplace_back(file.generic_string(), std::move(text));
  }
  return BuildFromSections(sections, module_name, register_types);
}

bool ScriptRuntime::RunString(std::string_view source, std::string_view name,
                              std::string* result) {
  if (!engine_) return false;
  const std::string module_name =
      std::format("eval_{}", reinterpret_cast<uintptr_t>(&source));
  CScriptBuilder builder;
  if (builder.StartNewModule(engine_, module_name.c_str()) < 0) return false;
  builder.AddSectionFromMemory("<engine>", ApiPreludeSource());
  builder.AddSectionFromMemory(std::string(name).c_str(),
                               std::string(source).c_str());
  compile_failed_ = false;
  if (builder.BuildModule() < 0 || compile_failed_) {
    engine_->DiscardModule(module_name.c_str());
    return false;
  }
  asIScriptModule* module = builder.GetModule();
  asIScriptFunction* main = module->GetFunctionByDecl("string main()");
  bool string_result = main != nullptr;
  if (!main) main = module->GetFunctionByDecl("int main()");
  if (!main) {
    engine_->DiscardModule(module_name.c_str());
    return false;
  }

  bool ok = false;
  asIScriptContext* context = engine_->CreateContext();
  if (context && context->Prepare(main) >= 0) {
    const bool outermost = call_depth_++ == 0;
    if (outermost) {
      call_start_ = std::chrono::steady_clock::now();
      timed_out_ = false;
    }
    context->SetLineCallback(asFUNCTION(LineCallback), this, asCALL_CDECL);
    const int r = context->Execute();
    context->ClearLineCallback();
    call_depth_--;
    if (r == asEXECUTION_FINISHED) {
      ok = true;
      if (result) {
        if (string_result) {
          *result =
              *static_cast<std::string*>(context->GetAddressOfReturnValue());
        } else {
          *result = std::to_string(static_cast<int>(context->GetReturnDWord()));
        }
      }
    } else if (timed_out_) {
      PDebug::error("{}: script took too long (infinite loop?)", name);
    } else {
      PDebug::error("{}: {}", name, FormatException(context));
    }
  }
  if (context) context->Release();
  engine_->DiscardModule(module_name.c_str());
  return ok;
}

namespace {

// ScriptArg integers are 32-bit; a bigger script value saturates rather than
// wrapping around into a different number.
int SaturateToInt(int64_t value) {
  return static_cast<int>(
      std::clamp<int64_t>(value, std::numeric_limits<int>::min(),
                          std::numeric_limits<int>::max()));
}
int SaturateToInt(uint64_t value) {
  return static_cast<int>(std::min<uint64_t>(
      value, static_cast<uint64_t>(std::numeric_limits<int>::max())));
}

// Reads a finished call's return value. For a function that returns a
// reference the context holds the address of the value, not the value, so it
// is read through that address; the plain accessors would give 0 or garbage.
// Object handles and value types are not returned (the handle belongs to the
// context, which is released right after, and nothing consumes them).
ScriptArg DecodeReturn(asIScriptEngine* engine, asIScriptContext* context,
                       int type_id, bool is_reference) {
  const void* ref = nullptr;
  if (is_reference) {
    ref = *static_cast<void* const*>(context->GetAddressOfReturnValue());
    if (!ref) return ScriptArg{};
  }
  switch (type_id) {
    case asTYPEID_VOID:
      return ScriptArg{};
    case asTYPEID_BOOL:
      return ScriptArg::Bool(ref ? *static_cast<const bool*>(ref)
                                 : context->GetReturnByte() != 0);
    case asTYPEID_INT8:
      return ScriptArg::Int(ref ? *static_cast<const int8_t*>(ref)
                                : static_cast<int8_t>(context->GetReturnByte()));
    case asTYPEID_UINT8:
      return ScriptArg::Int(ref ? *static_cast<const uint8_t*>(ref)
                                : context->GetReturnByte());
    case asTYPEID_INT16:
      return ScriptArg::Int(ref ? *static_cast<const int16_t*>(ref)
                                : static_cast<int16_t>(context->GetReturnWord()));
    case asTYPEID_UINT16:
      return ScriptArg::Int(ref ? *static_cast<const uint16_t*>(ref)
                                : context->GetReturnWord());
    case asTYPEID_INT32:
      return ScriptArg::Int(ref ? *static_cast<const int32_t*>(ref)
                                : static_cast<int32_t>(context->GetReturnDWord()));
    case asTYPEID_UINT32:
      return ScriptArg::Int(SaturateToInt(
          static_cast<uint64_t>(ref ? *static_cast<const uint32_t*>(ref)
                                    : context->GetReturnDWord())));
    case asTYPEID_INT64:
      return ScriptArg::Int(SaturateToInt(
          ref ? *static_cast<const int64_t*>(ref)
              : static_cast<int64_t>(context->GetReturnQWord())));
    case asTYPEID_UINT64:
      return ScriptArg::Int(SaturateToInt(
          ref ? *static_cast<const uint64_t*>(ref)
              : static_cast<uint64_t>(context->GetReturnQWord())));
    case asTYPEID_FLOAT:
      return ScriptArg::Float(ref ? *static_cast<const float*>(ref)
                                  : context->GetReturnFloat());
    case asTYPEID_DOUBLE:
      return ScriptArg::Double(ref ? *static_cast<const double*>(ref)
                                   : context->GetReturnDouble());
    default:
      break;
  }
  if (type_id & asTYPEID_OBJHANDLE) return ScriptArg{};
  asITypeInfo* type = engine->GetTypeInfoById(type_id);
  if (type && std::string(type->GetName()) == "string") {
    const void* text = ref ? ref : context->GetAddressOfReturnValue();
    return ScriptArg::String(*static_cast<const std::string*>(text));
  }
  if (type_id & asTYPEID_MASK_OBJECT) return ScriptArg{};
  // An enum: read it as its underlying integer type (32-bit unless declared).
  const int underlying = type ? type->GetUnderlyingTypeId() : asTYPEID_INT32;
  return DecodeReturn(engine, context,
                      underlying > 0 && underlying != type_id
                          ? underlying
                          : asTYPEID_INT32,
                      is_reference);
}

}  // namespace

bool ScriptRuntime::Call(asIScriptFunction* function, asIScriptObject* object,
                         std::span<const ScriptArg> args,
                         std::string_view label, ScriptArg* result) {
  if (!engine_ || !function) return false;
  asIScriptContext* context = engine_->CreateContext();
  if (!context) return false;
  bool ok = false;
  if (context->Prepare(function) < 0) {
    context->Release();
    return false;
  }
  if (object) context->SetObject(object);
  if (args.size() != static_cast<size_t>(function->GetParamCount())) {
    PDebug::error("{}: the function takes {} argument(s), {} were passed",
                  label, function->GetParamCount(), args.size());
    context->Release();
    return false;
  }
  // String arguments must outlive Execute(): a `const string &in` parameter is
  // bound to this storage, not copied by the VM.
  std::vector<std::string> strings;
  strings.reserve(args.size());
  for (size_t i = 0; i < args.size(); i++) {
    const ScriptArg& arg = args[i];
    const asUINT index = static_cast<asUINT>(i);
    int param_type = 0;
    function->GetParam(index, &param_type);
    const int base =
        param_type & ~(asTYPEID_OBJHANDLE | asTYPEID_HANDLETOCONST);
    asITypeInfo* param_info = engine_->GetTypeInfoById(base);
    const std::string param_name = param_info ? param_info->GetName() : "";
    bool matches = false;
    switch (arg.kind) {
      case ScriptArg::Kind::kBool:
        matches = param_type == asTYPEID_BOOL;
        break;
      case ScriptArg::Kind::kInt:
        matches = param_type == asTYPEID_INT32 || param_type == asTYPEID_UINT32;
        break;
      case ScriptArg::Kind::kFloat:
        matches = param_type == asTYPEID_FLOAT;
        break;
      case ScriptArg::Kind::kDouble:
        matches = param_type == asTYPEID_DOUBLE;
        break;
      case ScriptArg::Kind::kString:
        matches = param_name == "string";
        break;
      case ScriptArg::Kind::kObject:
        matches = arg.type_id != 0 &&
                  base == (arg.type_id &
                           ~(asTYPEID_OBJHANDLE | asTYPEID_HANDLETOCONST));
        break;
      case ScriptArg::Kind::kNone:
        break;
    }
    if (!matches) {
      PDebug::error(
          "{}: argument {} does not match the parameter type ({})", label, i,
          param_name.empty() ? std::to_string(param_type) : param_name);
      context->Release();
      return false;
    }
    switch (arg.kind) {
      case ScriptArg::Kind::kBool:
        context->SetArgByte(index, arg.b ? 1 : 0);
        break;
      case ScriptArg::Kind::kInt:
        context->SetArgDWord(index, arg.i);
        break;
      case ScriptArg::Kind::kFloat:
        context->SetArgFloat(index, arg.f);
        break;
      case ScriptArg::Kind::kDouble:
        context->SetArgDouble(index, arg.d);
        break;
      case ScriptArg::Kind::kString:
        strings.push_back(arg.s);
        context->SetArgObject(index, &strings.back());
        break;
      case ScriptArg::Kind::kObject:
        context->SetArgObject(index, arg.object);
        break;
      case ScriptArg::Kind::kNone:
        break;
    }
  }

  const bool outermost = call_depth_++ == 0;
  if (outermost) {
    call_start_ = std::chrono::steady_clock::now();
    timed_out_ = false;
  }
  context->SetLineCallback(asFUNCTION(LineCallback), this, asCALL_CDECL);
  const int r = context->Execute();
  context->ClearLineCallback();
  call_depth_--;
  if (r == asEXECUTION_FINISHED) {
    ok = true;
    if (result) {
      asDWORD flags = 0;
      const int type_id = function->GetReturnTypeId(&flags);
      *result = DecodeReturn(engine_, context, type_id,
                             (flags & asTM_INOUTREF) != 0);
    }
  } else if (timed_out_) {
    PDebug::error("{}: script took too long (infinite loop?)", label);
  } else {
    PDebug::error("{}: {}", label, FormatException(context));
  }
  context->Release();
  return ok;
}

bool ScriptRuntime::RunFactory(asIScriptFunction* factory,
                               std::string_view label,
                               asIScriptObject** instance) {
  if (!engine_ || !factory || !instance) return false;
  *instance = nullptr;
  asIScriptContext* context = engine_->CreateContext();
  if (!context) return false;
  bool ok = false;
  if (context->Prepare(factory) < 0) {
    PDebug::error("{}: the constructor could not be prepared", label);
    context->Release();
    return false;
  }
  const bool outermost = call_depth_++ == 0;
  if (outermost) {
    call_start_ = std::chrono::steady_clock::now();
    timed_out_ = false;
  }
  context->SetLineCallback(asFUNCTION(LineCallback), this, asCALL_CDECL);
  const int r = context->Execute();
  context->ClearLineCallback();
  call_depth_--;
  if (r == asEXECUTION_FINISHED) {
    auto* object =
        *static_cast<asIScriptObject**>(context->GetAddressOfReturnValue());
    if (object) {
      object->AddRef();
      *instance = object;
      ok = true;
    }
  } else if (timed_out_) {
    PDebug::error("{}: script took too long (infinite loop?)", label);
  } else {
    PDebug::error("{}: {}", label, FormatException(context));
  }
  context->Release();
  return ok;
}

std::string ScriptRuntime::FormatException(asIScriptContext* context) const {
  if (!context) return "AngelScript error";
  const char* section = nullptr;
  int column = 0;
  const int line = context->GetExceptionLineNumber(&column, &section);
  const char* message = context->GetExceptionString();
  std::string text;
  if (section && *section) {
    text = std::format("{}:{}:{}: {}", section, line, column,
                       message ? message : "exception");
  } else {
    text =
        std::format("{}:{}: {}", line, column, message ? message : "exception");
  }
  if (asIScriptFunction* function = context->GetExceptionFunction()) {
    text += std::format(" (in {})", function->GetDeclaration());
  }
  return text;
}

ScriptBehaviour* ScriptRuntime::BehaviourOf(asIScriptObject* object) const {
  if (!object) return nullptr;
  return static_cast<ScriptBehaviour*>(object->GetUserData());
}

bool ScriptRuntime::CallMethod(asIScriptObject* object, std::string_view method,
                               std::span<const ScriptArg> args,
                               std::string_view label) {
  if (!object) return false;
  asITypeInfo* type = object->GetObjectType();
  if (!type) return false;
  asIScriptFunction* function =
      type->GetMethodByName(std::string(method).c_str(), true);
  if (!function) {
    PDebug::warn("{}: the script has no method {}", label, method);
    return false;
  }
  return Call(function, object, args, label);
}

void ScriptRuntime::RegisterScene(uint32_t serial, MapScene* scene) {
  scenes_[serial] = scene;
}
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

asIScriptObject* ScriptRuntime::FindDataAsset(const std::string& key,
                                              const void* version) const {
  auto it = data_assets_.find(key);
  if (it == data_assets_.end() || it->second.version.get() != version ||
      !it->second.behaviour) {
    return nullptr;
  }
  return it->second.behaviour->Instance();
}

void ScriptRuntime::OwnDataAsset(std::string key,
                                 std::shared_ptr<const void> version,
                                 std::unique_ptr<ScriptBehaviour> asset) {
  auto& entry = data_assets_[std::move(key)];
  if (entry.behaviour) {
    replaced_data_assets_.push_back(std::move(entry.behaviour));
  }
  entry.version = std::move(version);
  entry.behaviour = std::move(asset);
}

int ScriptRuntime::AddTimer(int behaviour, std::string method, double seconds,
                            bool repeat) {
  const int id = next_timer_id_++;
  timers_[id] = Timer{behaviour, std::move(method), seconds, seconds, repeat};
  return id;
}

void ScriptRuntime::PumpTimers() {
  if (timers_.empty()) return;
  const double dt = static_cast<double>(Time::GetFixedDeltaTime());
  std::vector<int> due;
  for (auto& [id, timer] : timers_) {
    timer.remaining -= dt;
    if (timer.remaining <= 0.0) due.push_back(id);
  }
  for (int id : due) {
    auto it = timers_.find(id);
    if (it == timers_.end()) continue;
    Timer timer = it->second;
    ScriptBehaviour* behaviour = FindBehaviour(timer.behaviour);
    if (!behaviour) {
      timers_.erase(id);
      continue;
    }
    CallMethod(behaviour->Instance(), timer.method, {},
               "timer " + timer.method);
    if (timer.repeat) {
      auto again = timers_.find(id);
      if (again != timers_.end()) again->second.remaining += timer.interval;
    } else {
      timers_.erase(id);
    }
  }
}

bool ScriptRuntime::AddScriptClass(std::unique_ptr<ScriptClass> script_class,
                                   std::string* error) {
  for (const auto& existing : classes_) {
    if (existing->name == script_class->name) {
      if (error) {
        *error = std::format("Two scripts are named \"{}\": {} and {}",
                             script_class->name, existing->location,
                             script_class->location);
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
