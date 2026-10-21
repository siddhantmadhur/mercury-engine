#include "vk_context.h"

#include <GLFW/glfw3.h>

#include <cstring>
#include <vector>

#include "debugger/debugger.h"
#include "vk_check.h"

namespace Pequod::vk {

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
  if (loader_version < kApiVersion) {
    PDebug::error("Vulkan: loader supports {}.{}, but 1.3 is required",
                  VK_API_VERSION_MAJOR(loader_version),
                  VK_API_VERSION_MINOR(loader_version));
    return false;
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
  app_info.pApplicationName = "Pequod";
  app_info.pEngineName = "Pequod";
  app_info.apiVersion = kApiVersion;

  VkInstanceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
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
    if (properties.apiVersion < kApiVersion) continue;
    if (!HasDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
      continue;
    }

    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features13};
    vkGetPhysicalDeviceFeatures2(candidate, &features);
    if (!features13.dynamicRendering || !features13.synchronization2) continue;

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
        "Vulkan: no GPU supports Vulkan 1.3 with dynamic rendering and can "
        "present to this window");
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

  VkPhysicalDeviceVulkan13Features features13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  features13.dynamicRendering = VK_TRUE;
  features13.synchronization2 = VK_TRUE;
  VkPhysicalDeviceFeatures2 features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &features13};

  const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  VkDeviceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  create_info.pNext = &features;
  create_info.queueCreateInfoCount = 1;
  create_info.pQueueCreateInfos = &queue_info;
  create_info.enabledExtensionCount = 1;
  create_info.ppEnabledExtensionNames = extensions;

  if (!VK_CHECK(
          vkCreateDevice(physical_device, &create_info, nullptr, &device))) {
    return false;
  }
  vkGetDeviceQueue(device, queue_family, 0, &queue);
  return true;
}

bool Context::CreateAllocator() {
  VmaAllocatorCreateInfo create_info = {};
  create_info.vulkanApiVersion = kApiVersion;
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

}  // namespace Pequod::vk
