// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * vk_scene_renderer.h
 *
 * Basic Vulkan model renderer: uploads RenderMesh geometry and textures,
 * syncs ECS transforms/visibility/skinning, and draws opaque & transparent
 * meshes directly into the target image (no lighting, GI, AO, or post chain).
 */
#ifndef MERCURY_ENGINE_VK_SCENE_RENDERER_H
#define MERCURY_ENGINE_VK_SCENE_RENDERER_H

#include <vulkan/vulkan.h>

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
  glm::vec4 flags;  // x > 0.5: encode linear -> sRGB
};

// Must match DrawData in model_common.hlsli.
struct DrawData {
  glm::mat4 world;
  glm::vec4 color;
  glm::vec4 uv;
  glm::uvec4 info;  // x: texture slot, y: flags, z: first bone (~0: rigid)
};
static_assert(sizeof(DrawData) == 112);

// The renderer's record of an entity it draws.
struct RenderProxy {
  std::vector<uint32_t> slots;
  glm::vec3 position = glm::vec3(0.0f);
  glm::vec3 rotation = glm::vec3(0.0f);
  glm::vec3 scale = glm::vec3(0.0f);
  uint32_t layer = 0;
  bool inactive = false;
  bool mesh_visible = false;
  bool visible = false;
  bool animated = false;
  bool dirty = true;
  bool built = false;
};

class SceneRenderer;

struct RenderHooks {
  SceneRenderer* renderer = nullptr;
  void OnMeshChanged(entt::registry& registry, entt::entity entity);
  void OnProxyDestroyed(entt::registry& registry, entt::entity entity);
};

class SceneRenderer {
 public:
  static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

  using ShaderLoader = std::function<VkShaderModule(const std::string&)>;
  using LegacyDraws =
      std::function<void(VkCommandBuffer cmd, VkExtent2D extent)>;

  SceneRenderer() = default;
  ~SceneRenderer() { Shutdown(); }
  SceneRenderer(const SceneRenderer&) = delete;
  SceneRenderer& operator=(const SceneRenderer&) = delete;

  bool Init(Context& context, uint32_t frames_in_flight,
            const ShaderLoader& shaders);
  void Shutdown();

  void BeginFrame(uint32_t frame_index);
  void Prepare(VkCommandBuffer cmd, GameScene& scene);

  void RenderView(VkCommandBuffer cmd, GameScene& scene,
                  const std::vector<RenderCamera>& cameras, VkImage output,
                  VkImageView output_view, VkExtent2D extent,
                  VkFormat output_format, VkImageLayout output_layout,
                  const LegacyDraws& legacy, VkImageView sky_texture,
                  VkSampler sky_sampler);

  void SetGpuTimer(GpuTimer* timer) { gpu_timer_ = timer; }

 private:
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

  struct DepthTarget {
    VkExtent2D extent = {};
    vk::Image depth;
    uint64_t last_used = 0;
  };

  struct RenderTextureTarget {
    vk::Image color;
    uint32_t slot = 0;
    bool drawn = false;
    VkExtent2D extent = {};
  };

  struct FrameDraw {
    const GpuGeometry* geometry = nullptr;
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    uint32_t record = 0;
    uint32_t layer = 0;
    bool alive = false;
    bool transparent = false;
    bool double_sided = false;
    bool mirrored = false;
    glm::vec3 center = glm::vec3(0.0f);
  };

  struct FrameResources {
    vk::Buffer uniforms;
    vk::Buffer bones;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkBuffer bound_draws = VK_NULL_HANDLE;
    std::vector<vk::Buffer> staging;
    uint32_t passes_used = 0;
  };

  struct FormatPipelines {
    VkPipeline opaque[2] = {};
    VkPipeline transparent[2] = {};
    VkPipeline skybox = VK_NULL_HANDLE;
  };

  bool CreateLayouts();
  bool CreateFrameResources();
  void UpdateFrameSet(FrameResources& frame);

  const FormatPipelines& GetPipelines(VkFormat color_format);
  VkPipeline BuildModelPipeline(VkFormat color_format, bool transparent,
                                VkCullModeFlags cull);
  VkPipeline BuildSkyboxPipeline(VkFormat color_format);

  void SyncScene(VkCommandBuffer cmd, FrameResources& frame, GameScene& scene);
  void BuildProxy(VkCommandBuffer cmd, FrameResources& frame, GameScene& scene,
                  entt::entity entity, const RenderMesh& mesh,
                  const Transform& transform, RenderProxy& proxy);
  void FreeProxySlots(RenderProxy& proxy);
  uint32_t AllocateSlot();
  void MarkSlotDirty(uint32_t slot);
  void UploadRecords(VkCommandBuffer cmd, FrameResources& frame);

  const GpuGeometry* UploadGeometry(
      VkCommandBuffer cmd, FrameResources& frame,
      const std::shared_ptr<const MeshGeometry>& geometry);
  uint32_t TextureSlot(VkCommandBuffer cmd, FrameResources& frame,
                       const std::shared_ptr<ImageData>& image);
  uint32_t RenderTextureSlot(const std::string& path, int width, int height);
  void ReleaseUnused();
  bool GrowBuffer(VkCommandBuffer cmd, vk::Buffer& buffer, VkDeviceSize needed,
                  VkBufferUsageFlags usage);
  void WriteTextureSlot(uint32_t slot, VkImageView view);

  DepthTarget& GetDepthTarget(VkExtent2D extent);
  uint32_t PushUniforms(FrameResources& frame, const FrameUniforms& uniforms);

  void DrawCamera(VkCommandBuffer cmd, FrameResources& frame,
                  const RenderCamera& camera, VkImage output,
                  VkImageView output_view, VkExtent2D extent,
                  VkFormat output_format, VkImageLayout output_layout,
                  bool clear_color, bool clear_depth,
                  const LegacyDraws* legacy, VkImageView sky_texture,
                  VkSampler sky_sampler);
  void DrawList(VkCommandBuffer cmd, FrameResources& frame,
                uint32_t uniform_offset,
                const std::vector<const FrameDraw*>& list,
                const VkPipeline (&pipelines)[2]);

  void AttachRegistry(entt::registry& registry);
  void FreeAllSlots();

  Context* context_ = nullptr;
  GpuTimer* gpu_timer_ = nullptr;
  uint32_t frames_in_flight_ = 2;
  uint32_t frame_index_ = 0;
  uint64_t frame_number_ = 0;
  VkDeviceSize uniform_stride_ = 256;

  VkDescriptorSetLayout frame_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout texture_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout skybox_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout model_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout skybox_pipeline_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkDescriptorPool texture_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet texture_set_ = VK_NULL_HANDLE;
  VkDescriptorSet skybox_set_ = VK_NULL_HANDLE;
  VkImageView last_sky_view_ = VK_NULL_HANDLE;
  VkSampler last_sky_sampler_ = VK_NULL_HANDLE;

  VkShaderModule model_vs_ = VK_NULL_HANDLE;
  VkShaderModule model_ps_ = VK_NULL_HANDLE;
  VkShaderModule skybox_vs_ = VK_NULL_HANDLE;
  VkShaderModule skybox_ps_ = VK_NULL_HANDLE;
  std::map<VkFormat, FormatPipelines> pipelines_;

  VkSampler material_sampler_ = VK_NULL_HANDLE;
  VkSampler clamp_sampler_ = VK_NULL_HANDLE;

  std::vector<FrameResources> frames_;

  friend struct RenderHooks;
  entt::registry* registry_ = nullptr;
  std::shared_ptr<RenderHooks> hooks_;
  std::vector<entt::entity> changed_;
  bool rebuild_all_ = false;

  vk::Buffer draw_buffer_;
  vk::Buffer instance_buffer_;
  std::vector<uint32_t> free_draw_slots_;
  std::vector<uint32_t> dirty_records_;
  std::vector<uint8_t> record_dirty_;

  vk::Buffer vertex_buffer_;
  vk::Buffer index_buffer_;
  VkDeviceSize vertex_capacity_ = 0;
  VkDeviceSize index_capacity_ = 0;
  uint32_t vertices_used_ = 0;
  uint32_t indices_used_ = 0;
  std::unordered_map<uint64_t, GpuGeometry> geometry_;

  static constexpr uint32_t kMaxTextures = 4096;
  std::unordered_map<uint64_t, GpuTexture> textures_;
  std::map<std::string, RenderTextureTarget> render_textures_;
  uint32_t next_slot_ = 1;
  vk::Image white_;

  std::vector<std::unique_ptr<DepthTarget>> depth_targets_;

  struct Garbage {
    uint64_t frame;
    vk::Buffer buffer;
  };
  std::vector<Garbage> garbage_;
  struct ImageGarbage {
    uint64_t frame;
    vk::Image image;
    uint32_t slot;
  };
  std::vector<ImageGarbage> image_garbage_;
  std::vector<uint32_t> free_slots_;

  std::vector<FrameDraw> draws_;
  std::vector<DrawData> records_;
  std::vector<glm::mat4> bones_;
};

}  // namespace Mercury::vk

#endif  // MERCURY_ENGINE_VK_SCENE_RENDERER_H
