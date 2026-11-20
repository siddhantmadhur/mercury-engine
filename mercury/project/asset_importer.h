// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * asset_importer.h
 *
 * The extension -> asset-type table the editor and runtime use to know what a
 * file is ("coins.png" is a Texture, "hero.fbx" is a Model). The engine's
 * built-ins are registered by default; a game can add its own extensions or
 * replace the type of an existing one.
 *
 *   Mercury::AssetImporterRegistry::Instance().Register({
 *       .extensions = {".vox", ".voxel"},
 *       .type = "VoxelModel",
 *       .description = "Voxel model",
 *   });
 *
 * An importer may also supply an `import` callback, which reads the source file
 * into the properties of its .masset form. The engine's built-in importers do
 * not need one: their loaders already read the file directly.
 */
#ifndef MERCURY_PROJECT_ASSET_IMPORTER_H
#define MERCURY_PROJECT_ASSET_IMPORTER_H

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "serialization/mtext.h"

namespace Mercury {

/**
 * @brief What an asset file with the listed extensions is
 */
struct AssetImporterInfo {
  // Lowercase extensions, with or without the leading dot ("png" or ".png").
  std::vector<std::string> extensions;
  // The asset type shown in the editor and written in a .masset header.
  std::string type;
  std::string description;
  // Optional: fills `properties` from `source`. Called by Import().
  std::function<bool(const std::filesystem::path& source,
                     mtext::PropertyList& properties, std::string* error)>
      import;
};

/**
 * @brief The process-wide extension -> asset-type registry
 *
 * Built-ins are registered when the singleton is first used. Registering a
 * second importer for an extension replaces the first one's type.
 */
class AssetImporterRegistry {
 public:
  static AssetImporterRegistry& Instance();

  // Adds an importer. Its extensions start using the new type immediately.
  void Register(AssetImporterInfo info);

  // The asset type for a path or bare extension; "" when unknown.
  std::string TypeFromExtension(std::string_view path) const;

  // The importer registered for an extension, or null.
  const AssetImporterInfo* Find(std::string_view extension) const;

  // Every importer in registration order.
  const std::vector<AssetImporterInfo>& Importers() const { return importers_; }

  // Runs the importer for `source`'s extension. False when there is no
  // importer or no import callback.
  bool Import(const std::filesystem::path& source,
              mtext::PropertyList& properties,
              std::string* error = nullptr) const;

  // Drops every registration and restores the engine's built-ins.
  void Reset();

 private:
  AssetImporterRegistry();

  void RegisterBuiltins();
  static std::string NormalizeExtension(std::string_view extension);

  std::vector<AssetImporterInfo> importers_;
  std::map<std::string, size_t, std::less<>> by_extension_;
};

}  // namespace Mercury

#endif  // MERCURY_PROJECT_ASSET_IMPORTER_H
