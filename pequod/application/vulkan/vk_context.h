#ifndef PEQUOD_ENGINE_VK_CONTEXT_H
#define PEQUOD_ENGINE_VK_CONTEXT_H

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>

struct GLFWwindow;

namespace Pequod::vk {

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

}  // namespace Pequod::vk

#endif  // PEQUOD_ENGINE_VK_CONTEXT_H
