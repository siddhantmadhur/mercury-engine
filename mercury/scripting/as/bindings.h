/*
 * bindings.h
 *
 * Helpers shared by the AngelScript native bindings: value-type registration,
 * argument validation and the small wrappers that turn an invalid handle into
 * a script exception instead of a crash.
 */
#ifndef MERCURY_SCRIPTING_AS_BINDINGS_H
#define MERCURY_SCRIPTING_AS_BINDINGS_H

#include <angelscript.h>

#include <cstdint>
#include <string>

#include "scene/map_scene.h"
#include "scripting/as/script_runtime.h"

namespace Mercury::as {

template <typename T>
void ConstructDefault(T* self) {
  new (self) T();
}

template <typename T>
void ConstructCopy(T* self, const T& other) {
  new (self) T(other);
}

template <typename T>
void Destroy(T* self) {
  self->~T();
}

template <typename T>
T& Assign(T* self, const T& other) {
  *self = other;
  return *self;
}

// Registers a trivially copyable C++ value as a POD AngelScript value type.
// POD types are returned in registers; a type with a registered constructor or
// destructor would be returned through a hidden pointer instead (the engine's
// COMPLEX_RETURN_MASK), which the C++ ABI does not match for small structs.
template <typename T>
void RegisterValueType(asIScriptEngine* engine, const char* name, asQWORD extra_flags = 0) {
  engine->RegisterObjectType(name, sizeof(T),
                             asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS | extra_flags);
  const std::string decl(name);
  engine->RegisterObjectBehaviour(name, asBEHAVE_CONSTRUCT, "void f()",
                                  asFUNCTION(ConstructDefault<T>), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectBehaviour(name, asBEHAVE_CONSTRUCT,
                                  ("void f(const " + decl + " &in)").c_str(),
                                  asFUNCTION(ConstructCopy<T>), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectBehaviour(name, asBEHAVE_DESTRUCT, "void f()",
                                  asFUNCTION(Destroy<T>), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(name, (decl + " &opAssign(const " + decl + " &in)").c_str(),
                               asFUNCTION(Assign<T>), asCALL_CDECL_OBJFIRST);
}

// A value type with a non-trivial constructor/destructor (e.g. a std::string
// member). The engine returns it through hidden memory, matching the C++ ABI
// for objects larger than two registers.
template <typename T>
void RegisterComplexValueType(asIScriptEngine* engine, const char* name) {
  engine->RegisterObjectType(name, sizeof(T), asOBJ_VALUE | asOBJ_APP_CLASS_CDAK);
  const std::string decl(name);
  engine->RegisterObjectBehaviour(name, asBEHAVE_CONSTRUCT, "void f()",
                                  asFUNCTION(ConstructDefault<T>), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectBehaviour(name, asBEHAVE_CONSTRUCT,
                                  ("void f(const " + decl + " &in)").c_str(),
                                  asFUNCTION(ConstructCopy<T>), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectBehaviour(name, asBEHAVE_DESTRUCT, "void f()",
                                  asFUNCTION(Destroy<T>), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(name, (decl + " &opAssign(const " + decl + " &in)").c_str(),
                               asFUNCTION(Assign<T>), asCALL_CDECL_OBJFIRST);
}

inline ScriptRuntime& Runtime() { return ScriptRuntime::Instance(); }

inline void Throw(const char* message) { ScriptRuntime::Throw(message); }

// Validates a GameObject and returns its scene, or throws and returns null.
MapScene* SceneArg(GameObject object);

// Ensures the native call has an active context (always true in script calls).
inline asIScriptContext* Ctx() { return asGetActiveContext(); }

// Native API registration, split by area.
void InstallCore(asIScriptEngine* engine);
void InstallComponents(asIScriptEngine* engine);
void InstallUi(asIScriptEngine* engine);
void InstallAll(asIScriptEngine* engine);

}  // namespace Mercury::as

#endif  // MERCURY_SCRIPTING_AS_BINDINGS_H
