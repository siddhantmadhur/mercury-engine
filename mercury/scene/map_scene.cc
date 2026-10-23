#include "map_scene.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include <algorithm>
#include <cstdlib>

#include "animation/animator.h"
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

GameObject GameObject::FindChild(std::string_view path) const {
  if (!IsValid()) return {};
  GameObject current = *this;
  while (!path.empty()) {
    auto slash = path.find('/');
    std::string_view part = path.substr(0, slash);
    path = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
    GameObject next;
    for (const GameObject& child : current.GetChildren()) {
      if (child.GetName() == part) {
        next = child;
        break;
      }
    }
    if (!next) return {};
    current = next;
  }
  return current;
}

void GameObject::SetParent(GameObject new_parent, bool keep_world_transform) {
  if (!IsValid()) return;
  scene_->SetEntityParent(entity_, new_parent.GetEntity(), keep_world_transform);
}

std::string GameObject::GetTag() const {
  if (auto info = GetComponent<NodeInfo>()) return info->tag;
  return "";
}

void GameObject::SetTag(std::string tag) const {
  if (auto info = GetComponent<NodeInfo>()) info->tag = std::move(tag);
}

int GameObject::GetLayer() const {
  if (auto info = GetComponent<NodeInfo>()) return info->layer;
  return 0;
}

void GameObject::SetLayer(int layer, bool include_children) const {
  if (!IsValid()) return;
  scene_->SetLayer(entity_, layer);
  if (!include_children) return;
  for (const GameObject& child : GetChildren()) child.SetLayer(layer, true);
}

bool GameObject::IsActiveSelf() const {
  if (auto info = GetComponent<NodeInfo>()) return info->active;
  return false;
}

bool GameObject::IsActiveInHierarchy() const {
  return IsValid() && scene_->IsActiveInHierarchy(entity_);
}

void GameObject::SetActive(bool active) const {
  if (IsValid()) scene_->SetActive(entity_, active);
}

glm::vec3 GameObject::GetVelocity() const {
  if (!IsValid()) return glm::vec3(0.0f);
  return scene_->Physics().GetLinearVelocity(entity_);
}

void GameObject::SetVelocity(glm::vec3 velocity) const {
  if (IsValid()) scene_->Physics().SetLinearVelocity(entity_, velocity);
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

void Behaviour::SetEnabled(bool enabled) {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  if (scene_) scene_->OnBehaviourEnabledChanged(*this);
}

bool Behaviour::IsActiveAndEnabled() const {
  return enabled_ && scene_ && scene_->IsActiveInHierarchy(entity_);
}

InputActionAsset* Behaviour::GetInputActions() const {
  return scene_ ? scene_->GetInputActions() : nullptr;
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
    : project_(std::move(project)), mode_(mode), prefabs_(project_) {
  RegisterBuiltinTypes();
  assets_ = std::make_unique<AssetDatabase>(project_);
  default_camera_ = player_camera_;
  ui_.resources().SetResolver([this](std::string_view path) {
    return project_ ? project_->Resolve(path) : fs::path(std::string(path));
  });
  GetRegistry().on_destroy<UIDocument>().connect<&MapScene::OnUIDocumentDestroyed>(this);
  GetRegistry().on_destroy<AudioSource>().connect<&MapScene::OnAudioSourceDestroyed>(this);
  ApplyProjectSettings();
  if (mode == Mode::kPlay) {
    audio_ = std::make_unique<AudioSystem>();
    // MERCURY_AUDIO=none: mix without a device (headless runs).
    const char* audio_mode = std::getenv("MERCURY_AUDIO");
    const bool no_device = audio_mode && std::string_view(audio_mode) == "none";
    audio_->Initialize(
        [this](std::string_view path) {
          return project_ ? project_->Resolve(path).string() : std::string(path);
        },
        no_device);
  }

  // Physics reports these after each step, on the main thread; they reach
  // the scripts at the start of the next tick.
  physics_engine_->SetTriggerCallback(
      [this](TriggerEvent event, kEntityId trigger, kEntityId other) {
        static constexpr CollisionEvent kMap[] = {CollisionEvent::kTriggerEnter,
                                                  CollisionEvent::kTriggerStay,
                                                  CollisionEvent::kTriggerExit};
        std::lock_guard lock(collision_mutex_);
        collisions_.push_back({kMap[static_cast<int>(event)], trigger, other});
        collisions_.push_back({kMap[static_cast<int>(event)], other, trigger});
      });
  physics_engine_->SetContactCallback(
      [this](ContactEvent event, kEntityId a, kEntityId b) {
        static constexpr CollisionEvent kMap[] = {
            CollisionEvent::kEnter, CollisionEvent::kStay, CollisionEvent::kLeave};
        std::lock_guard lock(collision_mutex_);
        collisions_.push_back({kMap[static_cast<int>(event)], a, b});
        collisions_.push_back({kMap[static_cast<int>(event)], b, a});
      });
}

void MapScene::ApplyProjectSettings() {
  if (!project_) return;
  physics_engine_->SetTickRate(project_->GetTickRate());
  physics_engine_->SetQueriesHitTriggers(project_->GetQueriesHitTriggers());
  Time::Reset(1.0f / static_cast<float>(project_->GetTickRate()));
  input_actions_.reset();
  const std::string actions = project_->GetInputActionsPath();
  if (!actions.empty()) {
    auto asset = std::make_unique<InputActionAsset>();
    std::string error;
    if (asset->Load(project_->Resolve(actions), &error)) {
      input_actions_ = std::move(asset);
    } else {
      PDebug::error("Could not load input actions: {}", error);
    }
  }
}

int MapScene::GetTicksPerSecond() const {
  return physics_engine_->GetTickRate();
}

void MapScene::SetProject(std::shared_ptr<Project> project) {
  ClearMap();
  textures_.clear();
  models_.clear();
  project_ = std::move(project);
  assets_ = std::make_unique<AssetDatabase>(project_);
  prefabs_.SetProject(project_);
  ui_.resources().Reload();
  ApplyProjectSettings();
}

void MapScene::OnAudioSourceDestroyed(entt::registry& registry, kEntityId entity) {
  if (auto* source = registry.try_get<AudioSource>(entity)) {
    if (source->audio_) source->audio_->DestroySource(source->source_);
    source->audio_ = nullptr;
  }
}

MapScene::~MapScene() {
  // The registry outlives ui_, so stop hearing about its UIDocuments.
  GetRegistry().on_destroy<UIDocument>().disconnect(this);
  GetRegistry().on_destroy<AudioSource>().disconnect(this);
  physics_engine_->SetTriggerCallback({});
  physics_engine_->SetContactCallback({});
  // Behaviours may touch the scene from OnDestroy, so tear them down first.
  for (auto& behaviour : behaviours_) {
    if (behaviour->awoken_) behaviour->OnDestroy();
  }
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

  std::vector<kEntityId> created;
  root_entity_ = BuildSubtree(*map.GetRoot(), entt::null, ".", created);
  FinishCreated(created);

  if (mode_ == Mode::kPlay) {
    for (kEntityId entity : entities_) {
      auto camera = object_manager_->GetProperty<CameraComponent>(entity);
      if (camera && camera->current && camera->enabled &&
          IsActiveInHierarchy(entity)) {
        SetActiveCamera(entity);
        break;
      }
    }
    if (player_camera_ == default_camera_) {
      PDebug::warn("Map has no Camera with current = true; using a default");
    }
  }
  ApplyEnvironment();
  if (running_) StartPendingBehaviours();
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
    if (!light || !IsActiveInHierarchy(entity)) continue;
    auto transform = object_manager_->GetProperty<Transform>(entity);
    lighting_.direction =
        glm::normalize(transform->GetRotationQuat() * glm::vec3(0, 0, -1));
    lighting_.color = glm::vec3(light->color) * light->intensity;
    lighting_.ambient = glm::vec3(light->ambient);
    lighting_.ambient_ground = light->ground_ambient.a > 0.0f
                                   ? glm::vec3(light->ground_ambient)
                                   : lighting_.ambient * 0.35f;
    lighting_.cast_shadows = light->cast_shadows;
    lighting_.shadow_bias = light->shadow_bias;
    lighting_.shadow_normal_bias = light->shadow_normal_bias;
    lighting_.shadow_distance = light->shadow_distance;
    break;
  }
  ApplyEnvironmentFrom(player_camera_);
  if (project_) physics_engine_->SetGravity(project_->GetGravity());
}

void MapScene::ApplyEnvironmentFrom(kEntityId camera_entity) {
  clear_color_ = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
  skybox_path_.clear();
  auto camera = object_manager_->GetProperty<CameraComponent>(camera_entity);
  if (!camera) {
    // Edit mode, or no camera: the first camera's settings.
    for (kEntityId entity : entities_) {
      camera = object_manager_->GetProperty<CameraComponent>(entity);
      if (camera && camera->current) break;
      camera = nullptr;
    }
  }
  if (!camera) return;
  clear_color_ = camera->background;
  // Loaded by the renderer into its own texture: a panorama is far too
  // big for the sprite atlas that GetTexture() packs into.
  if (camera->skybox.IsSet() && project_) {
    skybox_path_ = project_->Resolve(camera->skybox.path).string();
  }
}

void MapScene::SetCurrentCamera(GameObject camera) {
  if (!camera.IsValid() || !camera.GetComponent<CameraComponent>()) return;
  SetActiveCamera(camera.GetEntity());
  ApplyEnvironmentFrom(camera.GetEntity());
}

void MapScene::ClearMap() {
  pending_destroy_.clear();
  for (auto& behaviour : behaviours_) {
    if (behaviour->awoken_) behaviour->OnDestroy();
  }
  behaviours_.clear();
  behaviours_by_entity_.clear();

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

kEntityId MapScene::BuildSubtree(const MapNode& node, kEntityId parent,
                                 const std::string& path,
                                 std::vector<kEntityId>& created) {
  kEntityId entity = CreateNodeEntity(node, parent, path);
  created.push_back(entity);
  for (const auto& child : node.children) {
    const std::string child_path =
        path == "." ? child->name : path + "/" + child->name;
    BuildSubtree(*child, entity, child_path, created);
  }
  return entity;
}

void MapScene::FinishCreated(const std::vector<kEntityId>& created) {
  // Active flags first: inactive objects get no physics bodies in the world
  // and no Awake until they are activated.
  for (kEntityId entity : created) {
    auto info = object_manager_->GetProperty<NodeInfo>(entity);
    if (!info) continue;
    const bool parent_active =
        info->parent == entt::null || IsActiveInHierarchy(info->parent);
    info->active_in_hierarchy = parent_active && info->active;
    if (!info->active_in_hierarchy) object_manager_->AddProperty(entity, InactiveTag{});
  }
  if (mode_ != Mode::kPlay) return;
  for (kEntityId entity : created) CreatePhysicsBody(entity);
  for (kEntityId entity : created) {
    if (object_manager_->GetProperty<Animator>(entity)) SetupAnimator(entity);
  }
  AwakeBehaviours();
}

kEntityId MapScene::CreateNodeEntity(const MapNode& node, kEntityId parent,
                                     const std::string& path) {
  auto& registry = TypeRegistry::Instance();
  kEntityId entity = object_manager_->NewObject();
  {
    NodeInfo info;
    info.name = path == "." ? node.name : path.substr(path.rfind('/') + 1);
    info.path = path;
    info.parent = parent;
    info.uid = node.uid;
    info.tag = node.tag;
    info.layer = node.layer;
    info.active = node.active;
    object_manager_->AddProperty(entity, std::move(info));
  }

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
    const size_t before = behaviours_.size();
    info->attach(*this, entity, component.properties);
    // Unity's MonoBehaviour.enabled checkbox.
    if (info->is_script && behaviours_.size() > before &&
        !component.properties.GetBool("enabled", true)) {
      behaviours_.back()->enabled_ = false;
    }
  }
  return entity;
}

void MapScene::DestroyNodeEntity(kEntityId entity) {
  physics_engine_->RemoveBodyNow(entity);
  object_manager_->DestroyObject(entity);
}

namespace {

// Scales an unscaled collider shape by a node's world scale.
ColliderShape ScaleShape(const ColliderShape& shape, glm::vec3 scale) {
  ColliderShape scaled = shape;
  scale = glm::abs(scale);
  const float radial = std::max(scale.x, scale.z);
  scaled.center = shape.center * scale;
  switch (shape.kind) {
    case ColliderShape::Kind::kBox:
      scaled.size = shape.size * scale;
      break;
    case ColliderShape::Kind::kSphere:
      scaled.radius = shape.radius * std::max({scale.x, scale.y, scale.z});
      break;
    case ColliderShape::Kind::kCapsule:
    case ColliderShape::Kind::kCylinder:
      scaled.radius = shape.radius * radial;
      scaled.height = shape.height * scale.y;
      break;
    case ColliderShape::Kind::kMesh:
    case ColliderShape::Kind::kConvexHull:
      for (auto& v : scaled.vertices) v *= scale;
      break;
  }
  return scaled;
}

}  // namespace

void MapScene::CreatePhysicsBody(kEntityId entity) {
  if (mode_ != Mode::kPlay) return;
  physics_engine_->RemoveBodyNow(entity);
  auto info = object_manager_->GetProperty<NodeInfo>(entity);
  auto transform = object_manager_->GetProperty<Transform>(entity);
  if (!info || !transform) return;
  const int layer = info->layer;

  if (auto controller = object_manager_->GetProperty<CharacterController>(entity)) {
    CharacterDesc desc;
    const glm::vec3 scale = glm::abs(transform->GetScale());
    desc.height = controller->height * scale.y;
    desc.radius = controller->radius * std::max(scale.x, scale.z);
    desc.center = controller->center * scale;
    desc.slope_limit = controller->slope_limit;
    desc.step_offset = controller->step_offset;
    desc.skin_width = controller->skin_width;
    desc.min_move_distance = controller->min_move_distance;
    physics_engine_->CreateCharacter(entity, desc, layer);
    controller->physics_ = physics_engine_.get();
    controller->entity_ = entity;
  }

  auto colliders = object_manager_->GetProperty<ColliderSet>(entity);
  if (colliders && !colliders->shapes.empty()) {
    auto rigidbody = object_manager_->GetProperty<Rigidbody>(entity);
    // Colliders under a moving Rigidbody belong to it in Unity; here they
    // ride along through the hierarchy without a body of their own.
    if (!rigidbody) {
      for (kEntityId p = info->parent; p != entt::null && GetRegistry().valid(p);) {
        auto parent_body = object_manager_->GetProperty<Rigidbody>(p);
        if (parent_body && parent_body->motion_type != MotionType::kStatic) return;
        auto parent_info = object_manager_->GetProperty<NodeInfo>(p);
        p = parent_info ? parent_info->parent : entt::null;
      }
    }
    BodyDesc desc;
    if (rigidbody) {
      switch (rigidbody->motion_type) {
        case MotionType::kStatic: desc.motion = BodyDesc::Motion::kStatic; break;
        case MotionType::kKinematic: desc.motion = BodyDesc::Motion::kKinematic; break;
        case MotionType::kDynamic: desc.motion = BodyDesc::Motion::kDynamic; break;
      }
      desc.allowed_dofs = rigidbody->ComputeAllowedDOFs();
      if (desc.allowed_dofs == 0) desc.motion = BodyDesc::Motion::kStatic;
      desc.mass = rigidbody->mass;
      desc.linear_cast = rigidbody->motion_quality == MotionQuality::kLinearCast;
      desc.gravity_factor = rigidbody->use_gravity ? rigidbody->gravity_factor : 0.0f;
      desc.linear_damping = rigidbody->linear_damping;
      desc.angular_damping = rigidbody->angular_damping;
      desc.allow_sleeping = rigidbody->allow_sleeping;
    }
    std::vector<ColliderShape> shapes;
    shapes.reserve(colliders->shapes.size());
    for (const auto& shape : colliders->shapes) {
      shapes.push_back(ScaleShape(shape, transform->GetScale()));
    }
    physics_engine_->CreateBody(entity, shapes, desc, layer);
  } else if (auto plane = object_manager_->GetProperty<PlaneCollider>(entity)) {
    // Infinite planes have no Jolt shape that can be compounded.
    const auto material = assets_->LoadAs<PhysicsMaterialAsset>(plane->material.path);
    const bool is_trigger = plane->is_trigger;
    glm::vec3 normal = plane->normal;
    if (glm::length(normal) < 1e-6f) normal = glm::vec3(0, 1, 0);
    physics_engine_->CreatePlane(entity, glm::normalize(normal),
                                 [material, is_trigger](JPH::BodyCreationSettings& settings) {
                                   settings.mFriction = material.friction;
                                   settings.mRestitution = material.restitution;
                                   settings.mIsSensor = is_trigger;
                                 });
    physics_engine_->SetLayer(entity, layer);
  }
  if (!info->active_in_hierarchy) physics_engine_->SetBodyEnabled(entity, false);
}

void MapScene::DispatchCollisions() {
  std::vector<PendingCollision> pending;
  {
    std::lock_guard lock(collision_mutex_);
    pending.swap(collisions_);
  }
  auto& registry = GetRegistry();
  for (const auto& collision : pending) {
    if (!registry.valid(collision.self) || !registry.valid(collision.other)) continue;
    auto it = behaviours_by_entity_.find(collision.self);
    if (it == behaviours_by_entity_.end()) continue;
    GameObject other(this, collision.other);
    // Copy: a handler may add behaviours, which can reallocate the list.
    auto behaviours = it->second;
    for (Behaviour* behaviour : behaviours) {
      if (!behaviour->awoken_ || !behaviour->IsActiveAndEnabled()) continue;
      switch (collision.event) {
        case CollisionEvent::kEnter: behaviour->OnCollisionEnter(other); break;
        case CollisionEvent::kStay: behaviour->OnCollisionStay(other); break;
        case CollisionEvent::kLeave: behaviour->OnCollisionLeave(other); break;
        case CollisionEvent::kTriggerEnter: behaviour->OnTriggerEnter(other); break;
        case CollisionEvent::kTriggerStay: behaviour->OnTriggerStay(other); break;
        case CollisionEvent::kTriggerExit: behaviour->OnTriggerExit(other); break;
      }
      // A handler may have destroyed the other object already; it stays
      // valid until DestroyPending, as in Unity.
    }
  }
}

void MapScene::AwakeBehaviours() {
  if (mode_ != Mode::kPlay) return;
  // Awake/OnEnable may add more behaviours; index so the loop sees them.
  for (size_t i = 0; i < behaviours_.size(); i++) {
    Behaviour* behaviour = behaviours_[i].get();
    if (behaviour->awoken_ || !IsActiveInHierarchy(behaviour->entity_)) continue;
    behaviour->awoken_ = true;
    auto binder = Reflector::Events([this](Event& event) { event.Bind(this); });
    behaviour->Reflect(binder);
    behaviour->ReflectData(binder);
    behaviour->Awake();
    if (behaviour->enabled_ && !behaviour->enable_called_ &&
        IsActiveInHierarchy(behaviour->entity_)) {
      behaviour->enable_called_ = true;
      behaviour->OnEnable();
    }
  }
}

void MapScene::OnBehaviourEnabledChanged(Behaviour& behaviour) {
  if (!behaviour.awoken_) return;
  const bool should_be_enabled =
      behaviour.enabled_ && IsActiveInHierarchy(behaviour.entity_);
  if (should_be_enabled && !behaviour.enable_called_) {
    behaviour.enable_called_ = true;
    behaviour.OnEnable();
  } else if (!should_be_enabled && behaviour.enable_called_) {
    behaviour.enable_called_ = false;
    behaviour.OnDisable();
  }
}

void MapScene::StartPendingBehaviours() {
  // OnStart may create more behaviours; index so the loop picks them up.
  for (size_t i = 0; i < behaviours_.size(); i++) {
    Behaviour* behaviour = behaviours_[i].get();
    if (behaviour->started_ || !behaviour->awoken_) continue;
    if (!behaviour->IsActiveAndEnabled()) continue;
    behaviour->started_ = true;
    behaviour->OnStart();
  }
}

void MapScene::OnStart() {
  running_ = true;
  // Sources set to play on awake start with the scene.
  auto sources = GetRegistry().view<AudioSource>();
  for (auto [entity, source] : sources.each()) {
    if (source.play_on_awake && source.enabled && IsActiveInHierarchy(entity)) source.Play();
  }
  AwakeBehaviours();
  StartPendingBehaviours();
}

void MapScene::OnFrame(double delta_t) {
  const double seconds = delta_t / 1000.0;
  if (mode_ == Mode::kPlay) {
    Time::BeginFrame(seconds);
    if (input_actions_ && input_manager_) input_actions_->Update(*input_manager_);
    StartPendingBehaviours();
  }
  const bool ui = mode_ == Mode::kPlay && !ui_.empty();
  // UI callbacks (clicked, value changes) run before the scripts' OnFrame...
  if (ui && input_manager_) ui_.ProcessInput(*input_manager_);
  const float dt = mode_ == Mode::kPlay ? Time::GetDeltaTime() : static_cast<float>(seconds);
  for (size_t i = 0; i < behaviours_.size(); i++) {
    Behaviour* behaviour = behaviours_[i].get();
    if (behaviour->started_ && behaviour->IsActiveAndEnabled()) behaviour->OnFrame(dt);
  }
  for (size_t i = 0; i < behaviours_.size(); i++) {
    Behaviour* behaviour = behaviours_[i].get();
    if (behaviour->started_ && behaviour->IsActiveAndEnabled()) {
      behaviour->OnLateFrame(dt);
    }
  }
  DestroyPending();
  if (mode_ == Mode::kPlay) UpdateFrameSystems(dt);
  // ...and bindings pick up what the scripts changed before drawing.
  if (ui) {
    const float dpi = input_manager_ ? input_manager_->GetContentScale() : 1.0f;
    ui_.Update(seconds, glm::vec2(GetWidth(), GetHeight()), dpi);
  }
}

void MapScene::OnTick(double delta_t) {
  if (mode_ != Mode::kPlay) return;
  Time::BeginTick();
  StartPendingBehaviours();
  DispatchCollisions();
  const float dt = Time::GetDeltaTime();
  for (size_t i = 0; i < behaviours_.size(); i++) {
    Behaviour* behaviour = behaviours_[i].get();
    if (behaviour->started_ && behaviour->IsActiveAndEnabled()) behaviour->OnTick(dt);
  }
  DestroyPending();
  Time::EndTick();
}

void MapScene::OnDestroy() {
  for (auto& behaviour : behaviours_) {
    if (behaviour->awoken_) behaviour->OnDestroy();
  }
  behaviours_.clear();
  behaviours_by_entity_.clear();
  running_ = false;
}

void MapScene::SetupAnimator(kEntityId entity) {
  auto animator = object_manager_->GetProperty<Animator>(entity);
  if (!animator || !animator->controller.IsSet() || !project_) return;

  // The controller (loaded once per file).
  std::shared_ptr<const AnimatorController> controller;
  if (auto it = controllers_.find(animator->controller.path); it != controllers_.end()) {
    controller = it->second;
  } else {
    std::string error;
    controller = AnimatorController::Load(project_->Resolve(animator->controller.path).string(),
                                          &error);
    if (!controller) PDebug::error("Could not load animator controller: {}", error);
    controllers_.emplace(animator->controller.path, controller);
  }
  if (!controller) return;

  // The model it animates: the target, or the nearest model below.
  kEntityId target = entt::null;
  if (animator->target.IsSet()) {
    target = Find(animator->target.path).GetEntity();
  } else {
    std::vector<kEntityId> queue = {entity};
    for (size_t i = 0; i < queue.size() && target == entt::null; i++) {
      auto mesh = object_manager_->GetProperty<RenderMesh>(queue[i]);
      if (mesh && mesh->model && !mesh->model->Clips().empty()) {
        target = queue[i];
        break;
      }
      if (auto info = object_manager_->GetProperty<NodeInfo>(queue[i])) {
        queue.insert(queue.end(), info->children.begin(), info->children.end());
      }
    }
  }
  auto mesh = target != entt::null ? object_manager_->GetProperty<RenderMesh>(target) : nullptr;
  if (!mesh || !mesh->model) {
    PDebug::warn("{}: Animator has no model to animate", GetGameObject(entity).GetPath());
    return;
  }
  animator->runtime_ = std::make_shared<AnimatorRuntime>(controller, mesh->model);
  mesh->pose_entity = entity;
  ModelPose pose;
  object_manager_->AddProperty(entity, std::move(pose));
}

void MapScene::UpdateFrameSystems(float dt) {
  auto animators = GetRegistry().view<Animator, ModelPose>();
  for (auto [entity, animator, pose] : animators.each()) {
    if (!animator.runtime_ || !animator.enabled || !IsActiveInHierarchy(entity)) continue;
    animator.runtime_->SetHoldRoot(!animator.apply_root_motion);
    animator.runtime_->Update(dt * animator.speed, pose.locals);
    animator.runtime_->GetModel().ComputeGlobals(pose.locals, pose.globals);
    pose.valid = true;
  }

  if (AudioSystem* audio = GetAudio()) {
    // The listener follows the first enabled AudioListener.
    for (kEntityId entity : entities_) {
      auto listener = object_manager_->GetProperty<AudioListener>(entity);
      if (!listener || !listener->enabled || !IsActiveInHierarchy(entity)) continue;
      auto transform = object_manager_->GetProperty<Transform>(entity);
      const glm::quat rotation = transform->GetRotationQuat();
      audio->SetListener(transform->GetPosition(), rotation * glm::vec3(0, 0, -1),
                         rotation * glm::vec3(0, 1, 0));
      break;
    }
    auto view = GetRegistry().view<AudioSource, Transform>();
    for (auto [entity, source, transform] : view.each()) {
      if (source.spatial_blend > 0.5f) audio->SetPosition(source.source_, transform.GetPosition());
    }
    audio->Update();
  }
}

RenderSettings MapScene::GetRenderSettings() const {
  RenderSettings settings;
  if (!project_) return settings;
  const mtext::Section* render = project_->GetDocument().Find("render");
  std::string profile_path;
  if (render) {
    const auto& p = render->properties;
    settings.post_processing = p.GetBool("post_processing", false);
    settings.ssao_intensity = static_cast<float>(p.GetFloat("ssao_intensity", 0.0));
    settings.ssao_radius = static_cast<float>(p.GetFloat("ssao_radius", 0.3));
    settings.ssao_samples = static_cast<int>(p.GetInt("ssao_samples", 12));
    settings.fxaa = p.GetBool("fxaa", false);
    settings.outline_layers = static_cast<uint32_t>(p.GetInt("outline_layers", 0));
    settings.outline_width = static_cast<float>(p.GetFloat("outline_width", 3.0));
    if (auto color = p.Get("outline_color")) {
      if (auto c = color->Get<mtext::Color>()) settings.outline_color = c->rgba;
    }
    if (auto profile = p.Get("profile")) {
      if (auto res = profile->Get<mtext::Resource>()) profile_path = res->path;
      if (auto text = profile->ToString()) profile_path = *text;
    }
  }
  // A global volume in the map overrides the project's default profile.
  for (kEntityId entity : entities_) {
    auto volume = object_manager_->GetProperty<PostProcessVolume>(entity);
    if (!volume || volume->weight < 0.5f || !IsActiveInHierarchy(entity)) continue;
    if (volume->profile.IsSet()) profile_path = volume->profile.path;
    settings.post_processing = true;
    break;
  }
  if (!profile_path.empty()) {
    settings.post_processing = true;
    const auto profile = assets_->LoadAs<PostProcessProfile>(profile_path);
    settings.tonemap = static_cast<int>(profile.tonemap);
    settings.exposure = profile.exposure;
    settings.bloom_intensity = profile.bloom_intensity;
    settings.bloom_threshold = profile.bloom_threshold;
    settings.bloom_scatter = profile.bloom_scatter;
    settings.vignette_intensity = profile.vignette_intensity;
    settings.vignette_smoothness = profile.vignette_smoothness;
    settings.vignette_center = profile.vignette_center;
  }
  return settings;
}

void MapScene::GetRenderTextureSize(std::string_view res_path, int& width, int& height) {
  width = height = 1024;
  if (res_path.empty()) return;
  const auto asset = assets_->LoadAs<RenderTextureAsset>(res_path);
  width = asset.width;
  height = asset.height;
}

std::vector<RenderCamera> MapScene::GetRenderCameras(float width, float height) {
  std::vector<RenderCamera> result;
  if (mode_ != Mode::kPlay) return result;

  struct Candidate {
    kEntityId entity;
    CameraComponent* settings;
  };
  std::vector<Candidate> bases, overlays, textures;
  for (kEntityId entity : entities_) {
    auto settings = object_manager_->GetProperty<CameraComponent>(entity);
    if (!settings || !settings->enabled || !IsActiveInHierarchy(entity)) continue;
    if (!object_manager_->GetProperty<Camera>(entity)) continue;
    if (settings->target_texture.IsSet()) {
      textures.push_back({entity, settings});
    } else if (settings->overlay) {
      overlays.push_back({entity, settings});
    } else {
      bases.push_back({entity, settings});
    }
  }
  auto by_depth = [](const Candidate& a, const Candidate& b) {
    return a.settings->depth < b.settings->depth;
  };
  std::stable_sort(bases.begin(), bases.end(), by_depth);
  std::stable_sort(overlays.begin(), overlays.end(), by_depth);
  std::stable_sort(textures.begin(), textures.end(), by_depth);

  // Unity draws every base camera in depth order; the last one covers the
  // screen. Cameras marked current win over others at the same depth.
  const Candidate* main = nullptr;
  for (const auto& candidate : bases) {
    if (!candidate.settings->current) continue;
    if (!main || candidate.settings->depth >= main->settings->depth) main = &candidate;
  }
  for (const auto& candidate : bases) {
    if (!main || candidate.settings->depth > main->settings->depth) main = &candidate;
  }

  auto make = [&](const Candidate& candidate, float w, float h) {
    RenderCamera camera;
    camera.entity = candidate.entity;
    auto cam = object_manager_->GetProperty<Camera>(candidate.entity);
    auto transform = object_manager_->GetProperty<Transform>(candidate.entity);
    cam->SetFOV(candidate.settings->fov);
    cam->SetClipPlanes(candidate.settings->near_clip, candidate.settings->far_clip);
    camera.view = cam->GetView(*transform);
    camera.projection = cam->GetProjection(w, h);
    camera.position = transform->GetInterpolatedPosition();
    camera.near_clip = candidate.settings->near_clip;
    camera.far_clip = candidate.settings->far_clip;
    camera.culling_mask = static_cast<uint32_t>(candidate.settings->culling_mask);
    camera.clear = static_cast<int>(candidate.settings->clear);
    if (candidate.settings->skybox.IsSet()) camera.clear = static_cast<int>(CameraClear::kSkybox);
    camera.background = candidate.settings->background;
    camera.overlay = candidate.settings->overlay;
    camera.post_processing = candidate.settings->post_processing;
    return camera;
  };

  for (const auto& candidate : textures) {
    int w = 1024, h = 1024;
    GetRenderTextureSize(candidate.settings->target_texture.path, w, h);
    RenderCamera camera = make(candidate, static_cast<float>(w), static_cast<float>(h));
    camera.target_texture = candidate.settings->target_texture.path;
    camera.target_width = w;
    camera.target_height = h;
    result.push_back(camera);
  }
  if (main) {
    if (player_camera_ != main->entity) {
      SetActiveCamera(main->entity);
      ApplyEnvironmentFrom(main->entity);
    }
    result.push_back(make(*main, width, height));
    for (const auto& candidate : overlays) {
      if (candidate.settings->base_camera.IsSet()) {
        GameObject base = Find(candidate.settings->base_camera.path);
        if (!base || base.GetEntity() != main->entity) continue;
      }
      result.push_back(make(candidate, width, height));
    }
  }
  return result;
}

// ---------------------------------------------------- Runtime objects ---

std::string MapScene::UniqueChildPath(kEntityId parent, std::string_view name) const {
  std::string prefix;
  if (parent != entt::null && parent != root_entity_) {
    if (auto info = object_manager_->GetProperty<NodeInfo>(parent)) {
      prefix = info->path + "/";
    }
  }
  const std::string clean = SanitizeNodeName(name);
  std::string candidate = prefix + clean;
  for (int i = 1; by_path_.contains(candidate); i++) {
    candidate = prefix + clean + " (" + std::to_string(i) + ")";
  }
  return candidate;
}

GameObject MapScene::InstantiateNode(const MapNode& node, GameObject parent,
                                     std::string_view name) {
  kEntityId parent_entity =
      parent.IsValid() ? parent.GetEntity() : root_entity_;
  const std::string path =
      UniqueChildPath(parent_entity, name.empty() ? node.name : name);
  // NodePaths inside the copy are relative to its root, as in a prefab.
  auto copy = Map::CloneTree(node);
  Map::RebaseNodePaths(*copy, path);
  std::vector<kEntityId> created;
  kEntityId entity = BuildSubtree(*copy, parent_entity, path, created);
  FinishCreated(created);
  return {this, entity};
}

GameObject MapScene::InstantiatePrefab(std::string_view res_path, glm::vec3 position,
                                       glm::quat rotation, GameObject parent,
                                       bool world_space) {
  const Map* prefab = GetPrefab(res_path);
  if (!prefab) return {};
  const MapNode& root = *prefab->GetRoot();
  kEntityId parent_entity = parent.IsValid() ? parent.GetEntity() : root_entity_;
  const std::string path = UniqueChildPath(parent_entity, root.name + "(Clone)");
  auto copy = Map::CloneTree(root);
  Map::RebaseNodePaths(*copy, path);
  // Place the root before bodies and scripts see it.
  if (auto* transform = copy->FindComponent("Transform")) {
    if (!world_space) {
      transform->properties.Set("position", position);
      transform->properties.Set("rotation", Transform::QuatToEuler(rotation));
    }
  }
  std::vector<kEntityId> created;
  kEntityId entity = BuildSubtree(*copy, parent_entity, path, created);
  if (world_space) {
    if (auto transform = object_manager_->GetProperty<Transform>(entity)) {
      transform->SetPosition(position);
      transform->SetRotate(Transform::QuatToEuler(rotation));
      transform->SnapInterpolation();
      transform->ClearTransformations();
      UpdateEntityTransformHierarchy(entity);
      // Children were placed relative to the old pose; snap them too.
      std::function<void(kEntityId)> snap = [&](kEntityId e) {
        auto info = object_manager_->GetProperty<NodeInfo>(e);
        if (!info) return;
        for (kEntityId child : info->children) {
          if (auto t = object_manager_->GetProperty<Transform>(child)) {
            t->SnapInterpolation();
            t->ClearTransformations();
          }
          snap(child);
        }
      };
      snap(entity);
    }
  }
  FinishCreated(created);
  return {this, entity};
}

GameObject MapScene::CreateGameObject(std::string_view name, GameObject parent) {
  MapNode node;
  node.name = std::string(name);
  return InstantiateNode(node, parent, name);
}

bool MapScene::AddComponent(GameObject object, std::string_view type,
                            const mtext::PropertyList& properties) {
  if (!object.IsValid() || object.GetScene() != this) return false;
  const ComponentInfo* info = TypeRegistry::Instance().FindComponent(type);
  if (!info || !info->attach) {
    PDebug::warn("{}: cannot add unknown component \"{}\"", object.GetPath(), type);
    return false;
  }
  if (info->is_script && mode_ == Mode::kEdit) return false;
  const kEntityId entity = object.GetEntity();
  info->attach(*this, entity, properties);
  static const std::set<std::string, std::less<>> kPhysicsTypes = {
      "Rigidbody",     "BoxCollider",  "SphereCollider",     "CapsuleCollider",
      "CylinderCollider", "PlaneCollider", "MeshCollider", "CharacterController"};
  if (kPhysicsTypes.contains(type)) CreatePhysicsBody(entity);
  if (info->is_script) AwakeBehaviours();
  return true;
}

void MapScene::RemoveComponent(GameObject object, std::string_view type) {
  if (!object.IsValid()) return;
  const kEntityId entity = object.GetEntity();
  if (type == "MeshRenderer") {
    object_manager_->GetRegistry().remove<MeshRenderer, Mesh, TextureRef>(entity);
  } else if (type == "Rigidbody") {
    object_manager_->GetRegistry().remove<Rigidbody>(entity);
    CreatePhysicsBody(entity);
  } else if (type.ends_with("Collider")) {
    object_manager_->GetRegistry().remove<ColliderSet, BoxCollider, SphereCollider,
                                          CapsuleCollider, CylinderCollider,
                                          MeshCollider, PlaneCollider>(entity);
    CreatePhysicsBody(entity);
  } else if (type == "CharacterController") {
    object_manager_->GetRegistry().remove<CharacterController>(entity);
    physics_engine_->RemoveCharacter(entity);
  }
}

bool MapScene::IsActiveInHierarchy(kEntityId entity) const {
  if (!GetRegistry().valid(entity)) return false;
  if (auto info = object_manager_->GetProperty<NodeInfo>(entity)) {
    return info->active_in_hierarchy;
  }
  return true;
}

void MapScene::SetActive(kEntityId entity, bool active) {
  auto info = object_manager_->GetProperty<NodeInfo>(entity);
  if (!info || info->active == active) return;
  info->active = active;
  const bool parent_active =
      info->parent == entt::null || IsActiveInHierarchy(info->parent);
  RefreshActive(entity, parent_active);
  if (mode_ == Mode::kPlay) AwakeBehaviours();
}

void MapScene::RefreshActive(kEntityId entity, bool parent_active) {
  auto info = object_manager_->GetProperty<NodeInfo>(entity);
  if (!info) return;
  const bool now = parent_active && info->active;
  if (now != info->active_in_hierarchy) {
    info->active_in_hierarchy = now;
    if (now) {
      GetRegistry().remove<InactiveTag>(entity);
    } else {
      object_manager_->AddProperty(entity, InactiveTag{});
    }
    if (auto transform = object_manager_->GetProperty<Transform>(entity)) {
      transform->MarkDirty();
    }
    if (mode_ == Mode::kPlay) {
      physics_engine_->SetBodyEnabled(entity, now);
      for (Behaviour* behaviour : GetBehaviours(entity)) {
        OnBehaviourEnabledChanged(*behaviour);
      }
    }
  }
  // Copy: OnEnable/OnDisable may change the hierarchy.
  const std::vector<kEntityId> children = info->children;
  for (kEntityId child : children) RefreshActive(child, now);
}

void MapScene::SetLayer(kEntityId entity, int layer) {
  auto info = object_manager_->GetProperty<NodeInfo>(entity);
  if (!info) return;
  info->layer = layer;
  physics_engine_->SetLayer(entity, layer);
}

int MapScene::LayerFromName(std::string_view name) const {
  return project_ ? project_->LayerFromName(name) : -1;
}

std::string MapScene::LayerToName(int layer) const {
  return project_ ? project_->LayerName(layer) : "";
}

GameObject MapScene::FindWithTag(std::string_view tag) {
  for (kEntityId entity : entities_) {
    auto info = object_manager_->GetProperty<NodeInfo>(entity);
    if (info && info->tag == tag && info->active_in_hierarchy) return {this, entity};
  }
  return {};
}

std::vector<GameObject> MapScene::FindGameObjectsWithTag(std::string_view tag) {
  std::vector<GameObject> result;
  for (kEntityId entity : entities_) {
    auto info = object_manager_->GetProperty<NodeInfo>(entity);
    if (info && info->tag == tag && info->active_in_hierarchy) {
      result.emplace_back(this, entity);
    }
  }
  return result;
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

  UpdatePaths(entity);
  UpdateEntityTransformHierarchy(entity);
  // A moved object takes the active state of its new parent.
  const bool parent_active = new_parent == entt::null || IsActiveInHierarchy(new_parent);
  RefreshActive(entity, parent_active);
}

void MapScene::UpdatePaths(kEntityId entity) {
  auto info = object_manager_->GetProperty<NodeInfo>(entity);
  if (!info || entity == root_entity_) return;
  std::string parent_path;
  if (HasParent(entity)) {
    if (auto parent = object_manager_->GetProperty<NodeInfo>(info->parent)) {
      parent_path = parent->path;
    }
  }
  std::string path = parent_path.empty() ? info->name : parent_path + "/" + info->name;
  if (path != info->path) {
    // Keep paths unique, as the map does.
    if (by_path_.contains(path) && by_path_[path] != entity) {
      for (int i = 1;; i++) {
        std::string candidate = path + " (" + std::to_string(i) + ")";
        if (!by_path_.contains(candidate)) {
          path = candidate;
          info->name = info->name + " (" + std::to_string(i) + ")";
          break;
        }
      }
    }
    if (auto it = by_path_.find(info->path); it != by_path_.end() && it->second == entity) {
      by_path_.erase(it);
    }
    info->path = path;
    by_path_[path] = entity;
  }
  const std::vector<kEntityId> children = info->children;
  for (kEntityId child : children) UpdatePaths(child);
}

GameObject MapScene::Find(std::string_view path) {
  if (path.empty()) path = ".";
  auto it = by_path_.find(std::string(path));
  if (it != by_path_.end()) return {this, it->second};
  // Unity's GameObject.Find("Name") searches every active object.
  if (path.find('/') == std::string_view::npos) {
    for (kEntityId entity : entities_) {
      auto info = object_manager_->GetProperty<NodeInfo>(entity);
      if (info && info->name == path && info->active_in_hierarchy) {
        return {this, entity};
      }
    }
  }
  return {};
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

void MapScene::AddBehaviour(kEntityId entity, std::unique_ptr<Behaviour> behaviour,
                            std::string type_name) {
  behaviour->scene_ = this;
  behaviour->entity_ = entity;
  behaviour->type_name_ = std::move(type_name);
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
    if (!GetRegistry().valid(root)) continue;
    if (!object_manager_->GetProperty<NodeInfo>(root)) continue;

    // The object and everything below it, deepest first.
    std::vector<kEntityId> doomed;
    std::function<void(kEntityId)> collect = [&](kEntityId entity) {
      doomed.push_back(entity);
      if (auto node = object_manager_->GetProperty<NodeInfo>(entity)) {
        for (kEntityId child : node->children) {
          if (GetRegistry().valid(child)) collect(child);
        }
      }
    };
    collect(root);
    std::reverse(doomed.begin(), doomed.end());

    for (kEntityId entity : doomed) {
      auto it = behaviours_by_entity_.find(entity);
      if (it != behaviours_by_entity_.end()) {
        const auto behaviours = it->second;
        for (Behaviour* behaviour : behaviours) {
          if (behaviour->enable_called_) {
            behaviour->enable_called_ = false;
            behaviour->OnDisable();
          }
          if (behaviour->awoken_) behaviour->OnDestroy();
        }
        for (Behaviour* behaviour : behaviours) {
          auto owned = std::find_if(
              behaviours_.begin(), behaviours_.end(),
              [&](const auto& b) { return b.get() == behaviour; });
          if (owned != behaviours_.end()) behaviours_.erase(owned);
        }
        behaviours_by_entity_.erase(entity);
      }
      if (auto node = object_manager_->GetProperty<NodeInfo>(entity)) {
        if (node->parent != entt::null && GetRegistry().valid(node->parent)) {
          if (auto p_info = object_manager_->GetProperty<NodeInfo>(node->parent)) {
            std::erase(p_info->children, entity);
          }
        }
        if (auto path = by_path_.find(node->path);
            path != by_path_.end() && path->second == entity) {
          by_path_.erase(path);
        }
        by_uid_.erase(node->uid);
      }
      if (player_camera_ == entity) SetActiveCamera(default_camera_);
      DestroyNodeEntity(entity);
      std::erase(entities_, entity);
    }
  }
}

std::shared_ptr<const Model> MapScene::GetModelAsset(std::string_view res_path) {
  if (res_path.empty() || !project_) return nullptr;
  if (auto it = model_assets_.find(res_path); it != model_assets_.end()) {
    return it->second;
  }
  const std::string file = project_->Resolve(res_path).string();
  ModelImportSettings defaults;
  defaults.convention = project_->GetModelConvention() == "unity"
                            ? ModelConvention::kUnity
                            : ModelConvention::kNative;
  std::string error;
  std::shared_ptr<const Model> model =
      Model::Load(file, ModelImportSettings::ForFile(file, defaults), &error);
  if (!model) PDebug::warn("Could not load model {}: {}", res_path, error);
  model_assets_.emplace(std::string(res_path), model);
  return model;
}

std::shared_ptr<ImageData> MapScene::GetImage(std::string_view res_path) {
  if (res_path.empty() || !project_) return nullptr;
  if (auto it = images_.find(res_path); it != images_.end()) return it->second;
  auto image = ImageData::Load(project_->Resolve(res_path).string());
  if (!image) PDebug::warn("Could not load image {}", res_path);
  images_.emplace(std::string(res_path), image);
  return image;
}

std::shared_ptr<const MeshGeometry> MapScene::GetPrimitive(MeshShape shape,
                                                           glm::vec3 mesh_scale) {
  const std::string key = std::to_string(static_cast<int>(shape)) + ":" +
                          std::to_string(mesh_scale.x) + "," +
                          std::to_string(mesh_scale.y) + "," +
                          std::to_string(mesh_scale.z);
  if (auto it = primitives_.find(key); it != primitives_.end()) return it->second;
  std::optional<Mesh> mesh;
  switch (shape) {
    case MeshShape::kCube: mesh = Primitives::Cube(glm::vec4(1.0f)); break;
    case MeshShape::kSphere: mesh = Primitives::Sphere(glm::vec4(1.0f)); break;
    case MeshShape::kCapsule: mesh = Primitives::Capsule(glm::vec4(1.0f)); break;
    case MeshShape::kCylinder: mesh = Primitives::Cylinder(glm::vec4(1.0f)); break;
    case MeshShape::kPlane: mesh = Primitives::Plane(glm::vec4(1.0f)); break;
    case MeshShape::kQuad: mesh = Primitives::Quad(glm::vec4(1.0f)); break;
    case MeshShape::kModel: return nullptr;
  }
  std::vector<ModelVertex> vertices;
  vertices.reserve(mesh->vertices_.size());
  for (const auto& v : mesh->vertices_) {
    ModelVertex vertex;
    vertex.position = v.position * mesh_scale;
    vertex.normal = v.normal;
    vertex.uv = v.uv;
    vertices.push_back(vertex);
  }
  auto geometry = MeshGeometry::Create(std::move(vertices), mesh->indices_);
  primitives_.emplace(key, geometry);
  return geometry;
}

bool MapScene::GetModelTriangles(std::string_view res_path, std::string_view node,
                                 std::vector<glm::vec3>& vertices,
                                 std::vector<uint32_t>& indices) {
  auto model = GetModelAsset(res_path);
  if (!model) return false;
  int index = node.empty() ? 0 : model->FindNode(node);
  if (index < 0) {
    PDebug::warn("{} has no node \"{}\"", res_path, node);
    return false;
  }
  // The whole model (node "") includes the root's transform only when the
  // root is not the object's own (native convention).
  model->CollectTriangles(index, node.empty(), vertices, indices);
  if (node.empty() && !model->RootOnObject()) {
    const glm::mat4 root = model->BindGlobals()[0];
    for (auto& v : vertices) v = glm::vec3(root * glm::vec4(v, 1.0f));
  }
  return true;
}

void MapScene::BuildTextMesh(kEntityId entity) {
  auto text = object_manager_->GetProperty<TextMesh>(entity);
  if (!text) return;
  const std::string font_path =
      text->font.IsSet() ? text->font.path : "mercury://fonts/LiberationSans-Regular.ttf";
  std::shared_ptr<UI::Font> font = ui_.resources().LoadFont(font_path);
  if (!font) font = ui_.resources().DefaultFont();
  RenderMesh mesh;
  mesh.visible = text->visible;
  mesh.cast_shadows = false;
  if (!font || text->text.empty()) {
    object_manager_->AddProperty(entity, std::move(mesh));
    return;
  }

  // Lay out in local units: one em is font_size / 10.
  UI::TextSettings settings;
  settings.font = font.get();
  settings.size = text->font_size * 0.1f;
  settings.color = UI::Color(1.0f, 1.0f, 1.0f, 1.0f);
  settings.rich_text = text->rich_text;
  settings.white_space = text->word_wrap ? UI::WhiteSpace::kNormal : UI::WhiteSpace::kNoWrap;
  const int align = static_cast<int>(text->alignment);
  const int column = align % 3;  // 0 left, 1 center, 2 right
  const int row = align / 3;     // 0 top, 1 middle, 2 bottom
  static constexpr UI::TextAnchor kAnchors[3] = {UI::TextAnchor::kUpperLeft,
                                                 UI::TextAnchor::kUpperCenter,
                                                 UI::TextAnchor::kUpperRight};
  settings.align = kAnchors[column];
  const float width = std::max(text->size.x, 1e-3f);
  const UI::TextLayout layout = UI::LayoutText(text->text, settings, width);

  // The box is centered on the node; +Y up, text faces +Z.
  const float half_w = width * 0.5f;
  const float half_h = text->size.y * 0.5f;
  float top = half_h;
  if (row == 1) top = layout.size.y * 0.5f;
  if (row == 2) top = -half_h + layout.size.y;
  const auto& atlas = font->atlas();
  std::vector<ModelVertex> vertices;
  std::vector<uint32_t> indices;
  for (const auto& glyph : layout.glyphs) {
    if (glyph.size.x <= 0.0f || glyph.size.y <= 0.0f) continue;
    const float x0 = -half_w + glyph.position.x;
    const float x1 = x0 + glyph.size.x;
    const float y0 = top - glyph.position.y;
    const float y1 = y0 - glyph.size.y;
    const glm::vec4 c = glyph.color.rgba;
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    const glm::vec3 n(0.0f, 0.0f, 1.0f);
    auto add = [&](float x, float y, float u, float v) {
      ModelVertex vertex;
      vertex.position = glm::vec3(x, y, 0.0f);
      vertex.normal = n;
      vertex.uv = glm::vec2(u, v);
      vertex.color = c;
      vertices.push_back(vertex);
    };
    add(x0, y0, glyph.uv.x, glyph.uv.y);
    add(x1, y0, glyph.uv.z, glyph.uv.y);
    add(x1, y1, glyph.uv.z, glyph.uv.w);
    add(x0, y1, glyph.uv.x, glyph.uv.w);
    for (uint32_t i : {0u, 2u, 1u, 0u, 3u, 2u}) indices.push_back(base + i);
  }

  // The atlas as it is now (glyphs were just added to it).
  const auto key = std::make_pair(atlas.id, atlas.version);
  std::shared_ptr<ImageData> image;
  if (auto it = text_atlases_.find(key); it != text_atlases_.end()) {
    image = it->second;
  } else {
    image = ImageData::FromPixels(atlas.width, atlas.height, atlas.rgba, font_path);
    image->srgb = false;
    text_atlases_[key] = image;
  }

  RenderDraw draw;
  draw.geometry = MeshGeometry::Create(std::move(vertices), std::move(indices));
  mesh.draws.push_back(draw);
  RenderMaterial material;
  material.color = text->color;
  material.texture = image;
  material.transparent = true;
  material.double_sided = true;
  material.unlit = true;
  material.sdf_text = true;
  mesh.materials.push_back(material);
  mesh.ComputeBounds();
  object_manager_->AddProperty(entity, std::move(mesh));
}

RenderMaterial MapScene::MakeRenderMaterial(const MaterialAsset& material, glm::vec4 color) {
  RenderMaterial result;
  result.color = material.color * color;
  if (material.texture.IsSet()) result.texture = GetImage(material.texture.path);
  result.render_texture = material.render_texture.path;
  result.tiling = material.tiling;
  result.offset = material.offset;
  result.metallic = material.metallic;
  result.smoothness = 1.0f - material.roughness;
  result.emission = glm::vec3(material.emission);
  result.transparent = material.surface == SurfaceType::kTransparent;
  result.double_sided = material.cull == CullMode::kOff;
  result.unlit = material.unlit;
  return result;
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
