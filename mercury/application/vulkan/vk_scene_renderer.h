/*
 * vk_scene_renderer.h
 *
 * The model renderer: draws every RenderMesh (MeshRenderer, models,
 * skinned characters, TextMesh) with shared GPU geometry and bindless
 * textures, for each camera of the frame, then post-processes the result
 * (SSAO, bloom, outline, tonemapping, vignette, FXAA) into the output image.
 *
 *   renderer.BeginFrame(frame_index);              // after the frame fence
 *   renderer.Prepare(cmd, scene);                  // outside any pass
 *   renderer.RenderShadows(cmd, light_view_proj);  // outside any pass: the
 *                                                  // sun's map, point and area
 *                                                  // light cubes, light probes
 *   renderer.RenderView(cmd, cameras, ...);        // per output image
 *
 * Older Mesh/MeshInstance/static draws are recorded into the main camera's
 * pass through a callback, so sprites and code-built scenes still appear.
 */
#ifndef MERCURY_ENGINE_VK_SCENE_RENDERER_H
#define MERCURY_ENGINE_VK_SCENE_RENDERER_H

#include <vulkan/vulkan.h>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "globals.h"
#include "properties/render_mesh.h"
#include "properties/transform.h"
#include "scene/scene.h"
#include "vk_buffer.h"
#include "vk_context.h"
#include "vk_gpu_timer.h"

namespace Mercury::vk {

// Must match FrameUniforms in model_common.hlsli.
struct FrameUniforms {
  glm::mat4 view_proj;
  glm::mat4 view;
  glm::mat4 proj;
  glm::mat4 inv_view_proj;
  glm::mat4 light_view_proj;
  glm::vec4 light_dir;
  glm::vec4 light_color;
  glm::vec4 ambient_sky;
  glm::vec4 ambient_ground;
  glm::vec4 camera_pos;
  glm::vec4 resolution;
  glm::vec4 shadow_params;
  glm::vec4 flags;  // w: point light count
  glm::vec4 point_lights[2 * 16];  // position + range, color + shadow slot
  glm::vec4 point_shadows[16];     // radius, bias, soft
  glm::vec4 area_lights[4 * 4];    // center + range, right + two-sided, up + slot, color + soft
  glm::vec4 area_shadow_bias[4];
  glm::vec4 shadow_cubes_info[8];  // center + far
  glm::vec4 light_info;            // area count, capture pass, cube size, capture emission
  glm::vec4 gi_min;                // + enabled
  glm::vec4 gi_max;                // + intensity
  glm::vec4 gi_counts;             // + probes per atlas row
  // The sun's shadow over the whole scene, past the camera's map.
  glm::mat4 far_light_view_proj;
  glm::vec4 far_shadow_params;     // resolution (0: none), normal bias, -, use it alone
};
inline constexpr uint32_t kMaxPointLights = 16;
inline constexpr uint32_t kMaxAreaLights = 4;
inline constexpr uint32_t kMaxShadowCubes = 8;

// Must match DrawData in model_common.hlsli.
struct DrawData {
  glm::mat4 world;
  glm::mat4 normal_matrix;
  glm::vec4 color;
  glm::vec4 emission;
  glm::vec4 uv;
  glm::uvec4 info;
  glm::vec4 material;
  glm::uvec4 maps;  // x: normal map slot, y: emission map slot (0: none)
};
static_assert(sizeof(DrawData) == 224);

// The renderer's record of an entity it draws (a component in the scene's
// registry): which draw slots it has and what they were built from.
struct RenderProxy {
  std::vector<uint32_t> slots;
  glm::vec3 position = glm::vec3(0.0f);  // interpolated, when built
  glm::vec3 rotation = glm::vec3(0.0f);
  glm::vec3 scale = glm::vec3(0.0f);
  uint32_t layer = 0;
  bool visible = false;
  bool cast_shadows = false;
  uint32_t rendering_layers = 0;
  // Posed by an Animator (or skinned): rebuilt every frame.
  bool animated = false;
  uint64_t moved = 0;  // renderer frame it last moved; 0 never
  bool dirty = true;
  bool built = false;
};

class SceneRenderer;

// Forwards a registry's signals to the renderer drawing it. The registry's
// context and the renderer share it, so either may go first: the renderer
// clears `renderer` when it shuts down or moves to another scene.
struct RenderHooks {
  SceneRenderer* renderer = nullptr;
  void OnMeshChanged(entt::registry& registry, entt::entity entity);
  void OnProxyDestroyed(entt::registry& registry, entt::entity entity);
};

class SceneRenderer {
 public:
  static constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
  static constexpr VkFormat kNormalFormat = VK_FORMAT_R8G8B8A8_UNORM;
  static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
  static constexpr uint32_t kShadowResolution = 2048;
  static constexpr uint32_t kShadowCubeResolution = 512;
  static constexpr uint32_t kFarShadowResolution = 4096;
  static constexpr uint32_t kProbeFaceSize = 16;   // each face a probe captures
  static constexpr uint32_t kMaxProbeBatch = 32;   // probes updated per frame at most
  static constexpr uint32_t kProbesPerAtlasRow = 64;
  // Must match kProbeVisibilitySize in lighting_math.hlsli.
  static constexpr uint32_t kProbeVisibilitySize = 8;
  static constexpr uint32_t kMaxProbes = 4096;
  static constexpr uint32_t kAutoProbes = 512;      // probes of a volume fitted to the scene
  // Indices drawn per frame for probe captures; a scene's probes take more
  // frames to settle the more geometry it has.
  static constexpr uint64_t kProbeIndexBudget = 40'000'000;

  using ShaderLoader = std::function<VkShaderModule(const std::string&)>;
  // Records the older draws (skybox panorama, Mesh, static batches) into the
  // current pass of the main camera. The camera set is the legacy one.
  using LegacyDraws = std::function<void(VkCommandBuffer cmd, VkExtent2D extent)>;

  SceneRenderer() = default;
  ~SceneRenderer() { Shutdown(); }
  SceneRenderer(const SceneRenderer&) = delete;
  SceneRenderer& operator=(const SceneRenderer&) = delete;

  bool Init(Context& context, uint32_t frames_in_flight, const ShaderLoader& shaders);
  void Shutdown();

  // After waiting for the frame's fence.
  void BeginFrame(uint32_t frame_index);

  // Uploads new geometry and textures, and builds this frame's draw
  // records. Must be recorded outside a rendering pass.
  void Prepare(VkCommandBuffer cmd, GameScene& scene);

  // Draws the shadow casters into the shadow map and the point and area
  // lights' cube maps, then updates some light probes (outside a pass).
  void RenderShadows(VkCommandBuffer cmd, const glm::mat4& light_view_proj,
                     const RenderCamera& main_camera, bool enabled);

  /**
   * @brief Draw `cameras` (as GetRenderCameras returns them) and the post
   * chain into `output` (left in COLOR_ATTACHMENT_OPTIMAL)
   *
   * @param legacy Draws for the main camera's pass, may be empty
   * @param sky_texture A 2:1 panorama for the background, or null
   */
  void RenderView(VkCommandBuffer cmd, GameScene& scene,
                  const std::vector<RenderCamera>& cameras, VkImage output,
                  VkImageView output_view, VkExtent2D extent, VkFormat output_format,
                  VkImageLayout output_layout, const LegacyDraws& legacy,
                  VkImageView sky_texture, VkSampler sky_sampler);

  // Formats the legacy pipelines must be built for to draw into the main
  // pass: two color attachments (HDR, normal) and depth.
  static void MainPassFormats(VkFormat (&colors)[2], VkFormat& depth);

  VkImageView ShadowMapView() const { return shadow_image_.View(); }

  // Times the passes on the GPU (may be null).
  void SetGpuTimer(GpuTimer* timer) { gpu_timer_ = timer; }

 private:
  struct Pipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
  };

  struct GpuGeometry {
    uint32_t first_vertex = 0;
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    std::weak_ptr<const MeshGeometry> source;
  };

  struct GpuTexture {
    vk::Image image;
    uint32_t slot = 0;
    std::weak_ptr<const ImageData> source;
  };

  struct RenderTextureTarget;

  // Offscreen images for one output size.
  struct Targets {
    VkExtent2D extent = {};
    vk::Image hdr;
    vk::Image normal;
    vk::Image depth;
    vk::Image ao;
    vk::Image ao_blur;
    vk::Image bloom;  // half size, with mips
    uint32_t bloom_mips = 0;
    vk::Image outline;
    vk::Image ldr;    // composite result before FXAA
    VkDescriptorSet ssao_set = VK_NULL_HANDLE;
    VkDescriptorSet blur_set = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> bloom_down_sets;  // one per mip
    std::vector<VkDescriptorSet> bloom_up_sets;
    VkDescriptorSet composite_set = VK_NULL_HANDLE;
    VkDescriptorSet fxaa_set = VK_NULL_HANDLE;
    VkDescriptorSet sky_set = VK_NULL_HANDLE;
    VkImageView sky_view = VK_NULL_HANDLE;
    uint64_t last_used = 0;
  };

  struct RenderTextureTarget {
    vk::Image color;  // sRGB, sampled by materials
    uint32_t slot = 0;
    bool drawn = false;  // the slot still shows white until the first draw
    std::unique_ptr<Targets> targets;
  };

  // A draw slot: one RenderDraw of one entity, kept from frame to frame and
  // rebuilt only when the entity changes. Its DrawData is records_[slot].
  struct FrameDraw {
    const GpuGeometry* geometry = nullptr;
    uint32_t record = 0;  // its slot: index into records_ and the draw buffer
    uint32_t layer = 0;   // node layer
    uint32_t entity = 0;
    bool alive = false;   // the slot is in use
    bool transparent = false;
    bool double_sided = false;
    bool cast_shadows = true;
    uint32_t rendering_layers = 0;  // RenderMesh's, for outlines
    bool mirrored = false;  // negative scale: triangles wind the other way
    bool skinned = false;
    // Moved recently or animated: lit by the probes but not seen by them.
    bool dynamic = false;
    uint64_t moved = 0;  // frame its entity last moved; 0 never
    glm::vec3 center = glm::vec3(0.0f);  // world, for sorting
    glm::vec3 bounds_min = glm::vec3(0.0f);  // world box
    glm::vec3 bounds_max = glm::vec3(0.0f);
  };

  struct FrameResources {
    vk::Buffer uniforms;  // kMaxPasses FrameUniforms slots
    vk::Buffer bones;
    VkDescriptorSet set = VK_NULL_HANDLE;
    // The draw buffer the set points at: it is replaced when it grows, and a
    // set may only be rewritten once its frame is done.
    VkBuffer bound_draws = VK_NULL_HANDLE;
    std::vector<vk::Buffer> staging;  // uploads still in flight
    uint32_t passes_used = 0;
  };

  bool CreateLayouts();
  bool CreatePipelines(const ShaderLoader& shaders);
  bool CreateFrameResources();
  bool CreateShadowMap();
  bool CreateWhiteTexture();
  bool CreateLightingResources();
  // Brings the draw slots up to date with the scene: entities that moved or
  // changed are rebuilt, new ones get slots, removed ones free theirs.
  void SyncScene(VkCommandBuffer cmd, FrameResources& frame, GameScene& scene);
  // (Re)builds one entity's slots.
  void BuildProxy(VkCommandBuffer cmd, FrameResources& frame, GameScene& scene,
                  entt::entity entity, const RenderMesh& mesh, const Transform& transform,
                  RenderProxy& proxy);
  void FreeProxySlots(RenderProxy& proxy);
  uint32_t AllocateSlot();
  // Copies the changed records into the draw buffer (growing it if needed).
  void UploadRecords(VkCommandBuffer cmd, FrameResources& frame);
  // Marks still draws that moved lately as dynamic; changes to what is
  // still bump static_version_.
  void UpdateDynamic();
  // Picks which point and area lights get this frame's shadow cubes.
  void AssignShadowCubes();
  void RenderShadowCubes(VkCommandBuffer cmd, FrameResources& frame);
  void UpdateProbes(VkCommandBuffer cmd, FrameResources& frame);
  // The probe grid this frame: the scene's volume, or one fitted around
  // the still geometry. False when there is none.
  bool ResolveProbeVolume(ProbeVolumeData& volume);
  // `casters` into a sun shadow map (`image`, `size` square) as `view_proj`
  // sees them.
  void DrawSunShadow(VkCommandBuffer cmd, FrameResources& frame, vk::Image& image,
                     uint32_t size, const glm::mat4& view_proj,
                     const std::vector<const FrameDraw*>& casters);
  // Redraws the scene-wide sun shadow map when the sun or still geometry
  // changed.
  void UpdateFarShadow(VkCommandBuffer cmd, FrameResources& frame);
  // Uniforms of a 90 degree view along cube face `face` from `center`.
  FrameUniforms CubeFaceUniforms(glm::vec3 center, uint32_t face, float near_clip,
                                 float far_clip, VkExtent2D extent) const;
  // Lights, shadow cubes and probes, as every pass sees them.
  void FillLighting(FrameUniforms& u) const;
  void UpdateFrameSet(FrameResources& frame);

  VkPipeline BuildPipeline(VkShaderModule vs, VkShaderModule ps, bool model_input,
                           const std::vector<VkFormat>& colors, VkFormat depth,
                           int blend, bool depth_test, bool depth_write,
                           VkCompareOp compare, VkCullModeFlags cull, bool depth_bias,
                           VkPipelineLayout layout, uint32_t color_write_count = 0);

  const GpuGeometry* UploadGeometry(VkCommandBuffer cmd, FrameResources& frame,
                                    const std::shared_ptr<const MeshGeometry>& geometry);
  uint32_t TextureSlot(VkCommandBuffer cmd, FrameResources& frame,
                       const std::shared_ptr<ImageData>& image);
  uint32_t RenderTextureSlot(const std::string& path, int width, int height);
  // Frees textures and geometry nothing uses any more (e.g. after a map
  // change), once the frames in flight are done with them.
  void ReleaseUnused(VkCommandBuffer cmd);
  bool GrowBuffer(VkCommandBuffer cmd, vk::Buffer& buffer, VkDeviceSize needed,
                  VkBufferUsageFlags usage);
  void WriteTextureSlot(uint32_t slot, VkImageView view);

  Targets& GetTargets(VkExtent2D extent);
  bool CreateTargets(Targets& targets, VkExtent2D extent);
  void DestroyTargets(Targets& targets);
  VkDescriptorSet AllocatePostSet(const std::array<VkImageView, 6>& inputs);
  void WritePostSet(VkDescriptorSet set, const std::array<VkImageView, 6>& inputs);

  uint32_t PushUniforms(FrameResources& frame, const FrameUniforms& uniforms);
  // One camera's geometry into `targets` (hdr, normal, depth).
  // `linear`: write linear color (post-processing or a RenderTexture);
  // otherwise the shaders tonemap and encode themselves.
  void DrawCamera(VkCommandBuffer cmd, FrameResources& frame, const RenderCamera& camera,
                  Targets& targets, bool clear_color, bool clear_depth, bool overlay,
                  bool linear, const LegacyDraws* legacy, VkImageView sky_view, bool has_sky);
  void InitTargets(VkCommandBuffer cmd, Targets& targets);
  void DrawList(VkCommandBuffer cmd, FrameResources& frame, uint32_t uniform_offset,
                const std::vector<const FrameDraw*>& list, bool transparent_pass);
  void DrawOutlineMask(VkCommandBuffer cmd, FrameResources& frame, const RenderCamera& camera,
                       Targets& targets, const RenderSettings& settings);
  void PostProcess(VkCommandBuffer cmd, Targets& targets, const RenderCamera& camera,
                   const RenderSettings& settings, VkImage output, VkImageView output_view,
                   VkFormat output_format, VkImageLayout output_layout, bool srgb_output);
  void Fullscreen(VkCommandBuffer cmd, VkPipeline pipeline, VkDescriptorSet set,
                  VkImageView target, VkExtent2D extent, const void* params,
                  bool clear, bool load = false);
  FrameUniforms CameraUniforms(const RenderCamera& camera, VkExtent2D extent, bool overlay,
                               bool linear) const;
  VkPipeline PipelineForFormat(VkFormat format, bool fxaa);

  Context* context_ = nullptr;
  GpuTimer* gpu_timer_ = nullptr;
  uint32_t frames_in_flight_ = 2;
  uint32_t frame_index_ = 0;
  uint64_t frame_number_ = 0;
  VkDeviceSize uniform_stride_ = 256;

  VkDescriptorSetLayout frame_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout texture_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout post_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout model_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout post_pipeline_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkDescriptorPool texture_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet texture_set_ = VK_NULL_HANDLE;

  // [cull back, cull none]
  VkPipeline opaque_[2] = {};
  VkPipeline transparent_[2] = {};
  VkPipeline shadow_ = VK_NULL_HANDLE;
  VkPipeline outline_mask_ = VK_NULL_HANDLE;
  VkPipeline sky_ = VK_NULL_HANDLE;
  VkPipeline ssao_ = VK_NULL_HANDLE;
  VkPipeline blur_ = VK_NULL_HANDLE;
  VkPipeline bloom_down_ = VK_NULL_HANDLE;
  VkPipeline bloom_up_ = VK_NULL_HANDLE;
  VkPipeline shadow_cube_ = VK_NULL_HANDLE;   // distance to the light
  VkPipeline probe_capture_ = VK_NULL_HANDLE; // the scene as a probe sees it
  VkPipeline probe_sh_ = VK_NULL_HANDLE;      // capture -> spherical harmonics
  VkPipeline probe_visibility_pass_ = VK_NULL_HANDLE;  // capture -> distance map
  // Composite/FXAA pipelines per output format.
  std::map<std::pair<VkFormat, bool>, VkPipeline> output_pipelines_;
  VkShaderModule fullscreen_vs_ = VK_NULL_HANDLE;
  VkShaderModule composite_ps_ = VK_NULL_HANDLE;
  VkShaderModule fxaa_ps_ = VK_NULL_HANDLE;

  VkSampler material_sampler_ = VK_NULL_HANDLE;
  VkSampler clamp_sampler_ = VK_NULL_HANDLE;
  VkSampler point_sampler_ = VK_NULL_HANDLE;
  VkSampler shadow_sampler_ = VK_NULL_HANDLE;

  vk::Image shadow_image_;
  bool shadow_written_ = false;

  // The sun's shadow over all still geometry, for what lies past the
  // camera's map and for the probes. Drawn again only when it changes.
  vk::Image far_shadow_image_;
  bool far_shadow_valid_ = false;
  glm::mat4 far_view_proj_ = glm::mat4(1.0f);
  float far_normal_bias_ = 0.0f;

  // This frame's still, opaque draws the main camera sees (what the probes
  // and the far shadow map see), and their bounds. Rebuilt when
  // static_version_ or the camera's mask changes.
  std::vector<const FrameDraw*> still_;
  glm::vec3 still_min_ = glm::vec3(0.0f), still_max_ = glm::vec3(0.0f);
  uint64_t still_indices_ = 0;
  uint64_t still_version_ = ~0ull;
  uint32_t still_mask_ = 0;
  const FrameDraw* still_base_ = nullptr;  // draws_ when it was built
  // Bumped whenever still geometry changes (added, removed, moved, recolored,
  // or turning still or dynamic): what the far shadow, the shadow cubes and
  // the probes saw is out of date.
  uint64_t static_version_ = 1;
  uint64_t far_shadow_version_ = 0;
  glm::vec3 far_shadow_direction_ = glm::vec3(0.0f);

  // Point and area light shadows: 6 faces per slot of distances to the
  // nearest caster.
  struct ShadowCube {
    glm::vec3 center = glm::vec3(0.0f);
    float far_clip = 10.0f;
    // Single-sided area lights: only what is in front of this plane casts.
    bool one_sided = false;
    glm::vec3 normal = glm::vec3(0.0f);
  };
  vk::Image shadow_cubes_;
  vk::Image shadow_cube_depth_;
  std::vector<ShadowCube> cubes_;   // this frame's slots
  std::vector<int> point_slots_;    // per point light: slot or -1
  std::vector<int> area_slots_;     // per area light
  // What each slot last drew; a slot whose light and casters are unchanged
  // keeps its faces.
  std::array<uint64_t, kMaxShadowCubes> cube_hashes_ = {};

  // Light probes: 9 spherical harmonics texels each in the atlas.
  vk::Image probe_atlas_;
  // Per probe an octahedral map of the distance to the nearest surface
  // (mean, mean square), so walls block the probes behind them.
  vk::Image probe_visibility_;
  vk::Image probe_faces_;   // kMaxProbeBatch probes x 6 faces
  vk::Image probe_normal_;  // the pass's second attachment, unused
  vk::Image probe_depth_;
  VkDescriptorSet probe_sh_set_ = VK_NULL_HANDLE;
  bool lighting_initialized_ = false;
  struct ProbeState {
    bool active = false;        // the volume's probes light this frame
    glm::vec3 min = glm::vec3(0.0f), max = glm::vec3(0.0f);
    glm::ivec3 counts = glm::ivec3(0);
    uint32_t next = 0;          // next probe to update
    int passes = 0;             // full passes since the scene last changed
    uint64_t light_hash = 0;    // the lights they saw
    uint64_t static_version = 0;  // and the still geometry
    bool clear_atlas = true;
    float intensity = 1.0f;
    // A volume fitted to the scene: the geometry's box it was fitted to, and
    // the grid.
    bool fitted = false;
    glm::vec3 fit_min = glm::vec3(0.0f), fit_max = glm::vec3(0.0f);
    glm::vec3 grid_min = glm::vec3(0.0f), grid_max = glm::vec3(0.0f);
    glm::ivec3 grid_counts = glm::ivec3(0);
  };
  ProbeState probes_;

  std::vector<FrameResources> frames_;

  // The scene the slots belong to, and the hooks its registry signals.
  friend struct RenderHooks;
  entt::registry* registry_ = nullptr;
  std::shared_ptr<RenderHooks> hooks_;
  // Entities whose RenderMesh was added, replaced or removed, or that turned
  // active or inactive: rebuilt next frame.
  std::vector<entt::entity> changed_;
  // A geometry buffer restart: every slot is rebuilt.
  bool rebuild_all_ = false;
  void AttachRegistry(entt::registry& registry);
  void FreeAllSlots();

  // Persistent draw records, by slot, and the device-local buffer the
  // shaders read them from. Changed slots are copied each frame.
  vk::Buffer draw_buffer_;
  vk::Buffer instance_buffer_;  // 0, 1, 2, ... (the draw index per instance)
  std::vector<uint32_t> free_draw_slots_;
  std::vector<uint32_t> dirty_records_;
  std::vector<uint8_t> record_dirty_;  // by slot, to list each once

  vk::Buffer vertex_buffer_;
  vk::Buffer index_buffer_;
  VkDeviceSize vertex_capacity_ = 0;  // in vertices
  VkDeviceSize index_capacity_ = 0;   // in indices
  uint32_t vertices_used_ = 0;
  uint32_t indices_used_ = 0;
  std::unordered_map<uint64_t, GpuGeometry> geometry_;

  static constexpr uint32_t kMaxTextures = 4096;
  std::unordered_map<uint64_t, GpuTexture> textures_;
  std::map<std::string, RenderTextureTarget> render_textures_;
  uint32_t next_slot_ = 1;  // 0 is white
  vk::Image white_;

  std::vector<std::unique_ptr<Targets>> targets_;

  // Old buffers and images that frames in flight may still use.
  struct Garbage {
    uint64_t frame;
    vk::Buffer buffer;
  };
  std::vector<Garbage> garbage_;
  struct ImageGarbage {
    uint64_t frame;
    vk::Image image;
    uint32_t slot;  // freed with the image
  };
  std::vector<ImageGarbage> image_garbage_;
  std::vector<uint32_t> free_slots_;

  // Every slot, by slot (free ones have alive false).
  std::vector<FrameDraw> draws_;
  std::vector<DrawData> records_;
  std::vector<glm::mat4> bones_;
  RenderSettings settings_;
  SceneLighting lighting_;
  glm::mat4 light_view_proj_ = glm::mat4(1.0f);
  bool shadows_enabled_ = false;
};

}  // namespace Mercury::vk

#endif  // MERCURY_ENGINE_VK_SCENE_RENDERER_H
