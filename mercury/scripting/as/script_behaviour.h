/*
 * script_behaviour.h
 *
 * The C++ Behaviour that stands for one AngelScript script instance. MapScene
 * runs it like any other Behaviour; every callback is forwarded to the script
 * object, and the fields go through Reflect, so the Inspector, map loading, UI
 * bindings and Events all use the existing code.
 */
#ifndef MERCURY_SCRIPTING_AS_SCRIPT_BEHAVIOUR_H
#define MERCURY_SCRIPTING_AS_SCRIPT_BEHAVIOUR_H

#include <angelscript.h>

#include <map>
#include <memory>
#include <string>

#include "scripting/as/script_event.h"
#include "scripting/as/script_runtime.h"
#include "scripting/behaviour.h"

namespace Mercury {

class ScriptBehaviour : public Behaviour {
 public:
  /**
   * @brief Constructs the script instance (field initializers run here)
   * @return null if the constructor threw
   */
  static std::unique_ptr<ScriptBehaviour> Create(
      const ScriptClass& script_class, MapScene* scene, kEntityId entity);
  ~ScriptBehaviour() override;

  void Reflect(Reflector& r) override;
  void ReflectData(Reflector& r) override;
  void ReflectMethods(MethodReflector& m) override;

  void Awake() override { Forward(kCbAwake, "awake"); }
  void OnEnable() override { Forward(kCbOnEnable, "onEnable"); }
  void OnDisable() override { Forward(kCbOnDisable, "onDisable"); }
  void OnStart() override { Forward(kCbOnStart, "onStart"); }
  void OnFrame(double dt) override { Forward(kCbOnFrame, "onFrame", dt); }
  void OnLateFrame(double dt) override {
    Forward(kCbOnLateFrame, "onLateFrame", dt);
  }
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
  // The AngelScript object; valid until the behaviour is destroyed.
  asIScriptObject* Instance() const { return instance_; }
  // Event field `field` of this script, or null.
  Event* FindEvent(const std::string& field);

 private:
  explicit ScriptBehaviour(const ScriptClass& script_class)
      : class_(&script_class) {}
  void Forward(ScriptCallback callback, const char* method);
  void Forward(ScriptCallback callback, const char* method, double dt);
  void ForwardObject(ScriptCallback callback, const char* method,
                     GameObject other);
  void CallMethod(const std::string& name, const ScriptArg* argument);
  void ReflectFields(Reflector& r);

  const ScriptClass* class_;
  int id_ = 0;
  uint32_t scene_serial_ = 0;
  asIScriptObject* instance_ = nullptr;
  std::map<std::string, Event> events_;
};

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_AS_SCRIPT_BEHAVIOUR_H
