#ifndef PEQUOD_ENGINE_VK_SWAPCHAIN_H
#define PEQUOD_ENGINE_VK_SWAPCHAIN_H

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

#include "vk_buffer.h"

namespace Pequod::vk {

class Context;

/**
 * @brief Swapchain, its image views, a matching depth buffer, and one
 * render-finished semaphore per swapchain image
 *
 * The present semaphores are per image rather than per frame in flight: a
 * semaphore waited on by vkQueuePresentKHR can only be reused once that image
 * is acquired again.
 */
class Swapchain {
 public:
  static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

  Swapchain() = default;
  ~Swapchain() { Destroy(); }
  Swapchain(const Swapchain&) = delete;
  Swapchain& operator=(const Swapchain&) = delete;

  // Creates the swapchain, or recreates it in place (reusing the old one as
  // oldSwapchain). The caller must make sure the device is idle first.
  bool Create(Context& context, uint32_t framebuffer_width,
              uint32_t framebuffer_height);
  void Destroy();

  VkSwapchainKHR Get() const { return swapchain_; }
  VkFormat ColorFormat() const { return color_format_; }
  VkExtent2D Extent() const { return extent_; }
  uint32_t MinImageCount() const { return min_image_count_; }
  uint32_t ImageCount() const { return static_cast<uint32_t>(images_.size()); }
  VkImage ImageAt(uint32_t index) const { return images_[index]; }
  VkImageView ImageView(uint32_t index) const { return image_views_[index]; }
  VkSemaphore RenderFinished(uint32_t index) const {
    return render_finished_[index];
  }
  const vk::Image& Depth() const { return depth_; }

 private:
  void DestroyImageResources();

  VkDevice device_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat color_format_ = VK_FORMAT_UNDEFINED;
  VkExtent2D extent_ = {};
  uint32_t min_image_count_ = 0;
  std::vector<VkImage> images_;
  std::vector<VkImageView> image_views_;
  std::vector<VkSemaphore> render_finished_;
  vk::Image depth_;
};

}  // namespace Pequod::vk

#endif  // PEQUOD_ENGINE_VK_SWAPCHAIN_H
