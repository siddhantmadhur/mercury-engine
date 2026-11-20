// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A render hook plus a gameplay toggle: F2 flips a flag that a pre-render
// callback applies to Filament's View. The hook gets the live engine and view,
// so no engine wrapper is needed.

#include <application/render_hooks.h>
#include <debugger/debugger.h>
#include <filament/View.h>
#include <scripting/app_hook.h>
#include <scripting/behaviour.h>

#include <atomic>

namespace {

std::atomic<bool> g_bloom_enabled{true};

class BloomToggle : public Mercury::Behaviour {
 public:
  void OnFrame(double) override {
    if (!IsKeyJustPressed(GLFW_KEY_F2)) return;
    const bool enabled = !g_bloom_enabled.load();
    g_bloom_enabled = enabled;
    PDebug::info("Hello: bloom {}", enabled ? "on" : "off");
  }
};

class RenderHookInstaller : public Mercury::AppHook {
 public:
  bool OnPreInit(std::span<const std::string_view>) override {
    hook_ = Mercury::RenderHooks::Instance().AddPreRender(
        [](filament::Engine&, filament::View& view) {
          auto options = view.getBloomOptions();
          const bool enabled = g_bloom_enabled.load();
          if (options.enabled == enabled) return;
          options.enabled = enabled;
          options.strength = 0.3f;
          view.setBloomOptions(options);
        });
    PDebug::info("Hello: press F2 to toggle Filament bloom");
    return true;
  }

  void OnShutdown() override { Mercury::RenderHooks::Instance().Remove(hook_); }

 private:
  int hook_ = 0;
};

}  // namespace

MERCURY_SCRIPT(BloomToggle);
MERCURY_APP_HOOK(RenderHookInstaller);
