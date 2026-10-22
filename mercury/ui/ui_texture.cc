#include "ui_texture.h"

#include <cstring>

#include "debugger/debugger.h"
#include "stb/stb_image.h"
#include "ui/draw_data.h"

namespace Mercury::UI {

void UIDrawData::Append(const UIDrawData& other) {
  const uint32_t base_vertex = static_cast<uint32_t>(vertices.size());
  const uint32_t base_index = static_cast<uint32_t>(indices.size());
  vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
  indices.reserve(indices.size() + other.indices.size());
  for (uint32_t index : other.indices) indices.push_back(index + base_vertex);
  for (UIDrawCommand command : other.commands) {
    command.index_offset += base_index;
    commands.push_back(command);
  }
  target_size = glm::max(target_size, other.target_size);
}

UITextureRegistry& UITextureRegistry::Instance() {
  static UITextureRegistry registry;
  return registry;
}

std::shared_ptr<UITexture> UITextureRegistry::Create(int width, int height,
                                                     std::vector<uint8_t> rgba) {
  auto texture = std::make_shared<UITexture>();
  texture->width = width;
  texture->height = height;
  texture->rgba = std::move(rgba);
  texture->rgba.resize(static_cast<size_t>(width) * height * 4, 0);
  std::lock_guard lock(mutex_);
  texture->id = next_id_++;
  textures_[texture->id] = texture;
  return texture;
}

std::shared_ptr<UITexture> UITextureRegistry::LoadFile(
    const std::string& absolute_path) {
  {
    std::lock_guard lock(mutex_);
    auto it = by_path_.find(absolute_path);
    if (it != by_path_.end()) {
      auto tex = textures_.find(it->second);
      if (tex != textures_.end()) return tex->second;
      return nullptr;  // failed before
    }
  }
  int width = 0, height = 0, channels = 0;
  stbi_uc* pixels = stbi_load(absolute_path.c_str(), &width, &height, &channels, 4);
  if (!pixels) {
    PDebug::warn("UI: could not load image {}", absolute_path);
    std::lock_guard lock(mutex_);
    by_path_[absolute_path] = 0;
    return nullptr;
  }
  std::vector<uint8_t> rgba(pixels, pixels + static_cast<size_t>(width) * height * 4);
  stbi_image_free(pixels);
  auto texture = Create(width, height, std::move(rgba));
  std::lock_guard lock(mutex_);
  by_path_[absolute_path] = texture->id;
  return texture;
}

void UITextureRegistry::Release(uint32_t id) {
  std::lock_guard lock(mutex_);
  if (textures_.erase(id)) generation_++;
  std::erase_if(by_path_, [id](const auto& entry) { return entry.second == id; });
}

std::shared_ptr<UITexture> UITextureRegistry::Find(uint32_t id) const {
  std::lock_guard lock(mutex_);
  auto it = textures_.find(id);
  return it == textures_.end() ? nullptr : it->second;
}

}  // namespace Mercury::UI
