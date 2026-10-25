/*
 * model.h
 *
 * A 3D model file (FBX, glTF, OBJ, ...) loaded through assimp, keeping what
 * Unity keeps from an imported model: the node hierarchy, one mesh per
 * material, the materials, skins (bones and weights) and animation clips.
 *
 * Import settings live next to the file in "<file>.mimport" (Unity's .meta):
 *
 *   [mimport]
 *   convention = "unity"   ; "native" (default) or "unity"
 *   scale = 1.0            ; extra uniform scale
 *   use_file_scale = true  ; convert the file's units to meters
 *   max_texture_size = 8192 ; larger textures are scaled down to fit
 *
 * "unity" lays the model out the way Unity's importer does (axis conversion
 * on the top nodes, the file's units converted to meters, a single top node
 * becoming the model root), expressed in Mercury's coordinates, so scenes
 * converted from Unity line up with their models.
 */
#ifndef MERCURY_PROPERTIES_MODEL_H
#define MERCURY_PROPERTIES_MODEL_H

#include <atomic>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Mercury {

/**
 * @brief Vertex of the model renderer
 */
struct ModelVertex {
  glm::vec3 position = glm::vec3(0.0f);
  glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
  glm::vec2 uv = glm::vec2(0.0f);
  glm::vec4 color = glm::vec4(1.0f);
  glm::uvec4 joints = glm::uvec4(0);  // bone indices into the mesh's skin
  glm::vec4 weights = glm::vec4(0.0f);
  glm::vec4 tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);  // w: bitangent sign
};
static_assert(sizeof(ModelVertex) == 96);

/**
 * @brief Decoded RGBA8 image shared by everything that uses it
 */
struct ImageData {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;  // width * height * 4
  bool srgb = true;             // color data (false: masks, normals)
  uint64_t id = 0;              // unique per image, for GPU caches
  std::string source;           // file it came from, for messages

  static std::shared_ptr<ImageData> Load(const std::string& path);
  static std::shared_ptr<ImageData> FromMemory(const uint8_t* data, size_t size,
                                               std::string source);
  static std::shared_ptr<ImageData> FromPixels(int width, int height,
                                               std::vector<uint8_t> pixels,
                                               std::string source);
  static uint64_t NextId();
};

/**
 * @brief Immutable triangle geometry, uploaded once and shared by every node
 * that draws it
 */
// A spatially compact run of a mesh's triangles, culled on its own.
struct MeshCluster {
  uint32_t first_index = 0;
  uint32_t index_count = 0;
  glm::vec3 bounds_min = glm::vec3(0.0f);
  glm::vec3 bounds_max = glm::vec3(0.0f);
};

struct MeshGeometry {
  std::vector<ModelVertex> vertices;
  std::vector<uint32_t> indices;
  glm::vec3 bounds_min = glm::vec3(0.0f);
  glm::vec3 bounds_max = glm::vec3(0.0f);
  bool skinned = false;
  uint64_t id = 0;  // unique, for GPU caches
  // Large meshes in pieces that views cull one by one; empty: one piece.
  std::vector<MeshCluster> clusters;

  void ComputeBounds();
  // Puts the triangles of a large mesh in spatial order and splits them
  // into `clusters` (vertices unchanged).
  void BuildClusters();
  // Tangents from the UV layout, for normal maps.
  void ComputeTangents();
  static std::shared_ptr<MeshGeometry> Create(std::vector<ModelVertex> vertices,
                                              std::vector<uint32_t> indices);
};

struct ModelMaterial {
  std::string name;
  glm::vec4 color = glm::vec4(1.0f);  // sRGB, like Unity's material colors
  std::shared_ptr<ImageData> texture;
  float metallic = 0.0f;
  float smoothness = 0.5f;
  glm::vec3 emission = glm::vec3(0.0f);  // linear
  std::shared_ptr<ImageData> emission_texture;  // multiplies emission
  std::shared_ptr<ImageData> normal_texture;    // tangent space
  float normal_scale = 1.0f;
  bool transparent = false;
  // Glass-like: the share of light that passes through (refracted) rather
  // than being reflected diffusely, the index of refraction, and the tint
  // what passes through takes on (linear) after `thickness` world units of
  // it (0: thin walled, tinted once at each surface).
  float transmission = 0.0f;
  float ior = 1.5f;
  glm::vec3 transmission_color = glm::vec3(1.0f);
  float thickness = 0.0f;
};

struct ModelBone {
  int node = -1;                  // node the bone follows
  glm::mat4 offset = glm::mat4(1.0f);  // mesh space -> bone space (bind)
};

struct ModelMesh {
  std::string name;
  int node = -1;  // node that owns the mesh
  int material = 0;
  std::shared_ptr<MeshGeometry> geometry;
  std::vector<ModelBone> bones;  // empty for rigid meshes
};

struct ModelNode {
  std::string name;
  int parent = -1;
  std::vector<int> children;
  std::vector<int> meshes;
  glm::vec3 position = glm::vec3(0.0f);
  glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  glm::vec3 scale = glm::vec3(1.0f);

  glm::mat4 LocalMatrix() const;
};

/**
 * @brief Keyframes of one node
 */
struct AnimationChannel {
  int node = -1;
  std::vector<float> position_times;
  std::vector<glm::vec3> positions;
  std::vector<float> rotation_times;
  std::vector<glm::quat> rotations;
  std::vector<float> scale_times;
  std::vector<glm::vec3> scales;
};

struct AnimationClip {
  std::string name;   // e.g. "Armature|Jog_Fwd_Loop"
  float duration = 0.0f;  // seconds
  std::vector<AnimationChannel> channels;

  // Samples node `channel` at time t (seconds, clamped) into position,
  // rotation and scale; values the clip does not animate are left alone.
  void Sample(const AnimationChannel& channel, float t, glm::vec3& position,
              glm::quat& rotation, glm::vec3& scale) const;
};

enum class ModelConvention { kNative, kUnity };

// How a material uses a texture: color (sRGB), data (linear, like normal
// maps), or a height map to be turned into a normal map.
enum class ModelTextureUse : uint8_t { kColor, kData, kHeight };

// Replaces what the file says about one material, for formats that cannot
// say it (OBJ has no real roughness) or say it wrong. From a
// `[material name="..."]` section of the .mimport file.
struct ModelMaterialOverride {
  std::string name;
  std::optional<glm::vec4> color;  // sRGB
  std::optional<float> metallic;
  std::optional<float> smoothness;
  std::optional<glm::vec3> emission;  // linear
  std::optional<float> transmission;
  std::optional<float> ior;
  std::optional<glm::vec3> transmission_color;  // linear
  std::optional<float> thickness;
};

struct ModelImportSettings {
  ModelConvention convention = ModelConvention::kNative;
  float scale = 1.0f;
  bool use_file_scale = true;
  int max_texture_size = 8192;
  std::vector<ModelMaterialOverride> materials;
  // .obj files at least this big are read in pieces on several threads; not
  // part of the .mimport file.
  size_t parallel_obj_min_bytes = 32 * 1024 * 1024;
  size_t parallel_obj_pieces = 0;  // the most pieces; 0 picks from the core count

  // Reads "<model path>.mimport" if it exists; otherwise `defaults`.
  static ModelImportSettings ForFile(const std::string& model_path,
                                     const ModelImportSettings& defaults);
};

class Model {
 public:
  // `progress`, if given, is set from 0 to 1 as the model loads, from any
  // of the threads that load it. A model imported before comes from the
  // cache (model_cache.h) while its files and settings are unchanged.
  static std::shared_ptr<Model> Load(const std::string& path,
                                     const ModelImportSettings& settings,
                                     std::string* error = nullptr,
                                     std::atomic<float>* progress = nullptr);

  const std::vector<ModelNode>& Nodes() const { return nodes_; }
  const std::vector<ModelMesh>& Meshes() const { return meshes_; }
  const std::vector<ModelMaterial>& Materials() const { return materials_; }
  const std::vector<AnimationClip>& Clips() const { return clips_; }
  const std::string& GetPath() const { return path_; }
  // The root node's transform is the drawing object's (Unity convention),
  // so drawing starts below it.
  bool RootOnObject() const { return root_on_object_; }

  // By name; "Name#2" finds the second node called Name ("Name#1" the first).
  int FindNode(std::string_view name) const;
  // Clip by full name, or by the part after "|" (Armature|Run -> Run).
  const AnimationClip* FindClip(std::string_view name) const;

  // Model-space matrix of every node in the bind (rest) pose.
  const std::vector<glm::mat4>& BindGlobals() const { return bind_globals_; }

  // Every triangle of a node's meshes (with its children's when asked), in
  // that node's space, for collision.
  void CollectTriangles(int node, bool include_children,
                        std::vector<glm::vec3>& vertices,
                        std::vector<uint32_t>& indices) const;

  glm::vec3 BoundsMin() const { return bounds_min_; }
  glm::vec3 BoundsMax() const { return bounds_max_; }

  // Computes model-space matrices for node local matrices given in `locals`.
  void ComputeGlobals(const std::vector<glm::mat4>& locals,
                      std::vector<glm::mat4>& globals) const;

 private:
  friend class ModelCache;

  // A texture as the file names it, and how a material uses it.
  struct TextureSource {
    std::string reference;  // empty: none
    ModelTextureUse use = ModelTextureUse::kColor;
    float bump_scale = 1.0f;
  };
  struct MaterialSources {
    TextureSource texture, emission, normal;
  };

  // The import itself, with assimp.
  static std::shared_ptr<Model> Import(const std::string& path,
                                       const ModelImportSettings& settings, std::string* error,
                                       std::atomic<float>* progress);
  // A texture file the model refers to, found, decoded and prepared as the
  // import does it (null if missing).
  static std::shared_ptr<ImageData> DecodeTexture(const std::string& model_path,
                                                  const TextureSource& source,
                                                  const ModelImportSettings& settings);
  // The bind pose and bounds, from the nodes and meshes.
  void Finish();

  std::string path_;
  std::vector<ModelNode> nodes_;  // parents before children; 0 is the root
  std::vector<ModelMesh> meshes_;
  std::vector<ModelMaterial> materials_;
  std::vector<AnimationClip> clips_;
  std::vector<glm::mat4> bind_globals_;
  glm::vec3 bounds_min_ = glm::vec3(0.0f);
  glm::vec3 bounds_max_ = glm::vec3(0.0f);
  bool root_on_object_ = false;
  // Per material, where its textures came from (for the cache), and
  // whether any came from inside the file (no cache then).
  std::vector<MaterialSources> material_sources_;
  bool embedded_textures_ = false;
};

}  // namespace Mercury

#endif  // MERCURY_PROPERTIES_MODEL_H
