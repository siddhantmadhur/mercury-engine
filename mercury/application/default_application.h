#ifndef MERCURYENGINE_DEFAULT_APPLICATION_H
#define MERCURYENGINE_DEFAULT_APPLICATION_H

#if defined(MERCURY_GRAPHICS_VULKAN)
#include "application/vulkan_application.h"
namespace Mercury {
using DefaultApplication = VulkanApplication;
}
#else
#error "No MERCURY_GRAPHICS_* backend selected"
#endif

#endif  // MERCURYENGINE_DEFAULT_APPLICATION_H
