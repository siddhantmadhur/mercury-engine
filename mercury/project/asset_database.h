/*
 * asset_database.h
 *
 * Loads .masset files (Godot's .tres, Unity's .mat/.physicMaterial) by their
 * res:// path and caches them. A .masset is one section:
 *
 *   [masset type="Material" format=1]
 *   color = Color(1, 1, 1, 1)
 *
 * Asset types and their fields are registered in TypeRegistry.
 */
#ifndef MERCURY_PROJECT_ASSET_DATABASE_H
#define MERCURY_PROJECT_ASSET_DATABASE_H

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "project/project.h"
#include "scripting/type_registry.h"
#include "serialization/mtext.h"

namespace Mercury {

struct Asset {
  std::string type;
  std::string path;  // res:// path
  mtext::PropertyList properties;
};

class AssetDatabase {
 public:
  explicit AssetDatabase(std::shared_ptr<const Project> project);

  /**
   * @brief Load (or return the cached copy of) an asset
   *
   * The file is read again when it changed on disk since the last load.
   * @return nullptr if the file is missing or invalid; the error is logged
   */
  const Asset* Load(std::string_view res_path);

  /**
   * @brief Load an asset into its reflected C++ type, e.g. MaterialAsset
   *
   * Returns a default value when the path is empty or the asset is missing.
   */
  template <typename T>
  T LoadAs(std::string_view res_path) {
    if (res_path.empty()) return T{};
    if (auto asset = Load(res_path)) {
      return ReadFields<T>(asset->properties, asset->path);
    }
    return T{};
  }

  bool Save(const Asset& asset);

  // The type in the file's header, or "" if the file cannot be read.
  std::string TypeOf(std::string_view res_path);
  // The type of a file that is not a .masset, from its extension ("Model",
  // "AudioClip", "Prefab", ...); "" for a .masset or an unknown file.
  static std::string TypeFromExtension(std::string_view res_path);

  void Forget(std::string_view res_path);
  void Clear() { cache_.clear(); }

  const Project* GetProject() const { return project_.get(); }

 private:
  struct Entry {
    Asset asset;
    std::filesystem::file_time_type modified;
  };

  std::shared_ptr<const Project> project_;
  std::map<std::string, Entry, std::less<>> cache_;
};

bool LoadAssetFile(const std::filesystem::path& file, Asset& out,
                   std::string* error = nullptr);
bool SaveAssetFile(const std::filesystem::path& file, const Asset& asset);

}  // namespace Mercury

#endif  // MERCURY_PROJECT_ASSET_DATABASE_H
