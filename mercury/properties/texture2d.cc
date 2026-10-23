//
// Created by smadhur on 4/25/2026.
//

#include "texture2d.h"

#include <cstdint>

#include "debugger/debugger.h"
#include "os/filesystem.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

namespace Mercury {
stbi_uc *LoadImageFile(const std::string &path, int *width, int *height,
                       int *channels, int desired_channels) {
  auto data = os::ReadBytes(path);
  if (!data || data->empty() || data->size() > INT32_MAX) return nullptr;
  return stbi_load_from_memory(data->data(), static_cast<int>(data->size()),
                               width, height, channels, desired_channels);
}

Texture2D::Texture2D() {
  x = 1;
  y = 1;
  channels = 4;
  raw_data = (stbi_uc *)malloc(4);
  memset(raw_data, 255, 4);
}

Texture2D::Texture2D(const std::string &file_path, int depth) {
  this->path_ = file_path;
  raw_data = LoadImageFile(file_path, &x, &y, &channels, depth);
  if (raw_data) {
    PDebug::log("Loaded image: {} [{}x{} - {}]", file_path, x, y, channels);
  } else {
    PDebug::error("Could not load image: {}", file_path);
  }
}

std::string Texture2D::GetPath() const { return this->path_; }
void Texture2D::SetAtlasUV(glm::vec4 atlas_uv) {
  this->atlas_uv_ = atlas_uv;
  dirty_ = true;
}

Texture2D::~Texture2D() {}
}  // namespace Mercury
