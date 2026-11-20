// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * behaviour.h
 *
 * The engine's internal base class for scripts. MapScene runs Behaviours: it
 * calls Awake, OnStart, OnFrame, OnTick and the collision callbacks, and a
 * Behaviour's Reflect() describes the fields the Inspector shows and the map
 * saves.
 *
 * Game scripts are not written against this class. They are AngelScript classes
 * (mercury/scripting/as); each one is run by a ScriptBehaviour, which forwards
 * every callback to the script object. Tests of the engine may derive from
 * Behaviour directly (see tests/test_scripts.h).
 */
#ifndef MERCURY_SCRIPTING_BEHAVIOUR_H
#define MERCURY_SCRIPTING_BEHAVIOUR_H

#include <memory>
#include <string>
#include <type_traits>

#include "GLFW/glfw3.h"
#include "debugger/debugger.h"
#include "input/input_actions.h"
#include "input/input_manager.h"
#include "math/mathf.h"
#include "math/time.h"
#include "scene/map_scene.h"
#include "scripting/event.h"
#include "scripting/reflection.h"
#include "scripting/type_registry.h"
#include "ui/data_source.h"
#include "ui/ui_document.h"

namespace Mercury {

/**
 * @brief The engine's base class for gameplay behaviour
 *
 * A Behaviour is a component that receives lifecycle callbacks from
 * @ref MapScene. Game code is normally written in AngelScript, not C++: each
 * `[script] class Name : Behaviour` is wrapped by a `ScriptBehaviour` that
 * forwards every callback to the script object. Derive from this class in C++
 * for engine tests or native gameplay components (see `tests/test_scripts.h`).
 *
 * The callback order is:
 * 1. `Awake()` once, as soon as the object exists.
 * 2. `OnEnable()` when the script becomes enabled on an active object.
 * 3. `OnStart()` before the first tick/frame.
 * 4. `OnFrame(dt)` every rendered frame, then `OnLateFrame(dt)`.
 * 5. `OnTick(dt)` at the project's fixed rate, before physics.
 * 6. Collision and trigger callbacks at the start of a tick.
 * 7. `OnDisable()` / `OnDestroy()` when it stops or is removed.
 *
 * @par For script authors
 * The same callbacks exist in AngelScript in camelCase (`onStart`,
 * `onFrame`, `onTick`, ...). See @ref scripting_guide for a complete tutorial.
 *
 * @par Example
 * @code{.cpp}
 * class Spinner : public Mercury::Behaviour {
 *  public:
 *   void OnFrame(double dt) override {
 *     GetTransform().ChangeRotate({0.0f, float(90.0 * dt), 0.0f});
 *   }
 * };
 * @endcode
 *
 * @see Mercury::GameObject, Mercury::MapScene, Mercury::Reflector
 */
class Behaviour {
 public:
  virtual ~Behaviour() = default;

  // List the fields the editor can change. Called before OnStart.
  virtual void Reflect(Reflector&) {}
  // Extra values UI bindings may read or write, without saving them or
  // showing them in the Inspector (Unity's [CreateProperty]):
  //   r.Field("score", score_);
  //   r.Property("time", [this] { return mtext::Variant(FormatTime()); });
  virtual void ReflectData(Reflector&) {}
  // Methods an Event saved in the map may call (see scripting/event.h).
  virtual void ReflectMethods(MethodReflector&) {}

  // As soon as the object exists (every object of a loaded map exists by
  // then), even if this script is disabled.
  virtual void Awake() {}
  // Whenever the script becomes enabled on an active object, and right after
  // Awake if it starts that way.
  virtual void OnEnable() {}
  virtual void OnDisable() {}
  // Before the first tick or frame the script is enabled for.
  virtual void OnStart() {}
  // Every rendered frame. dt in seconds. Player-facing UI belongs in a
  // UIDocument.
  virtual void OnFrame(double dt) {}
  // Every frame after every script's OnFrame (Unity's LateUpdate).
  virtual void OnLateFrame(double dt) {}
  // Every fixed tick (the project's tick rate, 60 per second by default),
  // before physics runs. dt in seconds.
  virtual void OnTick(double dt) {}
  // Collisions and triggers are reported at the start of the next tick.
  virtual void OnCollisionEnter(GameObject other) {}
  virtual void OnCollisionStay(GameObject other) {}
  virtual void OnCollisionLeave(GameObject other) {}
  virtual void OnTriggerEnter(GameObject other) {}
  virtual void OnTriggerStay(GameObject other) {}
  virtual void OnTriggerExit(GameObject other) {}
  virtual void OnDestroy() {}

  GameObject GetGameObject() const { return {scene_, entity_}; }
  kEntityId GetEntity() const { return entity_; }
  Transform& GetTransform() const;
  MapScene& GetScene() const { return *scene_; }
  // The name this script was registered under.
  const std::string& GetTypeName() const { return type_name_; }

  // Behaviour.enabled: disabled scripts get no OnStart/OnFrame/OnTick.
  bool IsEnabled() const { return enabled_; }
  void SetEnabled(bool enabled);
  bool IsActiveAndEnabled() const;

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
  GameObject FindWithTag(std::string_view tag) const {
    return scene_->FindWithTag(tag);
  }
  GameObject Get(const NodeRef& ref) const;
  // Removes an object (and its children) at the end of this tick or frame.
  void Destroy(GameObject object) const { scene_->Destroy(object); }
  // Copies a .mprefab into the scene (Unity's Object.Instantiate).
  GameObject Instantiate(const AssetRef& prefab, glm::vec3 position,
                         glm::quat rotation, GameObject parent = {}) const {
    return scene_->InstantiatePrefab(prefab.path, position, rotation, parent);
  }

  GameObject GetParent() const { return GetGameObject().GetParent(); }
  std::vector<GameObject> GetChildren() const {
    return GetGameObject().GetChildren();
  }

  void AddForce(glm::vec3 force, ForceMode mode = ForceMode::kForce) const {
    GetGameObject().AddForce(force, mode);
  }
  void AddTorque(glm::vec3 torque, ForceMode mode = ForceMode::kForce) const {
    GetGameObject().AddTorque(torque, mode);
  }

  bool IsKeyPressed(Key key) const;
  bool IsKeyJustPressed(Key key) const;
  // The project's input actions (Unity's Input System), e.g.
  // GetInputActions()->Find("Player/Jump").
  InputActionAsset* GetInputActions() const;

  // The UIDocument on this object, if it has one.
  UIDocument* GetDocument() const { return GetComponent<UIDocument>(); }
  // True while the pointer is over in-game UI (so clicks are meant for it).
  bool IsPointerOverUI() const { return scene_->GetUI().IsPointerOverUI(); }
  // This script as a data source for UI bindings: fields from Reflect()
  // and ReflectData() become binding paths.
  //   GetDocument()->rootVisualElement()->SetDataSource(AsDataSource());
  std::shared_ptr<UI::IDataSource> AsDataSource() {
    return std::make_shared<UI::ReflectedDataSource>(
        [this](Reflector& r) {
          Reflect(r);
          ReflectData(r);
        },
        lifetime_);
  }
  PhysicsEngine& Physics() const { return scene_->Physics(); }
  AssetDatabase& Assets() const { return scene_->Assets(); }
  void Quit() const { scene_->QuitScene(); }
  // Replaces the scene with another map at the start of the next frame,
  // e.g. LoadMap("res://maps/level2.mmap").
  void LoadMap(std::string res_path) const {
    scene_->LoadMap(std::move(res_path));
  }

 private:
  friend class MapScene;
  MapScene* scene_ = nullptr;
  kEntityId entity_ = entt::null;
  std::string type_name_;
  bool enabled_ = true;
  bool awoken_ = false;
  bool started_ = false;
  // OnEnable has run without a matching OnDisable.
  bool enable_called_ = false;
  // Expires with the script so data sources made by AsDataSource() stop
  // reading it.
  std::shared_ptr<char> lifetime_ = std::make_shared<char>(0);
};

template <typename B>
B* GameObject::GetBehaviour() const {
  if (!IsValid()) return nullptr;
  for (Behaviour* behaviour : scene_->GetBehaviours(entity_)) {
    if (auto typed = dynamic_cast<B*>(behaviour)) return typed;
  }
  return nullptr;
}

template <typename B>
std::vector<B*> MapScene::FindObjectsOfType() {
  std::vector<B*> result;
  for (kEntityId entity : entities_) {
    if (!IsActiveInHierarchy(entity)) continue;
    for (Behaviour* behaviour : GetBehaviours(entity)) {
      if (auto typed = dynamic_cast<B*>(behaviour)) result.push_back(typed);
    }
  }
  return result;
}

/**
 * @brief Register a C++ Behaviour subclass so maps can use it by name
 *
 * Use through MERCURY_SCRIPT(Type) in the script's .cc file. The type must
 * derive from @ref Behaviour and be default-constructible. Fields listed in
 * `Reflect` become Inspector fields and are saved in the map; methods listed
 * in `ReflectMethods` can be called by Events.
 *
 * Registration is normally a static initializer, so it happens before `main`
 * and before any map is loaded. Native scripts live as long as the executable:
 * @ref ScriptRuntime::Shutdown only removes AngelScript classes.
 *
 * @par Example
 * @code{.cpp}
 * class Spinner : public Mercury::Behaviour {
 *  public:
 *   void Reflect(Mercury::Reflector& r) override {
 *     r.Field("speed", speed).Tooltip("Degrees per second");
 *   }
 *   void OnFrame(double dt) override {
 *     GetTransform().ChangeRotate({0.0f, float(speed * dt), 0.0f});
 *   }
 *   float speed = 90.0f;
 * };
 *
 * MERCURY_SCRIPT(Spinner);
 * @endcode
 *
 * The map uses the registered name:
 * @code{.ini}
 * [component type="Spinner" node="Cube"]
 * speed = 45.0
 * @endcode
 *
 * @return true, so the macro can initialize a variable with it.
 * @see RegisterDataAsset, Mercury::TypeRegistry
 */
template <typename T>
bool RegisterScript(const char* name) {
  static_assert(std::is_base_of_v<Behaviour, T>,
                "MERCURY_SCRIPT types must derive from Mercury::Behaviour");
  ComponentInfo info;
  info.name = name;
  info.category = "Scripts";
  info.is_script = true;
  info.native_script = true;
  info.from_game = true;
  {
    T instance{};
    auto reflector = Reflector::Describe();
    instance.Reflect(reflector);
    info.fields = reflector.GetFields();
    MethodReflector methods;
    instance.ReflectMethods(methods);
    for (const auto& method : methods.GetMethods()) {
      info.methods.push_back({method.name, ArgTypeName(method.arg)});
    }
  }
  info.attach = [name = std::string(name)](
                    MapScene& scene, kEntityId entity,
                    const mtext::PropertyList& properties) {
    auto behaviour = std::make_unique<T>();
    auto reflector = Reflector::Read(properties);
    behaviour->Reflect(reflector);
    LogReflectionWarnings(reflector,
                          scene.GetGameObject(entity).GetPath() + "." + name);
    scene.AddBehaviour(entity, std::move(behaviour), name);
  };
  TypeRegistry::Instance().RegisterComponent(std::move(info));
  return true;
}

/**
 * @brief Register a reflected C++ struct as a `.masset` type
 *
 * This is the C++ equivalent of AngelScript's `[dataAsset]` and Unity's
 * ScriptableObject. Load it with `Assets().LoadAs<T>(path)`.
 *
 * @par Example
 * @code{.cpp}
 * struct WaveSettings {
 *   float amplitude = 1.0f;
 *   float frequency = 0.5f;
 *   void Reflect(Mercury::Reflector& r) {
 *     r.Field("amplitude", amplitude);
 *     r.Field("frequency", frequency);
 *   }
 * };
 *
 * MERCURY_DATA_ASSET(WaveSettings);
 * @endcode
 *
 * @see RegisterScript, Mercury::AssetDatabase
 */
template <typename T>
bool RegisterDataAsset(const char* name, const char* description = "") {
  AssetTypeInfo info;
  info.name = name;
  info.description = description;
  info.fields = DescribeFields<T>();
  info.from_game = true;
  info.native_asset = true;
  TypeRegistry::Instance().RegisterAssetType(std::move(info));
  return true;
}

}  // namespace Mercury

/**
 * @brief Register a C++ Behaviour so maps can use it by name
 * @param Type A class deriving from Mercury::Behaviour
 *
 * Put this in the script's .cc file. It runs at static initialization time.
 */
#define MERCURY_SCRIPT(Type)                                    \
  [[maybe_unused]] static const bool mercury_script_##Type##_ = \
      ::Mercury::RegisterScript<Type>(#Type)

/**
 * @brief Register a C++ reflected struct as a `.masset` asset type
 * @param Type A struct with a `void Reflect(Mercury::Reflector&)` method
 */
#define MERCURY_DATA_ASSET(Type)                                    \
  [[maybe_unused]] static const bool mercury_data_asset_##Type##_ = \
      ::Mercury::RegisterDataAsset<Type>(#Type)

#endif  // MERCURY_SCRIPTING_BEHAVIOUR_H
