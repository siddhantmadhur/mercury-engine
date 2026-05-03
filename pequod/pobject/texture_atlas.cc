//
// Created by smadhur on 4/30/2026.
//

#define STB_RECT_PACK_IMPLEMENTATION
#include "texture_atlas.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include "globals.h"
#include "debugger/debugger.h"

namespace Pequod {

TextureAtlas::TextureAtlas() {
  stbrp_init_target(&ctx_, width_, height_, nodes_, 512);
  white_pixel_ = new Texture2D();
  AddTexture(white_pixel_);
}

void TextureAtlas::AddTexture(Texture2D* texture) {
  if (!texture) {
    return;
  }

  if (images_.contains(texture->GetPath())) {
    auto img_id = images_[texture->GetPath()];
    auto img = textures_[img_id];
    texture->SetAtlasUV(img->GetAtlasUV());
    return;
  }
  rects.push_back(stbrp_rect{
      .id = img_id_,
      .w = texture->GetWidth(),
      .h = texture->GetHeight(),
  });
  stbrp_pack_rects(&ctx_, rects.data(), static_cast<int>(rects.size()));
  textures_.push_back(texture);
  images_[texture->GetPath()] = img_id_;

  img_id_ += 1;
}

void TextureAtlas::UpdateAtlas() {
  if (img_id_ == last_img_id_updated_) {
    return;
  }
  last_img_id_updated_ = img_id_;
  if (raw_data_ == nullptr) {
    raw_data_ = new uint8_t[(size_t)width_ * (size_t)height_ * 4]{};
  } else {
    std::memset(raw_data_, 0, (size_t)width_ * (size_t)height_ * 4);
  }
  const float fw = static_cast<float>(width_);
  const float fh = static_cast<float>(height_);
  for (stbrp_rect& rect : rects) {
    auto img = textures_[rect.id];
    for (int row = 0; row < rect.h; row++) {
      const uint8_t* src_row = img->GetData() + (size_t)(row * rect.w * 4);
      uint8_t* dest_row =
          raw_data_ + (size_t)((rect.y + row) * width_ + rect.x) * 4;
      memcpy(dest_row, src_row, (size_t)rect.w * 4);
    }

    const float img_width = static_cast<float>(img->GetWidth());
    const float img_height = static_cast<float>(img->GetHeight());
    glm::vec4 atlas_uv;
    atlas_uv.x = static_cast<float>(rect.x) / fw;
    atlas_uv.y = static_cast<float>(rect.y) / fh;
    atlas_uv.z = atlas_uv.x + img_width / fw;
    atlas_uv.w = atlas_uv.y + img_height / fh;
    img->SetAtlasUV(atlas_uv);
  }

  PDebug::info("Writing atlas...");
  stbi_write_png("atlas.png", width_, height_, 4, raw_data_, 4 * width_);

  needs_gpu_upload_ = true;
}

TextureAtlas::~TextureAtlas() { delete[] raw_data_; }

}  // namespace Pequod