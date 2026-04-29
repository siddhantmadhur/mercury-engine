#ifndef PEQUODENGINE_DEFAULT_APPLICATION_H
#define PEQUODENGINE_DEFAULT_APPLICATION_H

#ifdef PEQUOD_GRAPHICS_VULKAN
#include "application/vulkan_application.h"
namespace Pequod {
using DefaultApplication = VulkanApplication;
}
#elif defined(PEQUOD_GRAPHICS_D3D11)
#include "application/d3d11_application.h"
namespace Pequod {
using DefaultApplication = D3D11Application;
}
#else
#error "No PEQUOD_GRAPHICS_* backend selected"
#endif

#endif  // PEQUODENGINE_DEFAULT_APPLICATION_H
