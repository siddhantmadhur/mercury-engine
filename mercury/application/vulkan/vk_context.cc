#include "vk_context.h"

#include <GLFW/glfw3.h>

#include <cstring>
#include <vector>

#include "debugger/debugger.h"
#include "vk_check.h"

namespace Mercury::vk {

namespace {

#ifdef NDEBUG
constexpr bool kWantValidation = false;
#else
constexpr bool kWantValidation = true;
#endif

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

VKAPI_ATTR VkBool32 VKAPI_CALL
DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
              VkDebugUtilsMessageTypeFlagsEXT,
              const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    PDebug::error("Vulkan validation: {}", data->pMessage);
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    PDebug::warn("Vulkan validation: {}", data->pMessage);
  }
  return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT DebugMessengerInfo() {
  VkDebugUtilsMessengerCreateInfoEXT info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  info.pfnUserCallback = DebugCallback;
  return info;
}

bool HasLayer(const char* name) {
  uint32_t count = 0;
  vkEnumerateInstanceLayerProperties(&count, nullptr);
  std::vector<VkLayerProperties> layers(count);
  vkEnumerateInstanceLayerProperties(&count, layers.data());
  for (const auto& layer : layers) {
    if (std::strcmp(layer.layerName, name) == 0) return true;
  }
  return false;
}

bool HasDeviceExtension(VkPhysicalDevice device, const char* name) {
  uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count,
                                       extensions.data());
  for (const auto& extension : extensions) {
    if (std::strcmp(extension.extensionName, name) == 0) return true;
  }
  return false;
}

}  // namespace

PFN_vkCmdBeginRenderingKHR pfn_vkCmdBeginRendering = nullptr;
PFN_vkCmdEndRenderingKHR pfn_vkCmdEndRendering = nullptr;
PFN_vkCmdPipelineBarrier2KHR pfn_vkCmdPipelineBarrier2 = nullptr;
PFN_vkQueueSubmit2KHR pfn_vkQueueSubmit2 = nullptr;

}  // namespace Mercury::vk

namespace Mercury::vk {

Context::~Context() { Destroy(); }

bool Context::Init(GLFWwindow* window) {
  if (!CreateInstance()) return false;

  if (!VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface))) {
    PDebug::error("Vulkan: could not create window surface");
    return false;
  }

  if (!PickPhysicalDevice()) return false;
  if (!CreateDevice()) return false;
  if (!CreateAllocator()) return false;
  return true;
}

bool Context::CreateInstance() {
  uint32_t loader_version = VK_API_VERSION_1_0;
  vkEnumerateInstanceVersion(&loader_version);
  if (loader_version < kMinApiVersion) {
    PDebug::error("Vulkan: loader supports {}.{}, but 1.2 is required",
                  VK_API_VERSION_MAJOR(loader_version),
                  VK_API_VERSION_MINOR(loader_version));
    return false;
  }

  uint32_t instance_extension_count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count,
                                         nullptr);
  std::vector<VkExtensionProperties> available_instance_extensions(
      instance_extension_count);
  vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count,
                                         available_instance_extensions.data());

  bool has_portability_enumeration = false;
  for (const auto& ext : available_instance_extensions) {
    if (std::strcmp(ext.extensionName,
                    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
      has_portability_enumeration = true;
      break;
    }
  }

  uint32_t glfw_extension_count = 0;
  const char** glfw_extensions =
      glfwGetRequiredInstanceExtensions(&glfw_extension_count);
  if (glfw_extensions == nullptr) {
    PDebug::error("Vulkan: GLFW found no surface extensions for this platform");
    return false;
  }
  std::vector<const char*> extensions(glfw_extensions,
                                      glfw_extensions + glfw_extension_count);

  VkInstanceCreateFlags create_flags = 0;
  if (has_portability_enumeration) {
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    create_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }

  std::vector<const char*> layers;
  if (kWantValidation) {
    if (HasLayer(kValidationLayer)) {
      validation_enabled_ = true;
      layers.push_back(kValidationLayer);
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    } else {
      PDebug::warn(
          "Vulkan: {} is not installed; running without validation",
          kValidationLayer);
    }
  }

  VkApplicationInfo app_info = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app_info.pApplicationName = "Mercury";
  app_info.pEngineName = "Mercury";
  app_info.apiVersion = kApiVersion;

  VkInstanceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  create_info.flags = create_flags;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
  create_info.ppEnabledLayerNames = layers.data();
  create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  create_info.ppEnabledExtensionNames = extensions.data();

  // Chaining the messenger info also reports problems in vkCreateInstance
  // and vkDestroyInstance themselves.
  VkDebugUtilsMessengerCreateInfoEXT messenger_info = DebugMessengerInfo();
  if (validation_enabled_) create_info.pNext = &messenger_info;

  if (!VK_CHECK(vkCreateInstance(&create_info, nullptr, &instance))) {
    return false;
  }

  if (validation_enabled_) {
    auto create_messenger =
        reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (create_messenger) {
      VK_CHECK(create_messenger(instance, &messenger_info, nullptr,
                                &debug_messenger));
    }
  }
  return true;
}

bool Context::PickPhysicalDevice() {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance, &count, nullptr);
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance, &count, devices.data());

  int best_score = -1;
  for (VkPhysicalDevice candidate : devices) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(candidate, &properties);
    if (properties.apiVersion < kMinApiVersion) continue;
    if (!HasDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
      continue;
    }

    bool has_dynamic_rendering = false;
    bool has_sync2 = false;

    if (properties.apiVersion >= VK_API_VERSION_1_3) {
      VkPhysicalDeviceVulkan13Features features13 = {
          .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
      VkPhysicalDeviceFeatures2 features = {
          .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
          .pNext = &features13};
      vkGetPhysicalDeviceFeatures2(candidate, &features);
      has_dynamic_rendering = features13.dynamicRendering;
      has_sync2 = features13.synchronization2;
    } else if (HasDeviceExtension(candidate,
                                  VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) &&
               HasDeviceExtension(candidate,
                                  VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
      VkPhysicalDeviceDynamicRenderingFeaturesKHR dyn_rendering = {
          .sType =
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
      VkPhysicalDeviceSynchronization2FeaturesKHR sync2 = {
          .sType =
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR,
          .pNext = &dyn_rendering};
      VkPhysicalDeviceFeatures2 features = {
          .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
          .pNext = &sync2};
      vkGetPhysicalDeviceFeatures2(candidate, &features);
      has_dynamic_rendering = dyn_rendering.dynamicRendering;
      has_sync2 = sync2.synchronization2;
    }

    if (!has_dynamic_rendering || !has_sync2) continue;

    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count,
                                             nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count,
                                             families.data());
    int family = -1;
    for (uint32_t i = 0; i < family_count; ++i) {
      VkBool32 present = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present);
      if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
        family = static_cast<int>(i);
        break;
      }
    }
    if (family < 0) continue;

    int score = 1;
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      score = 3;
    } else if (properties.deviceType ==
               VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
      score = 2;
    }
    if (score > best_score) {
      best_score = score;
      physical_device = candidate;
      queue_family = static_cast<uint32_t>(family);
    }
  }

  if (physical_device == VK_NULL_HANDLE) {
    PDebug::error(
        "Vulkan: no GPU supports dynamic rendering and synchronization2 "
        "and can present to this window");
    return false;
  }

  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(physical_device, &properties);
  PDebug::info("Vulkan: using {} (Vulkan {}.{}.{})", properties.deviceName,
               VK_API_VERSION_MAJOR(properties.apiVersion),
               VK_API_VERSION_MINOR(properties.apiVersion),
               VK_API_VERSION_PATCH(properties.apiVersion));
  return true;
}

bool Context::CreateDevice() {
  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queue_info.queueFamilyIndex = queue_family;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &priority;

  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(physical_device, &properties);

  std::vector<const char*> extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  if (HasDeviceExtension(physical_device, "VK_KHR_portability_subset")) {
    extensions.push_back("VK_KHR_portability_subset");
  }

  VkPhysicalDeviceFeatures2 features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

  VkPhysicalDeviceVulkan13Features features13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  VkPhysicalDeviceDynamicRenderingFeaturesKHR dyn_rendering = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
  VkPhysicalDeviceSynchronization2FeaturesKHR sync2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};

  if (properties.apiVersion >= VK_API_VERSION_1_3) {
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features.pNext = &features13;
  } else {
    extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    dyn_rendering.dynamicRendering = VK_TRUE;
    sync2.synchronization2 = VK_TRUE;
    sync2.pNext = &dyn_rendering;
    features.pNext = &sync2;
  }

  VkDeviceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  create_info.pNext = &features;
  create_info.queueCreateInfoCount = 1;
  create_info.pQueueCreateInfos = &queue_info;
  create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  create_info.ppEnabledExtensionNames = extensions.data();

  if (!VK_CHECK(
          vkCreateDevice(physical_device, &create_info, nullptr, &device))) {
    return false;
  }
  vkGetDeviceQueue(device, queue_family, 0, &queue);

  pfn_vkCmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(
      vkGetDeviceProcAddr(device, "vkCmdBeginRendering"));
  if (!pfn_vkCmdBeginRendering) {
    pfn_vkCmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(
        vkGetDeviceProcAddr(device, "vkCmdBeginRenderingKHR"));
  }

  pfn_vkCmdEndRendering = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(
      vkGetDeviceProcAddr(device, "vkCmdEndRendering"));
  if (!pfn_vkCmdEndRendering) {
    pfn_vkCmdEndRendering = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(
        vkGetDeviceProcAddr(device, "vkCmdEndRenderingKHR"));
  }

  pfn_vkCmdPipelineBarrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(
      vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2"));
  if (!pfn_vkCmdPipelineBarrier2) {
    pfn_vkCmdPipelineBarrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(
        vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2KHR"));
  }

  pfn_vkQueueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2KHR>(
      vkGetDeviceProcAddr(device, "vkQueueSubmit2"));
  if (!pfn_vkQueueSubmit2) {
    pfn_vkQueueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2KHR>(
        vkGetDeviceProcAddr(device, "vkQueueSubmit2KHR"));
  }

  return true;
}

bool Context::CreateAllocator() {
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(physical_device, &properties);

  VmaAllocatorCreateInfo create_info = {};
  create_info.vulkanApiVersion = std::min(properties.apiVersion, kApiVersion);
  create_info.instance = instance;
  create_info.physicalDevice = physical_device;
  create_info.device = device;
  return VK_CHECK(vmaCreateAllocator(&create_info, &allocator));
}

void Context::Destroy() {
  if (allocator != VK_NULL_HANDLE) {
    vmaDestroyAllocator(allocator);
    allocator = VK_NULL_HANDLE;
  }
  if (device != VK_NULL_HANDLE) {
    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
    pfn_vkCmdBeginRendering = nullptr;
    pfn_vkCmdEndRendering = nullptr;
    pfn_vkCmdPipelineBarrier2 = nullptr;
    pfn_vkQueueSubmit2 = nullptr;
  }
  if (surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance, surface, nullptr);
    surface = VK_NULL_HANDLE;
  }
  if (debug_messenger != VK_NULL_HANDLE) {
    auto destroy_messenger =
        reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy_messenger) {
      destroy_messenger(instance, debug_messenger, nullptr);
    }
    debug_messenger = VK_NULL_HANDLE;
  }
  if (instance != VK_NULL_HANDLE) {
    vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
  }
}

}  // namespace Mercury::vk
