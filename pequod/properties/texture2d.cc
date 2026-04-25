//
// Created by smadhur on 4/25/2026.
//

#include "texture2d.h"

#include "debugger/debugger.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

namespace Pequod {

Texture2D::Texture2D(const std::string& file_path) {
  raw_data = stbi_load(file_path.c_str(), &x, &y, &channels, 4);
  if (raw_data) {
    PDebug::log("Loaded image: {}", file_path);
  } else {
    PDebug::error("Could not load image: {}", file_path);
  }
}

Texture2D::~Texture2D() {
  if (raw_data) {
    stbi_image_free(raw_data);
  }
}

}  // namespace Pequod