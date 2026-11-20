// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "application/render_hooks.h"

namespace Mercury {

RenderHooks& RenderHooks::Instance() {
  static RenderHooks hooks;
  return hooks;
}

int RenderHooks::AddPreRender(Callback callback) {
  const int id = next_id_++;
  pre_render_.push_back({id, std::move(callback)});
  return id;
}

int RenderHooks::AddPostRender(Callback callback) {
  const int id = next_id_++;
  post_render_.push_back({id, std::move(callback)});
  return id;
}

void RenderHooks::Remove(int id) {
  std::erase_if(pre_render_,
                [id](const Entry& entry) { return entry.id == id; });
  std::erase_if(post_render_,
                [id](const Entry& entry) { return entry.id == id; });
}

void RenderHooks::InvokePreRender(filament::Engine& engine,
                                  filament::View& view) const {
  // Copy so a callback may register or remove hooks safely, and check the live
  // list each time: a hook removed by an earlier callback must not run.
  const std::vector<Entry> hooks = pre_render_;
  for (const Entry& hook : hooks) {
    bool registered = false;
    for (const Entry& live : pre_render_) {
      if (live.id == hook.id) {
        registered = true;
        break;
      }
    }
    if (registered && hook.callback) hook.callback(engine, view);
  }
}

void RenderHooks::InvokePostRender(filament::Engine& engine,
                                   filament::View& view) const {
  const std::vector<Entry> hooks = post_render_;
  for (const Entry& hook : hooks) {
    bool registered = false;
    for (const Entry& live : post_render_) {
      if (live.id == hook.id) {
        registered = true;
        break;
      }
    }
    if (registered && hook.callback) hook.callback(engine, view);
  }
}

void RenderHooks::Clear() {
  pre_render_.clear();
  post_render_.clear();
}

}  // namespace Mercury
