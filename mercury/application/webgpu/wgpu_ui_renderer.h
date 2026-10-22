/*
 * wgpu_ui_renderer.h
 *
 * Draws UI Toolkit draw lists (Mercury::UI::UIDrawData) with WebGPU, the
 * counterpart of vk::UIRenderer.
 *
 *   ui_.Prepare(*draw_data);               // before the pass is submitted
 *   ui_.Record(pass, width, height);       // inside the render pass
 */
#ifndef MERCURY_ENGINE_WGPU_UI_RENDERER_H
#define MERCURY_ENGINE_WGPU_UI_RENDERER_H

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <unordered_map>

#include "ui/draw_data.h"

namespace Mercury::wgpu_ui {

class UIRenderer {
 public:
  bool Init(wgpu::Device device, wgpu::Queue queue, wgpu::TextureFormat color_format,
            wgpu::TextureFormat depth_format);
  // Uploads the draw list and its textures; false if there is nothing to draw.
  bool Prepare(const UI::UIDrawData& data, uint32_t width, uint32_t height);
  void Record(wgpu::RenderPassEncoder& pass, uint32_t width, uint32_t height);

 private:
  struct GpuTexture {
    wgpu::Texture texture;
    wgpu::BindGroup bind_group;
    uint64_t version = 0;
    int width = 0, height = 0;
    bool linear = true;
  };
  GpuTexture* SyncTexture(uint32_t id);
  bool EnsureBuffer(wgpu::Buffer& buffer, uint64_t& capacity, uint64_t bytes,
                    wgpu::BufferUsage usage);

  wgpu::Device device_;
  wgpu::Queue queue_;
  wgpu::RenderPipeline pipeline_;
  wgpu::BindGroupLayout uniform_layout_;
  wgpu::BindGroupLayout texture_layout_;
  wgpu::BindGroup uniform_group_;
  wgpu::Buffer uniform_buffer_;
  wgpu::Sampler linear_sampler_;
  wgpu::Sampler nearest_sampler_;
  wgpu::Buffer vertex_buffer_;
  wgpu::Buffer index_buffer_;
  uint64_t vertex_capacity_ = 0;
  uint64_t index_capacity_ = 0;
  const UI::UIDrawData* prepared_ = nullptr;
  uint64_t registry_generation_ = 0;
  std::unordered_map<uint32_t, GpuTexture> textures_;
};

}  // namespace Mercury::wgpu_ui

#endif  // MERCURY_ENGINE_WGPU_UI_RENDERER_H
