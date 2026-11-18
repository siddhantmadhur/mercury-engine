// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MERCURY_ENGINE_FILAMENT_METAL_LAYER_H
#define MERCURY_ENGINE_FILAMENT_METAL_LAYER_H

struct GLFWwindow;

namespace Mercury::fil {

// The CAMetalLayer of a GLFW window's content view, making one if needed.
// Filament's Vulkan backend on Apple platforms builds its VkSurface from a
// metal layer, so every windowed SwapChain is created from this. Returns
// nullptr on non-Apple platforms.
void* CocoaGetMetalLayer(GLFWwindow* window);

}  // namespace Mercury::fil

#endif  // MERCURY_ENGINE_FILAMENT_METAL_LAYER_H
