// Registers the engine's built-in components and asset types with the
// TypeRegistry, along with how MapScene attaches each one to an entity.

#include <cmath>
#include <optional>

#include "components/components.h"
#include "properties/camera.h"
#include "properties/primitives.h"
#include "scene/map_scene.h"
#include "scripting/type_registry.h"

namespace Mercury {

namespace {

template <typename T>
ComponentInfo MakeInfo(const char* name, const char* category,
                       const char* description) {
  ComponentInfo info;
  info.name = name;
  info.category = category;
  info.description = description;
  info.fields = DescribeFields<T>();
  return info;
}

// Most built-ins are plain data that MapScene (or the physics setup) reads
// later, so attaching just stores the reflected struct on the entity.
template <typename T>
AttachFn StoreAsProperty(const char* name) {
  return [name](MapScene& scene, kEntityId entity,
                const mtext::PropertyList& properties) {
    auto value = ReadFields<T>(properties,
                               scene.GetGameObject(entity).GetPath() + "." + name);
    scene.Objects().AddProperty(entity, value);
  };
}

void AttachSprite(MapScene& scene, kEntityId entity,
                  const mtext::PropertyList& properties) {
  auto sprite = ReadFields<SpriteRenderer>(
      properties, scene.GetGameObject(entity).GetPath() + ".SpriteRenderer");
  scene.Objects().AddProperty(entity, sprite);
  if (!sprite.visible) return;

  const auto material =
      scene.Assets().LoadAs<MaterialAsset>(sprite.material.path);
  Mesh quad = PObjectManager::MakeQuadMesh(sprite.color * material.color);
  quad.tiling_ = material.tiling;
  scene.Objects().AddProperty(entity, std::move(quad));
  if (material.texture.IsSet()) {
    if (auto texture = scene.GetTexture(material.texture.path)) {
      scene.Objects().AddProperty(entity, TextureRef{texture});
    }
  }
}

void AttachMeshRenderer(MapScene& scene, kEntityId entity,
                        const mtext::PropertyList& properties) {
  const std::string path = scene.GetGameObject(entity).GetPath();
  auto renderer = ReadFields<MeshRenderer>(properties, path + ".MeshRenderer");
  scene.Objects().AddProperty(entity, renderer);
  if (!renderer.visible) return;

  const auto material =
      scene.Assets().LoadAs<MaterialAsset>(renderer.material.path);
  const glm::vec4 color = renderer.color * material.color;
  std::optional<Mesh> mesh;
  switch (renderer.mesh) {
    case MeshShape::kCube: mesh = Primitives::Cube(color); break;
    case MeshShape::kSphere: mesh = Primitives::Sphere(color); break;
    case MeshShape::kCapsule: mesh = Primitives::Capsule(color); break;
    case MeshShape::kCylinder: mesh = Primitives::Cylinder(color); break;
    case MeshShape::kPlane: mesh = Primitives::Plane(color); break;
    case MeshShape::kQuad: mesh = Primitives::Quad(color); break;
    case MeshShape::kModel:
      if (!renderer.model.IsSet()) return;
      if (const Mesh* model = scene.GetModel(renderer.model.path)) {
        mesh = *model;
        for (auto& vertex : mesh->vertices_) vertex.color = glm::vec3(color);
        mesh->opacity_ = color.a;
      }
      break;
  }
  if (!mesh) return;
  mesh->roughness_ = material.roughness;
  mesh->metallic_ = material.metallic;
  mesh->tiling_ = material.tiling;
  scene.Objects().AddProperty(entity, std::move(*mesh));
  if (material.texture.IsSet()) {
    if (auto texture = scene.GetTexture(material.texture.path)) {
      scene.Objects().AddProperty(entity, TextureRef{texture});
    }
  }
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
    transform->SetPosition(glm::vec3(position.x, position.y, position.z + distance));
    transform->SetRotate(glm::vec3(0.0f));
    transform->SnapInterpolation();
    transform->ClearTransformations();
  }
}

}  // namespace

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
        "Draws a colored or textured quad sized by the Transform's scale");
    info.attach = AttachSprite;
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<MeshRenderer>(
        "MeshRenderer", "Rendering",
        "Draws a lit 3D shape or model sized by the Transform's scale");
    info.attach = AttachMeshRenderer;
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<CameraComponent>("Camera", "Rendering",
                                          "Renders the game");
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
    auto info = MakeInfo<Rigidbody>(
        "Rigidbody", "Physics",
        "Lets the node's collider move under physics or by its velocity");
    info.attach = StoreAsProperty<Rigidbody>("Rigidbody");
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<BoxCollider>("BoxCollider", "Physics",
                                      "Box shape for collisions");
    info.attach = StoreAsProperty<BoxCollider>("BoxCollider");
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<SphereCollider>("SphereCollider", "Physics",
                                         "Sphere shape for collisions");
    info.attach = StoreAsProperty<SphereCollider>("SphereCollider");
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<CapsuleCollider>(
        "CapsuleCollider", "Physics",
        "Capsule shape for collisions, good for characters");
    info.attach = StoreAsProperty<CapsuleCollider>("CapsuleCollider");
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<CylinderCollider>("CylinderCollider", "Physics",
                                           "Cylinder shape for collisions");
    info.attach = StoreAsProperty<CylinderCollider>("CylinderCollider");
    registry.RegisterComponent(std::move(info));
  }
  {
    auto info = MakeInfo<PlaneCollider>("PlaneCollider", "Physics",
                                        "Infinite static plane for walls");
    info.attach = StoreAsProperty<PlaneCollider>("PlaneCollider");
    registry.RegisterComponent(std::move(info));
  }

  registry.RegisterAssetType(AssetTypeInfo{
      "Material", "Color and texture for a SpriteRenderer or MeshRenderer",
      DescribeFields<MaterialAsset>()});
  registry.RegisterAssetType(AssetTypeInfo{
      "PhysicsMaterial", "Friction and bounciness for a collider",
      DescribeFields<PhysicsMaterialAsset>()});
}

}  // namespace Mercury
