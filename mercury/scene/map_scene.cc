#include "map_scene.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include <algorithm>

#include "debugger/debugger.h"
#include "properties/camera.h"
#include "scripting/behaviour.h"
#include "scripting/type_registry.h"

namespace Mercury {

// ------------------------------------------------------------- GameObject ---

bool GameObject::IsValid() const {
  return scene_ && entity_ != entt::null &&
         scene_->GetRegistry().valid(entity_);
}

std::string GameObject::GetName() const {
  if (auto info = GetComponent<NodeInfo>()) return info->name;
  return "";
}

std::string GameObject::GetPath() const {
  if (auto info = GetComponent<NodeInfo>()) return info->path;
  return "";
}

Transform* GameObject::GetTransform() const {
  return GetComponent<Transform>();
}

// -------------------------------------------------------------- Behaviour ---

Transform& Behaviour::GetTransform() const {
  // Every map entity gets a Transform when it is created.
  return *scene_->Objects().GetProperty<Transform>(entity_);
}

GameObject Behaviour::Get(const NodeRef& ref) const {
  if (!ref.IsSet()) return {};
  auto object = scene_->Find(ref.path);
  if (!object) {
    PDebug::warn("{}: no node at path \"{}\"", GetGameObject().GetPath(),
                 ref.path);
  }
  return object;
}

bool Behaviour::IsKeyPressed(Key key) const {
  auto input = scene_->Input();
  return input && input->IsPressed(key);
}

bool Behaviour::IsKeyJustPressed(Key key) const {
  auto input = scene_->Input();
  return input && input->IsJustPressed(key);
}

// --------------------------------------------------------------- MapScene ---

MapScene::MapScene(std::shared_ptr<Project> project, Mode mode)
    : project_(std::move(project)), mode_(mode) {
  RegisterBuiltinTypes();
  assets_ = std::make_unique<AssetDatabase>(project_);
  default_camera_ = player_camera_;
}

void MapScene::SetProject(std::shared_ptr<Project> project) {
  ClearMap();
  textures_.clear();
  project_ = std::move(project);
  assets_ = std::make_unique<AssetDatabase>(project_);
}

MapScene::~MapScene() {
  // Behaviours may touch the scene from OnDestroy, so tear them down first.
  for (auto& behaviour : behaviours_) behaviour->OnDestroy();
  behaviours_.clear();
}

void MapScene::Instantiate(const Map& map) {
  ClearMap();

  std::function<void(const MapNode&, kEntityId, glm::vec3)> build =
      [&](const MapNode& node, kEntityId parent, glm::vec3 parent_position) {
        kEntityId entity = CreateNodeEntity(node, parent, parent_position);
        glm::vec3 position =
            object_manager_->GetProperty<Transform>(entity)->GetPosition();
        for (const auto& child : node.children) build(*child, entity, position);
      };
  build(*map.GetRoot(), entt::null, glm::vec3(0.0f));

  if (mode_ == Mode::kPlay) {
    CreatePhysicsBodies();
    for (kEntityId entity : entities_) {
      auto camera = object_manager_->GetProperty<CameraComponent>(entity);
      if (camera && camera->current) {
        SetActiveCamera(entity);
        break;
      }
    }
    if (player_camera_ == default_camera_) {
      PDebug::warn("Map has no Camera with current = true; using a default");
    }
  }
  if (running_) StartBehaviours();
}

void MapScene::SyncFromMap(const Map& map) {
  // Maps edited in the editor are small, so rebuilding everything keeps the
  // scene exactly in step with the data without per-field bookkeeping.
  Instantiate(map);
}

void MapScene::ClearMap() {
  for (auto& behaviour : behaviours_) behaviour->OnDestroy();
  behaviours_.clear();
  behaviours_by_entity_.clear();
  started_behaviours_ = 0;

  // Children first, the reverse of creation order.
  for (auto it = entities_.rbegin(); it != entities_.rend(); ++it) {
    DestroyNodeEntity(*it);
  }
  entities_.clear();
  by_path_.clear();
  by_uid_.clear();
  SetActiveCamera(default_camera_);

  std::lock_guard lock(collision_mutex_);
  collisions_.clear();
}

kEntityId MapScene::CreateNodeEntity(const MapNode& node, kEntityId parent,
                                     glm::vec3 parent_position) {
  auto& registry = TypeRegistry::Instance();
  const std::string path = node.GetPath();
  kEntityId entity = object_manager_->NewObject();
  object_manager_->AddProperty(entity, NodeInfo{node.name, path, parent,
                                                node.uid});

  // Transform comes first so every other component can rely on it.
  TransformComponent transform_data;
  if (auto data = node.FindComponent("Transform")) {
    transform_data =
        ReadFields<TransformComponent>(data->properties, path + ".Transform");
  }
  Transform transform(parent_position + transform_data.position);
  transform.SetRotate(transform_data.rotation);
  transform.SetScale(transform_data.scale);
  transform.SnapInterpolation();
  transform.ClearTransformations();
  object_manager_->AddProperty(entity, transform);

  entities_.push_back(entity);
  by_path_[path] = entity;
  by_uid_[node.uid] = entity;

  for (const auto& component : node.components) {
    if (component.type == "Transform") continue;
    const ComponentInfo* info = registry.FindComponent(component.type);
    if (!info) {
      if (mode_ == Mode::kPlay) {
        PDebug::warn("{}: unknown component \"{}\"", path, component.type);
      }
      continue;
    }
    // Scripts only run in the game, never inside the editor.
    if (info->is_script && mode_ == Mode::kEdit) continue;
    if (!info->attach) {
      PDebug::warn("{}: script \"{}\" is not compiled into this executable",
                   path, component.type);
      continue;
    }
    info->attach(*this, entity, component.properties);
  }
  return entity;
}

void MapScene::DestroyNodeEntity(kEntityId entity) {
  if (physics_engine_->IsRegistered(entity)) physics_engine_->DisableBody(entity);
  object_manager_->DestroyObject(entity);
}

void MapScene::CreatePhysicsBodies() {
  for (kEntityId entity : entities_) {
    auto box = object_manager_->GetProperty<BoxCollider>(entity);
    auto plane = object_manager_->GetProperty<PlaneCollider>(entity);
    if (!box && !plane) continue;
    if (box && plane) {
      PDebug::warn("{}: has both a BoxCollider and a PlaneCollider; using the box",
                   GetGameObject(entity).GetPath());
    }

    const AssetRef& material_ref = box ? box->material : plane->material;
    const auto material =
        assets_->LoadAs<PhysicsMaterialAsset>(material_ref.path);
    const bool is_trigger = box ? box->is_trigger : plane->is_trigger;
    std::optional<Rigidbody> body;
    if (auto rb = object_manager_->GetProperty<Rigidbody>(entity)) body = *rb;

    auto configure = [material, is_trigger,
                      body](JPH::BodyCreationSettings& settings) {
      settings.mFriction = material.friction;
      settings.mRestitution = material.restitution;
      settings.mIsSensor = is_trigger;
      if (!body || body->motion_type == MotionType::kStatic) {
        settings.mMotionType = JPH::EMotionType::Static;
        return;
      }
      settings.mMotionType = body->motion_type == MotionType::kKinematic
                                 ? JPH::EMotionType::Kinematic
                                 : JPH::EMotionType::Dynamic;
      using D = JPH::EAllowedDOFs;
      switch (body->allowed_dofs) {
        case AllowedDofs::kAll: settings.mAllowedDOFs = D::All; break;
        case AllowedDofs::kTranslationXYZ:
          settings.mAllowedDOFs = D::TranslationX | D::TranslationY | D::TranslationZ;
          break;
        case AllowedDofs::kTranslationXY:
          settings.mAllowedDOFs = D::TranslationX | D::TranslationY;
          break;
        case AllowedDofs::kPlane2D: settings.mAllowedDOFs = D::Plane2D; break;
        case AllowedDofs::kTranslationX: settings.mAllowedDOFs = D::TranslationX; break;
        case AllowedDofs::kTranslationY: settings.mAllowedDOFs = D::TranslationY; break;
      }
      settings.mMotionQuality = body->motion_quality == MotionQuality::kLinearCast
                                    ? JPH::EMotionQuality::LinearCast
                                    : JPH::EMotionQuality::Discrete;
      settings.mGravityFactor = body->gravity_factor;
      settings.mLinearDamping = body->linear_damping;
      settings.mAngularDamping = body->angular_damping;
      settings.mAllowSleeping = body->allow_sleeping;
    };

    bool created = false;
    if (box) {
      auto scale = object_manager_->GetProperty<Transform>(entity)->GetScale();
      created = physics_engine_->CreateBox3D(entity, box->size * scale, configure);
    } else {
      glm::vec3 normal = plane->normal;
      if (glm::length(normal) < 1e-6f) normal = glm::vec3(0, 1, 0);
      created = physics_engine_->CreatePlane(entity, glm::normalize(normal),
                                             configure);
    }
    if (!created) continue;

    // Jolt reports contacts from its worker threads; queue them and hand them
    // to behaviours on the main thread at the start of the next tick.
    physics_engine_->AddCollisionCallback<kCollisionEnter>(
        entity, [this](kEntityId self, kEntityId other) {
          std::lock_guard lock(collision_mutex_);
          collisions_.push_back({CollisionEvent::kEnter, self, other});
        });
    physics_engine_->AddCollisionCallback<kCollisionLeave>(
        entity, [this](kEntityId self, kEntityId other) {
          std::lock_guard lock(collision_mutex_);
          collisions_.push_back({CollisionEvent::kLeave, self, other});
        });
  }
}

void MapScene::DispatchCollisions() {
  std::vector<PendingCollision> pending;
  {
    std::lock_guard lock(collision_mutex_);
    pending.swap(collisions_);
  }
  auto& registry = GetRegistry();
  for (const auto& collision : pending) {
    if (!registry.valid(collision.self)) continue;
    auto it = behaviours_by_entity_.find(collision.self);
    if (it == behaviours_by_entity_.end()) continue;
    GameObject other(this, collision.other);
    // Copy: a handler may add behaviours, which can reallocate the list.
    auto behaviours = it->second;
    for (Behaviour* behaviour : behaviours) {
      if (collision.event == CollisionEvent::kEnter) {
        behaviour->OnCollisionEnter(other);
      } else {
        behaviour->OnCollisionLeave(other);
      }
    }
  }
}

void MapScene::StartBehaviours() {
  // OnStart may create more behaviours; index so the loop picks them up.
  while (started_behaviours_ < behaviours_.size()) {
    behaviours_[started_behaviours_++]->OnStart();
  }
}

void MapScene::OnStart() {
  running_ = true;
  StartBehaviours();
}

void MapScene::OnFrame(double delta_t) {
  const double seconds = delta_t / 1000.0;
  for (size_t i = 0; i < behaviours_.size(); i++) {
    behaviours_[i]->OnFrame(seconds);
  }
}

void MapScene::OnTick(double delta_t) {
  if (mode_ != Mode::kPlay) return;
  DispatchCollisions();
  const double seconds = delta_t / 1000.0;
  for (size_t i = 0; i < behaviours_.size(); i++) {
    behaviours_[i]->OnTick(seconds);
  }
}

void MapScene::OnDestroy() {
  for (auto& behaviour : behaviours_) behaviour->OnDestroy();
  behaviours_.clear();
  behaviours_by_entity_.clear();
  started_behaviours_ = 0;
  running_ = false;
}

GameObject MapScene::Find(std::string_view path) {
  if (path.empty()) path = ".";
  auto it = by_path_.find(std::string(path));
  if (it == by_path_.end()) return {};
  return {this, it->second};
}

kEntityId MapScene::GetEntityForNode(uint64_t uid) const {
  auto it = by_uid_.find(uid);
  return it == by_uid_.end() ? entt::null : it->second;
}

uint64_t MapScene::GetNodeUid(kEntityId entity) const {
  if (auto info = object_manager_->GetProperty<NodeInfo>(entity)) {
    return info->uid;
  }
  return 0;
}

void MapScene::AddBehaviour(kEntityId entity,
                            std::unique_ptr<Behaviour> behaviour) {
  behaviour->scene_ = this;
  behaviour->entity_ = entity;
  behaviours_by_entity_[entity].push_back(behaviour.get());
  behaviours_.push_back(std::move(behaviour));
}

const std::vector<Behaviour*>& MapScene::GetBehaviours(kEntityId entity) const {
  static const std::vector<Behaviour*> kNone;
  auto it = behaviours_by_entity_.find(entity);
  return it == behaviours_by_entity_.end() ? kNone : it->second;
}

Texture2D* MapScene::GetTexture(std::string_view res_path) {
  if (res_path.empty() || !project_) return nullptr;
  auto it = textures_.find(res_path);
  if (it != textures_.end()) return it->second.get();

  auto texture =
      std::make_unique<Texture2D>(project_->Resolve(res_path).string());
  Texture2D* raw = nullptr;
  if (texture->GetData()) {
    raw = texture.get();
    auto& atlas = object_manager_->GetAtlas();
    atlas.AddTexture(raw);
    atlas.UpdateAtlas();
  } else {
    texture.reset();  // remember the failure so it is only logged once
  }
  textures_.emplace(std::string(res_path), std::move(texture));
  return raw;
}

}  // namespace Mercury
