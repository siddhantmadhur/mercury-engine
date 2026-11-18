// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "prefab_cache.h"

#include "debugger/debugger.h"

namespace Mercury {

const Map* PrefabCache::Get(std::string_view res_path) {
  if (res_path.empty() || !project_) return nullptr;
  if (auto it = prefabs_.find(res_path); it != prefabs_.end()) {
    return it->second.get();
  }
  if (loading_.contains(res_path)) {
    PDebug::error("Prefab {} contains itself", res_path);
    return nullptr;
  }
  loading_.insert(std::string(res_path));
  auto map = std::make_unique<Map>();
  std::string error;
  if (!map->Load(project_->Resolve(res_path), &error, Loader())) {
    PDebug::error("Could not load prefab: {}", error);
    map.reset();  // remembered as missing so it is only reported once
  }
  loading_.erase(loading_.find(res_path));
  const Map* raw = map.get();
  prefabs_.emplace(std::string(res_path), std::move(map));
  return raw;
}

}  // namespace Mercury
