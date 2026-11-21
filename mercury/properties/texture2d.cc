// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//
// Created by smadhur on 4/25/2026.
//

#include "texture2d.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "debugger/debugger.h"
#include "os/filesystem.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/src/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "nanosvg/src/nanosvgrast.h"

namespace Mercury {

namespace {

bool IsSvg(const std::string& path) {
  if (path.size() < 4) return false;
  std::string ext = path.substr(path.size() - 4);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext == ".svg";
}

// Rasterizes an SVG so its longer side has at least kSvgSize pixels (icons
// are small; this keeps them sharp when scaled up).
stbi_uc* RasterizeSvg(std::vector<uint8_t> text, int* width, int* height,
                      int* channels) {
  constexpr float kSvgSize = 128.0f;
  text.push_back(0);
  NSVGimage* image =
      nsvgParse(reinterpret_cast<char*>(text.data()), "px", 96.0f);
  if (!image) return nullptr;
  if (image->width <= 0 || image->height <= 0) {
    nsvgDelete(image);
    return nullptr;
  }
  // The file chooses its own size ("width=60000", "1e10", "inf"), so bound the
  // output: the longer side never exceeds kMaxSvgSide pixels.
  constexpr float kMaxSvgSide = 8192.0f;
  const float longest = std::max(image->width, image->height);
  if (!std::isfinite(longest) || !std::isfinite(image->width) ||
      !std::isfinite(image->height)) {
    nsvgDelete(image);
    return nullptr;
  }
  const float scale =
      std::min(std::max(1.0f, kSvgSize / longest), kMaxSvgSide / longest);
  const int w = std::clamp(static_cast<int>(std::ceil(image->width * scale)), 1,
                           static_cast<int>(kMaxSvgSide));
  const int h = std::clamp(static_cast<int>(std::ceil(image->height * scale)),
                           1, static_cast<int>(kMaxSvgSide));
  // stbi_image_free() is free(), so allocate with malloc.
  auto* pixels =
      static_cast<stbi_uc*>(std::malloc(static_cast<size_t>(w) * h * 4));
  if (!pixels) {
    nsvgDelete(image);
    return nullptr;
  }
  NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
  if (!rasterizer) {
    std::free(pixels);
    nsvgDelete(image);
    return nullptr;
  }
  nsvgRasterize(rasterizer, image, 0, 0, scale, pixels, w, h, w * 4);
  nsvgDeleteRasterizer(rasterizer);
  nsvgDelete(image);
  *width = w;
  *height = h;
  if (channels) *channels = 4;
  return pixels;
}

}  // namespace

stbi_uc* LoadImageFile(const std::string& path, int* width, int* height,
                       int* channels, int desired_channels) {
  auto data = os::ReadBytes(path);
  if (!data || data->empty() || data->size() > INT32_MAX) return nullptr;
  if (IsSvg(path)) {
    // Always RGBA; callers here ask for 4 channels.
    return RasterizeSvg(std::move(*data), width, height, channels);
  }
  return stbi_load_from_memory(data->data(), static_cast<int>(data->size()),
                               width, height, channels, desired_channels);
}

Texture2D::Texture2D() {
  x = 1;
  y = 1;
  channels = 4;
  raw_data = (stbi_uc*)malloc(4);
  if (raw_data) memset(raw_data, 255, 4);
}

Texture2D::Texture2D(const std::string& file_path, int depth) {
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
