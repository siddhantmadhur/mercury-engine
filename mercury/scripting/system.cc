// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "scripting/system.h"

#include <algorithm>

namespace Mercury {

SystemRegistry& SystemRegistry::Instance() {
  static SystemRegistry registry;
  return registry;
}

void SystemRegistry::Register(SystemInfo info) {
  auto existing = std::find_if(
      systems_.begin(), systems_.end(),
      [&](const SystemInfo& entry) { return entry.name == info.name; });
  if (existing != systems_.end()) {
    *existing = std::move(info);
  } else {
    systems_.push_back(std::move(info));
  }
  std::stable_sort(systems_.begin(), systems_.end(),
                   [](const SystemInfo& a, const SystemInfo& b) {
                     if (a.phase != b.phase) return a.phase < b.phase;
                     if (a.order != b.order) return a.order < b.order;
                     return a.name < b.name;
                   });
}

void SystemRegistry::Clear() { systems_.clear(); }

}  // namespace Mercury
