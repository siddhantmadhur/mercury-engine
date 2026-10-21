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
#include "project/asset_database.h"
#include "project/map.h"
#include "project/project.h"
#include "scene/scene.h"

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

  bool operator==(const GameObject& other) const {
    return scene_ == other.scene_ && entity_ == other.entity_;
  }

 private:
  MapScene* scene_ = nullptr;
  kEntityId entity_ = entt::null;
};

enum class CollisionEvent { kEnter, kLeave };

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

  Mode GetMode() const { return mode_; }
  bool IsEditMode() const { return mode_ == Mode::kEdit; }

  GameObject Find(std::string_view path);
  GameObject GetGameObject(kEntityId entity) { return {this, entity}; }
  kEntityId GetEntityForNode(uint64_t uid) const;
  uint64_t GetNodeUid(kEntityId entity) const;

  PObjectManager& Objects() { return *object_manager_; }
  PhysicsEngine& Physics() { return *physics_engine_; }
  InputManager* Input() { return input_manager_; }
  AssetDatabase& Assets() { return *assets_; }
  std::shared_ptr<Project> GetProject() const { return project_; }

  void AddBehaviour(kEntityId entity, std::unique_ptr<Behaviour> behaviour);
  const std::vector<Behaviour*>& GetBehaviours(kEntityId entity) const;

  // Loads (once) a texture owned by the scene; nullptr if it can't be read.
  Texture2D* GetTexture(std::string_view res_path);

  kEntityId GetActiveCamera() const { return player_camera_; }

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
                             glm::vec3 parent_position);
  void DestroyNodeEntity(kEntityId entity);
  void CreatePhysicsBodies();
  void DispatchCollisions();
  void StartBehaviours();

  std::shared_ptr<Project> project_;
  std::unique_ptr<AssetDatabase> assets_;
  Mode mode_;
  kEntityId default_camera_ = entt::null;

  std::vector<kEntityId> entities_;  // in map (pre-)order
  std::unordered_map<std::string, kEntityId> by_path_;
  std::unordered_map<uint64_t, kEntityId> by_uid_;

  std::vector<std::unique_ptr<Behaviour>> behaviours_;
  std::unordered_map<kEntityId, std::vector<Behaviour*>> behaviours_by_entity_;
  size_t started_behaviours_ = 0;
  bool running_ = false;

  std::map<std::string, std::unique_ptr<Texture2D>, std::less<>> textures_;

  std::mutex collision_mutex_;
  std::vector<PendingCollision> collisions_;
};

template <typename T>
T* GameObject::GetComponent() const {
  if (!IsValid()) return nullptr;
  return scene_->Objects().GetProperty<T>(entity_);
}

}  // namespace Mercury

#endif  // MERCURY_SCENE_MAP_SCENE_H
