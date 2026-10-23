/*
 * prefab_cache.h
 *
 * Loads .mprefab files once per project. A .mprefab is a map whose root node
 * is the prefab (Unity's .prefab); maps place copies of it with
 *
 *   [node name="Crate" parent="." instance=ExtResource("2")]
 *
 * and override its values with ordinary [component] sections.
 */
#ifndef MERCURY_PROJECT_PREFAB_CACHE_H
#define MERCURY_PROJECT_PREFAB_CACHE_H

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>

#include "project/map.h"
#include "project/project.h"

namespace Mercury {

class PrefabCache {
 public:
  explicit PrefabCache(std::shared_ptr<const Project> project = nullptr)
      : project_(std::move(project)) {}

  void SetProject(std::shared_ptr<const Project> project) {
    project_ = std::move(project);
    prefabs_.clear();
  }

  // The prefab at a res:// path, loading it (and the prefabs inside it) the
  // first time; nullptr if it cannot be read. Errors are logged once.
  const Map* Get(std::string_view res_path);

  // Pass to Map::Load so instance nodes are expanded.
  Map::PrefabLoaderFn Loader() {
    return [this](const std::string& path) { return Get(path); };
  }

  void Clear() { prefabs_.clear(); }

 private:
  std::shared_ptr<const Project> project_;
  std::map<std::string, std::unique_ptr<Map>, std::less<>> prefabs_;
  std::set<std::string, std::less<>> loading_;  // guards against cycles
};

}  // namespace Mercury

#endif  // MERCURY_PROJECT_PREFAB_CACHE_H
