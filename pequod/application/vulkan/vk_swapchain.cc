#include "vk_swapchain.h"

#include <algorithm>

#include "debugger/debugger.h"
#include "vk_check.h"
#include "vk_context.h"

namespace Pequod::vk {

namespace {

VkSurfaceFormatKHR ChooseSurfaceFormat(VkPhysicalDevice physical_device,
                                       VkSurfaceKHR surface) {
  uint32_t count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count,
                                       nullptr);
  std::vector<VkSurfaceFormatKHR> formats(count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count,
                                       formats.data());
  for (const auto& format : formats) {
    if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return format;
    }
  }
  PDebug::warn("Vulkan: B8G8R8A8_UNORM unavailable, using surface format {}",
               static_cast<int>(formats.front().format));
  return formats.front();
}

}  // namespace

bool Swapchain::Create(Context& context, uint32_t framebuffer_width,
                       uint32_t framebuffer_height) {
  device_ = context.device;

  VkSurfaceCapabilitiesKHR capabilities;
  if (!VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          context.physical_device, context.surface, &capabilities))) {
    return false;
  }

  // On Wayland the surface has no size of its own (currentExtent is the
  // special value 0xFFFFFFFF), so the framebuffer size decides.
  if (capabilities.currentExtent.width != UINT32_MAX) {
    extent_ = capabilities.currentExtent;
  } else {
    extent_.width =
        std::clamp(framebuffer_width, capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width);
    extent_.height =
        std::clamp(framebuffer_height, capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height);
  }
  if (extent_.width == 0 || extent_.height == 0) return false;

  const VkSurfaceFormatKHR surface_format =
      ChooseSurfaceFormat(context.physical_device, context.surface);
  color_format_ = surface_format.format;

  min_image_count_ = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0) {
    min_image_count_ = std::min(min_image_count_, capabilities.maxImageCount);
  }

  VkCompositeAlphaFlagBitsKHR composite_alpha =
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  if (!(capabilities.supportedCompositeAlpha & composite_alpha)) {
    for (VkCompositeAlphaFlagBitsKHR candidate :
         {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
      if (capabilities.supportedCompositeAlpha & candidate) {
        composite_alpha = candidate;
        break;
      }
    }
  }

  VkSwapchainKHR old_swapchain = swapchain_;

  VkSwapchainCreateInfoKHR create_info = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  create_info.surface = context.surface;
  create_info.minImageCount = min_image_count_;
  create_info.imageFormat = surface_format.format;
  create_info.imageColorSpace = surface_format.colorSpace;
  create_info.imageExtent = extent_;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  create_info.preTransform = capabilities.currentTransform;
  create_info.compositeAlpha = composite_alpha;
  // FIFO is the only mode every implementation must support (vsync).
  create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  create_info.clipped = VK_TRUE;
  create_info.oldSwapchain = old_swapchain;

  VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
  const bool created = VK_CHECK(
      vkCreateSwapchainKHR(device_, &create_info, nullptr, &new_swapchain));

  DestroyImageResources();
  if (old_swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device_, old_swapchain, nullptr);
  }
  swapchain_ = new_swapchain;
  if (!created) return false;

  uint32_t image_count = 0;
  vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr);
  images_.resize(image_count);
  vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, images_.data());

  image_views_.resize(image_count, VK_NULL_HANDLE);
  render_finished_.resize(image_count, VK_NULL_HANDLE);
  for (uint32_t i = 0; i < image_count; ++i) {
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = images_[i];
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = color_format_;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!VK_CHECK(
            vkCreateImageView(device_, &view_info, nullptr, &image_views_[i]))) {
      return false;
    }

    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (!VK_CHECK(vkCreateSemaphore(device_, &semaphore_info, nullptr,
                                    &render_finished_[i]))) {
      return false;
    }
  }

  return depth_.Create(context.allocator, device_, extent_.width,
                       extent_.height, kDepthFormat,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                       VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Swapchain::DestroyImageResources() {
  depth_.Reset();
  for (VkSemaphore semaphore : render_finished_) {
    if (semaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(device_, semaphore, nullptr);
    }
  }
  for (VkImageView view : image_views_) {
    if (view != VK_NULL_HANDLE) vkDestroyImageView(device_, view, nullptr);
  }
  render_finished_.clear();
  image_views_.clear();
  images_.clear();
}

void Swapchain::Destroy() {
  if (device_ == VK_NULL_HANDLE) return;
  DestroyImageResources();
  if (swapchain_ != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
  device_ = VK_NULL_HANDLE;
}

}  // namespace Pequod::vk
