// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * render_hooks.h
 *
 * Process-wide callbacks around the renderer's main frame. They receive the
 * running Filament engine and the scene View, so a game or system can change
 * Filament's own state (view options, renderables, a runtime material) without
 * the engine having to wrap or replace the renderer.
 *
 * Pre-render hooks run after the scene has been synchronized into Filament and
 * the main view has been configured (viewport, layers, post-processing), just
 * before it is submitted. Post-render hooks run after the frame is submitted.
 *
 * The hooks are a singleton because the renderer is: there is one application
 * (and one Filament engine) per process. Hook on the thread that renders.
 */
#ifndef MERCURY_APPLICATION_RENDER_HOOKS_H
#define MERCURY_APPLICATION_RENDER_HOOKS_H

#include <filament/Engine.h>
#include <filament/View.h>

#include <functional>
#include <vector>

namespace Mercury {

/**
 * @brief Registry of callbacks around the main render
 */
class RenderHooks {
 public:
  // Called with the live Filament engine and the scene view.
  using Callback =
      std::function<void(filament::Engine& engine, filament::View& view)>;

  static RenderHooks& Instance();

  // Registers a callback and returns its id. Hooks run in registration order.
  int AddPreRender(Callback callback);
  int AddPostRender(Callback callback);
  // Removes a hook by id. Unknown ids are ignored.
  void Remove(int id);

  // Called by FilamentApplication. Safe to add/remove hooks from a hook.
  void InvokePreRender(filament::Engine& engine, filament::View& view) const;
  void InvokePostRender(filament::Engine& engine, filament::View& view) const;

  size_t GetPreRenderCount() const { return pre_render_.size(); }
  size_t GetPostRenderCount() const { return post_render_.size(); }
  // Removes every hook (tests, and hosts that replace the renderer).
  void Clear();

 private:
  RenderHooks() = default;

  struct Entry {
    int id = 0;
    Callback callback;
  };

  std::vector<Entry> pre_render_;
  std::vector<Entry> post_render_;
  int next_id_ = 1;
};

}  // namespace Mercury

#endif  // MERCURY_APPLICATION_RENDER_HOOKS_H
