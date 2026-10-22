#include "asset_database.h"

#include <system_error>

#include "debugger/debugger.h"

namespace Mercury {

bool LoadAssetFile(const std::filesystem::path& file, Asset& out,
                   std::string* error) {
  mtext::Document document;
  mtext::ParseError parse_error;
  if (!mtext::LoadFile(file, document, &parse_error)) {
    if (error) *error = file.string() + ":" + parse_error.ToString();
    return false;
  }
  auto header = document.Find("masset");
  if (!header) {
    if (error) *error = file.string() + ": missing [masset type=...] header";
    return false;
  }
  out.type = header->attributes.GetString("type");
  out.properties = header->properties;
  return true;
}

bool SaveAssetFile(const std::filesystem::path& file, const Asset& asset) {
  mtext::Document document;
  auto& header = document.Add("masset");
  header.attributes.Set("type", asset.type);
  header.attributes.Set("format", 1);
  header.properties = asset.properties;
  return mtext::SaveFile(file, document);
}

AssetDatabase::AssetDatabase(std::shared_ptr<const Project> project)
    : project_(std::move(project)) {}

const Asset* AssetDatabase::Load(std::string_view res_path) {
  if (res_path.empty() || !project_) return nullptr;
  const auto file = project_->Resolve(res_path);
  std::error_code ec;
  const auto modified = std::filesystem::last_write_time(file, ec);
  if (ec) {
    PDebug::error("Asset not found: {}", res_path);
    cache_.erase(std::string(res_path));
    return nullptr;
  }

  auto it = cache_.find(res_path);
  if (it != cache_.end() && it->second.modified == modified) {
    return &it->second.asset;
  }

  Entry entry;
  std::string error;
  if (!LoadAssetFile(file, entry.asset, &error)) {
    PDebug::error("Could not load asset: {}", error);
    return nullptr;
  }
  entry.asset.path = std::string(res_path);
  entry.modified = modified;
  if (!TypeRegistry::Instance().FindAssetType(entry.asset.type)) {
    PDebug::warn("{}: unknown asset type \"{}\"", res_path, entry.asset.type);
  }
  auto [inserted, _] = cache_.insert_or_assign(std::string(res_path), entry);
  return &inserted->second.asset;
}

bool AssetDatabase::Save(const Asset& asset) {
  if (!project_) return false;
  if (!SaveAssetFile(project_->Resolve(asset.path), asset)) {
    PDebug::error("Could not save asset {}", asset.path);
    return false;
  }
  Forget(asset.path);
  return true;
}

std::string AssetDatabase::TypeOf(std::string_view res_path) {
  if (res_path.ends_with(kMapExtension)) return "Map";
  if (res_path.ends_with(kUxmlExtension)) return "UXML";
  if (res_path.ends_with(kUssExtension)) return "StyleSheet";
  if (res_path.ends_with(".ttf") || res_path.ends_with(".otf")) return "Font";
  if (!res_path.ends_with(kAssetExtension)) return "Texture";
  if (auto asset = Load(res_path)) return asset->type;
  return "";
}

void AssetDatabase::Forget(std::string_view res_path) {
  auto it = cache_.find(res_path);
  if (it != cache_.end()) cache_.erase(it);
}

}  // namespace Mercury
