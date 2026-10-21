#ifndef MERCURYENGINE_VULKAN_APPLICATION_H
#define MERCURYENGINE_VULKAN_APPLICATION_H

#include <vulkan/vulkan.h>

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "application.h"
#include "globals.h"
#include "vulkan/vk_buffer.h"
#include "vulkan/vk_context.h"
#include "vulkan/vk_swapchain.h"

namespace Mercury {

struct CameraCBuffer {
  PQ_MATRIX mWorldViewProj;
  PQ_FLOAT2 mResolution;
  PQ_FLOAT2 _pad;
};

// Cached per-mesh draw-call info, rebuilt only when entities are added or
// removed. instance_count counts how many instance slots in the persistent
// instance_buffer_ belong to this mesh's draw group, starting at
// start_instance.
struct MeshLayoutEntry {
  UINT index_count = 0;
  UINT start_index = 0;
  UINT start_instance = 0;
  UINT instance_count = 0;
};

class VulkanApplication : public Application {
 public:
  VulkanApplication(const std::string& window_title,
                    float initial_width = 1280, float initial_height = 720);
  ~VulkanApplication() override;

 protected:
  bool OnLoad() override;      // Runs when the application is created
  void Render() override;      // Renders objects
  bool Initialize() override;  // Creates the device, swapchain and ImGui
  void OnNewTick() override;

  void OnResize(int32_t width, int32_t height) override;
  void ImGuiNewFrame() override;

 private:
  static constexpr uint32_t kFramesInFlight = 2;
  static constexpr UINT kMaxInstances = 8192;
  static constexpr size_t kMaxVertices = 64000;
  static constexpr size_t kMaxStaticVertices = 640000;
  static constexpr size_t kMaxIndices = 64000;
  static constexpr VkDeviceSize kInstanceBufferBytes =
      sizeof(VsModelBuffer) * kMaxInstances;

  struct FrameData {
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkSemaphore image_available = VK_NULL_HANDLE;
    VkFence in_flight = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

    vk::Buffer camera_ubo;
    vk::Buffer vertices;
    vk::Buffer indices;
    vk::Buffer static_vertices;
    vk::Buffer static_indices;
    // Mirrors the instance buffer layout (slot N at N * sizeof(VsModelBuffer))
    // so each copy region uses the same src and dst offset.
    vk::Buffer instance_staging;
    // Only alive while an atlas upload recorded in this frame is in flight.
    vk::Buffer atlas_staging;
  };

  bool CreateFrameResources();
  bool CreateDescriptors();
  bool CreatePipelines();
  bool CreateAtlasImage(uint32_t width, uint32_t height);
  void WriteDescriptorSets();
  bool InitImGui();
  void RecreateSwapchain();
  VkShaderModule LoadShaderModule(const std::string& file_name) const;

  // Records a one-off command buffer and waits for it to finish.
  template <typename F>
  bool SubmitImmediate(F&& record);

  // Copies data into a mapped per-frame buffer, logging (once) and returning
  // false if it does not fit.
  bool UploadToFrameBuffer(vk::Buffer& buffer, const void* data,
                           VkDeviceSize bytes, const char* name);

  void RecordAtlasUpload(FrameData& frame, TextureAtlas& atlas);
  void RecordInstanceUploads(FrameData& frame);

  // Rebuild mesh_layout_/mesh_order_/entity_slot_ and stage the full
  // instance buffer for upload. Called on first frame and whenever an EnTT
  // lifecycle signal flips layout_dirty_.
  void RebuildInstanceLayout(entt::registry& registry, TextureAtlas& atlas,
                             FrameData& frame);

  // Walk every Transform marked dirty, build its VsModelBuffer, and stage a
  // copy of just that slot. No-op if nothing is dirty.
  void UpdateDirtyInstances(entt::registry& registry, TextureAtlas& atlas,
                            FrameData& frame);

  // Build a VsModelBuffer record for an instance. parent_mesh_entity is the
  // owning Mesh entity (== entity when the entity itself is a Mesh, or the
  // MeshInstance's parent_entity otherwise). Opacity comes from the parent
  // Mesh; atlas UV from entity's Texture2D, else parent's, else white pixel.
  VsModelBuffer BuildInstanceRecord(entt::registry& registry,
                                    TextureAtlas& atlas, entt::entity entity,
                                    entt::entity parent_mesh_entity);

  void EnsureRegistrySignalsHooked(entt::registry& registry);
  void OnLayoutChanged() { layout_dirty_ = true; }

  vk::Context context_;
  vk::Swapchain swapchain_;
  bool swapchain_dirty_ = false;

  std::array<FrameData, kFramesInFlight> frames_;
  uint32_t frame_index_ = 0;
  VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline instanced_pipeline_ = VK_NULL_HANDLE;
  VkPipeline static_pipeline_ = VK_NULL_HANDLE;
  VkSampler texture_sampler_ = VK_NULL_HANDLE;

  vk::Buffer instance_buffer_;  // device-local, kMaxInstances records
  std::vector<VkBufferCopy> instance_copies_;  // staged this frame

  vk::Image atlas_image_;
  VkImageLayout atlas_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;

  bool imgui_glfw_initialized_ = false;
  bool imgui_vulkan_initialized_ = false;
  bool overflow_logged_ = false;

  CameraCBuffer camera_ = {};
  std::vector<Vertex> vertex_buffer_;
  std::vector<UINT> index_buffer_;

  // Persistent instance layout. Tracked across frames so that frames where
  // nothing structurally changed can patch only the slots whose Transform
  // (or companion Texture2D) is dirty, instead of rebuilding every frame.
  std::unordered_map<entt::entity, MeshLayoutEntry> mesh_layout_;
  std::vector<entt::entity> mesh_order_;
  std::unordered_map<entt::entity, UINT> entity_slot_;
  std::vector<entt::entity> slot_owner_;   // slot index -> entity
  std::vector<entt::entity> slot_parent_;  // slot index -> parent mesh entity
  bool layout_dirty_ = true;
  entt::registry* hooked_registry_ = nullptr;
};
}  // namespace Mercury

#endif  // MERCURYENGINE_VULKAN_APPLICATION_H
