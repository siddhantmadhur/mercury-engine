/*
 * bindings.h
 *
 * Helpers for writing native functions that scripts call through `__native`. Each helper checks
 * its input and throws a JS error instead of returning a bad value, so a native stays a few
 * lines long and a script can never reach freed memory:
 *
 *   void GetName(const v8::FunctionCallbackInfo<v8::Value>& info) {
 *     auto object = ArgGameObject(info, 0);
 *     if (!object) return;  // already threw "GameObject was destroyed"
 *     info.GetReturnValue().Set(Str(info.GetIsolate(), object->GetName()));
 *   }
 */
#ifndef MERCURY_SCRIPTING_JS_BINDINGS_H
#define MERCURY_SCRIPTING_JS_BINDINGS_H

#include <v8.h>

#include <optional>
#include <string>
#include <string_view>

#include "scene/map_scene.h"
#include "scripting/js/script_runtime.h"

namespace Mercury::js {

using Info = v8::FunctionCallbackInfo<v8::Value>;

inline v8::Local<v8::String> Str(v8::Isolate* isolate, std::string_view text) {
  return v8::String::NewFromUtf8(isolate, text.data(), v8::NewStringType::kNormal,
                                 static_cast<int>(text.size()))
      .ToLocalChecked();
}

// Throws a JS Error with `message`.
inline void Throw(v8::Isolate* isolate, std::string_view message) {
  isolate->ThrowException(v8::Exception::Error(Str(isolate, message)));
}

inline void ThrowType(v8::Isolate* isolate, std::string_view message) {
  isolate->ThrowException(v8::Exception::TypeError(Str(isolate, message)));
}

// Each Arg* returns false after throwing a TypeError when argument `i` is missing or has the
// wrong type.
inline bool ArgNumber(const Info& info, int i, double* out) {
  if (i >= info.Length() || !info[i]->IsNumber()) {
    ThrowType(info.GetIsolate(), "argument " + std::to_string(i) + " must be a number");
    return false;
  }
  *out = info[i].As<v8::Number>()->Value();
  return true;
}

inline bool ArgInt(const Info& info, int i, int* out) {
  double value;
  if (!ArgNumber(info, i, &value)) return false;
  *out = static_cast<int>(value);
  return true;
}

inline bool ArgBool(const Info& info, int i, bool* out) {
  if (i >= info.Length() || !info[i]->IsBoolean()) {
    ThrowType(info.GetIsolate(), "argument " + std::to_string(i) + " must be a boolean");
    return false;
  }
  *out = info[i].As<v8::Boolean>()->Value();
  return true;
}

inline bool ArgString(const Info& info, int i, std::string* out) {
  if (i >= info.Length() || !info[i]->IsString()) {
    ThrowType(info.GetIsolate(), "argument " + std::to_string(i) + " must be a string");
    return false;
  }
  v8::String::Utf8Value utf8(info.GetIsolate(), info[i]);
  out->assign(*utf8, static_cast<size_t>(utf8.length()));
  return true;
}

// Looks up the scene whose serial is argument `i`. Throws if it no longer exists.
inline MapScene* ArgScene(const Info& info, int i) {
  double serial;
  if (!ArgNumber(info, i, &serial)) return nullptr;
  MapScene* scene = ScriptRuntime::Instance().FindScene(static_cast<uint32_t>(serial));
  if (!scene) Throw(info.GetIsolate(), "The scene was unloaded");
  return scene;
}

// Resolves the (sceneSerial, entity) pair at arguments i and i+1. Throws "GameObject was
// destroyed" if the object no longer exists.
inline std::optional<GameObject> ArgGameObject(const Info& info, int i) {
  MapScene* scene = ArgScene(info, i);
  if (!scene) return std::nullopt;
  double entity;
  if (!ArgNumber(info, i + 1, &entity)) return std::nullopt;
  GameObject object(scene, static_cast<kEntityId>(static_cast<uint32_t>(entity)));
  if (!object.IsValid()) {
    Throw(info.GetIsolate(), "GameObject was destroyed");
    return std::nullopt;
  }
  return object;
}

inline void SetFunction(v8::Local<v8::Context> context, v8::Local<v8::Object> target,
                        const char* name, v8::FunctionCallback callback) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  target->Set(context, Str(isolate, name),
              v8::Function::New(context, callback).ToLocalChecked())
      .Check();
}

// The Float64Array(16) shared with JS (`__native.scratch`).
inline double* Scratch() { return ScriptRuntime::Instance().Scratch(); }

// An entity id as a JS number, or -1 for none.
inline double EntityToJs(kEntityId entity) {
  return entity == entt::null ? -1.0 : static_cast<double>(static_cast<uint32_t>(entity));
}

}  // namespace Mercury::js

#endif  // MERCURY_SCRIPTING_JS_BINDINGS_H
