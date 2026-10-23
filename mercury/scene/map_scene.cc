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

GameObject GameObject::GetParent() const {
  if (!IsValid()) return {};
  if (scene_->HasParent(entity_)) {
    if (auto info = GetComponent<NodeInfo>()) {
      return {scene_, info->parent};
    }
  }
  return {};
}

std::vector<GameObject> GameObject::GetChildren() const {
  if (!IsValid()) return {};
  std::vector<GameObject> result;
  if (auto info = GetComponent<NodeInfo>()) {
    result.reserve(info->children.size());
    for (kEntityId child_id : info->children) {
      if (scene_->GetRegistry().valid(child_id)) {
        result.emplace_back(scene_, child_id);
      }
    }
  }
  return result;
}

void GameObject::SetParent(GameObject new_parent, bool keep_world_transform) {
  if (!IsValid()) return;
  scene_->SetEntityParent(entity_, new_parent.GetEntity(), keep_world_transform);
}

void GameObject::AddForce(glm::vec3 force, ForceMode mode) const {
  if (!IsValid()) return;
  scene_->Physics().AddForce(entity_, force, mode);
}

void GameObject::AddTorque(glm::vec3 torque, ForceMode mode) const {
  if (!IsValid()) return;
  scene_->Physics().AddTorque(entity_, torque, mode);
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

  std::function<kEntityId(const MapNode&, kEntityId, glm::vec3)> build =
      [&](const MapNode& node, kEntityId parent, glm::vec3 parent_position) {
        kEntityId entity = CreateNodeEntity(node, parent, parent_position);
        glm::vec3 position =
            object_manager_->GetProperty<Transform>(entity)->GetPosition();
        for (const auto& child : node.children) build(*child, entity, position);
        return entity;
      };
  root_entity_ = build(*map.GetRoot(), entt::null, glm::vec3(0.0f));

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
  root_entity_ = entt::null;
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

  if (parent != entt::null && GetRegistry().valid(parent)) {
    if (auto p_info = object_manager_->GetProperty<NodeInfo>(parent)) {
      p_info->children.push_back(entity);
    }
  }

  // Transform comes first so every other component can rely on it.
  TransformComponent transform_data;
  if (auto data = node.FindComponent("Transform")) {
    transform_data =
        ReadFields<TransformComponent>(data->properties, path + ".Transform");
  }

  glm::vec3 world_pos = transform_data.position;
  glm::quat world_rot = Transform::EulerToQuat(transform_data.rotation);
  glm::vec3 world_scale = transform_data.scale;

  if (parent != entt::null && GetRegistry().valid(parent)) {
    if (auto p_trans = object_manager_->GetProperty<Transform>(parent)) {
      const glm::vec3 p_pos = p_trans->GetPosition();
      const glm::quat p_rot = p_trans->GetRotationQuat();
      const glm::vec3 p_scale = p_trans->GetScale();
      world_pos = p_pos + p_rot * (p_scale * transform_data.position);
      world_rot = p_rot * world_rot;
      world_scale = p_scale * world_scale;
    } else {
      world_pos = parent_position + transform_data.position;
    }
  }

  Transform transform(world_pos);
  transform.SetRotate(Transform::QuatToEuler(world_rot));
  transform.SetScale(world_scale);
  transform.SetLocalPosition(transform_data.position);
  transform.SetLocalRotate(transform_data.rotation);
  transform.SetLocalScale(transform_data.scale);
  transform.SnapInterpolation();
  transform.ClearTransformations();

  transform.SetOnWorldChanged(
      [this, entity] { OnWorldTransformChanged(entity); });
  transform.SetOnLocalChanged(
      [this, entity] { OnLocalTransformChanged(entity); });
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
      const uint8_t allowed = body->ComputeAllowedDOFs();
      if (allowed == 0) {
        settings.mMotionType = JPH::EMotionType::Static;
      } else {
        settings.mAllowedDOFs = JPH::EAllowedDOFs(allowed);
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

bool MapScene::HasParent(kEntityId entity) const {
  if (!GetRegistry().valid(entity)) return false;
  if (auto node_info = object_manager_->GetProperty<NodeInfo>(entity)) {
    return node_info->parent != entt::null && node_info->parent != root_entity_ &&
           GetRegistry().valid(node_info->parent);
  }
  return false;
}

void MapScene::OnWorldTransformChanged(kEntityId entity) {
  if (in_hierarchy_update_) return;
  in_hierarchy_update_ = true;

  auto transform = object_manager_->GetProperty<Transform>(entity);
  auto node_info = object_manager_->GetProperty<NodeInfo>(entity);

  if (transform && node_info) {
    if (HasParent(entity)) {
      if (auto parent_transform =
              object_manager_->GetProperty<Transform>(node_info->parent)) {
        const glm::vec3 p_pos = parent_transform->GetPosition();
        const glm::quat p_rot = parent_transform->GetRotationQuat();
        const glm::vec3 p_scale = parent_transform->GetScale();
        const glm::quat p_rot_inv = glm::inverse(p_rot);

        const glm::vec3 safe_scale = glm::vec3(
            std::abs(p_scale.x) < 1e-6f ? 1.0f : p_scale.x,
            std::abs(p_scale.y) < 1e-6f ? 1.0f : p_scale.y,
            std::abs(p_scale.z) < 1e-6f ? 1.0f : p_scale.z);

        const glm::vec3 local_pos =
            (p_rot_inv * (transform->GetPosition() - p_pos)) / safe_scale;
        const glm::quat local_rot_q = p_rot_inv * transform->GetRotationQuat();
        const glm::vec3 local_scale = transform->GetScale() / safe_scale;

        transform->SetLocalPosition(local_pos);
        transform->SetLocalRotate(Transform::QuatToEuler(local_rot_q));
        transform->SetLocalScale(local_scale);
      }
    } else {
      transform->SetLocalPosition(transform->GetPosition());
      transform->SetLocalRotate(transform->GetRotate());
      transform->SetLocalScale(transform->GetScale());
    }

    UpdateEntityTransformHierarchy(entity);
  }

  in_hierarchy_update_ = false;
}

void MapScene::OnLocalTransformChanged(kEntityId entity) {
  if (in_hierarchy_update_) return;
  in_hierarchy_update_ = true;

  auto transform = object_manager_->GetProperty<Transform>(entity);
  auto node_info = object_manager_->GetProperty<NodeInfo>(entity);

  if (transform && node_info) {
    if (HasParent(entity)) {
      if (auto parent_transform =
              object_manager_->GetProperty<Transform>(node_info->parent)) {
        const glm::vec3 p_pos = parent_transform->GetPosition();
        const glm::quat p_rot = parent_transform->GetRotationQuat();
        const glm::vec3 p_scale = parent_transform->GetScale();

        const glm::vec3 world_pos =
            p_pos + p_rot * (p_scale * transform->GetLocalPosition());
        const glm::quat world_rot = p_rot * transform->GetLocalRotationQuat();
        const glm::vec3 world_scale = p_scale * transform->GetLocalScale();

        transform->SetWorldTransformInternal(
            world_pos, Transform::QuatToEuler(world_rot), world_scale);
      }
    } else {
      transform->SetWorldTransformInternal(
          transform->GetLocalPosition(), transform->GetLocalRotate(),
          transform->GetLocalScale());
    }

    UpdateEntityTransformHierarchy(entity);
  }

  in_hierarchy_update_ = false;
}

void MapScene::UpdateEntityTransformHierarchy(kEntityId entity) {
  auto transform = object_manager_->GetProperty<Transform>(entity);
  auto node_info = object_manager_->GetProperty<NodeInfo>(entity);
  if (!transform || !node_info) return;

  const glm::vec3 parent_pos = transform->GetPosition();
  const glm::quat parent_rot = transform->GetRotationQuat();
  const glm::vec3 parent_scale = transform->GetScale();

  for (kEntityId child_id : node_info->children) {
    if (!GetRegistry().valid(child_id)) continue;
    auto child_transform = object_manager_->GetProperty<Transform>(child_id);
    if (!child_transform) continue;

    const glm::vec3 child_local_pos = child_transform->GetLocalPosition();
    const glm::quat child_local_rot = child_transform->GetLocalRotationQuat();
    const glm::vec3 child_local_scale = child_transform->GetLocalScale();

    const glm::vec3 world_pos =
        parent_pos + parent_rot * (parent_scale * child_local_pos);
    const glm::quat world_rot = parent_rot * child_local_rot;
    const glm::vec3 world_scale = parent_scale * child_local_scale;

    child_transform->SetWorldTransformInternal(
        world_pos, Transform::QuatToEuler(world_rot), world_scale);

    UpdateEntityTransformHierarchy(child_id);
  }
}

void MapScene::UpdateInterpolatedHierarchy() {
  for (kEntityId entity : entities_) {
    if (!HasParent(entity)) continue;
    auto node_info = object_manager_->GetProperty<NodeInfo>(entity);
    auto parent_transform =
        object_manager_->GetProperty<Transform>(node_info->parent);
    auto child_transform = object_manager_->GetProperty<Transform>(entity);
    if (!parent_transform || !child_transform) continue;

    const glm::vec3 p_pos = parent_transform->GetInterpolatedPosition();
    const glm::quat p_rot = Transform::EulerToQuat(
        parent_transform->GetInterpolatedRotation());
    const glm::vec3 p_scale = parent_transform->GetInterpolatedScale();

    const glm::vec3 child_local_pos = child_transform->GetLocalPosition();
    const glm::quat child_local_rot = child_transform->GetLocalRotationQuat();
    const glm::vec3 child_local_scale = child_transform->GetLocalScale();

    const glm::vec3 interp_pos =
        p_pos + p_rot * (p_scale * child_local_pos);
    const glm::quat interp_rot = p_rot * child_local_rot;
    const glm::vec3 interp_scale = p_scale * child_local_scale;

    child_transform->SetInterpolatedTransformInternal(
        interp_pos, Transform::QuatToEuler(interp_rot), interp_scale);
  }
}

void MapScene::ProcessOnFrame(float alpha) {
  GameScene::ProcessOnFrame(alpha);
  UpdateInterpolatedHierarchy();
}

void MapScene::SetEntityParent(kEntityId entity, kEntityId new_parent,
                              bool keep_world_transform) {
  if (!GetRegistry().valid(entity)) return;
  auto node_info = object_manager_->GetProperty<NodeInfo>(entity);
  auto transform = object_manager_->GetProperty<Transform>(entity);
  if (!node_info || !transform) return;

  if (node_info->parent == new_parent) return;

  // Remove from old parent's children
  if (node_info->parent != entt::null && GetRegistry().valid(node_info->parent)) {
    if (auto old_p_info =
            object_manager_->GetProperty<NodeInfo>(node_info->parent)) {
      std::erase(old_p_info->children, entity);
    }
  }

  node_info->parent = new_parent;

  // Add to new parent's children
  if (new_parent != entt::null && GetRegistry().valid(new_parent)) {
    if (auto new_p_info = object_manager_->GetProperty<NodeInfo>(new_parent)) {
      if (std::find(new_p_info->children.begin(), new_p_info->children.end(),
                    entity) == new_p_info->children.end()) {
        new_p_info->children.push_back(entity);
      }
    }
  }

  if (keep_world_transform) {
    if (new_parent != entt::null && GetRegistry().valid(new_parent)) {
      if (auto p_trans = object_manager_->GetProperty<Transform>(new_parent)) {
        const glm::vec3 p_pos = p_trans->GetPosition();
        const glm::quat p_rot_inv = glm::inverse(p_trans->GetRotationQuat());
        const glm::vec3 p_scale = p_trans->GetScale();
        const glm::vec3 safe_scale = glm::vec3(
            std::abs(p_scale.x) < 1e-6f ? 1.0f : p_scale.x,
            std::abs(p_scale.y) < 1e-6f ? 1.0f : p_scale.y,
            std::abs(p_scale.z) < 1e-6f ? 1.0f : p_scale.z);
        transform->SetLocalPosition(
            (p_rot_inv * (transform->GetPosition() - p_pos)) / safe_scale);
        transform->SetLocalRotate(
            Transform::QuatToEuler(p_rot_inv * transform->GetRotationQuat()));
        transform->SetLocalScale(transform->GetScale() / safe_scale);
      }
    } else {
      transform->SetLocalPosition(transform->GetPosition());
      transform->SetLocalRotate(transform->GetRotate());
      transform->SetLocalScale(transform->GetScale());
    }
  } else {
    if (new_parent != entt::null && GetRegistry().valid(new_parent)) {
      if (auto p_trans = object_manager_->GetProperty<Transform>(new_parent)) {
        const glm::vec3 world_pos =
            p_trans->GetPosition() +
            p_trans->GetRotationQuat() *
                (p_trans->GetScale() * transform->GetLocalPosition());
        const glm::quat world_rot =
            p_trans->GetRotationQuat() * transform->GetLocalRotationQuat();
        const glm::vec3 world_scale =
            p_trans->GetScale() * transform->GetLocalScale();
        transform->SetWorldTransformInternal(
            world_pos, Transform::QuatToEuler(world_rot), world_scale);
      }
    }
  }

  UpdateEntityTransformHierarchy(entity);
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
        if (node->parent != entt::null && GetRegistry().valid(node->parent)) {
          if (auto p_info = object_manager_->GetProperty<NodeInfo>(node->parent)) {
            std::erase(p_info->children, entity);
          }
        }
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
