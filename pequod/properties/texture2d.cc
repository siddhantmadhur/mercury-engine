//
// Created by smadhur on 4/25/2026.
//

#include "texture2d.h"

#include "debugger/debugger.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

namespace Pequod {

Texture2D::Texture2D() {
  x = 1;
  y = 1;
  channels = 4;
  raw_data = (stbi_uc*)malloc(4);
  memset(raw_data, 255, 4);
}

Texture2D::Texture2D(const std::string& file_path) {
  this->path_ = file_path;
  raw_data = stbi_load(file_path.c_str(), &x, &y, &channels, 4);
  if (raw_data) {
    PDebug::log("Loaded image: {}", file_path);
  } else {
    PDebug::error("Could not load image: {}", file_path);
  }
}

std::string Texture2D::GetPath() const { return this->path_; }
void Texture2D::SetAtlasUV(glm::vec4 atlas_uv) { this->atlas_uv_ = atlas_uv; }

Texture2D::~Texture2D() {}

}  // namespace Pequod