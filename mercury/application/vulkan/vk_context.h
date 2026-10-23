#ifndef MERCURY_ENGINE_VK_CONTEXT_H
#define MERCURY_ENGINE_VK_CONTEXT_H

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>

struct GLFWwindow;

namespace Mercury::vk {

/**
 * @brief Owns the device-level Vulkan objects shared by the whole renderer
 *
 * Instance (with the validation layer and a debug messenger in Debug builds),
 * the window surface, the chosen physical device, the logical device with a
 * single graphics+present queue, and the VMA allocator.
 */
class Context {
 public:
  static constexpr uint32_t kApiVersion = VK_API_VERSION_1_3;
  static constexpr uint32_t kMinApiVersion = VK_API_VERSION_1_2;

  Context() = default;
  ~Context();
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  bool Init(GLFWwindow* window);
  void Destroy();

  VkInstance instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  uint32_t queue_family = 0;
  VkQueue queue = VK_NULL_HANDLE;
  VmaAllocator allocator = VK_NULL_HANDLE;

 private:
  bool CreateInstance();
  bool PickPhysicalDevice();
  bool CreateDevice();
  bool CreateAllocator();

  bool validation_enabled_ = false;
};

extern PFN_vkCmdBeginRenderingKHR pfn_vkCmdBeginRendering;
extern PFN_vkCmdEndRenderingKHR pfn_vkCmdEndRendering;
extern PFN_vkCmdPipelineBarrier2KHR pfn_vkCmdPipelineBarrier2;
extern PFN_vkQueueSubmit2KHR pfn_vkQueueSubmit2;

inline void CmdBeginRendering(VkCommandBuffer cmd, const VkRenderingInfo* rendering_info) {
  if (pfn_vkCmdBeginRendering) {
    pfn_vkCmdBeginRendering(cmd, rendering_info);
  } else {
    ::vkCmdBeginRendering(cmd, rendering_info);
  }
}

inline void CmdEndRendering(VkCommandBuffer cmd) {
  if (pfn_vkCmdEndRendering) {
    pfn_vkCmdEndRendering(cmd);
  } else {
    ::vkCmdEndRendering(cmd);
  }
}

inline void CmdPipelineBarrier2(VkCommandBuffer cmd, const VkDependencyInfo* dependency_info) {
  if (pfn_vkCmdPipelineBarrier2) {
    pfn_vkCmdPipelineBarrier2(cmd, dependency_info);
  } else {
    ::vkCmdPipelineBarrier2(cmd, dependency_info);
  }
}

inline VkResult QueueSubmit2(VkQueue queue, uint32_t submit_count,
                            const VkSubmitInfo2* submits, VkFence fence) {
  if (pfn_vkQueueSubmit2) {
    return pfn_vkQueueSubmit2(queue, submit_count, submits, fence);
  } else {
    return ::vkQueueSubmit2(queue, submit_count, submits, fence);
  }
}

#define vkCmdBeginRendering ::Mercury::vk::CmdBeginRendering
#define vkCmdEndRendering ::Mercury::vk::CmdEndRendering
#define vkCmdPipelineBarrier2 ::Mercury::vk::CmdPipelineBarrier2
#define vkQueueSubmit2 ::Mercury::vk::QueueSubmit2

}  // namespace Mercury::vk

#endif  // MERCURY_ENGINE_VK_CONTEXT_H
