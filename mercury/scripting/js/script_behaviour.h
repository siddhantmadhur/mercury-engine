/*
 * script_behaviour.h
 *
 * The C++ Behaviour that stands for one TypeScript script instance. MapScene runs it like any
 * other Behaviour; every callback is forwarded to the JS object, and the fields go through
 * Reflect, so the Inspector, map loading, UI bindings and Events all use the existing code.
 */
#ifndef MERCURY_SCRIPTING_JS_SCRIPT_BEHAVIOUR_H
#define MERCURY_SCRIPTING_JS_SCRIPT_BEHAVIOUR_H

#include <v8.h>

#include <map>
#include <memory>
#include <string>

#include "scripting/behaviour.h"
#include "scripting/js/script_runtime.h"

namespace Mercury {

class ScriptBehaviour : public Behaviour {
 public:
  /**
   * @brief Constructs the JS instance (field initializers run here) and sets its __id, __scene and
   * __entity. Returns null if the constructor threw.
   */
  static std::unique_ptr<ScriptBehaviour> Create(const ScriptClass& script_class, MapScene* scene,
                                                 kEntityId entity);
  ~ScriptBehaviour() override;

  void Reflect(Reflector& r) override;
  void ReflectData(Reflector& r) override;
  void ReflectMethods(MethodReflector& m) override;

  void Awake() override { Forward(kCbAwake, "awake"); }
  void OnEnable() override { Forward(kCbOnEnable, "onEnable"); }
  void OnDisable() override { Forward(kCbOnDisable, "onDisable"); }
  void OnStart() override { Forward(kCbOnStart, "onStart"); }
  void OnFrame(double dt) override { Forward(kCbOnFrame, "onFrame", dt); }
  void OnLateFrame(double dt) override { Forward(kCbOnLateFrame, "onLateFrame", dt); }
  void OnTick(double dt) override { Forward(kCbOnTick, "onTick", dt); }
  void OnCollisionEnter(GameObject other) override {
    ForwardObject(kCbOnCollisionEnter, "onCollisionEnter", other);
  }
  void OnCollisionStay(GameObject other) override {
    ForwardObject(kCbOnCollisionStay, "onCollisionStay", other);
  }
  void OnCollisionLeave(GameObject other) override {
    ForwardObject(kCbOnCollisionLeave, "onCollisionLeave", other);
  }
  void OnTriggerEnter(GameObject other) override {
    ForwardObject(kCbOnTriggerEnter, "onTriggerEnter", other);
  }
  void OnTriggerStay(GameObject other) override {
    ForwardObject(kCbOnTriggerStay, "onTriggerStay", other);
  }
  void OnTriggerExit(GameObject other) override {
    ForwardObject(kCbOnTriggerExit, "onTriggerExit", other);
  }
  void OnDestroy() override { Forward(kCbOnDestroy, "onDestroy"); }

  int GetId() const { return id_; }
  const ScriptClass& GetClass() const { return *class_; }
  // The JS object; valid only inside a v8 HandleScope.
  v8::Local<v8::Object> Instance() const;
  // Event field `field` of this script, or null.
  Event* FindEvent(const std::string& field);

 private:
  explicit ScriptBehaviour(const ScriptClass& script_class) : class_(&script_class) {}
  void Forward(ScriptCallback callback, const char* method);
  void Forward(ScriptCallback callback, const char* method, double dt);
  void ForwardObject(ScriptCallback callback, const char* method, GameObject other);
  // Calls a @callable method with an optional argument.
  void CallMethod(const std::string& name, v8::Local<v8::Value> argument);
  void ReflectFields(Reflector& r);

  const ScriptClass* class_;
  int id_ = 0;
  uint32_t scene_serial_ = 0;
  v8::Global<v8::Object> instance_;
  std::map<std::string, Event> events_;
};

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_JS_SCRIPT_BEHAVIOUR_H
