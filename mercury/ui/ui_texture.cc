// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui_texture.h"

#include <cstring>

#include "debugger/debugger.h"
#include "properties/texture2d.h"
#include "stb/stb_image.h"
#include "ui/draw_data.h"

namespace Mercury::UI {

namespace {

// Renderers upload these as one image; anything larger is almost certainly a
// bad size and would exhaust memory while resizing.
constexpr int kMaxTextureDimension = 16384;

}  // namespace

void UIDrawData::Append(const UIDrawData& other) {
  const uint64_t base_vertex = vertices.size();
  const uint64_t base_index = indices.size();
  const uint64_t total_vertices = base_vertex + other.vertices.size();
  const uint64_t total_indices = base_index + other.indices.size();
  // UIDrawData stores 32-bit indices, so merging must not narrow silently.
  if (total_vertices > UINT32_MAX || total_indices > UINT32_MAX) {
    PDebug::error("UI: draw data exceeds 32-bit vertex or index limits");
    return;
  }
  for (const UIDrawCommand& command : other.commands) {
    if (base_index + command.index_offset > UINT32_MAX) {
      PDebug::error("UI: draw command index offset overflows uint32_t");
      return;
    }
  }
  vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
  indices.reserve(static_cast<size_t>(total_indices));
  for (uint32_t index : other.indices) {
    indices.push_back(static_cast<uint32_t>(base_vertex) + index);
  }
  for (UIDrawCommand command : other.commands) {
    command.index_offset += static_cast<uint32_t>(base_index);
    commands.push_back(command);
  }
  target_size = glm::max(target_size, other.target_size);
}

UITextureRegistry& UITextureRegistry::Instance() {
  static UITextureRegistry registry;
  return registry;
}

std::shared_ptr<UITexture> UITextureRegistry::Create(
    int width, int height, std::vector<uint8_t> rgba) {
  if (width < 1 || height < 1 || width > kMaxTextureDimension ||
      height > kMaxTextureDimension) {
    PDebug::warn("UI: invalid texture size {}x{}", width, height);
    return nullptr;
  }
  auto texture = std::make_shared<UITexture>();
  texture->width = width;
  texture->height = height;
  texture->rgba = std::move(rgba);
  const size_t required =
      static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  texture->rgba.resize(required, 0);
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
  stbi_uc* pixels = LoadImageFile(absolute_path, &width, &height, &channels, 4);
  if (!pixels) {
    PDebug::warn("UI: could not load image {}", absolute_path);
    std::lock_guard lock(mutex_);
    by_path_[absolute_path] = 0;
    return nullptr;
  }
  std::vector<uint8_t> rgba(pixels,
                            pixels + static_cast<size_t>(width) * height * 4);
  stbi_image_free(pixels);
  auto texture = Create(width, height, std::move(rgba));
  std::lock_guard lock(mutex_);
  if (!texture) {
    by_path_[absolute_path] = 0;
    return nullptr;
  }
  by_path_[absolute_path] = texture->id;
  return texture;
}

void UITextureRegistry::Release(uint32_t id) {
  std::lock_guard lock(mutex_);
  if (textures_.erase(id)) generation_++;
  std::erase_if(by_path_,
                [id](const auto& entry) { return entry.second == id; });
}

std::shared_ptr<UITexture> UITextureRegistry::Find(uint32_t id) const {
  std::lock_guard lock(mutex_);
  auto it = textures_.find(id);
  return it == textures_.end() ? nullptr : it->second;
}

}  // namespace Mercury::UI
