// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "scripting/app_hook.h"

#include <algorithm>

namespace Mercury {

AppHookRegistry& AppHookRegistry::Instance() {
  static AppHookRegistry registry;
  return registry;
}

void AppHookRegistry::Register(AppHookInfo info) {
  auto existing = std::find_if(
      hooks_.begin(), hooks_.end(),
      [&](const AppHookInfo& entry) { return entry.name == info.name; });
  if (existing != hooks_.end()) {
    *existing = std::move(info);
  } else {
    hooks_.push_back(std::move(info));
  }
}

std::vector<std::unique_ptr<AppHook>> AppHookRegistry::CreateAll() const {
  std::vector<std::unique_ptr<AppHook>> hooks;
  hooks.reserve(hooks_.size());
  for (const AppHookInfo& info : hooks_) {
    if (info.create) hooks.push_back(info.create());
  }
  return hooks;
}

void AppHookRegistry::Clear() { hooks_.clear(); }

}  // namespace Mercury
