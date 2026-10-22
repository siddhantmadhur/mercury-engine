/*
 * vk_ui_renderer.h
 *
 * Draws UI Toolkit draw lists (Mercury::UI::UIDrawData) with Vulkan: one
 * pipeline (ui.vs/ui.ps), per-frame vertex and index buffers that grow as
 * needed, and a GPU copy of every UITexture that is kept in step with the
 * CPU one.
 *
 *   ui_.BeginFrame(frame_index);                        // after the fence
 *   auto batch = ui_.Prepare(cmd, *draw_data);          // outside a pass
 *   ...vkCmdBeginRendering...
 *   ui_.Record(cmd, batch, extent);                     // inside the pass
 */
#ifndef MERCURY_ENGINE_VK_UI_RENDERER_H
#define MERCURY_ENGINE_VK_UI_RENDERER_H

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ui/draw_data.h"
#include "vk_buffer.h"
#include "vk_context.h"

namespace Mercury::vk {

class UIRenderer {
 public:
  struct Batch {
    const UI::UIDrawData* data = nullptr;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceSize vertex_offset = 0;
    VkDeviceSize index_offset = 0;
    bool valid() const { return data != nullptr; }
  };

  UIRenderer() = default;
  ~UIRenderer() { Shutdown(); }
  UIRenderer(const UIRenderer&) = delete;
  UIRenderer& operator=(const UIRenderer&) = delete;

  // The shader modules are only needed while the pipeline is created.
  bool Init(Context& context, VkFormat color_format, VkFormat depth_format,
            VkShaderModule vertex_shader, VkShaderModule pixel_shader,
            uint32_t frames_in_flight);
  void Shutdown();

  // Call once per frame, after waiting for that frame's fence.
  void BeginFrame(uint32_t frame_index);
  // Uploads a draw list and any textures it needs. Must be recorded outside
  // a rendering pass, before the pass that draws it.
  Batch Prepare(VkCommandBuffer cmd, const UI::UIDrawData& data);
  // Draws a prepared list into the current rendering pass.
  void Record(VkCommandBuffer cmd, const Batch& batch, VkExtent2D extent);

 private:
  struct GpuTexture {
    vk::Image image;
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint64_t version = 0;
    int width = 0, height = 0;
    bool linear = true;
  };
  struct FrameBuffers {
    vk::Buffer vertices;
    vk::Buffer indices;
    VkDeviceSize vertices_used = 0;
    VkDeviceSize indices_used = 0;
    std::vector<vk::Buffer> staging;  // texture uploads still in flight
  };
  struct Garbage {
    uint64_t frame;
    std::unique_ptr<GpuTexture> texture;
  };

  bool CreatePipeline(VkFormat color_format, VkFormat depth_format,
                      VkShaderModule vs, VkShaderModule ps);
  GpuTexture* SyncTexture(VkCommandBuffer cmd, uint32_t id);
  bool Upload(VkCommandBuffer cmd, GpuTexture& texture, const uint8_t* rgba,
              int width, int height);
  bool EnsureCapacity(vk::Buffer& buffer, VkDeviceSize needed,
                      VkBufferUsageFlags usage);
  void Retire(std::unique_ptr<GpuTexture> texture);
  void CollectGarbage(bool everything);

  Context* context_ = nullptr;
  uint32_t frames_in_flight_ = 0;
  uint32_t frame_index_ = 0;
  uint64_t frame_counter_ = 0;
  uint64_t registry_generation_ = 0;

  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkSampler linear_sampler_ = VK_NULL_HANDLE;
  VkSampler nearest_sampler_ = VK_NULL_HANDLE;

  std::vector<FrameBuffers> frames_;
  std::unordered_map<uint32_t, std::unique_ptr<GpuTexture>> textures_;
  std::vector<Garbage> garbage_;
};

}  // namespace Mercury::vk

#endif  // MERCURY_ENGINE_VK_UI_RENDERER_H
