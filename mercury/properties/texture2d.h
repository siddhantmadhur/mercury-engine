//
// Created by smadhur on 4/25/2026.
//

#ifndef MERCURYENGINE_TEXTURE2D_H
#define MERCURYENGINE_TEXTURE2D_H
#include "stb/stb_image.h"

#include <string>

#include "glm/vec4.hpp"

namespace Mercury {

// stbi_load() through os::ReadBytes, so images can come from a mounted .mpack.
// Free the result with stbi_image_free().
stbi_uc *LoadImageFile(const std::string &path, int *width, int *height,
                       int *channels, int desired_channels);

class Texture2D {
 public:
  Texture2D();  // Creates a blank white texture
  Texture2D(const std::string &file_path, int depth = 4);

  ~Texture2D();

  const stbi_uc *GetData() const { return raw_data; }
  int GetWidth() const { return x; }
  int GetHeight() const { return y; }

  std::string GetPath() const;

  void SetAtlasUV(glm::vec4);

  glm::vec4 GetAtlasUV() const { return atlas_uv_; }

  bool IsDirty() const { return dirty_; }
  void ClearDirty() { dirty_ = false; }

 private:
  stbi_uc *raw_data = nullptr;
  int x, y, channels;
  std::string path_;

  glm::vec4 atlas_uv_;
  bool dirty_ = true;
};
}  // namespace Mercury

#endif  // MERCURYENGINE_TEXTURE2D_H
