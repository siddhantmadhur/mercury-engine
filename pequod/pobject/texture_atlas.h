//
// Created by smadhur on 4/30/2026.
//

#ifndef PEQUOD_ENGINE_TEXTURE_ATLAS_H
#define PEQUOD_ENGINE_TEXTURE_ATLAS_H
#include <map>
#include <memory>

#include "imstb_rectpack.h"

#include "properties/texture2d.h"
#include <vector>

namespace Pequod {
class TextureAtlas {
 public:
  TextureAtlas();

  ~TextureAtlas();

  void AddTexture(Texture2D *);

  void UpdateAtlas();

  bool NeedsGpuUpload() const { return needs_gpu_upload_; }
  void ClearGpuUploadFlag() { needs_gpu_upload_ = false; }
  const uint8_t *GetData() const { return raw_data_; }
  int GetWidth() const { return width_; }
  int GetHeight() const { return height_; }
  glm::vec4 GetWhitePixelUV() const { return white_pixel_->GetAtlasUV(); }

 protected:
 private:
  int width_ = 2300;   // max size of the atlas
  int height_ = 2300;  // max size of the atlas
  stbrp_context ctx_{};
  stbrp_node nodes_[512] = {};
  Texture2D *white_pixel_;
  std::vector<stbrp_rect> rects;
  std::vector<Texture2D *> textures_;
  // All Texture2D* instances that share each img_id (the first is also in
  // textures_ above). Repacking the atlas reassigns sub-rects, so every alias
  // must have its atlas_uv refreshed — not just the canonical one.
  std::vector<std::vector<Texture2D *>> aliases_;
  int img_id_ = 0;
  uint8_t *raw_data_ = nullptr;
  std::map<std::string, int> images_;
  int last_img_id_updated_ = 0;
  bool needs_gpu_upload_ = false;
};
}  // namespace Pequod

#endif  // PEQUOD_ENGINE_TEXTURE_ATLAS_H
