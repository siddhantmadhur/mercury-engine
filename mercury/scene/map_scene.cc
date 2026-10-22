#include "map_scene.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include <algorithm>

#include "debugger/debugger.h"
#include "properties/camera.h"
#include "properties/primitives.h"
#include "scripting/behaviour.h"
#include "scripting/type_registry.h"
#include "ui/ui_document.h"

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

void GameObject::Destroy() const {
  if (IsValid()) scene_->Destroy(*this);
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
  ui_.resources().SetResolver([this](std::string_view path) {
    return project_ ? project_->Resolve(path) : fs::path(std::string(path));
  });
  GetRegistry().on_destroy<UIDocument>().connect<&MapScene::OnUIDocumentDestroyed>(this);
}

void MapScene::SetProject(std::shared_ptr<Project> project) {
  ClearMap();
  textures_.clear();
  models_.clear();
  project_ = std::move(project);
  assets_ = std::make_unique<AssetDatabase>(project_);
  ui_.resources().Reload();
}

MapScene::~MapScene() {
  // The registry outlives ui_, so stop hearing about its UIDocuments.
  GetRegistry().on_destroy<UIDocument>().disconnect(this);
  // Behaviours may touch the scene from OnDestroy, so tear them down first.
  for (auto& behaviour : behaviours_) behaviour->OnDestroy();
  behaviours_.clear();
}

void MapScene::OnUIDocumentDestroyed(entt::registry& registry, kEntityId entity) {
  if (auto* document = registry.try_get<UIDocument>(entity)) {
    if (document->root) ui_.RemoveDocument(document->root);
    document->root = nullptr;
    document->panel = nullptr;
  }
}

const UI::UIDrawData* MapScene::GetUIDrawData() const {
  const auto& data = ui_.GetDrawData();
  return data.empty() ? nullptr : &data;
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
  ApplyEnvironment();
  if (running_) StartBehaviours();
}

void MapScene::SyncFromMap(const Map& map) {
  // Maps edited in the editor are small, so rebuilding everything keeps the
  // scene exactly in step with the data without per-field bookkeeping.
  Instantiate(map);
}

void MapScene::ApplyEnvironment() {
  lighting_ = SceneLighting{};
  for (kEntityId entity : entities_) {
    auto light = object_manager_->GetProperty<DirectionalLight>(entity);
    if (!light) continue;
    auto transform = object_manager_->GetProperty<Transform>(entity);
    lighting_.direction =
        glm::normalize(transform->GetRotationQuat() * glm::vec3(0, 0, -1));
    lighting_.color = glm::vec3(light->color) * light->intensity;
    lighting_.ambient = glm::vec3(light->ambient);
    lighting_.cast_shadows = light->cast_shadows;
    lighting_.shadow_bias = light->shadow_bias;
    lighting_.shadow_normal_bias = light->shadow_normal_bias;
    lighting_.shadow_distance = light->shadow_distance;
    break;
  }

  clear_color_ = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
  skybox_path_.clear();
  for (kEntityId entity : entities_) {
    auto camera = object_manager_->GetProperty<CameraComponent>(entity);
    if (camera && camera->current) {
      clear_color_ = camera->background;
      // Loaded by the renderer into its own texture: a panorama is far too
      // big for the sprite atlas that GetTexture() packs into.
      if (camera->skybox.IsSet() && project_) {
        skybox_path_ = project_->Resolve(camera->skybox.path).string();
      }
      break;
    }
  }
  if (project_) physics_engine_->SetGravity(project_->GetGravity());
}

void MapScene::ClearMap() {
  pending_destroy_.clear();
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
  ui_.Clear();

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
    // One collider per node; the first one found in this order is used.
    auto box = object_manager_->GetProperty<BoxCollider>(entity);
    auto sphere = object_manager_->GetProperty<SphereCollider>(entity);
    auto capsule = object_manager_->GetProperty<CapsuleCollider>(entity);
    auto cylinder = object_manager_->GetProperty<CylinderCollider>(entity);
    auto plane = object_manager_->GetProperty<PlaneCollider>(entity);
    const int count = (box != nullptr) + (sphere != nullptr) +
                      (capsule != nullptr) + (cylinder != nullptr) +
                      (plane != nullptr);
    if (count == 0) continue;
    if (count > 1) {
      PDebug::warn("{}: has more than one collider; only the first is used",
                   GetGameObject(entity).GetPath());
    }

    const AssetRef* material_ref = nullptr;
    bool is_trigger = false;
    if (box) {
      material_ref = &box->material;
      is_trigger = box->is_trigger;
    } else if (sphere) {
      material_ref = &sphere->material;
      is_trigger = sphere->is_trigger;
    } else if (capsule) {
      material_ref = &capsule->material;
      is_trigger = capsule->is_trigger;
    } else if (cylinder) {
      material_ref = &cylinder->material;
      is_trigger = cylinder->is_trigger;
    } else {
      material_ref = &plane->material;
      is_trigger = plane->is_trigger;
    }
    const auto material = assets_->LoadAs<PhysicsMaterialAsset>(material_ref->path);
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
      if (body->mass > 0.0f) {
        settings.mOverrideMassProperties =
            JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = body->mass;
      }
    };

    const glm::vec3 scale =
        glm::abs(object_manager_->GetProperty<Transform>(entity)->GetScale());
    const float radial_scale = std::max(scale.x, scale.z);
    bool created = false;
    if (box) {
      created = physics_engine_->CreateBox3D(entity, box->size * scale, configure);
    } else if (sphere) {
      const float largest = std::max({scale.x, scale.y, scale.z});
      created = physics_engine_->CreateSphere(entity, sphere->radius * largest,
                                              configure);
    } else if (capsule) {
      const float radius = capsule->radius * radial_scale;
      const float half_height =
          std::max(capsule->height * scale.y / 2.0f - radius, 0.0f);
      created = physics_engine_->CreateCapsule(entity, half_height, radius,
                                               configure);
    } else if (cylinder) {
      created = physics_engine_->CreateCylinder(
          entity, cylinder->height * scale.y / 2.0f,
          cylinder->radius * radial_scale, configure);
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
  const bool ui = mode_ == Mode::kPlay && !ui_.empty();
  // UI callbacks (clicked, value changes) run before the scripts' OnFrame...
  if (ui && input_manager_) ui_.ProcessInput(*input_manager_);
  for (size_t i = 0; i < behaviours_.size(); i++) {
    behaviours_[i]->OnFrame(seconds);
  }
  DestroyPending();
  // ...and bindings pick up what the scripts changed before drawing.
  if (ui) {
    const float dpi = input_manager_ ? input_manager_->GetContentScale() : 1.0f;
    ui_.Update(seconds, glm::vec2(GetWidth(), GetHeight()), dpi);
  }
}

void MapScene::OnTick(double delta_t) {
  if (mode_ != Mode::kPlay) return;
  DispatchCollisions();
  const double seconds = delta_t / 1000.0;
  for (size_t i = 0; i < behaviours_.size(); i++) {
    behaviours_[i]->OnTick(seconds);
  }
  DestroyPending();
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

void MapScene::Destroy(GameObject object) {
  if (object.GetScene() != this || !object.IsValid()) return;
  if (std::find(pending_destroy_.begin(), pending_destroy_.end(),
                object.GetEntity()) == pending_destroy_.end()) {
    pending_destroy_.push_back(object.GetEntity());
  }
}

void MapScene::DestroyPending() {
  while (!pending_destroy_.empty()) {
    const kEntityId root = pending_destroy_.back();
    pending_destroy_.pop_back();
    auto info = object_manager_->GetProperty<NodeInfo>(root);
    if (!info) continue;
    const std::string prefix = info->path == "." ? "" : info->path + "/";

    // The object and everything below it, deepest first.
    std::vector<kEntityId> doomed;
    for (kEntityId entity : entities_) {
      auto node = object_manager_->GetProperty<NodeInfo>(entity);
      if (entity == root || (node && (prefix.empty() ||
                                      node->path.starts_with(prefix)))) {
        doomed.push_back(entity);
      }
    }
    std::reverse(doomed.begin(), doomed.end());

    for (kEntityId entity : doomed) {
      auto it = behaviours_by_entity_.find(entity);
      if (it != behaviours_by_entity_.end()) {
        for (Behaviour* behaviour : it->second) behaviour->OnDestroy();
        for (Behaviour* behaviour : it->second) {
          auto owned = std::find_if(
              behaviours_.begin(), behaviours_.end(),
              [&](const auto& b) { return b.get() == behaviour; });
          if (owned == behaviours_.end()) continue;
          if (static_cast<size_t>(owned - behaviours_.begin()) <
              started_behaviours_) {
            started_behaviours_--;
          }
          behaviours_.erase(owned);
        }
        behaviours_by_entity_.erase(it);
      }
      if (auto node = object_manager_->GetProperty<NodeInfo>(entity)) {
        by_path_.erase(node->path);
        by_uid_.erase(node->uid);
      }
      if (player_camera_ == entity) SetActiveCamera(default_camera_);
      DestroyNodeEntity(entity);
      std::erase(entities_, entity);
    }
  }
}

const Mesh* MapScene::GetModel(std::string_view res_path) {
  if (res_path.empty() || !project_) return nullptr;
  auto it = models_.find(res_path);
  if (it != models_.end()) return it->second.get();

  std::string error;
  std::unique_ptr<Mesh> model;
  if (auto mesh = Primitives::LoadModel(project_->Resolve(res_path).string(),
                                        glm::vec4(1.0f), &error)) {
    model = std::make_unique<Mesh>(std::move(*mesh));
  } else {
    // Remember the failure so it is only logged once.
    PDebug::warn("Could not load model {}: {}", res_path, error);
  }
  const Mesh* raw = model.get();
  models_.emplace(std::string(res_path), std::move(model));
  return raw;
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
