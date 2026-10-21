#ifndef PEQUODENGINE_DEFAULT_APPLICATION_H
#define PEQUODENGINE_DEFAULT_APPLICATION_H

#if defined(PEQUOD_GRAPHICS_VULKAN)
#include "application/vulkan_application.h"
namespace Pequod {
using DefaultApplication = VulkanApplication;
}
#else
#error "No PEQUOD_GRAPHICS_* backend selected"
#endif

#endif  // PEQUODENGINE_DEFAULT_APPLICATION_H
