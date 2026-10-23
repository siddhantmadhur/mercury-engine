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
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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
  std::vector<kEntityId> children;
  std::string tag;
  int layer = 0;
  bool active = true;               // GameObject.activeSelf
  bool active_in_hierarchy = true;  // false if it or any parent is inactive
};

/**
 * @brief Added to entities that are inactive (or under an inactive parent),
 * so renderers can skip them cheaply
 */
struct InactiveTag {};

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
  // One node of the model (Unity's MeshFilter on a model's child object),
  // drawn without its children; "" draws the whole model.
  std::string node;
  glm::vec3 mesh_scale = glm::vec3(1.0f);
  glm::vec4 color = glm::vec4(1.0f);
  AssetRef material;
  // One material per mesh of the model, in order; replaces `material` and
  // the model's own materials (Unity's Materials list).
  std::vector<AssetRef> materials;
  bool visible = true;
  bool cast_shadows = true;
  // Unity's rendering layer mask; bit 8 draws an outline (see RenderSettings).
  int rendering_layers = 1;

  void Reflect(Reflector& r) {
    r.Enum("mesh", mesh,
           {"cube", "sphere", "capsule", "cylinder", "plane", "quad", "model"})
        .Tooltip("A built-in shape, or \"model\" to use the model file");
    r.Field("model", model, "Model")
        .Tooltip("A .obj, .gltf, .glb or .fbx file, used when mesh = model");
    r.Field("node", node)
        .Tooltip("Draw only this node of the model (empty: the whole model)");
    r.Field("mesh_scale", mesh_scale)
        .Tooltip("Scales the shape itself, not the node or its children");
    r.Field("color", color).Tooltip("Multiplied with the material color");
    r.Field("material", material, "Material");
    r.Field("materials", materials, "Material")
        .Tooltip("One material per mesh of the model");
    r.Field("visible", visible);
    r.Field("cast_shadows", cast_shadows);
    r.Field("rendering_layers", rendering_layers)
        .Tooltip("Bit mask; bit 8 (256) draws the outline");
  }
};

/**
 * @brief Perspective camera looking along -Z of its Transform's rotation
 */
enum class CameraClear { kSkybox, kColor, kDepthOnly, kNothing };

struct CameraComponent {
  float fov = 60.0f;
  float fit_height = 0.0f;
  float near_clip = 0.1f;
  float far_clip = 1000.0f;
  glm::vec4 background = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
  AssetRef skybox;
  bool current = true;
  // Camera.enabled: disabled cameras draw nothing.
  bool enabled = true;
  // Bit N set: draw nodes on layer N (Unity's culling mask).
  int culling_mask = -1;
  // Cameras draw in increasing depth; overlays go over the current camera.
  float depth = 0.0f;
  // skybox draws `skybox` (or Unity's default sky without one).
  CameraClear clear = CameraClear::kColor;
  // Draw on top of the current camera's image (a URP Overlay camera).
  bool overlay = false;
  // For an overlay: only draw over this camera (URP's camera stack); unset
  // draws over whichever camera is current.
  NodeRef base_camera;
  // Draw into this RenderTexture asset instead of the screen.
  AssetRef target_texture;
  bool post_processing = true;

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
    r.Field("skybox", skybox, "Texture")
        .Tooltip(
            "A 2:1 panorama (.png or .jpg) drawn behind everything; replaces "
            "the background color");
    r.Field("current", current).Tooltip("Render the game from this camera");
    r.Field("enabled", enabled).Tooltip("Disabled cameras draw nothing");
    r.Field("culling_mask", culling_mask)
        .Tooltip("Bit N set: draw nodes on layer N; -1 draws every layer");
    r.Field("depth", depth).Tooltip("Cameras draw in increasing depth");
    r.Enum("clear", clear, {"skybox", "color", "depth_only", "nothing"})
        .Tooltip(
            "What the camera clears to before drawing; skybox without a "
            "skybox texture draws a default sky");
    r.Field("overlay", overlay)
        .Tooltip("Draw over the current camera's image (its depth is cleared)");
    r.Field("base_camera", base_camera)
        .Tooltip("Overlay only: the camera whose image it draws over");
    r.Field("target_texture", target_texture, "RenderTexture")
        .Tooltip("Draw into a RenderTexture asset instead of the screen");
    r.Field("post_processing", post_processing);
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
  // Ambient light from below (Unity's ground color); alpha 0: 35% of ambient.
  glm::vec4 ground_ambient = glm::vec4(0.0f);
  bool cast_shadows = true;
  float shadow_bias = 0.0015f;
  float shadow_normal_bias = 0.035f;
  float shadow_distance = 120.0f;

  void Reflect(Reflector& r) {
    r.Field("color", color);
    r.Field("intensity", intensity);
    r.Field("ambient", ambient)
        .Tooltip("Light that reaches every surface, even facing away");
    r.Field("ground_ambient", ground_ambient)
        .Tooltip("Ambient light from below; alpha 0 uses 35% of ambient");
    r.Field("cast_shadows", cast_shadows).Tooltip("Enable shadow casting");
    r.Field("shadow_bias", shadow_bias).Tooltip("Depth bias against shadow acne");
    r.Field("shadow_normal_bias", shadow_normal_bias)
        .Tooltip("Normal offset bias for sloped geometry");
    r.Field("shadow_distance", shadow_distance)
        .Tooltip("Shadow coverage distance");
  }
};

/**
 * @brief Light given off in every direction from the node's position
 *
 * Like Unity's point light: brightness falls off with the square of the
 * distance and reaches 0 at `range`. No shadows. The renderer uses the first
 * 16 active point lights of a map.
 */
struct PointLight {
  glm::vec4 color = glm::vec4(1.0f);
  float intensity = 1.0f;
  float range = 10.0f;

  void Reflect(Reflector& r) {
    r.Field("color", color);
    r.Field("intensity", intensity);
    r.Field("range", range).Tooltip("Distance at which the light fades out");
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
  MotionQuality motion_quality = MotionQuality::kDiscrete;
  float gravity_factor = 1.0f;
  float linear_damping = 0.05f;
  float angular_damping = 0.05f;
  bool allow_sleeping = true;
  bool use_gravity = true;

  // Constraints: lock translation along world axes
  bool lock_translation_x = false;
  bool lock_translation_y = false;
  bool lock_translation_z = false;

  // Constraints: lock rotation around world axes
  bool lock_rotation_x = false;
  bool lock_rotation_y = false;
  bool lock_rotation_z = false;

  // Computes the bitmask of allowed DOFs (compatible with JPH::EAllowedDOFs)
  uint8_t ComputeAllowedDOFs() const {
    uint8_t allowed = 0b111111;
    if (lock_translation_x) allowed &= ~0b000001;
    if (lock_translation_y) allowed &= ~0b000010;
    if (lock_translation_z) allowed &= ~0b000100;
    if (lock_rotation_x) allowed &= ~0b001000;
    if (lock_rotation_y) allowed &= ~0b010000;
    if (lock_rotation_z) allowed &= ~0b100000;
    return allowed;
  }

  void Reflect(Reflector& r) {
    r.Enum("motion_type", motion_type, {"static", "kinematic", "dynamic"})
        .Tooltip(
            "kinematic bodies move only by the velocity you set and are not "
            "pushed by collisions");
    r.Field("mass", mass).Tooltip("In kilograms");
    r.Enum("motion_quality", motion_quality, {"discrete", "linear_cast"})
        .Tooltip("linear_cast stops fast bodies tunneling through others");
    r.Field("lock_translation_x", lock_translation_x)
        .Tooltip("Freeze movement along the world X axis");
    r.Field("lock_translation_y", lock_translation_y)
        .Tooltip("Freeze movement along the world Y axis");
    r.Field("lock_translation_z", lock_translation_z)
        .Tooltip("Freeze movement along the world Z axis");
    r.Field("lock_rotation_x", lock_rotation_x)
        .Tooltip("Freeze rotation around the world X axis");
    r.Field("lock_rotation_y", lock_rotation_y)
        .Tooltip("Freeze rotation around the world Y axis");
    r.Field("lock_rotation_z", lock_rotation_z)
        .Tooltip("Freeze rotation around the world Z axis");
    r.Field("gravity_factor", gravity_factor);
    r.Field("use_gravity", use_gravity);
    r.Field("linear_damping", linear_damping);
    r.Field("angular_damping", angular_damping);
    r.Field("allow_sleeping", allow_sleeping);

    if (r.GetMode() == Reflector::Mode::kRead) {
      // Also accept lock_transform_* and lock_position_* aliases if present
      r.Field("lock_transform_x", lock_translation_x);
      r.Field("lock_transform_y", lock_translation_y);
      r.Field("lock_transform_z", lock_translation_z);
      r.Field("lock_position_x", lock_translation_x);
      r.Field("lock_position_y", lock_translation_y);
      r.Field("lock_position_z", lock_translation_z);

      // Backward compatibility with legacy allowed_dofs
      AllowedDofs legacy_dofs = AllowedDofs::kAll;
      r.Enum("allowed_dofs", legacy_dofs,
             {"all", "translation_xyz", "translation_xy", "plane_2d",
              "translation_x", "translation_y"});
      if (legacy_dofs != AllowedDofs::kAll &&
          !lock_translation_x && !lock_translation_y && !lock_translation_z &&
          !lock_rotation_x && !lock_rotation_y && !lock_rotation_z) {
        switch (legacy_dofs) {
          case AllowedDofs::kTranslationXYZ:
            lock_rotation_x = lock_rotation_y = lock_rotation_z = true;
            break;
          case AllowedDofs::kTranslationXY:
            lock_translation_z = true;
            lock_rotation_x = lock_rotation_y = lock_rotation_z = true;
            break;
          case AllowedDofs::kPlane2D:
            lock_translation_z = true;
            lock_rotation_x = lock_rotation_y = true;
            break;
          case AllowedDofs::kTranslationX:
            lock_translation_y = lock_translation_z = true;
            lock_rotation_x = lock_rotation_y = lock_rotation_z = true;
            break;
          case AllowedDofs::kTranslationY:
            lock_translation_x = lock_translation_z = true;
            lock_rotation_x = lock_rotation_y = lock_rotation_z = true;
            break;
          default:
            break;
        }
      }
    }
  }
};

/**
 * @brief Box collision shape; its size is multiplied by the Transform's scale
 */
struct BoxCollider {
  glm::vec3 size = glm::vec3(1.0f);
  glm::vec3 center = glm::vec3(0.0f);
  AssetRef material;
  bool is_trigger = false;

  void Reflect(Reflector& r) {
    r.Field("size", size).Tooltip("Multiplied by the Transform's scale");
    r.Field("center", center).Tooltip("Offset from the node, before scaling");
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
  glm::vec3 center = glm::vec3(0.0f);
  AssetRef material;
  bool is_trigger = false;

  void Reflect(Reflector& r) {
    r.Field("radius", radius);
    r.Field("center", center).Tooltip("Offset from the node, before scaling");
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
  glm::vec3 center = glm::vec3(0.0f);
  AssetRef material;
  bool is_trigger = false;

  void Reflect(Reflector& r) {
    r.Field("radius", radius);
    r.Field("height", height).Tooltip("Total height, including the round ends");
    r.Field("center", center).Tooltip("Offset from the node, before scaling");
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
  glm::vec3 center = glm::vec3(0.0f);
  AssetRef material;
  bool is_trigger = false;

  void Reflect(Reflector& r) {
    r.Field("radius", radius);
    r.Field("height", height);
    r.Field("center", center).Tooltip("Offset from the node, before scaling");
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

/**
 * @brief Collision shape made of a mesh's triangles: a built-in shape or a
 * model file (Unity's MeshCollider)
 *
 * Moving bodies and triggers use the mesh's convex hull instead.
 */
struct MeshCollider {
  MeshShape mesh = MeshShape::kModel;
  AssetRef model;
  std::string submesh;  // one node of the model by name; "" = all of it
  glm::vec3 mesh_scale = glm::vec3(1.0f);
  bool convex = false;
  AssetRef material;
  bool is_trigger = false;

  void Reflect(Reflector& r) {
    r.Enum("mesh", mesh,
           {"cube", "sphere", "capsule", "cylinder", "plane", "quad", "model"});
    r.Field("model", model, "Model");
    r.Field("submesh", submesh).Tooltip("Name of one node in the model");
    r.Field("mesh_scale", mesh_scale);
    r.Field("convex", convex).Tooltip("Use the mesh's convex hull");
    r.Field("material", material, "PhysicsMaterial");
    r.Field("is_trigger", is_trigger);
  }
};

class PhysicsEngine;
class AudioSystem;

/**
 * @brief Plays sounds from the node's position (Unity's AudioSource)
 *
 * Clips are .wav, .ogg, .mp3 or .flac files.
 */
struct AudioSource {
  bool enabled = true;
  AssetRef clip;
  float volume = 1.0f;
  float pitch = 1.0f;
  bool loop = false;
  bool play_on_awake = true;
  bool mute = false;
  float spatial_blend = 0.0f;  // 0 = 2D, 1 = 3D
  float min_distance = 1.0f;
  float max_distance = 500.0f;

  void Reflect(Reflector& r) {
    r.Field("enabled", enabled);
    r.Field("clip", clip, "AudioClip").Tooltip("A .wav, .ogg, .mp3 or .flac file");
    r.Field("volume", volume);
    r.Field("pitch", pitch).Tooltip("Playback speed; 1 is normal");
    r.Field("loop", loop);
    r.Field("play_on_awake", play_on_awake).Tooltip("Play the clip when the scene starts");
    r.Field("mute", mute);
    r.Field("spatial_blend", spatial_blend).Tooltip("0 = 2D, 1 = 3D (fades with distance)");
    r.Field("min_distance", min_distance).Tooltip("3D: full volume within this distance");
    r.Field("max_distance", max_distance).Tooltip("3D: no more fading beyond this distance");
  }

  // Runtime (play mode).
  void Play();
  void Stop();
  void Pause();
  void UnPause();
  void PlayOneShot(const AssetRef& one_shot_clip, float volume_scale = 1.0f);
  bool IsPlaying() const;
  void SetClip(const AssetRef& new_clip);
  void SetVolume(float new_volume);
  void SetPitch(float new_pitch);
  void SetLoop(bool new_loop);
  void SetMute(bool new_mute);

  AudioSystem* audio_ = nullptr;
  uint32_t source_ = 0;
};

class MapScene;

enum class TextAlignment {
  kTopLeft, kTopCenter, kTopRight,
  kMiddleLeft, kMiddleCenter, kMiddleRight,
  kBottomLeft, kBottomCenter, kBottomRight,
};

/**
 * @brief Text drawn in the world, facing +Z (TextMesh Pro's 3D text)
 *
 * font_size works like TextMesh Pro's: an em is font_size / 10 units. The
 * text is laid out in a size.x by size.y box centered on the node.
 */
struct TextMesh {
  std::string text;
  AssetRef font;
  float font_size = 36.0f;
  glm::vec4 color = glm::vec4(1.0f);
  TextAlignment alignment = TextAlignment::kTopLeft;
  glm::vec2 size = glm::vec2(20.0f, 5.0f);
  bool word_wrap = true;
  bool rich_text = true;
  bool visible = true;

  void Reflect(Reflector& r) {
    r.Field("text", text);
    r.Field("font", font, "Font").Tooltip("A .ttf or .otf file; empty: Liberation Sans");
    r.Field("font_size", font_size).Tooltip("An em is font_size / 10 units");
    r.Field("color", color);
    r.Enum("alignment", alignment,
           {"top_left", "top_center", "top_right", "middle_left", "middle_center",
            "middle_right", "bottom_left", "bottom_center", "bottom_right"});
    r.Field("size", size).Tooltip("The box the text is laid out in");
    r.Field("word_wrap", word_wrap);
    r.Field("rich_text", rich_text).Tooltip("Understand <b>, <i>, <color=...> tags");
    r.Field("visible", visible);
  }

  // Runtime: change the text (rebuilds the mesh).
  void SetText(std::string new_text);
  void SetColor(glm::vec4 new_color);

  MapScene* scene_ = nullptr;
  kEntityId entity_ = entt::null;
};

class AnimatorRuntime;

/**
 * @brief Plays a model's animation clips through an AnimatorController
 * (.manim) driven by parameters (Unity's Animator)
 *
 * Animates the model drawn by `target`, or by this node or the nearest
 * descendant with a model MeshRenderer.
 */
struct Animator {
  bool enabled = true;
  AssetRef controller;
  bool apply_root_motion = false;
  float speed = 1.0f;
  NodeRef target;

  void Reflect(Reflector& r) {
    r.Field("enabled", enabled);
    r.Field("controller", controller, "AnimatorController").Tooltip("A .manim file");
    r.Field("apply_root_motion", apply_root_motion)
        .Tooltip("Off: the root bone stays in place (the object moves by script)");
    r.Field("speed", speed);
    r.Field("target", target).Tooltip("The node whose model is animated (optional)");
  }

  // Runtime (play mode).
  void SetFloat(std::string_view name, float value);
  void SetFloat(std::string_view name, float value, float damp_time, float delta_time);
  void SetInteger(std::string_view name, int value);
  void SetBool(std::string_view name, bool value);
  void SetTrigger(std::string_view name);
  void ResetTrigger(std::string_view name);
  float GetFloat(std::string_view name) const;
  bool GetBool(std::string_view name) const;
  int GetInteger(std::string_view name) const;
  void Play(std::string_view state, int layer = -1, float normalized_time = 0.0f);
  std::string GetCurrentState(int layer = 0) const;

  std::shared_ptr<AnimatorRuntime> runtime_;
};

/**
 * @brief Where sounds are heard from (Unity's AudioListener); one per scene
 */
struct AudioListener {
  bool enabled = true;

  void Reflect(Reflector& r) { r.Field("enabled", enabled); }
};

/**
 * @brief A capsule moved by script with Move(), sliding along walls and
 * climbing steps (Unity's CharacterController). Not pushed by physics.
 */
struct CharacterController {
  float height = 2.0f;
  float radius = 0.5f;
  glm::vec3 center = glm::vec3(0.0f);
  float slope_limit = 45.0f;
  float step_offset = 0.3f;
  float skin_width = 0.08f;
  float min_move_distance = 0.001f;

  void Reflect(Reflector& r) {
    r.Field("height", height);
    r.Field("radius", radius);
    r.Field("center", center).Tooltip("Capsule center, relative to the node");
    r.Field("slope_limit", slope_limit).Tooltip("Steepest walkable slope, degrees");
    r.Field("step_offset", step_offset).Tooltip("Tallest step climbed");
    r.Field("skin_width", skin_width);
    r.Field("min_move_distance", min_move_distance);
  }

  // Runtime (play mode). Move returns CharacterCollisionFlags bits.
  uint32_t Move(glm::vec3 motion);
  bool IsGrounded() const;
  glm::vec3 GetVelocity() const;
  void SetHeight(float new_height);
  void SetCenter(glm::vec3 new_center);
  void SetRadius(float new_radius);

  PhysicsEngine* physics_ = nullptr;
  kEntityId entity_ = entt::null;
};

// ------------------------------------------------------------ Asset types ---

enum class SurfaceType { kOpaque, kTransparent };
enum class CullMode { kBack, kFront, kOff };

struct MaterialAsset {
  glm::vec4 color = glm::vec4(1.0f);
  AssetRef texture;
  glm::vec2 tiling = glm::vec2(1.0f);
  glm::vec2 offset = glm::vec2(0.0f);
  float roughness = 0.5f;
  float metallic = 0.0f;
  glm::vec4 emission = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  AssetRef emission_map;
  AssetRef normal_map;
  float normal_scale = 1.0f;
  SurfaceType surface = SurfaceType::kOpaque;
  CullMode cull = CullMode::kBack;
  bool unlit = false;
  // Show a camera's RenderTexture (a mirror or a screen) instead of texture.
  AssetRef render_texture;

  void Reflect(Reflector& r) {
    r.Field("color", color);
    r.Field("texture", texture, "Texture").Tooltip("A .png or .jpg file");
    r.Field("tiling", tiling)
        .Tooltip(
            "How many times the texture repeats across each face: x = "
            "columns, y = rows");
    r.Field("roughness", roughness)
        .Tooltip("PBR surface roughness [0.0 = mirror, 1.0 = matte]");
    r.Field("metallic", metallic)
        .Tooltip("PBR metalness [0.0 = dielectric, 1.0 = pure metal]");
    r.Field("offset", offset).Tooltip("Texture offset, in tiles");
    r.Field("emission", emission).Tooltip("Light the surface gives off (rgb)");
    r.Field("emission_map", emission_map, "Texture")
        .Tooltip("Multiplies emission (a .png or .jpg file)");
    r.Field("normal_map", normal_map, "Texture")
        .Tooltip("Tangent-space normal map (OpenGL convention, green up)");
    r.Field("normal_scale", normal_scale).Tooltip("Strength of the normal map");
    r.Enum("surface", surface, {"opaque", "transparent"})
        .Tooltip("transparent blends with what is behind by the color's alpha");
    r.Enum("cull", cull, {"back", "front", "off"})
        .Tooltip("Which faces are hidden; off draws both sides");
    r.Field("unlit", unlit).Tooltip("Ignore lights; show the color as is");
    r.Field("render_texture", render_texture, "RenderTexture");
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

enum class TonemapMode { kNone, kNeutral, kAces };

/**
 * @brief Post-processing values (a URP Volume Profile's overrides)
 */
struct PostProcessProfile {
  TonemapMode tonemap = TonemapMode::kNone;
  float exposure = 1.0f;
  float bloom_intensity = 0.0f;
  float bloom_threshold = 1.0f;
  float bloom_scatter = 0.7f;
  float vignette_intensity = 0.0f;
  float vignette_smoothness = 0.2f;
  glm::vec2 vignette_center = glm::vec2(0.5f);

  void Reflect(Reflector& r) {
    r.Enum("tonemap", tonemap, {"none", "neutral", "aces"});
    r.Field("exposure", exposure).Tooltip("Multiplies the scene before tonemapping");
    r.Field("bloom_intensity", bloom_intensity).Tooltip("0 turns bloom off");
    r.Field("bloom_threshold", bloom_threshold)
        .Tooltip("Brightness (linear) above which pixels glow");
    r.Field("bloom_scatter", bloom_scatter).Tooltip("How far the glow spreads, 0 to 1");
    r.Field("vignette_intensity", vignette_intensity).Tooltip("0 turns the vignette off");
    r.Field("vignette_smoothness", vignette_smoothness);
    r.Field("vignette_center", vignette_center);
  }
};

/**
 * @brief Applies a PostProcessProfile to the cameras (a global URP Volume)
 */
struct PostProcessVolume {
  AssetRef profile;
  float weight = 1.0f;

  void Reflect(Reflector& r) {
    r.Field("profile", profile, "PostProcessProfile");
    r.Field("weight", weight).Tooltip("0 to 1; below 0.5 the volume is ignored");
  }
};

/**
 * @brief A texture a camera draws into (Unity's RenderTexture asset)
 */
struct RenderTextureAsset {
  int width = 1024;
  int height = 1024;

  void Reflect(Reflector& r) {
    r.Field("width", width);
    r.Field("height", height);
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
