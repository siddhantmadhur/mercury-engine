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
 *
 * Unity names for the callbacks: Awake = Awake, Start = OnStart,
 * Update = OnFrame, LateUpdate = OnLateFrame, FixedUpdate = OnTick.
 */
#ifndef MERCURY_SCRIPTING_BEHAVIOUR_H
#define MERCURY_SCRIPTING_BEHAVIOUR_H

#include <memory>
#include <string>

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
  // Every rendered frame. dt in seconds. ImGui calls are allowed here for
  // developer/debug windows; player-facing UI belongs in a UIDocument.
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
  // The name this script was registered under (MERCURY_SCRIPT).
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
  GameObject FindWithTag(std::string_view tag) const { return scene_->FindWithTag(tag); }
  GameObject Get(const NodeRef& ref) const;
  // Removes an object (and its children) at the end of this tick or frame.
  void Destroy(GameObject object) const { scene_->Destroy(object); }
  // Copies a .mprefab into the scene (Unity's Object.Instantiate).
  GameObject Instantiate(const AssetRef& prefab, glm::vec3 position,
                         glm::quat rotation, GameObject parent = {}) const {
    return scene_->InstantiatePrefab(prefab.path, position, rotation, parent);
  }

  GameObject GetParent() const { return GetGameObject().GetParent(); }
  std::vector<GameObject> GetChildren() const { return GetGameObject().GetChildren(); }

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
 * @brief Register a reflected struct as a .masset type, the C++ version of
 * a ScriptableObject. Load it with Assets().LoadAs<T>(path).
 */
template <typename T>
bool RegisterDataAsset(const char* name, const char* description = "") {
  TypeRegistry::Instance().RegisterAssetType(
      AssetTypeInfo{name, description, DescribeFields<T>(), true});
  return true;
}

}  // namespace Mercury

#define MERCURY_SCRIPT(Type)                                    \
  [[maybe_unused]] static const bool mercury_script_##Type##_ = \
      ::Mercury::RegisterScript<Type>(#Type)

#define MERCURY_DATA_ASSET(Type)                                    \
  [[maybe_unused]] static const bool mercury_data_asset_##Type##_ = \
      ::Mercury::RegisterDataAsset<Type>(#Type)

#endif  // MERCURY_SCRIPTING_BEHAVIOUR_H
