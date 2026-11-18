// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "metal_layer.h"

#if defined(__APPLE__)

// Plain C++ (no Objective-C sources) so the engine stays one cc_library:
// the few AppKit calls go through the Objective-C runtime directly.
#include <objc/message.h>
#include <objc/objc.h>
#include <objc/runtime.h>

#include <GLFW/glfw3.h>

extern "C" void* glfwGetCocoaWindow(GLFWwindow* window);  // GLFW native API

namespace Mercury::fil {

void* CocoaGetMetalLayer(GLFWwindow* window) {
  id ns_window = reinterpret_cast<id>(glfwGetCocoaWindow(window));
  if (ns_window == nil) return nullptr;

  id view = reinterpret_cast<id (*)(id, SEL)>(
      objc_msgSend)(ns_window, sel_registerName("contentView"));
  id metal_class = reinterpret_cast<id>(objc_getClass("CAMetalLayer"));
  id layer = reinterpret_cast<id (*)(id, SEL)>(
      objc_msgSend)(view, sel_registerName("layer"));
  if (layer == nil ||
      !reinterpret_cast<BOOL (*)(id, SEL, id)>(
          objc_msgSend)(layer, sel_registerName("isKindOfClass:"),
                        metal_class)) {
    reinterpret_cast<void (*)(id, SEL, BOOL)>(
        objc_msgSend)(view, sel_registerName("setWantsLayer:"), YES);
    layer = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(
        metal_class, sel_registerName("layer"));
    reinterpret_cast<void (*)(id, SEL, id)>(
        objc_msgSend)(view, sel_registerName("setLayer:"), layer);
  }
  // Keep the layer's resolution in step with the window's.
  const double scale = reinterpret_cast<double (*)(id, SEL)>(objc_msgSend)(
      ns_window, sel_registerName("backingScaleFactor"));
  reinterpret_cast<void (*)(id, SEL, double)>(objc_msgSend)(
      layer, sel_registerName("setContentsScale:"), scale);
  return layer;
}

}  // namespace Mercury::fil

#else

#include <GLFW/glfw3.h>

namespace Mercury::fil {

void* CocoaGetMetalLayer(GLFWwindow* /*window*/) { return nullptr; }

}  // namespace Mercury::fil

#endif  // __APPLE__
