// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
