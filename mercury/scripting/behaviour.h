/*
 * behaviour.h
 *
 * Base class for game scripts, the C++ version of Unity's MonoBehaviour.
 *
 *   class Ball : public Mercury::Behaviour {
 *    public:
 *     void Reflect(Mercury::Reflector& r) override { r.Field("speed", speed); }
 *     void OnStart() override;
 *     void OnTick(double dt) override;
 *     float speed = 60.0f;
 *   };
 *   MERCURY_SCRIPT(Ball);  // in ball.cc
 *
 * Add the script to a node in the editor (or in the .mmap file) with
 * [component type="Ball" node="..."]. Fields listed in Reflect show up in the
 * editor's Inspector and are saved in the map.
 */
#ifndef MERCURY_SCRIPTING_BEHAVIOUR_H
#define MERCURY_SCRIPTING_BEHAVIOUR_H

#include <memory>
#include <string>

#include "GLFW/glfw3.h"
#include "debugger/debugger.h"
#include "input/input_manager.h"
#include "scene/map_scene.h"
#include "scripting/reflection.h"
#include "scripting/type_registry.h"

namespace Mercury {

class Behaviour {
 public:
  virtual ~Behaviour() = default;

  // List the fields the editor can change. Called before OnStart.
  virtual void Reflect(Reflector&) {}

  // After every object in the map exists, before the first tick.
  virtual void OnStart() {}
  // Every rendered frame; ImGui calls are allowed here. dt in seconds.
  virtual void OnFrame(double dt) {}
  // Every fixed tick (60 per second), before physics runs. dt in seconds.
  virtual void OnTick(double dt) {}
  // Collisions are reported at the start of the next tick.
  virtual void OnCollisionEnter(GameObject other) {}
  virtual void OnCollisionLeave(GameObject other) {}
  virtual void OnDestroy() {}

  GameObject GetGameObject() const { return {scene_, entity_}; }
  kEntityId GetEntity() const { return entity_; }
  Transform& GetTransform() const;
  MapScene& GetScene() const { return *scene_; }

  template <typename T>
  T* GetComponent() const {
    return GetGameObject().GetComponent<T>();
  }
  template <typename B>
  B* GetBehaviour() const {
    return GetGameObject().GetBehaviour<B>();
  }

  // Find another object by its path from the map root, e.g. "Walls/Left".
  GameObject Find(std::string_view path) const { return scene_->Find(path); }
  GameObject Get(const NodeRef& ref) const;
  // Removes an object (and its children) at the end of this tick or frame.
  void Destroy(GameObject object) const { scene_->Destroy(object); }

  bool IsKeyPressed(Key key) const;
  bool IsKeyJustPressed(Key key) const;
  PhysicsEngine& Physics() const { return scene_->Physics(); }
  AssetDatabase& Assets() const { return scene_->Assets(); }
  void Quit() const { scene_->QuitScene(); }

 private:
  friend class MapScene;
  MapScene* scene_ = nullptr;
  kEntityId entity_ = entt::null;
};

template <typename B>
B* GameObject::GetBehaviour() const {
  if (!IsValid()) return nullptr;
  for (Behaviour* behaviour : scene_->GetBehaviours(entity_)) {
    if (auto typed = dynamic_cast<B*>(behaviour)) return typed;
  }
  return nullptr;
}

/**
 * @brief Register a Behaviour subclass so maps can use it by name
 *
 * Use through MERCURY_SCRIPT(Type) in the script's .cc file.
 */
template <typename T>
bool RegisterScript(const char* name) {
  static_assert(std::is_base_of_v<Behaviour, T>,
                "MERCURY_SCRIPT types must derive from Mercury::Behaviour");
  ComponentInfo info;
  info.name = name;
  info.category = "Scripts";
  info.is_script = true;
  {
    T instance{};
    auto reflector = Reflector::Describe();
    instance.Reflect(reflector);
    info.fields = reflector.GetFields();
  }
  info.attach = [name = std::string(name)](
                    MapScene& scene, kEntityId entity,
                    const mtext::PropertyList& properties) {
    auto behaviour = std::make_unique<T>();
    auto reflector = Reflector::Read(properties);
    behaviour->Reflect(reflector);
    LogReflectionWarnings(reflector, scene.GetGameObject(entity).GetPath() +
                                         "." + name);
    scene.AddBehaviour(entity, std::move(behaviour));
  };
  TypeRegistry::Instance().RegisterComponent(std::move(info));
  return true;
}

}  // namespace Mercury

#define MERCURY_SCRIPT(Type)                                     \
  [[maybe_unused]] static const bool mercury_script_##Type##_ = \
      ::Mercury::RegisterScript<Type>(#Type)

#endif  // MERCURY_SCRIPTING_BEHAVIOUR_H
