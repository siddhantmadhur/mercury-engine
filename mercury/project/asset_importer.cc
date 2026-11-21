// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "project/asset_importer.h"

#include <algorithm>
#include <cctype>

#include "project/project.h"

namespace Mercury {

AssetImporterRegistry& AssetImporterRegistry::Instance() {
  static AssetImporterRegistry registry;
  return registry;
}

AssetImporterRegistry::AssetImporterRegistry() { Reset(); }

std::string AssetImporterRegistry::NormalizeExtension(
    std::string_view extension) {
  std::string normalized(extension);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (!normalized.empty() && normalized.front() != '.') {
    normalized.insert(normalized.begin(), '.');
  }
  return normalized;
}

void AssetImporterRegistry::Register(AssetImporterInfo info) {
  RegisterOwned({}, std::move(info));
}

void AssetImporterRegistry::RegisterOwned(std::string owner,
                                          AssetImporterInfo info) {
  const size_t index = importers_.size();
  importers_.push_back(std::move(info));
  owners_.push_back(std::move(owner));
  for (const std::string& extension : importers_.back().extensions) {
    const std::string normalized = NormalizeExtension(extension);
    if (!normalized.empty()) by_extension_[normalized] = index;
  }
}

bool AssetImporterRegistry::Unregister(std::string_view type) {
  const size_t before = importers_.size();
  for (size_t i = importers_.size(); i-- > 0;) {
    if (importers_[i].type != type) continue;
    importers_.erase(importers_.begin() + static_cast<ptrdiff_t>(i));
    owners_.erase(owners_.begin() + static_cast<ptrdiff_t>(i));
  }
  if (importers_.size() == before) return false;
  RebuildIndex();
  return true;
}

bool AssetImporterRegistry::UnregisterOwner(std::string_view owner) {
  const size_t before = importers_.size();
  for (size_t i = importers_.size(); i-- > 0;) {
    if (owners_[i] != owner) continue;
    importers_.erase(importers_.begin() + static_cast<ptrdiff_t>(i));
    owners_.erase(owners_.begin() + static_cast<ptrdiff_t>(i));
  }
  if (importers_.size() == before) return false;
  RebuildIndex();
  return true;
}

void AssetImporterRegistry::RebuildIndex() {
  by_extension_.clear();
  for (size_t i = 0; i < importers_.size(); i++) {
    for (const std::string& extension : importers_[i].extensions) {
      const std::string normalized = NormalizeExtension(extension);
      if (!normalized.empty()) by_extension_[normalized] = i;
    }
  }
}

std::string AssetImporterRegistry::TypeFromExtension(
    std::string_view path) const {
  const std::string extension =
      std::filesystem::path(std::string(path)).extension().string();
  if (extension.empty()) return "";
  const AssetImporterInfo* info = Find(extension);
  return info ? info->type : "";
}

const AssetImporterInfo* AssetImporterRegistry::Find(
    std::string_view extension) const {
  const std::string normalized = NormalizeExtension(extension);
  auto it = by_extension_.find(normalized);
  if (it == by_extension_.end()) return nullptr;
  return &importers_[it->second];
}

bool AssetImporterRegistry::Import(const std::filesystem::path& source,
                                   mtext::PropertyList& properties,
                                   std::string* error) const {
  const AssetImporterInfo* info = Find(source.extension().string());
  if (!info) {
    if (error) *error = source.string() + ": no importer for this extension";
    return false;
  }
  if (!info->import) {
    if (error)
      *error = source.string() + ": the " + info->type +
               " importer has no import callback";
    return false;
  }
  return info->import(source, properties, error);
}

void AssetImporterRegistry::Reset() {
  importers_.clear();
  owners_.clear();
  by_extension_.clear();
  RegisterBuiltins();
}

void AssetImporterRegistry::RegisterBuiltins() {
  // Keep this table identical to the old AssetDatabase::TypeFromExtension
  // mapping: the editor and the runtime must agree on what a file is.
  auto add = [this](std::string_view extension, const char* type) {
    Register(AssetImporterInfo{{std::string(extension)}, type, {}, {}});
  };
  add(kMapExtension, "Map");
  add(kUxmlExtension, "UXML");
  add(kUssExtension, "StyleSheet");
  for (const char* extension : {".ttf", ".otf"}) add(extension, "Font");
  add(".mprefab", "Prefab");
  add(".manim", "AnimatorController");
  add(".minput", "InputActions");
  for (const char* extension : {".wav", ".ogg", ".mp3", ".flac"}) {
    add(extension, "AudioClip");
  }
  for (const char* extension : {".fbx", ".obj", ".gltf", ".glb", ".dae"}) {
    add(extension, "Model");
  }
  for (const char* extension :
       {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".svg"}) {
    add(extension, "Texture");
  }
}

}  // namespace Mercury
