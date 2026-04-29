//
// Created by smadhur on 4/28/2026.
//
#include "vulkan_application.h"

#ifdef PEQUOD_GRAPHICS_VULKAN

#include <GLFW/glfw3.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/imgui.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "debugger/debugger.h"
#include "glm/ext/matrix_transform.hpp"
#include "os/filesystem.h"

namespace Pequod {

static constexpr uint32_t kMaxVertices = 64000;
static constexpr uint32_t kMaxIndices = 64000;
static constexpr uint32_t kMaxTextures = 256;

static const std::vector<const char*> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"};
static const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#define VKCHECK(expr, msg)                              \
  do {                                                  \
    VkResult _r = (expr);                               \
    if (_r != VK_SUCCESS) {                             \
      PDebug::error("Vulkan: {} (VkResult {})", msg,    \
                    static_cast<int>(_r));              \
      return false;                                     \
    }                                                   \
  } while (0)

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    PDebug::error("Vulkan validation: {}", data->pMessage);
  } else {
    PDebug::info("Vulkan: {}", data->pMessage);
  }
  return VK_FALSE;
}

VulkanApplication::VulkanApplication(const std::string& window_title,
                                     float initial_width, float initial_height)
    : Application(window_title, initial_width, initial_height) {}

VulkanApplication::~VulkanApplication() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);

    ImGui_ImplVulkan_Shutdown();

    for (auto& [_, t] : texture_cache_) {
      if (t.view) vkDestroyImageView(device_, t.view, nullptr);
      if (t.image) vkDestroyImage(device_, t.image, nullptr);
      if (t.memory) vkFreeMemory(device_, t.memory, nullptr);
    }
    texture_cache_.clear();

    if (texture_sampler_)
      vkDestroySampler(device_, texture_sampler_, nullptr);
    if (descriptor_pool_)
      vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);

    auto destroy_buffer = [&](VkBuffer& b, VkDeviceMemory& m, void*& mapped) {
      if (mapped) {
        vkUnmapMemory(device_, m);
        mapped = nullptr;
      }
      if (b) vkDestroyBuffer(device_, b, nullptr);
      if (m) vkFreeMemory(device_, m, nullptr);
      b = VK_NULL_HANDLE;
      m = VK_NULL_HANDLE;
    };
    destroy_buffer(vertex_buffer_, vertex_memory_, vertex_mapped_);
    destroy_buffer(index_buffer_, index_memory_, index_mapped_);
    destroy_buffer(camera_ubo_, camera_ubo_memory_, camera_mapped_);
    destroy_buffer(model_ubo_, model_ubo_memory_, model_mapped_);

    if (plain_pipeline_)
      vkDestroyPipeline(device_, plain_pipeline_, nullptr);
    if (textured_pipeline_)
      vkDestroyPipeline(device_, textured_pipeline_, nullptr);
    if (pipeline_layout_)
      vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    if (descriptor_set_layout_)
      vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);

    if (in_flight_fence_) vkDestroyFence(device_, in_flight_fence_, nullptr);
    if (image_available_)
      vkDestroySemaphore(device_, image_available_, nullptr);
    if (render_finished_)
      vkDestroySemaphore(device_, render_finished_, nullptr);
    if (command_pool_) vkDestroyCommandPool(device_, command_pool_, nullptr);

    DestroySwapchainResources();
    if (render_pass_) vkDestroyRenderPass(device_, render_pass_, nullptr);

    vkDestroyDevice(device_, nullptr);
  }

  if (debug_messenger_ != VK_NULL_HANDLE) {
    auto destroy_fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
    if (destroy_fn) destroy_fn(instance_, debug_messenger_, nullptr);
  }
  if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
  if (instance_) vkDestroyInstance(instance_, nullptr);
}

void VulkanApplication::ImGuiNewFrame() { ImGui_ImplVulkan_NewFrame(); }

bool VulkanApplication::Initialize() {
  if (!Application::Initialize()) {
    PDebug::error("Could not initialize GLFW application");
    return false;
  }

  if (!CreateInstance()) return false;
  if (!CreateSurface()) return false;
  if (!PickPhysicalDevice()) return false;
  if (!CreateDevice()) return false;
  if (!CreateSwapchainResources()) return false;
  if (!CreateRenderPass()) return false;
  if (!CreateFramebuffers()) return false;
  if (!CreateCommandPool()) return false;
  if (!CreateSyncObjects()) return false;

  return true;
}

bool VulkanApplication::CreateInstance() {
  VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app_info.pApplicationName = "Pequod";
  app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app_info.pEngineName = "Pequod";
  app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  app_info.apiVersion = VK_API_VERSION_1_2;

  uint32_t glfw_count = 0;
  const char** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_count);
  std::vector<const char*> extensions(glfw_exts, glfw_exts + glfw_count);

  // Best-effort validation layer enable (mirrors D3D11_CREATE_DEVICE_DEBUG —
  // present if the validation layers package is installed).
  std::vector<const char*> layers;
  uint32_t layer_count = 0;
  vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
  std::vector<VkLayerProperties> available(layer_count);
  vkEnumerateInstanceLayerProperties(&layer_count, available.data());
  for (const auto* want : kValidationLayers) {
    for (const auto& have : available) {
      if (std::strcmp(want, have.layerName) == 0) {
        layers.push_back(want);
        break;
      }
    }
  }
  bool has_debug_utils = !layers.empty();
  if (has_debug_utils) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  } else {
    PDebug::warn("Vulkan: validation layer not found, continuing without");
  }

  VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  info.pApplicationInfo = &app_info;
  info.enabledExtensionCount = (uint32_t)extensions.size();
  info.ppEnabledExtensionNames = extensions.data();
  info.enabledLayerCount = (uint32_t)layers.size();
  info.ppEnabledLayerNames = layers.data();

  VKCHECK(vkCreateInstance(&info, nullptr, &instance_),
          "vkCreateInstance failed");

  VkDebugUtilsMessengerCreateInfoEXT dbg = {
      VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  dbg.pfnUserCallback = DebugCallback;

  if (has_debug_utils) {
    auto create_fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance_, "vkCreateDebugUtilsMessengerEXT");
    if (create_fn) {
      create_fn(instance_, &dbg, nullptr, &debug_messenger_);
    }
  }
  return true;
}

bool VulkanApplication::CreateSurface() {
  VKCHECK(glfwCreateWindowSurface(instance_, GetWindow(), nullptr, &surface_),
          "glfwCreateWindowSurface failed");
  return true;
}

bool VulkanApplication::PickPhysicalDevice() {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance_, &count, nullptr);
  if (count == 0) {
    PDebug::error("Vulkan: no physical devices");
    return false;
  }
  std::vector<VkPhysicalDevice> devs(count);
  vkEnumeratePhysicalDevices(instance_, &count, devs.data());

  for (auto d : devs) {
    uint32_t qcount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(d, &qcount, nullptr);
    std::vector<VkQueueFamilyProperties> qprops(qcount);
    vkGetPhysicalDeviceQueueFamilyProperties(d, &qcount, qprops.data());
    for (uint32_t i = 0; i < qcount; ++i) {
      VkBool32 present = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &present);
      if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
        physical_device_ = d;
        queue_family_ = i;
        return true;
      }
    }
  }
  PDebug::error("Vulkan: no suitable device with graphics+present queue");
  return false;
}

bool VulkanApplication::CreateDevice() {
  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = queue_family_;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;

  VkPhysicalDeviceFeatures features = {};

  VkDeviceCreateInfo info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  info.pQueueCreateInfos = &qci;
  info.queueCreateInfoCount = 1;
  info.pEnabledFeatures = &features;
  info.enabledExtensionCount = (uint32_t)kDeviceExtensions.size();
  info.ppEnabledExtensionNames = kDeviceExtensions.data();

  VKCHECK(vkCreateDevice(physical_device_, &info, nullptr, &device_),
          "vkCreateDevice failed");
  vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
  return true;
}

bool VulkanApplication::CreateSwapchainResources() {
  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps);

  swapchain_extent_ = caps.currentExtent;
  if (swapchain_extent_.width == 0xFFFFFFFF) {
    swapchain_extent_.width = (uint32_t)GetWidth();
    swapchain_extent_.height = (uint32_t)GetHeight();
  }
  if (swapchain_extent_.width == 0 || swapchain_extent_.height == 0) {
    return true;  // minimized; defer
  }

  uint32_t format_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_,
                                       &format_count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_,
                                       &format_count, formats.data());
  VkSurfaceFormatKHR chosen = formats[0];
  for (auto& f : formats) {
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
        f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      chosen = f;
      break;
    }
  }
  swapchain_format_ = chosen.format;

  uint32_t image_count = caps.minImageCount + 1;
  if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
    image_count = caps.maxImageCount;

  VkSwapchainCreateInfoKHR sci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  sci.surface = surface_;
  sci.minImageCount = image_count;
  sci.imageFormat = chosen.format;
  sci.imageColorSpace = chosen.colorSpace;
  sci.imageExtent = swapchain_extent_;
  sci.imageArrayLayers = 1;
  sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  sci.clipped = VK_TRUE;

  VKCHECK(vkCreateSwapchainKHR(device_, &sci, nullptr, &swapchain_),
          "vkCreateSwapchainKHR failed");

  uint32_t got = 0;
  vkGetSwapchainImagesKHR(device_, swapchain_, &got, nullptr);
  swapchain_images_.resize(got);
  vkGetSwapchainImagesKHR(device_, swapchain_, &got, swapchain_images_.data());

  swapchain_views_.resize(got);
  for (uint32_t i = 0; i < got; ++i) {
    VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = swapchain_images_[i];
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = swapchain_format_;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VKCHECK(vkCreateImageView(device_, &vci, nullptr, &swapchain_views_[i]),
            "vkCreateImageView failed");
  }
  return true;
}

void VulkanApplication::DestroySwapchainResources() {
  for (auto fb : framebuffers_) {
    if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
  }
  framebuffers_.clear();
  for (auto v : swapchain_views_) {
    if (v) vkDestroyImageView(device_, v, nullptr);
  }
  swapchain_views_.clear();
  swapchain_images_.clear();
  if (swapchain_) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
}

bool VulkanApplication::CreateRenderPass() {
  VkAttachmentDescription color = {};
  color.format = swapchain_format_;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_ref = {0,
                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub = {};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &color_ref;

  VkSubpassDependency dep = {};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.srcAccessMask = 0;
  dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo rpci = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpci.attachmentCount = 1;
  rpci.pAttachments = &color;
  rpci.subpassCount = 1;
  rpci.pSubpasses = &sub;
  rpci.dependencyCount = 1;
  rpci.pDependencies = &dep;

  VKCHECK(vkCreateRenderPass(device_, &rpci, nullptr, &render_pass_),
          "vkCreateRenderPass failed");
  return true;
}

bool VulkanApplication::CreateFramebuffers() {
  framebuffers_.resize(swapchain_views_.size());
  for (size_t i = 0; i < swapchain_views_.size(); ++i) {
    VkFramebufferCreateInfo fci = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = render_pass_;
    fci.attachmentCount = 1;
    fci.pAttachments = &swapchain_views_[i];
    fci.width = swapchain_extent_.width;
    fci.height = swapchain_extent_.height;
    fci.layers = 1;
    VKCHECK(vkCreateFramebuffer(device_, &fci, nullptr, &framebuffers_[i]),
            "vkCreateFramebuffer failed");
  }
  return true;
}

bool VulkanApplication::CreateCommandPool() {
  VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pci.queueFamilyIndex = queue_family_;
  VKCHECK(vkCreateCommandPool(device_, &pci, nullptr, &command_pool_),
          "vkCreateCommandPool failed");

  VkCommandBufferAllocateInfo ai = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = command_pool_;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VKCHECK(vkAllocateCommandBuffers(device_, &ai, &command_buffer_),
          "vkAllocateCommandBuffers failed");
  return true;
}

bool VulkanApplication::CreateSyncObjects() {
  VkSemaphoreCreateInfo sci = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  VKCHECK(vkCreateSemaphore(device_, &sci, nullptr, &image_available_),
          "image_available semaphore");
  VKCHECK(vkCreateSemaphore(device_, &sci, nullptr, &render_finished_),
          "render_finished semaphore");
  VKCHECK(vkCreateFence(device_, &fci, nullptr, &in_flight_fence_),
          "in_flight fence");
  return true;
}

uint32_t VulkanApplication::FindMemoryType(uint32_t type_bits,
                                           VkMemoryPropertyFlags props) {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(physical_device_, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
    if ((type_bits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags & props) == props) {
      return i;
    }
  }
  PDebug::error("Vulkan: no compatible memory type");
  return 0;
}

bool VulkanApplication::CreateBuffer(VkDeviceSize size,
                                     VkBufferUsageFlags usage,
                                     VkMemoryPropertyFlags props,
                                     VkBuffer& buffer,
                                     VkDeviceMemory& memory) {
  VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = size;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VKCHECK(vkCreateBuffer(device_, &bi, nullptr, &buffer), "vkCreateBuffer");

  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(device_, buffer, &req);

  VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, props);
  VKCHECK(vkAllocateMemory(device_, &ai, nullptr, &memory), "vkAllocateMemory");
  VKCHECK(vkBindBufferMemory(device_, buffer, memory, 0), "vkBindBufferMemory");
  return true;
}

bool VulkanApplication::CreateBuffers() {
  const VkMemoryPropertyFlags host_visible =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  if (!CreateBuffer(sizeof(Vertex) * kMaxVertices,
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host_visible,
                    vertex_buffer_, vertex_memory_))
    return false;
  vkMapMemory(device_, vertex_memory_, 0, VK_WHOLE_SIZE, 0, &vertex_mapped_);

  if (!CreateBuffer(sizeof(uint32_t) * kMaxIndices,
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT, host_visible,
                    index_buffer_, index_memory_))
    return false;
  vkMapMemory(device_, index_memory_, 0, VK_WHOLE_SIZE, 0, &index_mapped_);

  if (!CreateBuffer(sizeof(CameraCBuffer), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    host_visible, camera_ubo_, camera_ubo_memory_))
    return false;
  vkMapMemory(device_, camera_ubo_memory_, 0, VK_WHOLE_SIZE, 0,
              &camera_mapped_);

  // Model UBO holds one entry per primitive, each aligned to the device's
  // minUniformBufferOffsetAlignment so a dynamic descriptor can index into it.
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(physical_device_, &props);
  VkDeviceSize ubo_align = props.limits.minUniformBufferOffsetAlignment;
  model_aligned_size_ = sizeof(VsModelBuffer);
  if (ubo_align > 0) {
    model_aligned_size_ =
        (model_aligned_size_ + ubo_align - 1) & ~(ubo_align - 1);
  }

  if (!CreateBuffer(model_aligned_size_ * kMaxPrimitivesPerFrame,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host_visible,
                    model_ubo_, model_ubo_memory_))
    return false;
  vkMapMemory(device_, model_ubo_memory_, 0, VK_WHOLE_SIZE, 0, &model_mapped_);
  return true;
}

bool VulkanApplication::CreateDescriptorSetLayouts() {
  // Single descriptor set 0 holding everything DXC emits:
  //   binding 0  : camera UBO     (HLSL b0, vertex stage)
  //   binding 1  : model  UBO_DYN (HLSL b1, vertex stage)
  //   binding 10 : sampled image  (HLSL t0 + -fvk-t-shift 10, fragment stage)
  //   binding 20 : sampler        (HLSL s0 + -fvk-s-shift 20, fragment stage)
  // Both pipelines share this layout. The plain pipeline never references
  // bindings 10/20, so the descriptor set we bind for plain primitives can
  // leave those unwritten.
  std::array<VkDescriptorSetLayoutBinding, 4> b = {};
  b[0].binding = 0;
  b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  b[0].descriptorCount = 1;
  b[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  b[1].binding = 1;
  b[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  b[1].descriptorCount = 1;
  b[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  b[2].binding = 10;
  b[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  b[2].descriptorCount = 1;
  b[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  b[3].binding = 20;
  b[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  b[3].descriptorCount = 1;
  b[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo lci = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  lci.bindingCount = (uint32_t)b.size();
  lci.pBindings = b.data();
  VKCHECK(vkCreateDescriptorSetLayout(device_, &lci, nullptr,
                                      &descriptor_set_layout_),
          "descriptor set layout");

  VkPipelineLayoutCreateInfo plci = {
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &descriptor_set_layout_;
  VKCHECK(vkCreatePipelineLayout(device_, &plci, nullptr, &pipeline_layout_),
          "pipeline layout");
  return true;
}

VkShaderModule VulkanApplication::LoadShaderModule(const std::string& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f.is_open()) {
    PDebug::error("Vulkan: could not open SPIR-V {}", path);
    return VK_NULL_HANDLE;
  }
  std::streamsize size = f.tellg();
  f.seekg(0);
  std::vector<char> buffer(size);
  f.read(buffer.data(), size);

  VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = (size_t)size;
  info.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

  VkShaderModule mod = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device_, &info, nullptr, &mod) != VK_SUCCESS) {
    PDebug::error("Vulkan: vkCreateShaderModule failed for {}", path);
    return VK_NULL_HANDLE;
  }
  return mod;
}

VkPipeline VulkanApplication::BuildPipeline(VkShaderModule vs,
                                            VkShaderModule fs,
                                            VkPipelineLayout layout) {
  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs;
  stages[0].pName = "Main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs;
  stages[1].pName = "Main";

  VkVertexInputBindingDescription binding = {};
  binding.binding = 0;
  binding.stride = sizeof(Vertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 3> attrs = {};
  attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
  attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)};
  attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};

  VkPipelineVertexInputStateCreateInfo vi = {
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &binding;
  vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
  vi.pVertexAttributeDescriptions = attrs.data();

  VkPipelineInputAssemblyStateCreateInfo ia = {
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo vp = {
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rs = {
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms = {
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Match D3D11 alpha blend (d3d11_application.cc:218-227).
  VkPipelineColorBlendAttachmentState cba = {};
  cba.blendEnable = VK_TRUE;
  cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  cba.colorBlendOp = VK_BLEND_OP_ADD;
  cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  cba.alphaBlendOp = VK_BLEND_OP_ADD;
  cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo cb = {
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &cba;

  std::array<VkDynamicState, 2> dyn_states = {VK_DYNAMIC_STATE_VIEWPORT,
                                              VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn = {
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = (uint32_t)dyn_states.size();
  dyn.pDynamicStates = dyn_states.data();

  VkGraphicsPipelineCreateInfo info = {
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.stageCount = 2;
  info.pStages = stages;
  info.pVertexInputState = &vi;
  info.pInputAssemblyState = &ia;
  info.pViewportState = &vp;
  info.pRasterizationState = &rs;
  info.pMultisampleState = &ms;
  info.pColorBlendState = &cb;
  info.pDynamicState = &dyn;
  info.layout = layout;
  info.renderPass = render_pass_;
  info.subpass = 0;

  VkPipeline pipeline = VK_NULL_HANDLE;
  if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr,
                                &pipeline) != VK_SUCCESS) {
    PDebug::error("Vulkan: vkCreateGraphicsPipelines failed");
    return VK_NULL_HANDLE;
  }
  return pipeline;
}

bool VulkanApplication::CreatePipelines() {
#ifndef PEQUOD_SPIRV_PATH
  PDebug::error("PEQUOD_SPIRV_PATH not defined");
  return false;
#endif
  fs::path spv = PEQUOD_SPIRV_PATH;
  VkShaderModule vs = LoadShaderModule((spv / "main.vs.spv").string());
  VkShaderModule plain_fs = LoadShaderModule((spv / "main.ps.spv").string());
  VkShaderModule textured_fs =
      LoadShaderModule((spv / "textured.ps.spv").string());
  if (!vs || !plain_fs || !textured_fs) return false;

  plain_pipeline_ = BuildPipeline(vs, plain_fs, pipeline_layout_);
  textured_pipeline_ = BuildPipeline(vs, textured_fs, pipeline_layout_);

  vkDestroyShaderModule(device_, vs, nullptr);
  vkDestroyShaderModule(device_, plain_fs, nullptr);
  vkDestroyShaderModule(device_, textured_fs, nullptr);

  return plain_pipeline_ != VK_NULL_HANDLE &&
         textured_pipeline_ != VK_NULL_HANDLE;
}

bool VulkanApplication::CreateDescriptorPool() {
  // Pool covers: 1 plain uniform set + kMaxTextures texture sets, each holding
  // 1 UBO + 1 UBO_DYN + 1 SAMPLED_IMAGE + 1 SAMPLER. Plus headroom for ImGui.
  std::array<VkDescriptorPoolSize, 4> sizes = {};
  sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxTextures + 1 + 1024};
  sizes[1] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
              kMaxTextures + 1 + 1024};
  sizes[2] = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTextures + 1024};
  sizes[3] = {VK_DESCRIPTOR_TYPE_SAMPLER, kMaxTextures + 1024};

  VkDescriptorPoolCreateInfo pci = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pci.maxSets = 1 + kMaxTextures + 1024;
  pci.poolSizeCount = (uint32_t)sizes.size();
  pci.pPoolSizes = sizes.data();
  VKCHECK(vkCreateDescriptorPool(device_, &pci, nullptr, &descriptor_pool_),
          "vkCreateDescriptorPool failed");
  return true;
}

bool VulkanApplication::CreateUniformDescriptorSet() {
  VkDescriptorSetAllocateInfo ai = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = descriptor_pool_;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &descriptor_set_layout_;
  VKCHECK(vkAllocateDescriptorSets(device_, &ai, &uniform_set_),
          "vkAllocateDescriptorSets uniform");

  VkDescriptorBufferInfo cam = {camera_ubo_, 0, sizeof(CameraCBuffer)};
  // Dynamic UBO: range = one struct; the dynamic offset selects which entry.
  VkDescriptorBufferInfo mdl = {model_ubo_, 0, sizeof(VsModelBuffer)};

  std::array<VkWriteDescriptorSet, 2> w = {};
  w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  w[0].dstSet = uniform_set_;
  w[0].dstBinding = 0;
  w[0].descriptorCount = 1;
  w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  w[0].pBufferInfo = &cam;
  w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  w[1].dstSet = uniform_set_;
  w[1].dstBinding = 1;
  w[1].descriptorCount = 1;
  w[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  w[1].pBufferInfo = &mdl;
  vkUpdateDescriptorSets(device_, (uint32_t)w.size(), w.data(), 0, nullptr);
  return true;
}

bool VulkanApplication::CreateSampler() {
  VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = VK_FILTER_NEAREST;
  si.minFilter = VK_FILTER_NEAREST;
  si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  si.maxLod = VK_LOD_CLAMP_NONE;
  VKCHECK(vkCreateSampler(device_, &si, nullptr, &texture_sampler_),
          "vkCreateSampler failed");
  return true;
}

bool VulkanApplication::OnLoad() {
  if (!CreateDescriptorSetLayouts()) return false;
  if (!CreatePipelines()) return false;
  if (!CreateBuffers()) return false;
  if (!CreateDescriptorPool()) return false;
  if (!CreateUniformDescriptorSet()) return false;
  if (!CreateSampler()) return false;

  // ImGui Vulkan init. Newer backend nests RenderPass/MSAA in PipelineInfoMain
  // and builds the font texture lazily.
  ImGui_ImplVulkan_InitInfo ii = {};
  ii.Instance = instance_;
  ii.PhysicalDevice = physical_device_;
  ii.Device = device_;
  ii.QueueFamily = queue_family_;
  ii.Queue = queue_;
  ii.DescriptorPool = descriptor_pool_;
  ii.MinImageCount = 2;
  ii.ImageCount = (uint32_t)swapchain_images_.size();
  ii.PipelineInfoMain.RenderPass = render_pass_;
  ii.PipelineInfoMain.Subpass = 0;
  ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  ImGui_ImplVulkan_Init(&ii);

  primitives_ = game_scene_->GetPrimitives();
  return true;
}

VkCommandBuffer VulkanApplication::BeginOneShot() {
  VkCommandBufferAllocateInfo ai = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = command_pool_;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cb = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(device_, &ai, &cb);

  VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cb, &bi);
  return cb;
}

void VulkanApplication::EndOneShot(VkCommandBuffer cb) {
  vkEndCommandBuffer(cb);
  VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cb;
  vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue_);
  vkFreeCommandBuffers(device_, command_pool_, 1, &cb);
}

VulkanApplication::CachedTex& VulkanApplication::GetOrCreateTexture(
    const unsigned char* data, int width, int height) {
  auto it = texture_cache_.find(data);
  if (it != texture_cache_.end()) return it->second;

  CachedTex t = {};
  VkDeviceSize byte_size = (VkDeviceSize)width * height * 4;

  // Staging buffer.
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory staging_mem = VK_NULL_HANDLE;
  CreateBuffer(byte_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               staging, staging_mem);
  void* mapped = nullptr;
  vkMapMemory(device_, staging_mem, 0, byte_size, 0, &mapped);
  std::memcpy(mapped, data, (size_t)byte_size);
  vkUnmapMemory(device_, staging_mem);

  // Image.
  VkImageCreateInfo ici = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent = {(uint32_t)width, (uint32_t)height, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  vkCreateImage(device_, &ici, nullptr, &t.image);

  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(device_, t.image, &req);
  VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex =
      FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  vkAllocateMemory(device_, &mai, nullptr, &t.memory);
  vkBindImageMemory(device_, t.image, t.memory, 0);

  // Upload via one-shot CB with layout transitions.
  VkCommandBuffer cb = BeginOneShot();

  VkImageMemoryBarrier b1 = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b1.image = t.image;
  b1.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  b1.srcAccessMask = 0;
  b1.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &b1);

  VkBufferImageCopy region = {};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {(uint32_t)width, (uint32_t)height, 1};
  vkCmdCopyBufferToImage(cb, staging, t.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  VkImageMemoryBarrier b2 = b1;
  b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  b2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &b2);

  EndOneShot(cb);
  vkDestroyBuffer(device_, staging, nullptr);
  vkFreeMemory(device_, staging_mem, nullptr);

  // View.
  VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = t.image;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = VK_FORMAT_R8G8B8A8_UNORM;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCreateImageView(device_, &vci, nullptr, &t.view);

  // Allocate one set against the unified layout and write all four bindings:
  // UBOs (so the texture path doesn't need a different "uniform" set bound)
  // plus the texture/sampler at bindings 10/20.
  VkDescriptorSetAllocateInfo dai = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dai.descriptorPool = descriptor_pool_;
  dai.descriptorSetCount = 1;
  dai.pSetLayouts = &descriptor_set_layout_;
  vkAllocateDescriptorSets(device_, &dai, &t.set);

  VkDescriptorBufferInfo cam = {camera_ubo_, 0, sizeof(CameraCBuffer)};
  VkDescriptorBufferInfo mdl = {model_ubo_, 0, sizeof(VsModelBuffer)};
  VkDescriptorImageInfo image_info = {
      VK_NULL_HANDLE, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo sampler_info = {texture_sampler_, VK_NULL_HANDLE,
                                        VK_IMAGE_LAYOUT_UNDEFINED};
  std::array<VkWriteDescriptorSet, 4> w = {};
  w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  w[0].dstSet = t.set;
  w[0].dstBinding = 0;
  w[0].descriptorCount = 1;
  w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  w[0].pBufferInfo = &cam;
  w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  w[1].dstSet = t.set;
  w[1].dstBinding = 1;
  w[1].descriptorCount = 1;
  w[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  w[1].pBufferInfo = &mdl;
  w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  w[2].dstSet = t.set;
  w[2].dstBinding = 10;
  w[2].descriptorCount = 1;
  w[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  w[2].pImageInfo = &image_info;
  w[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  w[3].dstSet = t.set;
  w[3].dstBinding = 20;
  w[3].descriptorCount = 1;
  w[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  w[3].pImageInfo = &sampler_info;
  vkUpdateDescriptorSets(device_, (uint32_t)w.size(), w.data(), 0, nullptr);

  texture_cache_[data] = t;
  return texture_cache_[data];
}

void VulkanApplication::OnResize(int32_t width, int32_t height) {
  Application::OnResize(width, height);
  if (device_ == VK_NULL_HANDLE) return;
  vkDeviceWaitIdle(device_);
  DestroySwapchainResources();
  if (!CreateSwapchainResources()) return;
  CreateFramebuffers();
}

void VulkanApplication::Render() {
  if (need_resize_ || swapchain_ == VK_NULL_HANDLE ||
      swapchain_extent_.width == 0 || swapchain_extent_.height == 0) {
    OnResize(GetWidth(), GetHeight());
    need_resize_ = false;
    if (swapchain_ == VK_NULL_HANDLE) return;
  }

  vkWaitForFences(device_, 1, &in_flight_fence_, VK_TRUE, UINT64_MAX);

  uint32_t image_index = 0;
  VkResult acq =
      vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_available_,
                            VK_NULL_HANDLE, &image_index);
  if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
    need_resize_ = true;
    return;
  }
  if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
    PDebug::error("Vulkan: vkAcquireNextImageKHR failed: {}", (int)acq);
    return;
  }
  vkResetFences(device_, 1, &in_flight_fence_);

  // Camera UBO upload (mirrors d3d11_application.cc:236-252).
  if (game_scene_) {
    glm::mat4x4 camera_proj_view = {};
    if (game_scene_->GetCameraProj(camera_proj_view)) {
      CameraCBuffer cb = {};
      cb.mWorldViewProj = camera_proj_view;
      std::memcpy(camera_mapped_, &cb, sizeof(CameraCBuffer));
    }
  }

  vkResetCommandBuffer(command_buffer_, 0);
  VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkBeginCommandBuffer(command_buffer_, &bi);

  VkClearValue clear = {};
  clear.color = {{0.1f, 0.1f, 0.1f, 1.0f}};
  VkRenderPassBeginInfo rpbi = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rpbi.renderPass = render_pass_;
  rpbi.framebuffer = framebuffers_[image_index];
  rpbi.renderArea.offset = {0, 0};
  rpbi.renderArea.extent = swapchain_extent_;
  rpbi.clearValueCount = 1;
  rpbi.pClearValues = &clear;
  vkCmdBeginRenderPass(command_buffer_, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport vp = {0.0f,
                   0.0f,
                   (float)swapchain_extent_.width,
                   (float)swapchain_extent_.height,
                   0.0f,
                   1.0f};
  VkRect2D scissor = {{0, 0}, swapchain_extent_};
  vkCmdSetViewport(command_buffer_, 0, 1, &vp);
  vkCmdSetScissor(command_buffer_, 0, 1, &scissor);

  // Per-primitive ring offsets within the persistently-mapped buffers. We
  // cannot reuse the same region for every draw (the D3D11 map/discard model
  // gets renamed buffers under the hood; in Vulkan the GPU sees whichever
  // bytes happen to be there at submit time). Advancing offsets gives each
  // primitive its own slice that survives until the frame's submit completes.
  auto primitives = game_scene_->GetPrimitives();
  if (game_scene_) {
    VkDeviceSize vertex_byte_off = 0;
    VkDeviceSize index_byte_off = 0;
    uint32_t primitive_index = 0;

    for (const auto& primitive : primitives) {
      if (primitive_index >= kMaxPrimitivesPerFrame) {
        PDebug::warn("Vulkan: dropping primitives beyond {}",
                     kMaxPrimitivesPerFrame);
        break;
      }
      const VkDeviceSize vbytes =
          sizeof(Vertex) * primitive.vertices_.size();
      const VkDeviceSize ibytes =
          sizeof(uint32_t) * primitive.indices_.size();
      if (vertex_byte_off + vbytes > sizeof(Vertex) * kMaxVertices ||
          index_byte_off + ibytes > sizeof(uint32_t) * kMaxIndices) {
        PDebug::warn("Vulkan: per-frame VBO/IBO budget exceeded, skipping");
        break;
      }

      std::memcpy(static_cast<char*>(vertex_mapped_) + vertex_byte_off,
                  primitive.vertices_.data(), vbytes);
      std::memcpy(static_cast<char*>(index_mapped_) + index_byte_off,
                  primitive.indices_.data(), ibytes);

      VsModelBuffer vs_model_buffer = {};
      vs_model_buffer.scale = primitive.scale_;
      vs_model_buffer.opacity = 1;
      glm::mat4 model = glm::mat4(1.0f);
      model = glm::translate(model, primitive.world_position_);
      vs_model_buffer.world_position = model;
      const VkDeviceSize model_byte_off =
          (VkDeviceSize)primitive_index * model_aligned_size_;
      std::memcpy(static_cast<char*>(model_mapped_) + model_byte_off,
                  &vs_model_buffer, sizeof(VsModelBuffer));

      VkBuffer vbufs[1] = {vertex_buffer_};
      VkDeviceSize voffs[1] = {vertex_byte_off};
      vkCmdBindVertexBuffers(command_buffer_, 0, 1, vbufs, voffs);
      vkCmdBindIndexBuffer(command_buffer_, index_buffer_, index_byte_off,
                           VK_INDEX_TYPE_UINT32);

      uint32_t dynamic_off = (uint32_t)model_byte_off;
      if (primitive.texture_data_) {
        auto& cached = GetOrCreateTexture(primitive.texture_data_,
                                          primitive.texture_width_,
                                          primitive.texture_height_);
        vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          textured_pipeline_);
        vkCmdBindDescriptorSets(command_buffer_,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout_, 0, 1, &cached.set, 1,
                                &dynamic_off);
      } else {
        vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          plain_pipeline_);
        vkCmdBindDescriptorSets(command_buffer_,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout_, 0, 1, &uniform_set_, 1,
                                &dynamic_off);
      }
      vkCmdDrawIndexed(command_buffer_, (uint32_t)primitive.indices_.size(), 1,
                       0, 0, 0);

      vertex_byte_off += vbytes;
      index_byte_off += ibytes;
      ++primitive_index;
    }
  }

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer_);

  vkCmdEndRenderPass(command_buffer_);
  vkEndCommandBuffer(command_buffer_);

  VkPipelineStageFlags wait_stage =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &image_available_;
  si.pWaitDstStageMask = &wait_stage;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &command_buffer_;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &render_finished_;
  vkQueueSubmit(queue_, 1, &si, in_flight_fence_);

  VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &render_finished_;
  pi.swapchainCount = 1;
  pi.pSwapchains = &swapchain_;
  pi.pImageIndices = &image_index;
  VkResult pres = vkQueuePresentKHR(queue_, &pi);
  if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
    need_resize_ = true;
  }
}

}  // namespace Pequod

#endif  // PEQUOD_GRAPHICS_VULKAN
