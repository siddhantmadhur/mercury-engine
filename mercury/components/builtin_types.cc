// Registers the engine's built-in components and asset types with the
// TypeRegistry, along with how MapScene attaches each one to an entity.

#include <cmath>

#include "components/components.h"
#include "properties/camera.h"
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
  scene.Objects().AddProperty(
      entity, PObjectManager::MakeQuadMesh(sprite.color * material.color));
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
  scene.Objects().AddProperty(entity, camera);

  if (settings.fit_height > 0.0f) {
    // Back the camera off along +Z until fit_height world units fill the
    // view vertically, then look straight down -Z (+X right, +Y up).
    auto transform = scene.Objects().GetProperty<Transform>(entity);
    const float half_fov = glm::radians(settings.fov) / 2.0f;
    const float distance = (settings.fit_height / 2.0f) / std::tan(half_fov);
    auto position = transform->GetPosition();
    transform->SetPosition(glm::vec3(position.x, position.y, position.z + distance));
    transform->SetRotate(glm::vec3(0.0f, -90.0f, 0.0f));
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
    auto info = MakeInfo<CameraComponent>("Camera", "Rendering",
                                          "Renders the game");
    info.attach = AttachCamera;
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
    auto info = MakeInfo<PlaneCollider>("PlaneCollider", "Physics",
                                        "Infinite static plane for walls");
    info.attach = StoreAsProperty<PlaneCollider>("PlaneCollider");
    registry.RegisterComponent(std::move(info));
  }

  registry.RegisterAssetType(AssetTypeInfo{
      "Material", "Color and texture for a SpriteRenderer",
      DescribeFields<MaterialAsset>()});
  registry.RegisterAssetType(AssetTypeInfo{
      "PhysicsMaterial", "Friction and bounciness for a collider",
      DescribeFields<PhysicsMaterialAsset>()});
}

}  // namespace Mercury
