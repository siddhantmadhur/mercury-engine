/*
 * script_behaviour.cc
 */
#include "scripting/as/script_behaviour.h"

#include <bit>
#include <cmath>
#include <format>

#include "debugger/debugger.h"
#include "scriptarray/scriptarray.h"
#include "scripting/as/script_event.h"
#include "scripting/type_registry.h"

namespace Mercury {

namespace {

void LogException(asIScriptContext* context, std::string_view label) {
  PDebug::error("{}: {}", label, ScriptRuntime::Instance().FormatException(context));
}

// --- field conversion ---------------------------------------------------------------------

// --- list fields --------------------------------------------------------------------------

std::vector<int> ReadIntArray(CScriptArray* array) {
  std::vector<int> values;
  if (!array) return values;
  values.reserve(array->GetSize());
  for (asUINT i = 0; i < array->GetSize(); i++) values.push_back(*static_cast<int*>(array->At(i)));
  return values;
}

std::vector<float> ReadFloatArray(CScriptArray* array) {
  std::vector<float> values;
  if (!array) return values;
  values.reserve(array->GetSize());
  for (asUINT i = 0; i < array->GetSize(); i++) values.push_back(*static_cast<float*>(array->At(i)));
  return values;
}

std::vector<std::string> ReadStringArray(CScriptArray* array) {
  std::vector<std::string> values;
  if (!array) return values;
  values.reserve(array->GetSize());
  for (asUINT i = 0; i < array->GetSize(); i++) {
    values.push_back(*static_cast<std::string*>(array->At(i)));
  }
  return values;
}

template <typename T>
void WriteArray(CScriptArray** handle, asITypeInfo* type, const std::vector<T>& values) {
  if (!handle) return;
  CScriptArray* array = *handle;
  if (!array && type) {
    array = CScriptArray::Create(type, static_cast<asUINT>(values.size()));
    *handle = array;
  }
  if (!array) return;
  array->Resize(static_cast<asUINT>(values.size()));
  for (asUINT i = 0; i < values.size(); i++) array->SetValue(i, const_cast<T*>(&values[i]));
}

ScriptArg VariantToArg(const mtext::Variant& value) {
  if (auto b = value.Get<bool>()) return ScriptArg::Bool(*b);
  if (auto i = value.ToInt()) return ScriptArg::Int(static_cast<int>(*i));
  if (auto f = value.ToFloat()) return ScriptArg::Float(static_cast<float>(*f));
  if (auto s = value.ToString()) return ScriptArg::String(*s);
  return ScriptArg{};
}

mtext::Variant ArgToVariant(const ScriptArg& value) {
  switch (value.kind) {
    case ScriptArg::Kind::kBool: return mtext::Variant(value.b);
    case ScriptArg::Kind::kInt: return mtext::Variant(value.i);
    case ScriptArg::Kind::kFloat: return mtext::Variant(value.f);
    case ScriptArg::Kind::kDouble: return mtext::Variant(static_cast<float>(value.d));
    case ScriptArg::Kind::kString: return mtext::Variant(value.s);
    default: return mtext::Variant();
  }
}

}  // namespace

std::unique_ptr<ScriptBehaviour> ScriptBehaviour::Create(const ScriptClass& script_class,
                                                         MapScene* scene, kEntityId entity) {
  auto& runtime = ScriptRuntime::Instance();
  if (!runtime.IsInitialized() || !script_class.factory) {
    PDebug::error("{}: the script engine is not ready", script_class.name);
    return nullptr;
  }

  asIScriptContext* context = runtime.GetEngine()->CreateContext();
  if (!context) return nullptr;
  std::unique_ptr<ScriptBehaviour> behaviour(new ScriptBehaviour(script_class));
  asIScriptObject* instance = nullptr;
  if (context->Prepare(script_class.factory) >= 0) {
    if (context->Execute() == asEXECUTION_FINISHED) {
      instance = *static_cast<asIScriptObject**>(context->GetAddressOfReturnValue());
      if (instance) instance->AddRef();
    } else {
      LogException(context, script_class.name + ".constructor");
    }
  }
  context->Release();
  if (!instance) return nullptr;

  behaviour->instance_ = instance;
  behaviour->id_ = runtime.RegisterBehaviour(behaviour.get());
  behaviour->scene_serial_ = scene ? scene->GetSerial() : 0;
  instance->SetUserData(behaviour.get());

  for (const ScriptField& field : script_class.fields) {
    if (field.type != FieldType::kEvent) continue;
    Event& event = behaviour->events_[field.name];
    event.Bind(scene);
    void* address = instance->GetAddressOfProperty(field.property_index);
    if (auto* value = static_cast<ScriptEvent*>(address)) value->impl = &event;
  }
  return behaviour;
}

ScriptBehaviour::~ScriptBehaviour() {
  auto& runtime = ScriptRuntime::Instance();
  if (runtime.IsInitialized()) {
    if (id_) runtime.UnregisterBehaviour(id_);
    if (instance_) instance_->SetUserData(nullptr);
  }
  if (instance_) instance_->Release();
}

Event* ScriptBehaviour::FindEvent(const std::string& field) {
  auto it = events_.find(field);
  return it == events_.end() ? nullptr : &it->second;
}

void ScriptBehaviour::Forward(ScriptCallback callback, const char* method) {
  const int slot = std::countr_zero(static_cast<uint32_t>(callback));
  asIScriptFunction* function = class_->callbacks_by_slot[slot];
  if (!function) return;
  ScriptRuntime::Instance().SetCurrentScene(scene_serial_);
  ScriptRuntime::Instance().Call(function, instance_, {}, class_->name + "." + method);
}

void ScriptBehaviour::Forward(ScriptCallback callback, const char* method, double dt) {
  const int slot = std::countr_zero(static_cast<uint32_t>(callback));
  asIScriptFunction* function = class_->callbacks_by_slot[slot];
  if (!function) return;
  ScriptRuntime::Instance().SetCurrentScene(scene_serial_);
  if (class_->callback_arg_types[slot] == 1) {
    const ScriptArg args[] = {ScriptArg::Float(static_cast<float>(dt))};
    ScriptRuntime::Instance().Call(function, instance_, args, class_->name + "." + method);
  } else {
    const ScriptArg args[] = {ScriptArg::Double(dt)};
    ScriptRuntime::Instance().Call(function, instance_, args, class_->name + "." + method);
  }
}

void ScriptBehaviour::ForwardObject(ScriptCallback callback, const char* method, GameObject other) {
  const int slot = std::countr_zero(static_cast<uint32_t>(callback));
  asIScriptFunction* function = class_->callbacks_by_slot[slot];
  if (!function || !other.GetScene()) return;
  ScriptRuntime::Instance().SetCurrentScene(scene_serial_);
  const int type_id = ScriptRuntime::Instance().GetEngine()->GetTypeIdByDecl("GameObject");
  const ScriptArg args[] = {ScriptArg::Object(&other, type_id)};
  ScriptRuntime::Instance().Call(function, instance_, args, class_->name + "." + method);
}

void ScriptBehaviour::CallMethod(const std::string& name, const ScriptArg* argument) {
  asITypeInfo* type = instance_ ? instance_->GetObjectType() : nullptr;
  if (!type) return;
  asIScriptFunction* function = type->GetMethodByName(name.c_str(), true);
  if (!function) return;
  ScriptRuntime::Instance().SetCurrentScene(scene_serial_);
  if (argument) {
    const ScriptArg args[] = {*argument};
    ScriptRuntime::Instance().Call(function, instance_, args, class_->name + "." + name);
  } else {
    ScriptRuntime::Instance().Call(function, instance_, {}, class_->name + "." + name);
  }
}

void ScriptBehaviour::Reflect(Reflector& r) { ReflectFields(r); }

void ScriptBehaviour::ReflectFields(Reflector& r) {
  for (const ScriptField& field : class_->fields) {
    void* address = instance_ ? instance_->GetAddressOfProperty(field.property_index) : nullptr;
    if (!address) continue;
    const char* name = field.name.c_str();

    switch (field.type) {
      case FieldType::kBool: {
        bool value = *static_cast<bool*>(address);
        const bool original = value;
        r.Field(name, value);
        if (value != original) *static_cast<bool*>(address) = value;
        break;
      }
      case FieldType::kInt: {
        int value = *static_cast<int*>(address);
        const int original = value;
        r.Field(name, value);
        if (value != original) *static_cast<int*>(address) = value;
        break;
      }
      case FieldType::kFloat: {
        float value = *static_cast<float*>(address);
        const float original = value;
        r.Field(name, value);
        if (value != original) *static_cast<float*>(address) = value;
        break;
      }
      case FieldType::kString: {
        std::string value = *static_cast<std::string*>(address);
        const std::string original = value;
        r.Field(name, value);
        if (value != original) *static_cast<std::string*>(address) = value;
        break;
      }
      case FieldType::kEnum: {
        int index = *static_cast<int*>(address);
        r.Enum(name, index, field.enum_values);
        if (index >= 0 && index < static_cast<int>(field.enum_values.size())) {
          *static_cast<int*>(address) = index;
        }
        break;
      }
      case FieldType::kVector2: {
        glm::vec2 value = *static_cast<glm::vec2*>(address);
        r.Field(name, value);
        *static_cast<glm::vec2*>(address) = value;
        break;
      }
      case FieldType::kVector3: {
        glm::vec3 value = *static_cast<glm::vec3*>(address);
        r.Field(name, value);
        *static_cast<glm::vec3*>(address) = value;
        break;
      }
      case FieldType::kColor: {
        glm::vec4 value = *static_cast<glm::vec4*>(address);
        r.Field(name, value);
        *static_cast<glm::vec4*>(address) = value;
        break;
      }
      case FieldType::kNodeRef: {
        NodeRef value{*static_cast<std::string*>(address)};
        r.Field(name, value);
        *static_cast<std::string*>(address) = value.path;
        break;
      }
      case FieldType::kAsset: {
        AssetRef value{*static_cast<std::string*>(address)};
        r.Field(name, value, field.asset_type.c_str());
        *static_cast<std::string*>(address) = value.path;
        break;
      }
      case FieldType::kEvent:
        r.Field(name, events_[field.name]);
        break;
      case FieldType::kList: {
        auto** handle = static_cast<CScriptArray**>(address);
        asITypeInfo* array_type = ScriptRuntime::Instance().GetEngine()->GetTypeInfoById(field.type_id);
        switch (field.element_type) {
          case FieldType::kInt: {
            auto values = ReadIntArray(handle ? *handle : nullptr);
            r.Field(name, values);
            WriteArray(handle, array_type, values);
            break;
          }
          case FieldType::kFloat: {
            auto values = ReadFloatArray(handle ? *handle : nullptr);
            r.Field(name, values);
            WriteArray(handle, array_type, values);
            break;
          }
          case FieldType::kString:
          case FieldType::kNodeRef:
          case FieldType::kAsset: {
            auto values = ReadStringArray(handle ? *handle : nullptr);
            if (field.element_type == FieldType::kString) {
              r.Field(name, values);
            } else if (field.element_type == FieldType::kNodeRef) {
              std::vector<NodeRef> refs;
              for (auto& value : values) refs.push_back(NodeRef{std::move(value)});
              r.Field(name, refs);
              values.clear();
              for (auto& ref : refs) values.push_back(ref.path);
            } else {
              std::vector<AssetRef> refs;
              for (auto& value : values) refs.push_back(AssetRef{std::move(value)});
              r.Field(name, refs, field.asset_type.c_str());
              values.clear();
              for (auto& ref : refs) values.push_back(ref.path);
            }
            WriteArray(handle, array_type, values);
            break;
          }
          default:
            break;
        }
        break;
      }
    }
    if (!field.tooltip.empty()) r.Tooltip(field.tooltip.c_str());
  }
}

void ScriptBehaviour::ReflectData(Reflector& r) {
  if (!instance_) return;
  for (const std::string& name : class_->bindables) {
    r.Property(
        name.c_str(),
        [this, name]() -> mtext::Variant {
          asIScriptFunction* getter = instance_->GetObjectType()->GetMethodByName(
              ("get_" + name).c_str(), true);
          if (!getter) return {};
          ScriptArg result;
          if (!ScriptRuntime::Instance().Call(getter, instance_, {}, name, &result)) return {};
          return ArgToVariant(result);
        },
        [this, name](const mtext::Variant& value) {
          asIScriptFunction* setter = instance_->GetObjectType()->GetMethodByName(
              ("set_" + name).c_str(), true);
          if (!setter) return;
          const ScriptArg args[] = {VariantToArg(value)};
          ScriptRuntime::Instance().Call(setter, instance_, args, name);
        });
  }
}

void ScriptBehaviour::ReflectMethods(MethodReflector& m) {
  for (const ScriptCallable& callable : class_->callables) {
    const std::string name = callable.name;
    if (callable.arg.empty()) {
      m.Method(name.c_str(), [this, name] { CallMethod(name, nullptr); });
    } else if (callable.arg == "bool") {
      m.Method(name.c_str(), [this, name](bool v) {
        const ScriptArg arg = ScriptArg::Bool(v);
        CallMethod(name, &arg);
      });
    } else if (callable.arg == "int") {
      m.Method(name.c_str(), [this, name](int v) {
        const ScriptArg arg = ScriptArg::Int(v);
        CallMethod(name, &arg);
      });
    } else if (callable.arg == "float") {
      m.Method(name.c_str(), [this, name](float v) {
        const ScriptArg arg = ScriptArg::Float(v);
        CallMethod(name, &arg);
      });
    } else {
      m.Method(name.c_str(), [this, name](std::string v) {
        const ScriptArg arg = ScriptArg::String(std::move(v));
        CallMethod(name, &arg);
      });
    }
  }
}

}  // namespace Mercury
