//
// Created by smadhur on 4/28/2026.
//

#ifndef PEQUODENGINE_VULKAN_APPLICATION_H
#define PEQUODENGINE_VULKAN_APPLICATION_H

#ifdef PEQUOD_GRAPHICS_VULKAN

#include <vulkan/vulkan.h>

#include <unordered_map>
#include <vector>

#include "application.h"

namespace Pequod {

struct CameraCBuffer {
  PQ_MATRIX mWorldViewProj;
};

class VulkanApplication : public Application {
 public:
  VulkanApplication(const std::string& window_title, float initial_width = 1280,
                    float initial_height = 720);
  ~VulkanApplication() override;

 protected:
  bool Initialize() override;
  bool OnLoad() override;
  void Render() override;
  void OnResize(int32_t width, int32_t height) override;
  void ImGuiNewFrame() override;

 private:
  // Setup
  bool CreateInstance();
  bool CreateSurface();
  bool PickPhysicalDevice();
  bool CreateDevice();
  bool CreateSwapchainResources();
  void DestroySwapchainResources();
  bool CreateRenderPass();
  bool CreateFramebuffers();
  bool CreateCommandPool();
  bool CreateSyncObjects();
  bool CreateDescriptorSetLayouts();
  bool CreatePipelines();
  bool CreateBuffers();
  bool CreateDescriptorPool();
  bool CreateUniformDescriptorSet();
  bool CreateSampler();

  // Helpers
  VkShaderModule LoadShaderModule(const std::string& spv_path);
  VkPipeline BuildPipeline(VkShaderModule vs, VkShaderModule fs,
                           VkPipelineLayout layout);
  bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags props, VkBuffer& buffer,
                    VkDeviceMemory& memory);
  uint32_t FindMemoryType(uint32_t type_bits, VkMemoryPropertyFlags props);
  VkCommandBuffer BeginOneShot();
  void EndOneShot(VkCommandBuffer cb);

  struct CachedTex {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
  };
  CachedTex& GetOrCreateTexture(const unsigned char* data, int width,
                                int height);

  // Core
  VkInstance instance_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  uint32_t queue_family_ = 0;
  VkQueue queue_ = VK_NULL_HANDLE;

  // Swapchain
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat swapchain_format_ = VK_FORMAT_B8G8R8A8_UNORM;
  VkExtent2D swapchain_extent_ = {};
  std::vector<VkImage> swapchain_images_;
  std::vector<VkImageView> swapchain_views_;
  std::vector<VkFramebuffer> framebuffers_;

  VkRenderPass render_pass_ = VK_NULL_HANDLE;

  // Descriptor + pipeline. DXC's -fvk-{b,t,s}-shift only changes the binding
  // index, not the set, so every HLSL register (b0, b1, t0, s0) lands in
  // set 0. We use a single descriptor set layout that covers all four.
  VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline plain_pipeline_ = VK_NULL_HANDLE;
  VkPipeline textured_pipeline_ = VK_NULL_HANDLE;

  // Command + sync
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
  VkSemaphore image_available_ = VK_NULL_HANDLE;
  VkSemaphore render_finished_ = VK_NULL_HANDLE;
  VkFence in_flight_fence_ = VK_NULL_HANDLE;

  // Persistent host-coherent buffers (D3D11 dynamic-discard equivalent).
  VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
  void* vertex_mapped_ = nullptr;
  VkBuffer index_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory index_memory_ = VK_NULL_HANDLE;
  void* index_mapped_ = nullptr;
  VkBuffer camera_ubo_ = VK_NULL_HANDLE;
  VkDeviceMemory camera_ubo_memory_ = VK_NULL_HANDLE;
  void* camera_mapped_ = nullptr;
  VkBuffer model_ubo_ = VK_NULL_HANDLE;
  VkDeviceMemory model_ubo_memory_ = VK_NULL_HANDLE;
  void* model_mapped_ = nullptr;

  VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet uniform_set_ = VK_NULL_HANDLE;
  VkSampler texture_sampler_ = VK_NULL_HANDLE;

  std::unordered_map<const unsigned char*, CachedTex> texture_cache_;

  std::vector<Primitive> primitives_;

  bool need_resize_ = false;

  // Per-frame ring offsets so each primitive draws from its own region
  // (mirrors D3D11 map/discard renaming).
  VkDeviceSize model_aligned_size_ = 0;
  static constexpr uint32_t kMaxPrimitivesPerFrame = 1024;
};

}  // namespace Pequod

#endif  // PEQUOD_GRAPHICS_VULKAN

#endif  // PEQUODENGINE_VULKAN_APPLICATION_H
