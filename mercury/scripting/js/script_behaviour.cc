/*
 * script_behaviour.cc
 */
#include "scripting/js/script_behaviour.h"

#include <cmath>

#include "debugger/debugger.h"
#include "scripting/js/bindings.h"

namespace Mercury {

namespace {

using js::Str;

// Enters the isolate, a handle scope and the context for the duration of a C++ -> JS call.
struct Scope {
  Scope()
      : runtime(ScriptRuntime::Instance()),
        isolate(runtime.GetIsolate()),
        isolate_scope(isolate),
        handle_scope(isolate),
        context(runtime.GetContext()),
        context_scope(context) {}
  ScriptRuntime& runtime;
  v8::Isolate* isolate;
  v8::Isolate::Scope isolate_scope;
  v8::HandleScope handle_scope;
  v8::Local<v8::Context> context;
  v8::Context::Scope context_scope;
};

double NumberOf(v8::Local<v8::Value> value, double fallback = 0.0) {
  return value->IsNumber() ? value.As<v8::Number>()->Value() : fallback;
}

double Prop(Scope& s, v8::Local<v8::Object> object, const char* name) {
  v8::Local<v8::Value> value;
  if (!object->Get(s.context, Str(s.isolate, name)).ToLocal(&value)) return 0.0;
  return NumberOf(value);
}

std::string StringOf(Scope& s, v8::Local<v8::Value> value) {
  if (!value->IsString()) return {};
  v8::String::Utf8Value utf8(s.isolate, value);
  return std::string(*utf8, static_cast<size_t>(utf8.length()));
}

std::string PathOf(Scope& s, v8::Local<v8::Value> value) {
  if (!value->IsObject()) return {};
  v8::Local<v8::Value> path;
  if (!value.As<v8::Object>()->Get(s.context, Str(s.isolate, "path")).ToLocal(&path)) return {};
  return StringOf(s, path);
}

// Calls one of the factory helpers the TS runtime registered (newVector3, ...).
v8::Local<v8::Value> Make(Scope& s, const char* helper, std::initializer_list<v8::Local<v8::Value>> args) {
  v8::Local<v8::Function> fn = s.runtime.Helper(helper);
  if (fn.IsEmpty()) return v8::Undefined(s.isolate);
  std::vector<v8::Local<v8::Value>> list(args);
  v8::Local<v8::Value> result;
  if (!s.runtime.Call(fn, s.context->Global(), list, helper, &result)) {
    return v8::Undefined(s.isolate);
  }
  return result;
}

v8::Local<v8::Number> Num(Scope& s, double value) { return v8::Number::New(s.isolate, value); }

void SetProp(Scope& s, v8::Local<v8::Object> object, const std::string& name,
             v8::Local<v8::Value> value) {
  object->Set(s.context, Str(s.isolate, name), value).Check();
}

// Reads a JS array into a vector with `read`; writes back with `make` when asked.
template <typename T, typename Read>
std::vector<T> ReadArray(Scope& s, v8::Local<v8::Value> value, Read read) {
  std::vector<T> out;
  if (!value->IsArray()) return out;
  v8::Local<v8::Array> array = value.As<v8::Array>();
  for (uint32_t i = 0; i < array->Length(); i++) {
    v8::Local<v8::Value> item;
    if (array->Get(s.context, i).ToLocal(&item)) out.push_back(read(item));
  }
  return out;
}

template <typename T, typename Make>
v8::Local<v8::Array> WriteArray(Scope& s, const std::vector<T>& items, Make make) {
  v8::Local<v8::Array> array = v8::Array::New(s.isolate, static_cast<int>(items.size()));
  for (uint32_t i = 0; i < items.size(); i++) array->Set(s.context, i, make(items[i])).Check();
  return array;
}

}  // namespace

std::unique_ptr<ScriptBehaviour> ScriptBehaviour::Create(const ScriptClass& script_class,
                                                         MapScene* scene, kEntityId entity) {
  Scope s;
  std::unique_ptr<ScriptBehaviour> behaviour(new ScriptBehaviour(script_class));
  v8::Local<v8::Function> construct = s.runtime.Helper("construct");
  if (construct.IsEmpty()) {
    PDebug::error("{}: the TypeScript runtime is not loaded", script_class.name);
    return nullptr;
  }
  v8::Local<v8::Value> args[] = {script_class.constructor.Get(s.isolate)};
  v8::Local<v8::Value> result;
  if (!s.runtime.Call(construct, s.context->Global(), args, script_class.name + ".constructor",
                      &result) ||
      !result->IsObject()) {
    return nullptr;
  }
  v8::Local<v8::Object> instance = result.As<v8::Object>();
  behaviour->instance_.Reset(s.isolate, instance);
  behaviour->id_ = s.runtime.RegisterBehaviour(behaviour.get());
  behaviour->scene_serial_ = scene ? scene->GetSerial() : 0;
  SetProp(s, instance, "__id", Num(s, behaviour->id_));
  SetProp(s, instance, "__scene", Num(s, scene ? scene->GetSerial() : 0));
  SetProp(s, instance, "__entity", Num(s, js::EntityToJs(entity)));
  // Event fields know their owner, so event.invoke() can reach the saved calls.
  for (const ScriptField& field : script_class.fields) {
    if (field.type != FieldType::kEvent) continue;
    v8::Local<v8::Value> event;
    if (instance->Get(s.context, Str(s.isolate, field.name)).ToLocal(&event) && event->IsObject()) {
      SetProp(s, event.As<v8::Object>(), "__id", Num(s, behaviour->id_));
      SetProp(s, event.As<v8::Object>(), "__field", Str(s.isolate, field.name));
    }
  }
  return behaviour;
}

ScriptBehaviour::~ScriptBehaviour() {
  auto& runtime = ScriptRuntime::Instance();
  if (!runtime.IsInitialized()) return;
  if (id_) runtime.UnregisterBehaviour(id_);
  v8::Isolate::Scope isolate_scope(runtime.GetIsolate());
  instance_.Reset();
}

v8::Local<v8::Object> ScriptBehaviour::Instance() const {
  return instance_.Get(ScriptRuntime::Instance().GetIsolate());
}

Event* ScriptBehaviour::FindEvent(const std::string& field) {
  auto it = events_.find(field);
  return it == events_.end() ? nullptr : &it->second;
}

void ScriptBehaviour::Forward(ScriptCallback callback, const char* method) {
  if (!(class_->callbacks & callback)) return;
  Scope s;
  s.runtime.SetCurrentScene(scene_serial_);
  v8::Local<v8::Object> instance = Instance();
  v8::Local<v8::Value> fn;
  if (!instance->Get(s.context, Str(s.isolate, method)).ToLocal(&fn) || !fn->IsFunction()) return;
  s.runtime.Call(fn.As<v8::Function>(), instance, {}, class_->name + "." + method);
}

void ScriptBehaviour::Forward(ScriptCallback callback, const char* method, double dt) {
  if (!(class_->callbacks & callback)) return;
  Scope s;
  s.runtime.SetCurrentScene(scene_serial_);
  v8::Local<v8::Object> instance = Instance();
  v8::Local<v8::Value> fn;
  if (!instance->Get(s.context, Str(s.isolate, method)).ToLocal(&fn) || !fn->IsFunction()) return;
  v8::Local<v8::Value> args[] = {Num(s, dt)};
  s.runtime.Call(fn.As<v8::Function>(), instance, args, class_->name + "." + method);
}

void ScriptBehaviour::ForwardObject(ScriptCallback callback, const char* method,
                                    GameObject other) {
  if (!(class_->callbacks & callback)) return;
  Scope s;
  s.runtime.SetCurrentScene(scene_serial_);
  v8::Local<v8::Object> instance = Instance();
  v8::Local<v8::Value> fn;
  if (!instance->Get(s.context, Str(s.isolate, method)).ToLocal(&fn) || !fn->IsFunction()) return;
  v8::Local<v8::Function> make = s.runtime.Helper("makeGameObject");
  if (make.IsEmpty() || !other.GetScene()) return;
  v8::Local<v8::Value> make_args[] = {Num(s, other.GetScene()->GetSerial()),
                                      Num(s, js::EntityToJs(other.GetEntity()))};
  v8::Local<v8::Value> object;
  if (!s.runtime.Call(make, s.context->Global(), make_args, "makeGameObject", &object)) return;
  v8::Local<v8::Value> args[] = {object};
  s.runtime.Call(fn.As<v8::Function>(), instance, args, class_->name + "." + method);
}

void ScriptBehaviour::CallMethod(const std::string& name, v8::Local<v8::Value> argument) {
  Scope s;
  s.runtime.SetCurrentScene(scene_serial_);
  v8::Local<v8::Object> instance = Instance();
  v8::Local<v8::Value> fn;
  if (!instance->Get(s.context, Str(s.isolate, name)).ToLocal(&fn) || !fn->IsFunction()) return;
  std::vector<v8::Local<v8::Value>> args;
  if (!argument.IsEmpty()) args.push_back(argument);
  s.runtime.Call(fn.As<v8::Function>(), instance, args, class_->name + "." + name);
}

void ScriptBehaviour::Reflect(Reflector& r) { ReflectFields(r); }

void ScriptBehaviour::ReflectFields(Reflector& r) {
  Scope s;
  v8::Local<v8::Object> object = Instance();
  const bool write_back =
      r.GetMode() == Reflector::Mode::kRead || r.GetMode() == Reflector::Mode::kSet;

  for (const ScriptField& field : class_->fields) {
    const char* name = field.name.c_str();
    v8::Local<v8::Value> value;
    if (!object->Get(s.context, Str(s.isolate, field.name)).ToLocal(&value)) continue;
    switch (field.type) {
      case FieldType::kBool: {
        bool v = value->BooleanValue(s.isolate);
        const bool original = v;
        r.Field(name, v);
        if (write_back && v != original) SetProp(s, object, field.name, v8::Boolean::New(s.isolate, v));
        break;
      }
      case FieldType::kInt: {
        int v = static_cast<int>(NumberOf(value));
        const int original = v;
        r.Field(name, v);
        if (write_back && v != original) SetProp(s, object, field.name, Num(s, v));
        break;
      }
      case FieldType::kFloat: {
        float v = static_cast<float>(NumberOf(value));
        const float original = v;
        r.Field(name, v);
        if (write_back && v != original) SetProp(s, object, field.name, Num(s, v));
        break;
      }
      case FieldType::kString: {
        std::string v = StringOf(s, value);
        const std::string original = v;
        r.Field(name, v);
        if (write_back && v != original) SetProp(s, object, field.name, Str(s.isolate, v));
        break;
      }
      case FieldType::kEnum: {
        const std::string current = StringOf(s, value);
        int index = 0;
        for (size_t i = 0; i < field.enum_values.size(); i++) {
          if (field.enum_values[i] == current) index = static_cast<int>(i);
        }
        const int original = index;
        r.Enum(name, index, field.enum_values);
        if (write_back && index != original && index >= 0 &&
            index < static_cast<int>(field.enum_values.size())) {
          SetProp(s, object, field.name, Str(s.isolate, field.enum_values[index]));
        }
        break;
      }
      case FieldType::kVector2: {
        glm::vec2 v(0.0f);
        if (value->IsObject()) {
          v = glm::vec2(Prop(s, value.As<v8::Object>(), "x"), Prop(s, value.As<v8::Object>(), "y"));
        }
        const glm::vec2 original = v;
        r.Field(name, v);
        if (write_back && v != original) {
          SetProp(s, object, field.name, Make(s, "newVector2", {Num(s, v.x), Num(s, v.y)}));
        }
        break;
      }
      case FieldType::kVector3: {
        glm::vec3 v(0.0f);
        if (value->IsObject()) {
          auto o = value.As<v8::Object>();
          v = glm::vec3(Prop(s, o, "x"), Prop(s, o, "y"), Prop(s, o, "z"));
        }
        const glm::vec3 original = v;
        r.Field(name, v);
        if (write_back && v != original) {
          SetProp(s, object, field.name,
                  Make(s, "newVector3", {Num(s, v.x), Num(s, v.y), Num(s, v.z)}));
        }
        break;
      }
      case FieldType::kColor: {
        glm::vec4 v(1.0f);
        if (value->IsObject()) {
          auto o = value.As<v8::Object>();
          v = glm::vec4(Prop(s, o, "r"), Prop(s, o, "g"), Prop(s, o, "b"), Prop(s, o, "a"));
        }
        const glm::vec4 original = v;
        r.Field(name, v);
        if (write_back && v != original) {
          SetProp(s, object, field.name,
                  Make(s, "newColor", {Num(s, v.r), Num(s, v.g), Num(s, v.b), Num(s, v.a)}));
        }
        break;
      }
      case FieldType::kNodeRef: {
        NodeRef v{PathOf(s, value)};
        const std::string original = v.path;
        r.Field(name, v);
        if (write_back && v.path != original) {
          SetProp(s, object, field.name, Make(s, "newNodeRef", {Str(s.isolate, v.path)}));
        }
        break;
      }
      case FieldType::kAsset: {
        AssetRef v{PathOf(s, value)};
        const std::string original = v.path;
        r.Field(name, v, field.asset_type.c_str());
        if (write_back && v.path != original) {
          SetProp(s, object, field.name, Make(s, "newAssetRef", {Str(s.isolate, v.path)}));
        }
        break;
      }
      case FieldType::kEvent:
        r.Field(name, events_[field.name]);
        break;
      case FieldType::kList: {
        auto to_js = [&](auto&& items, auto&& make) {
          if (write_back) SetProp(s, object, field.name, WriteArray(s, items, make));
        };
        switch (field.element_type) {
          case FieldType::kInt: {
            auto v = ReadArray<int>(s, value, [](auto x) { return static_cast<int>(NumberOf(x)); });
            auto original = v;
            r.Field(name, v);
            if (v != original) to_js(v, [&](int x) -> v8::Local<v8::Value> { return Num(s, x); });
            break;
          }
          case FieldType::kFloat: {
            auto v = ReadArray<float>(s, value, [](auto x) { return static_cast<float>(NumberOf(x)); });
            auto original = v;
            r.Field(name, v);
            if (v != original) to_js(v, [&](float x) -> v8::Local<v8::Value> { return Num(s, x); });
            break;
          }
          case FieldType::kNodeRef: {
            auto v = ReadArray<NodeRef>(s, value, [&](auto x) { return NodeRef{PathOf(s, x)}; });
            auto original = v;
            r.Field(name, v);
            bool changed = v.size() != original.size();
            for (size_t i = 0; !changed && i < v.size(); i++) changed = v[i].path != original[i].path;
            if (changed) {
              to_js(v, [&](const NodeRef& x) {
                return Make(s, "newNodeRef", {Str(s.isolate, x.path)});
              });
            }
            break;
          }
          case FieldType::kAsset: {
            auto v = ReadArray<AssetRef>(s, value, [&](auto x) { return AssetRef{PathOf(s, x)}; });
            auto original = v;
            r.Field(name, v, field.asset_type.c_str());
            bool changed = v.size() != original.size();
            for (size_t i = 0; !changed && i < v.size(); i++) changed = v[i].path != original[i].path;
            if (changed) {
              to_js(v, [&](const AssetRef& x) {
                return Make(s, "newAssetRef", {Str(s.isolate, x.path)});
              });
            }
            break;
          }
          default: {
            auto v = ReadArray<std::string>(s, value, [&](auto x) { return StringOf(s, x); });
            auto original = v;
            r.Field(name, v);
            if (v != original) {
              to_js(v, [&](const std::string& x) -> v8::Local<v8::Value> { return Str(s.isolate, x); });
            }
            break;
          }
        }
        break;
      }
    }
    if (!field.tooltip.empty()) r.Tooltip(field.tooltip.c_str());
  }
}

void ScriptBehaviour::ReflectData(Reflector& r) {
  for (const std::string& name : class_->bindables) {
    r.Property(
        name.c_str(),
        [this, name]() -> mtext::Variant {
          Scope s;
          v8::Local<v8::Value> value;
          if (!Instance()->Get(s.context, Str(s.isolate, name)).ToLocal(&value)) return {};
          if (value->IsBoolean()) return mtext::Variant(value->BooleanValue(s.isolate));
          if (value->IsNumber()) {
            const double number = value.As<v8::Number>()->Value();
            if (std::floor(number) == number && std::abs(number) < 2e9) {
              return mtext::Variant(static_cast<int>(number));
            }
            return mtext::Variant(static_cast<float>(number));
          }
          if (value->IsString()) return mtext::Variant(StringOf(s, value));
          return {};
        },
        [this, name](const mtext::Variant& value) {
          Scope s;
          v8::TryCatch silent(s.isolate);
          v8::Local<v8::Value> js;
          if (auto b = value.Get<bool>()) {
            js = v8::Boolean::New(s.isolate, *b);
          } else if (auto text = value.ToString()) {
            js = Str(s.isolate, *text);
          } else if (auto number = value.ToFloat()) {
            js = Num(s, *number);
          } else {
            return;
          }
          Instance()->Set(s.context, Str(s.isolate, name), js).FromMaybe(false);
        });
  }
}

void ScriptBehaviour::ReflectMethods(MethodReflector& m) {
  for (const ScriptCallable& callable : class_->callables) {
    const std::string name = callable.name;
    const char* method = name.c_str();
    if (callable.arg.empty()) {
      m.Method(method, [this, name] { CallMethod(name, {}); });
    } else if (callable.arg == "bool") {
      m.Method(method, [this, name](bool v) {
        Scope s;
        CallMethod(name, v8::Boolean::New(s.isolate, v));
      });
    } else if (callable.arg == "int") {
      m.Method(method, [this, name](int v) {
        Scope s;
        CallMethod(name, Num(s, v));
      });
    } else if (callable.arg == "float") {
      m.Method(method, [this, name](float v) {
        Scope s;
        CallMethod(name, Num(s, v));
      });
    } else {
      m.Method(method, [this, name](std::string v) {
        Scope s;
        CallMethod(name, Str(s.isolate, v));
      });
    }
  }
}

}  // namespace Mercury
