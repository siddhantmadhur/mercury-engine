/*
 * components.h
 *
 * Built-in components a map can use. Each one is plain data with a Reflect
 * method; MapScene turns it into engine properties (Transform, Mesh, Camera,
 * physics bodies) when the map is loaded.
 */
#ifndef MERCURY_COMPONENTS_COMPONENTS_H
#define MERCURY_COMPONENTS_COMPONENTS_H

#include <cstdint>
#include <glm/glm.hpp>
#include <string>

#include "globals.h"
#include "properties/texture2d.h"
#include "scripting/reflection.h"

namespace Mercury {

/**
 * @brief Added to every entity created from a map node
 */
struct NodeInfo {
  std::string name;
  std::string path;  // from the map root, e.g. "Walls/Left"
  kEntityId parent = entt::null;
  uint64_t uid = 0;  // MapNode::uid
};

/**
 * @brief Position, rotation and scale. Every node has one.
 *
 * In the file, position is relative to the parent node. When the map is
 * loaded it is added to the parent's position; rotation and scale are not
 * inherited, and there is no live parenting at runtime yet.
 */
struct TransformComponent {
  glm::vec3 position = glm::vec3(0.0f);
  glm::vec3 rotation = glm::vec3(0.0f);  // Euler angles in degrees
  glm::vec3 scale = glm::vec3(1.0f);

  void Reflect(Reflector& r) {
    r.Field("position", position);
    r.Field("rotation", rotation)
        .Tooltip("Euler angles in degrees, applied X, then Y, then Z");
    r.Field("scale", scale).Tooltip("Also the size of a sprite");
  }
};

/**
 * @brief Draws a quad the size of the Transform's scale on the XY plane
 */
struct SpriteRenderer {
  glm::vec4 color = glm::vec4(1.0f);
  AssetRef material;
  bool visible = true;

  void Reflect(Reflector& r) {
    r.Field("color", color).Tooltip("Multiplied with the material color");
    r.Field("material", material, "Material");
    r.Field("visible", visible);
  }
};

enum class MeshShape { kCube, kSphere, kCapsule, kCylinder, kPlane, kQuad, kModel };

/**
 * @brief Draws a lit 3D shape, or a model file, sized by the Transform's scale
 *
 * See properties/primitives.h for each shape's size at scale 1.
 */
struct MeshRenderer {
  MeshShape mesh = MeshShape::kCube;
  AssetRef model;
  glm::vec4 color = glm::vec4(1.0f);
  AssetRef material;
  bool visible = true;

  void Reflect(Reflector& r) {
    r.Enum("mesh", mesh,
           {"cube", "sphere", "capsule", "cylinder", "plane", "quad", "model"})
        .Tooltip("A built-in shape, or \"model\" to use the model file");
    r.Field("model", model, "Model")
        .Tooltip("A .obj, .gltf, .glb or .fbx file, used when mesh = model");
    r.Field("color", color).Tooltip("Multiplied with the material color");
    r.Field("material", material, "Material");
    r.Field("visible", visible);
  }
};

/**
 * @brief Perspective camera looking along -Z of its Transform's rotation
 */
struct CameraComponent {
  float fov = 60.0f;
  float fit_height = 0.0f;
  float near_clip = 0.1f;
  float far_clip = 1000.0f;
  glm::vec4 background = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
  bool current = true;

  void Reflect(Reflector& r) {
    r.Field("fov", fov).Tooltip("Vertical field of view in degrees");
    r.Field("fit_height", fit_height)
        .Tooltip(
            "For 2D games: if above 0, the camera ignores its rotation and "
            "looks down -Z from far enough away that this many world units "
            "fit vertically on the z = 0 plane");
    r.Field("near_clip", near_clip).Tooltip("Closest distance drawn");
    r.Field("far_clip", far_clip).Tooltip("Farthest distance drawn");
    r.Field("background", background).Tooltip("Color behind everything");
    r.Field("current", current).Tooltip("Render the game from this camera");
  }
};

/**
 * @brief Sunlight: parallel rays along -Z of the Transform's rotation
 *
 * Lights every MeshRenderer in the map; sprites are never lit. A map uses
 * its first DirectionalLight. Without one, a default light shines down from
 * the front.
 */
struct DirectionalLight {
  glm::vec4 color = glm::vec4(1.0f);
  float intensity = 1.0f;
  glm::vec4 ambient = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
  bool cast_shadows = true;
  float shadow_bias = 0.0015f;
  float shadow_normal_bias = 0.035f;
  float shadow_distance = 120.0f;

  void Reflect(Reflector& r) {
    r.Field("color", color);
    r.Field("intensity", intensity);
    r.Field("ambient", ambient)
        .Tooltip("Light that reaches every surface, even facing away");
    r.Field("cast_shadows", cast_shadows).Tooltip("Enable shadow casting");
    r.Field("shadow_bias", shadow_bias).Tooltip("Depth bias against shadow acne");
    r.Field("shadow_normal_bias", shadow_normal_bias)
        .Tooltip("Normal offset bias for sloped geometry");
    r.Field("shadow_distance", shadow_distance)
        .Tooltip("Shadow coverage distance");
  }
};

enum class MotionType { kStatic, kKinematic, kDynamic };
enum class AllowedDofs {
  kAll,
  kTranslationXYZ,  // no rotation
  kTranslationXY,   // 2D without rotation
  kPlane2D,         // 2D with rotation around Z
  kTranslationX,
  kTranslationY,
};
enum class MotionQuality { kDiscrete, kLinearCast };

/**
 * @brief Makes a node with a collider move under physics
 *
 * Without a Rigidbody, a collider is a static body.
 */
struct Rigidbody {
  MotionType motion_type = MotionType::kDynamic;
  float mass = 1.0f;
  AllowedDofs allowed_dofs = AllowedDofs::kAll;
  MotionQuality motion_quality = MotionQuality::kDiscrete;
  float gravity_factor = 1.0f;
  float linear_damping = 0.05f;
  float angular_damping = 0.05f;
  bool allow_sleeping = true;

  void Reflect(Reflector& r) {
    r.Enum("motion_type", motion_type, {"static", "kinematic", "dynamic"})
        .Tooltip(
            "kinematic bodies move only by the velocity you set and are not "
            "pushed by collisions");
    r.Enum("allowed_dofs", allowed_dofs,
           {"all", "translation_xyz", "translation_xy", "plane_2d",
            "translation_x", "translation_y"})
        .Tooltip("Which axes the body may move or rotate along");
    r.Field("mass", mass).Tooltip("In kilograms");
    r.Enum("motion_quality", motion_quality, {"discrete", "linear_cast"})
        .Tooltip("linear_cast stops fast bodies tunneling through others");
    r.Field("gravity_factor", gravity_factor);
    r.Field("linear_damping", linear_damping);
    r.Field("angular_damping", angular_damping);
    r.Field("allow_sleeping", allow_sleeping);
  }
};

/**
 * @brief Box collision shape; its size is multiplied by the Transform's scale
 */
struct BoxCollider {
  glm::vec3 size = glm::vec3(1.0f);
  AssetRef material;
  bool is_trigger = false;

  void Reflect(Reflector& r) {
    r.Field("size", size).Tooltip("Multiplied by the Transform's scale");
    r.Field("material", material, "PhysicsMaterial");
    r.Field("is_trigger", is_trigger)
        .Tooltip("Reports collisions without pushing bodies apart");
  }
};

/**
 * @brief Sphere collision shape; the radius is multiplied by the largest of
 * the Transform's scale axes
 */
struct SphereCollider {
  float radius = 0.5f;
  AssetRef material;
  bool is_trigger = false;

  void Reflect(Reflector& r) {
    r.Field("radius", radius);
    r.Field("material", material, "PhysicsMaterial");
    r.Field("is_trigger", is_trigger)
        .Tooltip("Reports collisions without pushing bodies apart");
  }
};

/**
 * @brief Capsule along Y. The radius is scaled by the larger of the X and Z
 * scale, the height by the Y scale.
 */
struct CapsuleCollider {
  float radius = 0.5f;
  float height = 2.0f;
  AssetRef material;
  bool is_trigger = false;

  void Reflect(Reflector& r) {
    r.Field("radius", radius);
    r.Field("height", height).Tooltip("Total height, including the round ends");
    r.Field("material", material, "PhysicsMaterial");
    r.Field("is_trigger", is_trigger)
        .Tooltip("Reports collisions without pushing bodies apart");
  }
};

/**
 * @brief Cylinder along Y, scaled like a CapsuleCollider
 */
struct CylinderCollider {
  float radius = 0.5f;
  float height = 2.0f;
  AssetRef material;
  bool is_trigger = false;

  void Reflect(Reflector& r) {
    r.Field("radius", radius);
    r.Field("height", height);
    r.Field("material", material, "PhysicsMaterial");
    r.Field("is_trigger", is_trigger)
        .Tooltip("Reports collisions without pushing bodies apart");
  }
};

/**
 * @brief Infinite static plane through the node's position
 */
struct PlaneCollider {
  glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
  AssetRef material;
  bool is_trigger = false;

  void Reflect(Reflector& r) {
    r.Field("normal", normal).Tooltip("Points to the side bodies stay on");
    r.Field("material", material, "PhysicsMaterial");
    r.Field("is_trigger", is_trigger);
  }
};

// ------------------------------------------------------------ Asset types ---

struct MaterialAsset {
  glm::vec4 color = glm::vec4(1.0f);
  AssetRef texture;
  float roughness = 0.5f;
  float metallic = 0.0f;

  void Reflect(Reflector& r) {
    r.Field("color", color);
    r.Field("texture", texture, "Texture").Tooltip("A .png or .jpg file");
    r.Field("roughness", roughness)
        .Tooltip("PBR surface roughness [0.0 = mirror, 1.0 = matte]");
    r.Field("metallic", metallic)
        .Tooltip("PBR metalness [0.0 = dielectric, 1.0 = pure metal]");
  }
};

struct PhysicsMaterialAsset {
  float friction = 0.2f;
  float restitution = 0.0f;

  void Reflect(Reflector& r) {
    r.Field("friction", friction);
    r.Field("restitution", restitution).Tooltip("Bounciness, 0 to 1");
  }
};

/**
 * @brief Points the renderer at a texture owned by the scene, so entities can
 * share one image without each holding a Texture2D copy
 */
struct TextureRef {
  Texture2D* texture = nullptr;
};

}  // namespace Mercury

#endif  // MERCURY_COMPONENTS_COMPONENTS_H
