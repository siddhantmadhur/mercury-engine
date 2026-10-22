#ifndef MERCURYENGINE_WEBGPU_APPLICATION_H
#define MERCURYENGINE_WEBGPU_APPLICATION_H

#include "application/application.h"
#include "camera_buffer.h"

#include <webgpu/webgpu_cpp.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <map>
#include <string>
#include <vector>

#include "globals.h"

namespace Mercury {

class WebGpuApplication : public Application {
 public:
  WebGpuApplication(const std::string& window_title, float initial_width = 1280,
                    float initial_height = 720);
  ~WebGpuApplication() override;

 protected:
  bool Initialize() override;
  bool OnLoad() override;
  void Render() override;
  void OnNewTick() override;
  void OnResize(int32_t width, int32_t height) override;
  void ImGuiNewFrame() override;
  bool IsDeviceReady() const override { return device_ready_; }

 private:
  struct MeshLayoutEntry {
    UINT start_index = 0;
    UINT index_count = 0;
    UINT start_instance = 0;
    UINT instance_count = 0;
  };

  bool CreateDeviceAndSurface();
  void OnDeviceReady();
  bool ConfigureSurface(int width, int height);
  bool CreatePipelines();
  bool CreateBuffers();
  bool CreateShadowResources();
  bool CreateAtlasTexture(uint32_t width, uint32_t height);
  void UpdateAtlasTexture();
  // Loads the scene's skybox panorama when its path changed.
  void UpdateSkyboxTexture();

  void EnsureRegistrySignalsHooked(entt::registry& registry);
  void OnLayoutChanged(entt::registry&, entt::entity) { layout_dirty_ = true; }
  void RebuildInstanceLayout(entt::registry& registry, TextureAtlas& atlas);
  void UpdateDirtyInstances(entt::registry& registry, TextureAtlas& atlas);
  VsModelBuffer BuildInstanceRecord(entt::registry& registry,
                                    TextureAtlas& atlas, entt::entity entity,
                                    entt::entity parent_mesh_entity);

  void RecordShadowPass(wgpu::CommandEncoder& encoder);
  void RecordScenePass(wgpu::CommandEncoder& encoder,
                       wgpu::TextureView& current_view);

  // WebGPU Core
  wgpu::Instance instance_;
  wgpu::Surface surface_;
  wgpu::Adapter adapter_;
  wgpu::Device device_;
  wgpu::Queue queue_;
  wgpu::TextureFormat surface_format_ = wgpu::TextureFormat::BGRA8Unorm;

  // Render targets
  wgpu::Texture depth_texture_;
  wgpu::TextureView depth_view_;

  // Shadow resources
  static constexpr uint32_t kShadowMapResolution = 2048;
  wgpu::Texture shadow_texture_;
  wgpu::TextureView shadow_view_;
  wgpu::Sampler shadow_sampler_;

  // Atlas resources
  wgpu::Texture atlas_texture_;
  wgpu::TextureView atlas_view_;
  wgpu::Sampler atlas_sampler_;
  uint32_t atlas_width_ = 0;
  uint32_t atlas_height_ = 0;

  // Skybox resources; no bind group = draw the clear color only
  wgpu::Texture skybox_texture_;
  wgpu::Sampler skybox_sampler_;
  wgpu::Buffer skybox_ubo_;
  wgpu::BindGroupLayout skybox_bind_group_layout_;
  wgpu::BindGroup skybox_bind_group_;
  wgpu::RenderPipeline skybox_pipeline_;
  std::string skybox_loaded_path_;

  // Uniform Buffers & Layouts
  CameraCBuffer camera_ = {};
  wgpu::Buffer camera_ubo_;
  wgpu::Buffer shadow_ubo_;

  wgpu::BindGroupLayout main_bind_group_layout_;
  wgpu::BindGroup main_bind_group_;

  wgpu::BindGroupLayout shadow_bind_group_layout_;
  wgpu::BindGroup shadow_bind_group_;

  // Pipelines
  wgpu::RenderPipeline instanced_pipeline_;
  wgpu::RenderPipeline static_pipeline_;
  wgpu::RenderPipeline shadow_pipeline_;

  // Geometry & Instance Buffers
  static constexpr size_t kMaxVertices = 131072;
  static constexpr size_t kMaxIndices = 262144;
  static constexpr size_t kMaxInstances = 4096;

  wgpu::Buffer vertex_buffer_;
  wgpu::Buffer index_buffer_;
  wgpu::Buffer instance_buffer_;

  wgpu::Buffer static_vertex_buffer_;
  wgpu::Buffer static_index_buffer_;

  std::vector<Vertex> vertex_buffer_cpu_;
  std::vector<UINT> index_buffer_cpu_;

  // Scene instance layout caching
  bool device_ready_ = false;
  bool layout_dirty_ = true;
  entt::registry* hooked_registry_ = nullptr;
  std::map<entt::entity, MeshLayoutEntry> mesh_layout_;
  std::vector<entt::entity> mesh_order_;
  std::map<entt::entity, UINT> entity_slot_;
  std::vector<entt::entity> slot_owner_;
  std::vector<entt::entity> slot_parent_;
  std::vector<VsModelBuffer> instance_cpu_cache_;
};

}  // namespace Mercury

#endif  // MERCURYENGINE_WEBGPU_APPLICATION_H
