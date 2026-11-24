// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Registers the engine's built-in components and asset types with the
// TypeRegistry, along with how MapScene attaches each one to an entity.

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "animation/animator.h"
#include "audio/audio_system.h"
#include "components/components.h"
#include "properties/camera.h"
#include "properties/primitives.h"
#include "properties/render_mesh.h"
#include "scene/map_scene.h"
#include "scripting/type_registry.h"
#include "ui/ui_document.h"

namespace Mercury {

namespace {

template <typename T>
ComponentInfo MakeInfo(const char* name, const char* category,
                       const char* description, AfterSetFn after_set = {}) {
  ComponentInfo info;
  info.name = name;
  info.category = category;
  info.description = description;
  info.fields = DescribeFields<T>();
  info.after_set = after_set;
  info.has = [](MapScene& scene, kEntityId entity) {
    return scene.Objects().GetProperty<T>(entity) != nullptr;
  };
  info.get_field = [](MapScene& scene, kEntityId entity, std::string_view field,
                      mtext::Variant* out) {
    T* value = scene.Objects().GetProperty<T>(entity);
    if (!value) return false;
    auto reflector = Reflector::Get(field, out);
    value->Reflect(reflector);
    return reflector.Found();
  };
  info.set_field = [after_set](MapScene& scene, kEntityId entity,
                               std::string_view field,
                               const mtext::Variant& in) {
    T* value = scene.Objects().GetProperty<T>(entity);
    if (!value) return false;
    auto reflector = Reflector::Set(field, in);
    value->Reflect(reflector);
    for (const auto& warning : reflector.GetWarnings()) {
      PDebug::warn("{}", warning);
    }
    if (!reflector.Applied()) return false;
    if (after_set) after_set(scene, entity, field);
    return true;
  };
  return info;
}

// Most built-ins are plain data that MapScene (or the physics setup) reads
// later, so attaching just stores the reflected struct on the entity.
template <typename T>
AttachFn StoreAsProperty(const char* name) {
  return [name](MapScene& scene, kEntityId entity,
                const mtext::PropertyList& properties) {
    auto value = ReadFields<T>(
        properties, scene.GetGameObject(entity).GetPath() + "." + name);
    scene.Objects().AddProperty(entity, value);
  };
}

// Filament uses only the low 15 bits of a blend order. Bias Unity's signed
// sorting order into that range, keeping the neutral order in the middle so
// default sprites and meshes share one level and fall back to depth sorting.
constexpr int kBlendOrderNeutral = 16384;
constexpr int kSortingOrderMin = -kBlendOrderNeutral;
constexpr int kSortingOrderMax = kBlendOrderNeutral - 1;

uint16_t BlendOrderFor(int sorting_order) {
  return static_cast<uint16_t>(
      std::clamp(sorting_order, kSortingOrderMin, kSortingOrderMax) +
      kBlendOrderNeutral);
}

// A sprite's quad as RenderMesh geometry, so every renderer draws sprites
// through the same path as MeshRenderer.

std::shared_ptr<const MeshGeometry> MakeSpriteQuad(glm::vec4 color) {
  std::vector<ModelVertex> vertices(4);
  vertices[0].position = glm::vec3(0.5f, 0.5f, 0.0f);
  vertices[0].uv = glm::vec2(1.0f, 0.0f);
  vertices[1].position = glm::vec3(0.5f, -0.5f, 0.0f);
  vertices[1].uv = glm::vec2(1.0f, 1.0f);
  vertices[2].position = glm::vec3(-0.5f, -0.5f, 0.0f);
  vertices[2].uv = glm::vec2(0.0f, 1.0f);
  vertices[3].position = glm::vec3(-0.5f, 0.5f, 0.0f);
  vertices[3].uv = glm::vec2(0.0f, 0.0f);
  for (ModelVertex& vertex : vertices) vertex.color = color;
  // Counter-clockwise seen from +Z, so the quad faces a camera looking
  // down -Z (the same convention as the primitives).
  return MeshGeometry::Create(std::move(vertices), {0, 3, 1, 1, 3, 2});
}

// Materials for a MeshRenderer, in draw-slot order. When given, `mesh_indices`
// receives the model meshes in that order and `base_node` the node the draws
// are placed relative to; rebuilding them from the renderer also refreshes a
// runtime color change.
std::vector<RenderMaterial> MakeMeshMaterials(
    MapScene& scene, const MeshRenderer& renderer, const std::string& path,
    std::vector<int>* mesh_indices = nullptr, int* base_node = nullptr) {
  const bool has_material = renderer.material.IsSet();
  const auto material =
      scene.Assets().LoadAs<MaterialAsset>(renderer.material.path);

  // Unity's rule: the renderer's material list (one per submesh) wins, then
  // a single material, then the model's own materials.
  auto material_for = [&](size_t index, const ModelMaterial* own) {
    if (index < renderer.materials.size() &&
        renderer.materials[index].IsSet()) {
      return scene.MakeRenderMaterial(
          scene.Assets().LoadAs<MaterialAsset>(renderer.materials[index].path),
          renderer.color);
    }
    if (has_material || !own)
      return scene.MakeRenderMaterial(material, renderer.color);
    RenderMaterial result;
    result.color = own->color * renderer.color;
    result.texture = own->texture;
    result.metallic = own->metallic;
    result.smoothness = own->smoothness;
    result.emission = own->emission;
    result.emission_texture = own->emission_texture;
    result.normal_texture = own->normal_texture;
    result.normal_scale = own->normal_scale;
    result.transparent = own->transparent;
    result.double_sided = own->double_sided;
    result.transmission = own->transmission;
    result.ior = own->ior;
    result.transmission_color = own->transmission_color;
    result.thickness = own->thickness;
    result.reflectance = own->reflectance;
    return result;
  };

  std::vector<RenderMaterial> materials;
  if (renderer.mesh != MeshShape::kModel) {
    materials.push_back(material_for(0, nullptr));
    return materials;
  }
  if (!renderer.model.IsSet()) return materials;
  auto model = scene.GetModelAsset(renderer.model.path);
  if (!model) return materials;

  std::vector<int> indices;
  if (renderer.node.empty()) {
    for (size_t i = 0; i < model->Meshes().size(); i++) {
      indices.push_back(static_cast<int>(i));
    }
  } else {
    const int node = model->FindNode(renderer.node);
    if (base_node) *base_node = node;
    if (node < 0) {
      PDebug::warn("{}: {} has no node \"{}\"", path, renderer.model.path,
                   renderer.node);
      return materials;
    }
    indices = model->Nodes()[node].meshes;
  }
  // Unity numbers a node's submeshes in material-slot order.
  std::stable_sort(indices.begin(), indices.end(), [&](int a, int b) {
    const auto& ma = model->Meshes()[a];
    const auto& mb = model->Meshes()[b];
    return std::pair(ma.node, ma.material) < std::pair(mb.node, mb.material);
  });

  if (mesh_indices) *mesh_indices = indices;
  for (int index : indices) {
    materials.push_back(
        material_for(materials.size(),
                     &model->Materials()[model->Meshes()[index].material]));
  }
  return materials;
}

void SyncRendererField(MapScene& scene, kEntityId entity,
                       std::string_view field) {
  RenderMesh* mesh = scene.Objects().GetProperty<RenderMesh>(entity);
  if (!mesh) return;
  if (auto* sprite = scene.Objects().GetProperty<SpriteRenderer>(entity)) {
    if (field == "visible") {
      mesh->visible = sprite->visible;
    } else if (field == "color") {
      const auto material =
          scene.Assets().LoadAs<MaterialAsset>(sprite->material.path);
      for (RenderMaterial& surface : mesh->materials)
        surface.color = sprite->color * material.color;
    } else if (field == "sorting_order") {
      mesh->blend_order = BlendOrderFor(sprite->sorting_order);
    } else {
      return;
    }
  } else if (auto* renderer =
                 scene.Objects().GetProperty<MeshRenderer>(entity)) {
    if (field == "visible") {
      mesh->visible = renderer->visible;
    } else if (field == "color") {
      mesh->materials = MakeMeshMaterials(
          scene, *renderer, scene.GetGameObject(entity).GetPath());
    } else {
      return;
    }
  } else {
    return;
  }
  mesh->revision++;
}

void AttachSprite(MapScene& scene, kEntityId entity,
                  const mtext::PropertyList& properties) {
  auto sprite = ReadFields<SpriteRenderer>(
      properties, scene.GetGameObject(entity).GetPath() + ".SpriteRenderer");
  scene.Objects().AddProperty(entity, sprite);

  const auto material =
      scene.Assets().LoadAs<MaterialAsset>(sprite.material.path);

  RenderMesh mesh;
  mesh.visible = sprite.visible;
  mesh.blend_order = BlendOrderFor(sprite.sorting_order);
  mesh.cast_shadows = false;  // sprites never cast shadows
  RenderMaterial surface;
  surface.unlit = true;  // sprites are never lit
  surface.transparent = true;
  surface.color = sprite.color * material.color;
  surface.tiling = material.tiling;
  if (material.texture.IsSet()) {
    surface.texture = scene.GetImage(material.texture.path);
  }
  RenderDraw draw;
  draw.geometry = MakeSpriteQuad(surface.color);
  mesh.draws.push_back(draw);
  mesh.materials.push_back(std::move(surface));
  mesh.ComputeBounds();
  scene.Objects().AddProperty(entity, std::move(mesh));
}

void AttachMeshRenderer(MapScene& scene, kEntityId entity,
                        const mtext::PropertyList& properties) {
  const std::string path = scene.GetGameObject(entity).GetPath();
  auto renderer = ReadFields<MeshRenderer>(properties, path + ".MeshRenderer");
  scene.Objects().AddProperty(entity, renderer);

  RenderMesh mesh;
  mesh.visible = renderer.visible;
  mesh.cast_shadows = renderer.cast_shadows;
  mesh.rendering_layers = static_cast<uint32_t>(renderer.rendering_layers);
  std::vector<int> mesh_indices;
  mesh.materials =
      MakeMeshMaterials(scene, renderer, path, &mesh_indices, &mesh.base_node);

  if (renderer.mesh != MeshShape::kModel) {
    RenderDraw draw;
    draw.geometry = scene.GetPrimitive(renderer.mesh, renderer.mesh_scale);
    draw.material = 0;
    mesh.draws.push_back(draw);
  } else {
    if (!renderer.model.IsSet()) return;
    auto model = scene.GetModelAsset(renderer.model.path);
    if (!model) return;
    if (!renderer.node.empty() && mesh.base_node < 0) return;
    mesh.model = model;
    for (size_t slot = 0; slot < mesh_indices.size(); slot++) {
      const ModelMesh& source = model->Meshes()[mesh_indices[slot]];
      RenderDraw draw;
      draw.geometry = source.geometry;
      draw.node = source.node;
      draw.material = static_cast<int>(slot);
      if (!source.bones.empty()) {
        draw.skin = static_cast<int>(mesh.skins.size());
        mesh.skins.push_back(RenderSkin{source.bones});
      }
      mesh.draws.push_back(std::move(draw));
    }
  }
  mesh.ComputeBounds();
  scene.Objects().AddProperty(entity, std::move(mesh));
}

// Colliders: the first of each type is stored as a property (the editor
// draws it), and every one is added to the node's ColliderSet, which the
// physics bodies are built from.
ColliderSet& GetColliderSet(MapScene& scene, kEntityId entity) {
  if (auto set = scene.Objects().GetProperty<ColliderSet>(entity)) return *set;
  scene.Objects().AddProperty(entity, ColliderSet{});
  return *scene.Objects().GetProperty<ColliderSet>(entity);
}

template <typename T>
void StoreFirst(MapScene& scene, kEntityId entity, const T& value) {
  if (!scene.Objects().GetProperty<T>(entity))
    scene.Objects().AddProperty(entity, value);
}

void ApplyMaterial(MapScene& scene, const AssetRef& material, bool is_trigger,
                   ColliderShape& shape) {
  const auto physics =
      scene.Assets().LoadAs<PhysicsMaterialAsset>(material.path);
  shape.friction = physics.friction;
  shape.restitution = physics.restitution;
  shape.is_trigger = is_trigger;
}

void AttachBoxCollider(MapScene& scene, kEntityId entity,
                       const mtext::PropertyList& properties) {
  auto box = ReadFields<BoxCollider>(
      properties, scene.GetGameObject(entity).GetPath() + ".BoxCollider");
  StoreFirst(scene, entity, box);
  ColliderShape shape;
  shape.kind = ColliderShape::Kind::kBox;
  shape.size = box.size;
  shape.center = box.center;
  ApplyMaterial(scene, box.material, box.is_trigger, shape);
  GetColliderSet(scene, entity).shapes.push_back(std::move(shape));
}

void AttachSphereCollider(MapScene& scene, kEntityId entity,
                          const mtext::PropertyList& properties) {
  auto sphere = ReadFields<SphereCollider>(
      properties, scene.GetGameObject(entity).GetPath() + ".SphereCollider");
  StoreFirst(scene, entity, sphere);
  ColliderShape shape;
  shape.kind = ColliderShape::Kind::kSphere;
  shape.radius = sphere.radius;
  shape.center = sphere.center;
  ApplyMaterial(scene, sphere.material, sphere.is_trigger, shape);
  GetColliderSet(scene, entity).shapes.push_back(std::move(shape));
}

void AttachCapsuleCollider(MapScene& scene, kEntityId entity,
                           const mtext::PropertyList& properties) {
  auto capsule = ReadFields<CapsuleCollider>(
      properties, scene.GetGameObject(entity).GetPath() + ".CapsuleCollider");
  StoreFirst(scene, entity, capsule);
  ColliderShape shape;
  shape.kind = ColliderShape::Kind::kCapsule;
  shape.radius = capsule.radius;
  shape.height = capsule.height;
  shape.center = capsule.center;
  ApplyMaterial(scene, capsule.material, capsule.is_trigger, shape);
  GetColliderSet(scene, entity).shapes.push_back(std::move(shape));
}

void AttachCylinderCollider(MapScene& scene, kEntityId entity,
                            const mtext::PropertyList& properties) {
  auto cylinder = ReadFields<CylinderCollider>(
      properties, scene.GetGameObject(entity).GetPath() + ".CylinderCollider");
  StoreFirst(scene, entity, cylinder);
  ColliderShape shape;
  shape.kind = ColliderShape::Kind::kCylinder;
  shape.radius = cylinder.radius;
  shape.height = cylinder.height;
  shape.center = cylinder.center;
  ApplyMaterial(scene, cylinder.material, cylinder.is_trigger, shape);
  GetColliderSet(scene, entity).shapes.push_back(std::move(shape));
}

std::optional<Mesh> PrimitiveMesh(MeshShape shape) {
  switch (shape) {
    case MeshShape::kCube:
      return Primitives::Cube(glm::vec4(1.0f));
    case MeshShape::kSphere:
      return Primitives::Sphere(glm::vec4(1.0f));
    case MeshShape::kCapsule:
      return Primitives::Capsule(glm::vec4(1.0f));
    case MeshShape::kCylinder:
      return Primitives::Cylinder(glm::vec4(1.0f));
    case MeshShape::kPlane:
      return Primitives::Plane(glm::vec4(1.0f));
    case MeshShape::kQuad:
      return Primitives::Quad(glm::vec4(1.0f));
    case MeshShape::kModel:
      return std::nullopt;
  }
  return std::nullopt;
}

void AttachMeshCollider(MapScene& scene, kEntityId entity,
                        const mtext::PropertyList& properties) {
  const std::string path = scene.GetGameObject(entity).GetPath();
  auto collider = ReadFields<MeshCollider>(properties, path + ".MeshCollider");
  StoreFirst(scene, entity, collider);
  ColliderShape shape;
  shape.kind = collider.convex ? ColliderShape::Kind::kConvexHull
                               : ColliderShape::Kind::kMesh;
  if (collider.mesh == MeshShape::kModel) {
    if (!collider.model.IsSet()) return;
    if (!scene.GetModelTriangles(collider.model.path, collider.submesh,
                                 shape.vertices, shape.indices)) {
      return;
    }
  } else if (auto mesh = PrimitiveMesh(collider.mesh)) {
    for (const auto& vertex : mesh->vertices_)
      shape.vertices.push_back(vertex.position);
    shape.indices = mesh->indices_;
  }
  if (shape.vertices.empty()) return;
  for (auto& v : shape.vertices) v *= collider.mesh_scale;
  ApplyMaterial(scene, collider.material, collider.is_trigger, shape);
  GetColliderSet(scene, entity).shapes.push_back(std::move(shape));
}

void AttachCamera(MapScene& scene, kEntityId entity,
                  const mtext::PropertyList& properties) {
  auto settings = ReadFields<CameraComponent>(
      properties, scene.GetGameObject(entity).GetPath() + ".Camera");
  scene.Objects().AddProperty(entity, settings);

  Camera camera;
  camera.SetFOV(settings.fov);
  camera.SetClipPlanes(settings.near_clip, settings.far_clip);
  camera.SetUseObjectRotation(true);
  scene.Objects().AddProperty(entity, camera);

  if (settings.fit_height > 0.0f) {
    // Back the camera off along +Z until fit_height world units fill the
    // view vertically, then look straight down -Z (+X right, +Y up).
    auto transform = scene.Objects().GetProperty<Transform>(entity);
    const float half_fov = glm::radians(settings.fov) / 2.0f;
    const float distance = (settings.fit_height / 2.0f) / std::tan(half_fov);
    auto position = transform->GetPosition();
    transform->SetPosition(
        glm::vec3(position.x, position.y, position.z + distance));
    transform->SetRotate(glm::vec3(0.0f));
    transform->SnapInterpolation();
    transform->ClearTransformations();
  }
}

void AttachUIDocument(MapScene& scene, kEntityId entity,
                      const mtext::PropertyList& properties) {
  auto document = ReadFields<UIDocument>(
      properties, scene.GetGameObject(entity).GetPath() + ".UIDocument");
  // The editor shows UI in the UI Builder, not in the scene view.
  if (!scene.IsEditMode()) {
    auto& ui = scene.GetUI();
    UI::PanelSettings settings;
    if (document.panel_settings.IsSet()) {
      settings = scene.Assets()
                     .LoadAs<PanelSettingsAsset>(document.panel_settings.path)
                     .ToSettings();
    }
    UI::Panel* panel = ui.GetPanel(document.panel_settings.path, settings);
    auto tree = document.source_asset.IsSet()
                    ? ui.resources().LoadUxml(document.source_asset.path)
                    : nullptr;
    document.root = ui.AddDocument(panel, tree, document.sort_order);
    document.panel = panel;
  }
  scene.Objects().AddProperty(entity, document);
}

}  // namespace

void AttachAudioSource(MapScene& scene, kEntityId entity,
                       const mtext::PropertyList& properties) {
  auto source = ReadFields<AudioSource>(
      properties, scene.GetGameObject(entity).GetPath() + ".AudioSource");
  if (AudioSystem* audio = scene.GetAudio()) {
    source.audio_ = audio;
    source.source_ = audio->CreateSource();
    audio->SetVolume(source.source_, source.volume);
    audio->SetPitch(source.source_, source.pitch);
    audio->SetLoop(source.source_, source.loop);
    audio->SetMute(source.source_, source.mute);
    audio->SetSpatial(source.source_, source.spatial_blend, source.min_distance,
                      source.max_distance);
    if (source.clip.IsSet()) audio->SetClip(source.source_, source.clip.path);
  }
  scene.Objects().AddProperty(entity, source);
}

void AttachTextMesh(MapScene& scene, kEntityId entity,
                    const mtext::PropertyList& properties) {
  auto text = ReadFields<TextMesh>(
      properties, scene.GetGameObject(entity).GetPath() + ".TextMesh");
  text.scene_ = &scene;
  text.entity_ = entity;
  scene.Objects().AddProperty(entity, text);
  scene.BuildTextMesh(entity);
}

void TextMesh::SetText(std::string new_text) {
  if (new_text == text) return;
  text = std::move(new_text);
  if (scene_) scene_->BuildTextMesh(entity_);
}

void TextMesh::SetColor(glm::vec4 new_color) {
  color = new_color;
  if (scene_) scene_->BuildTextMesh(entity_);
}

// --------------------------------------------------------------- Animator ---

void Animator::SetFloat(std::string_view name, float value) {
  if (runtime_) runtime_->SetFloat(name, value);
}
void Animator::SetFloat(std::string_view name, float value, float damp_time,
                        float delta_time) {
  if (runtime_) runtime_->SetFloat(name, value, damp_time, delta_time);
}
void Animator::SetInteger(std::string_view name, int value) {
  if (runtime_) runtime_->SetInteger(name, value);
}
void Animator::SetBool(std::string_view name, bool value) {
  if (runtime_) runtime_->SetBool(name, value);
}
void Animator::SetTrigger(std::string_view name) {
  if (runtime_) runtime_->SetTrigger(name);
}
void Animator::ResetTrigger(std::string_view name) {
  if (runtime_) runtime_->ResetTrigger(name);
}
float Animator::GetFloat(std::string_view name) const {
  return runtime_ ? runtime_->GetFloat(name) : 0.0f;
}
bool Animator::GetBool(std::string_view name) const {
  return runtime_ && runtime_->GetBool(name);
}
int Animator::GetInteger(std::string_view name) const {
  return runtime_ ? runtime_->GetInteger(name) : 0;
}
void Animator::Play(std::string_view state, int layer, float normalized_time) {
  if (runtime_) runtime_->Play(state, layer, normalized_time);
}
std::string Animator::GetCurrentState(int layer) const {
  return runtime_ ? runtime_->GetCurrentState(layer) : "";
}

// ------------------------------------------------------------ AudioSource ---

void AudioSource::Play() {
  if (audio_ && enabled) audio_->Play(source_);
}
void AudioSource::Stop() {
  if (audio_) audio_->Stop(source_);
}
void AudioSource::Pause() {
  if (audio_) audio_->Pause(source_);
}
void AudioSource::UnPause() {
  if (audio_) audio_->UnPause(source_);
}
void AudioSource::PlayOneShot(const AssetRef& one_shot_clip,
                              float volume_scale) {
  if (audio_ && enabled && one_shot_clip.IsSet()) {
    audio_->PlayOneShot(source_, one_shot_clip.path, volume_scale);
  }
}
bool AudioSource::IsPlaying() const {
  return audio_ && audio_->IsPlaying(source_);
}
void AudioSource::SetClip(const AssetRef& new_clip) {
  clip = new_clip;
  if (audio_) audio_->SetClip(source_, clip.path);
}
void AudioSource::SetVolume(float new_volume) {
  volume = new_volume;
  if (audio_) audio_->SetVolume(source_, volume);
}
void AudioSource::SetPitch(float new_pitch) {
  pitch = new_pitch;
  if (audio_) audio_->SetPitch(source_, pitch);
}
void AudioSource::SetLoop(bool new_loop) {
  loop = new_loop;
  if (audio_) audio_->SetLoop(source_, loop);
}
void AudioSource::SetMute(bool new_mute) {
  mute = new_mute;
  if (audio_) audio_->SetMute(source_, mute);
}

// ------------------------------------------------- CharacterController ---

uint32_t CharacterController::Move(glm::vec3 motion) {
  return physics_ ? physics_->MoveCharacter(entity_, motion) : kCollidedNone;
}

bool CharacterController::IsGrounded() const {
  return physics_ && physics_->IsCharacterGrounded(entity_);
}

glm::vec3 CharacterController::GetVelocity() const {
  return physics_ ? physics_->GetCharacterVelocity(entity_) : glm::vec3(0.0f);
}

void CharacterController::SetHeight(float new_height) {
  height = new_height;
  if (physics_) physics_->SetCharacterShape(entity_, height, radius, center);
}

void CharacterController::SetCenter(glm::vec3 new_center) {
  center = new_center;
  if (physics_) physics_->SetCharacterShape(entity_, height, radius, center);
}

void CharacterController::SetRadius(float new_radius) {
  radius = new_radius;
  if (physics_) physics_->SetCharacterShape(entity_, height, radius, center);
}

void RegisterBuiltinTypes() {
  static bool registered = false;
  if (registered) return;
  registered = true;

  auto& registry = TypeRegistry::Instance();

  {
    auto info = MakeInfo<TransformComponent>(
        "Transform", "Core", "Position, rotation and scale of the node");
    info.removable = false;
    // MapScene creates the Transform itself before any other component.
    info.attach = [](MapScene&, kEntityId, const mtext::PropertyList&) {};
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<SpriteRenderer>(
        "SpriteRenderer", "Rendering",
        "Draws a colored or textured quad sized by the Transform's scale",
        SyncRendererField);
    info.attach = AttachSprite;
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<MeshRenderer>(
        "MeshRenderer", "Rendering",
        "Draws a lit 3D shape or model sized by the Transform's scale",
        SyncRendererField);
    info.attach = AttachMeshRenderer;
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info =
        MakeInfo<CameraComponent>("Camera", "Rendering", "Renders the game");
    info.attach = AttachCamera;
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<DirectionalLight>(
        "DirectionalLight", "Rendering",
        "Sunlight along the node's forward (-Z) direction, plus ambient light");
    info.attach = StoreAsProperty<DirectionalLight>("DirectionalLight");
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<PointLight>(
        "PointLight", "Rendering",
        "Light in every direction from the node, fading out at its range");
    info.attach = StoreAsProperty<PointLight>("PointLight");
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info =
        MakeInfo<AreaLight>("AreaLight", "Rendering",
                            "A glowing rectangle in the node's XY plane, "
                            "shining along -Z, with soft shadows");
    info.attach = StoreAsProperty<AreaLight>("AreaLight");
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info =
        MakeInfo<LightProbeVolume>("LightProbeVolume", "Rendering",
                                   "A grid of light probes that lets light "
                                   "bounce between surfaces inside it");
    info.attach = StoreAsProperty<LightProbeVolume>("LightProbeVolume");
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<Rigidbody>(
        "Rigidbody", "Physics",
        "Lets the node's collider move under physics or by its velocity");
    info.attach = StoreAsProperty<Rigidbody>("Rigidbody");
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<BoxCollider>("BoxCollider", "Physics",
                                      "Box shape for collisions");
    info.attach = AttachBoxCollider;
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<SphereCollider>("SphereCollider", "Physics",
                                         "Sphere shape for collisions");
    info.attach = AttachSphereCollider;
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<CapsuleCollider>(
        "CapsuleCollider", "Physics",
        "Capsule shape for collisions, good for characters");
    info.attach = AttachCapsuleCollider;
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<CylinderCollider>("CylinderCollider", "Physics",
                                           "Cylinder shape for collisions");
    info.attach = AttachCylinderCollider;
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<PlaneCollider>("PlaneCollider", "Physics",
                                        "Infinite static plane for walls");
    info.attach = StoreAsProperty<PlaneCollider>("PlaneCollider");
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<MeshCollider>(
        "MeshCollider", "Physics",
        "Collision shape from a mesh's triangles (convex for moving bodies)");
    info.attach = AttachMeshCollider;
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<CharacterController>(
        "CharacterController", "Physics",
        "A capsule moved by script with Move(), sliding along walls and "
        "climbing steps");
    info.attach = StoreAsProperty<CharacterController>("CharacterController");
    registry.RegisterComponent(std::move(info));
  }

  {
    auto info = MakeInfo<UIDocument>(
        "UIDocument", "UI Toolkit",
        "Shows a .uxml document on screen, styled by .uss style sheets");
    info.attach = AttachUIDocument;
    registry.RegisterComponent(std::move(info));
  }

  {
    auto info = MakeInfo<AudioSource>(
        "AudioSource", "Audio", "Plays sound clips from the node's position");
    info.attach = AttachAudioSource;
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<TextMesh>(
        "TextMesh", "Rendering", "Text in the world (TextMesh Pro's 3D text)");
    info.attach = AttachTextMesh;
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<Animator>(
        "Animator", "Animation",
        "Plays a model's animations through an Animator Controller (.manim)");
    info.attach = StoreAsProperty<Animator>("Animator");
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info =
        MakeInfo<AudioListener>("AudioListener", "Audio",
                                "Where sounds are heard from (one per scene)");
    info.attach = StoreAsProperty<AudioListener>("AudioListener");
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info =
        MakeInfo<PostProcessVolume>("PostProcessVolume", "Rendering",
                                    "Tonemapping, bloom and vignette for every "
                                    "camera (a global URP Volume)");
    info.attach = StoreAsProperty<PostProcessVolume>("PostProcessVolume");
    registry.RegisterComponent(std::move(info));
  }

  registry.RegisterAssetType(AssetTypeInfo{
      "PanelSettings", "How UI documents scale to the screen, and their theme",
      DescribeFields<PanelSettingsAsset>()});
  registry.RegisterAssetType(AssetTypeInfo{
      "PostProcessProfile", "Tonemapping, bloom and vignette values",
      DescribeFields<PostProcessProfile>()});
  registry.RegisterAssetType(AssetTypeInfo{
      "RenderTexture", "An image a Camera draws into, shown by a Material",
      DescribeFields<RenderTextureAsset>()});
  registry.RegisterAssetType(AssetTypeInfo{
      "Material", "Color and texture for a SpriteRenderer or MeshRenderer",
      DescribeFields<MaterialAsset>()});
  registry.RegisterAssetType(
      AssetTypeInfo{"PhysicsMaterial", "Friction and bounciness for a collider",
                    DescribeFields<PhysicsMaterialAsset>()});
}

}  // namespace Mercury
