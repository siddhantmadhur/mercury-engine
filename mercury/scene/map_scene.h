/*
 * map_scene.h
 *
 * A GameScene built from a .mmap file instead of hand-written setup code.
 * In play mode it creates physics bodies and runs Behaviour scripts. In edit
 * mode (used by the editor) it only builds what is needed to draw the map.
 */
#ifndef MERCURY_SCENE_MAP_SCENE_H
#define MERCURY_SCENE_MAP_SCENE_H

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "components/components.h"
#include "properties/model.h"
#include "properties/render_mesh.h"
#include "animation/animator_controller.h"
#include "audio/audio_system.h"
#include "input/input_actions.h"
#include "project/asset_database.h"
#include "project/map.h"
#include "project/prefab_cache.h"
#include "project/project.h"
#include "scene/scene.h"
#include "ui/ui_system.h"

namespace Mercury {

class Behaviour;
class MapScene;

/**
 * @brief Lightweight handle to an entity created from a map node
 */
class GameObject {
 public:
  GameObject() = default;
  GameObject(MapScene* scene, kEntityId entity)
      : scene_(scene), entity_(entity) {}

  bool IsValid() const;
  explicit operator bool() const { return IsValid(); }

  kEntityId GetEntity() const { return entity_; }
  MapScene* GetScene() const { return scene_; }
  std::string GetName() const;
  std::string GetPath() const;
  Transform* GetTransform() const;

  // Engine data on the entity, e.g. GetComponent<Rigidbody>()
  template <typename T>
  T* GetComponent() const;

  // A script on this object, e.g. GetBehaviour<Ball>()
  template <typename B>
  B* GetBehaviour() const;

  // Removes the object and its children at the end of the current tick or
  // frame. Their scripts get OnDestroy.
  void Destroy() const;

  GameObject GetParent() const;
  std::vector<GameObject> GetChildren() const;
  // A direct child by name, or a descendant by "A/B" path (Transform.Find).
  GameObject FindChild(std::string_view path) const;
  void SetParent(GameObject new_parent, bool keep_world_transform = true);

  // Unity's tag, layer and SetActive.
  std::string GetTag() const;
  void SetTag(std::string tag) const;
  bool CompareTag(std::string_view tag) const { return GetTag() == tag; }
  int GetLayer() const;
  void SetLayer(int layer, bool include_children = false) const;
  bool IsActiveSelf() const;
  bool IsActiveInHierarchy() const;
  void SetActive(bool active) const;

  void AddForce(glm::vec3 force, ForceMode mode = ForceMode::kForce) const;
  void AddTorque(glm::vec3 torque, ForceMode mode = ForceMode::kForce) const;
  // Rigidbody.linearVelocity
  glm::vec3 GetVelocity() const;
  void SetVelocity(glm::vec3 velocity) const;

  bool operator==(const GameObject& other) const {
    return scene_ == other.scene_ && entity_ == other.entity_;
  }

 private:
  MapScene* scene_ = nullptr;
  kEntityId entity_ = entt::null;
};

enum class CollisionEvent { kEnter, kStay, kLeave, kTriggerEnter, kTriggerStay, kTriggerExit };

/**
 * @brief Every collider on a node, unscaled; MapScene builds the node's
 * physics bodies from it
 */
struct ColliderSet {
  std::vector<ColliderShape> shapes;  // sizes before the Transform's scale
};

class MapScene : public GameScene {
 public:
  enum class Mode { kPlay, kEdit };

  // `project` may be null until SetProject is called (the editor starts
  // without one).
  MapScene(std::shared_ptr<Project> project, Mode mode);
  ~MapScene() override;

  // Switches project; clears the map and every cached asset.
  void SetProject(std::shared_ptr<Project> project);

  /**
   * @brief Replace everything in the scene with the contents of a map
   *
   * Behaviours get OnStart once the scene starts (or straight away if it is
   * already running).
   */
  void Instantiate(const Map& map);

  // Destroys every entity created from the map.
  void ClearMap();

  /**
   * @brief Replace the scene with another map (Unity's SceneManager.LoadScene)
   *
   * Safe to call from scripts and UI callbacks: the map is loaded at the
   * start of the next frame, before any script runs. Models and images the
   * old map loaded are released.
   */
  void LoadMap(std::string res_path) { pending_map_ = std::move(res_path); }

  /**
   * @brief Rebuild the entities for the given nodes after they were edited
   *
   * Used by the editor; only valid in edit mode. Entities for nodes that no
   * longer exist are removed.
   */
  void SyncFromMap(const Map& map);

  void OnStart() override;
  void OnFrame(double delta_t) override;
  void OnTick(double delta_t) override;
  void OnDestroy() override;
  void ProcessOnFrame(float alpha) override;

  void UpdateEntityTransformHierarchy(kEntityId entity);
  void UpdateInterpolatedHierarchy();
  void OnWorldTransformChanged(kEntityId entity);
  void OnLocalTransformChanged(kEntityId entity);
  bool HasParent(kEntityId entity) const;
  void SetEntityParent(kEntityId entity, kEntityId new_parent, bool keep_world_transform = true);

  Mode GetMode() const { return mode_; }
  bool IsEditMode() const { return mode_ == Mode::kEdit; }

  // By path from the map root ("Walls/Left"). A bare name that is not a
  // root child finds the first object with that name anywhere, like Unity's
  // GameObject.Find.
  GameObject Find(std::string_view path);
  GameObject FindWithTag(std::string_view tag);
  std::vector<GameObject> FindGameObjectsWithTag(std::string_view tag);
  // Every script of type B on active objects.
  template <typename B>
  std::vector<B*> FindObjectsOfType();
  GameObject GetGameObject(kEntityId entity) { return {this, entity}; }

  /**
   * @brief Create a copy of a .mprefab (Unity's Object.Instantiate)
   *
   * Scripts on it get Awake and OnEnable straight away and OnStart before
   * their next tick or frame.
   * @param parent Parent object, or none for the map root
   * @param world_space position/rotation are world space (else local)
   */
  GameObject InstantiatePrefab(std::string_view res_path, glm::vec3 position,
                               glm::quat rotation, GameObject parent = {},
                               bool world_space = true);
  // Copy the nodes of a map subtree (e.g. a loaded prefab's root).
  GameObject InstantiateNode(const MapNode& node, GameObject parent,
                             std::string_view name = "");
  // A new empty object (Unity's new GameObject(name)).
  GameObject CreateGameObject(std::string_view name, GameObject parent = {});

  /**
   * @brief Add a built-in component or script by type name, with fields read
   * from `properties`, and set up its rendering and physics right away
   * @return False if no such component type exists
   */
  bool AddComponent(GameObject object, std::string_view type,
                    const mtext::PropertyList& properties = {});
  // Typed form: `value`'s fields (via Reflect) become the properties.
  template <typename T>
  bool AddComponent(GameObject object, std::string_view type, T value) {
    mtext::PropertyList properties;
    auto writer = Reflector::Write(properties);
    value.Reflect(writer);
    return AddComponent(object, type, properties);
  }
  // Remove a built-in component (rendering/physics included).
  void RemoveComponent(GameObject object, std::string_view type);

  // Loads (once) a prefab; nullptr if it cannot be read.
  const Map* GetPrefab(std::string_view res_path) { return prefabs_.Get(res_path); }
  // Pass to Map::Load so the map's prefab instances are expanded.
  PrefabCache& GetPrefabCache() { return prefabs_; }

  void SetActive(kEntityId entity, bool active);
  void SetLayer(kEntityId entity, int layer);
  bool IsActiveInHierarchy(kEntityId entity) const;

  // Layer index for a name from the project's [layers], -1 if unknown.
  int LayerFromName(std::string_view name) const;
  std::string LayerToName(int layer) const;
  kEntityId GetEntityForNode(uint64_t uid) const;
  uint64_t GetNodeUid(kEntityId entity) const;

  PObjectManager& Objects() { return *object_manager_; }
  PhysicsEngine& Physics() { return *physics_engine_; }
  InputManager* Input() { return input_manager_; }
  AssetDatabase& Assets() { return *assets_; }
  std::shared_ptr<Project> GetProject() const { return project_; }

  void AddBehaviour(kEntityId entity, std::unique_ptr<Behaviour> behaviour,
                    std::string type_name = "");
  // Calls Awake and OnEnable on behaviours added since the last call.
  void AwakeBehaviours();
  // Called by Behaviour::SetEnabled.
  void OnBehaviourEnabledChanged(Behaviour& behaviour);
  const std::vector<Behaviour*>& GetBehaviours(kEntityId entity) const;

  // Loads (once) a texture owned by the scene; nullptr if it can't be read.
  Texture2D* GetTexture(std::string_view res_path);
  // Loads (once) a model file; nullptr if it can't be read.
  const Mesh* GetModel(std::string_view res_path);
  // Loads (once) a model with its hierarchy, materials and clips.
  std::shared_ptr<const Model> GetModelAsset(std::string_view res_path);
  // Loads (once) an image for the model renderer; null if unreadable.
  std::shared_ptr<ImageData> GetImage(std::string_view res_path, bool srgb = true);
  // Shared geometry of a built-in shape, scaled by mesh_scale.
  std::shared_ptr<const MeshGeometry> GetPrimitive(MeshShape shape,
                                                   glm::vec3 mesh_scale = glm::vec3(1.0f));
  // Triangles of a model (or one of its nodes, with children) for a
  // MeshCollider. False if the model cannot be loaded.
  bool GetModelTriangles(std::string_view res_path, std::string_view node,
                         std::vector<glm::vec3>& vertices,
                         std::vector<uint32_t>& indices);
  // (Re)builds the RenderMesh of a TextMesh.
  void BuildTextMesh(kEntityId entity);
  // A RenderMaterial from a Material asset, tinted by `color`.
  RenderMaterial MakeRenderMaterial(const MaterialAsset& material, glm::vec4 color);
  // Rebuilds the entity's physics bodies from its ColliderSet, Rigidbody and
  // CharacterController (after AddComponent, or a scale change).
  void CreatePhysicsBody(kEntityId entity);

  // See GameObject::Destroy.
  void Destroy(GameObject object);

  kEntityId GetActiveCamera() const { return player_camera_; }
  // Render the game from another camera (Camera.main changes).
  void SetCurrentCamera(GameObject camera);

  // Fixed ticks per second, from the project's [physics] tick_rate.
  int GetTicksPerSecond() const override;

  // The project's [render] settings with the first active
  // PostProcessVolume's profile.
  RenderSettings GetRenderSettings() const override;
  // Picks the current camera (the enabled one with the highest depth) and
  // lists it with RenderTexture and overlay cameras.
  std::vector<RenderCamera> GetRenderCameras(float width, float height) override;
  // Size of a RenderTexture asset (1024x1024 when it cannot be read).
  void GetRenderTextureSize(std::string_view res_path, int& width, int& height);

  // The project's input actions, or null if it has none.
  InputActionAsset* GetInputActions() { return input_actions_.get(); }
  // Sound (play mode only, and when a device is available).
  AudioSystem* GetAudio() { return audio_ && audio_->IsAvailable() ? audio_.get() : nullptr; }

  // Every UIDocument's panels (play mode only).
  UI::UISystem& GetUI() { return ui_; }
  const UI::UIDrawData* GetUIDrawData() const override;

 protected:
  void SetActiveCamera(kEntityId camera) { player_camera_ = camera; }
  // Entity of the default camera GameScene creates; the editor drives it.
  kEntityId GetDefaultCamera() const { return default_camera_; }

 private:
  struct PendingCollision {
    CollisionEvent event;
    kEntityId self;
    kEntityId other;
  };

  kEntityId CreateNodeEntity(const MapNode& node, kEntityId parent,
                             const std::string& path);
  void ApplyProjectSettings();
  // Per-frame engine systems (animation, audio) after the scripts ran.
  void UpdateFrameSystems(float dt);
  // Builds `node`'s subtree under `parent` at `path` (entities only; no
  // physics or Awake).
  kEntityId BuildSubtree(const MapNode& node, kEntityId parent, const std::string& path,
                         std::vector<kEntityId>& created);
  void FinishCreated(const std::vector<kEntityId>& created);
  std::string UniqueChildPath(kEntityId parent, std::string_view name) const;
  void UpdatePaths(kEntityId entity);
  void RefreshActive(kEntityId entity, bool parent_active);
  void ApplyEnvironmentFrom(kEntityId entity);
  void StartPendingBehaviours();
  void DestroyNodeEntity(kEntityId entity);
  void CreatePhysicsBodies();
  void ApplyEnvironment();
  // Copies the active PointLights into lighting_.
  void GatherPointLights();
  void DispatchCollisions();
  void StartBehaviours();
  void DestroyPending();
  void OnUIDocumentDestroyed(entt::registry& registry, kEntityId entity);

  std::shared_ptr<Project> project_;
  std::unique_ptr<AssetDatabase> assets_;
  Mode mode_;
  kEntityId default_camera_ = entt::null;
  kEntityId root_entity_ = entt::null;

  std::vector<kEntityId> entities_;  // in map (pre-)order
  std::unordered_map<std::string, kEntityId> by_path_;
  std::unordered_map<uint64_t, kEntityId> by_uid_;

  std::vector<std::unique_ptr<Behaviour>> behaviours_;
  std::unordered_map<kEntityId, std::vector<Behaviour*>> behaviours_by_entity_;
  size_t awoken_behaviours_ = 0;
  bool running_ = false;
  PrefabCache prefabs_;

  std::map<std::string, std::unique_ptr<Texture2D>, std::less<>> textures_;
  std::map<std::string, std::unique_ptr<Mesh>, std::less<>> models_;
  std::string pending_map_;  // LoadMap
  void LoadPendingMap();
  std::map<std::string, std::shared_ptr<const Model>, std::less<>> model_assets_;
  std::map<std::string, std::shared_ptr<ImageData>, std::less<>> images_;
  std::map<std::string, std::shared_ptr<const MeshGeometry>> primitives_;
  // Font atlas snapshots for TextMesh, by atlas id and version.
  std::map<std::pair<uint32_t, uint64_t>, std::shared_ptr<ImageData>> text_atlases_;
  std::vector<kEntityId> pending_destroy_;

  std::mutex collision_mutex_;
  std::vector<PendingCollision> collisions_;

  bool in_hierarchy_update_ = false;

  UI::UISystem ui_;
  std::unique_ptr<InputActionAsset> input_actions_;
  std::unique_ptr<AudioSystem> audio_;
  void OnAudioSourceDestroyed(entt::registry& registry, kEntityId entity);
  // Connects an Animator to the model it animates.
  void SetupAnimator(kEntityId entity);
  std::map<std::string, std::shared_ptr<const AnimatorController>, std::less<>> controllers_;
};

template <typename T>
T* GameObject::GetComponent() const {
  if (!IsValid()) return nullptr;
  return scene_->Objects().GetProperty<T>(entity_);
}

}  // namespace Mercury

#endif  // MERCURY_SCENE_MAP_SCENE_H
