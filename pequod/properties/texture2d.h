//
// Created by smadhur on 4/25/2026.
//

#ifndef PEQUODENGINE_TEXTURE2D_H
#define PEQUODENGINE_TEXTURE2D_H
#include "property.h"
#include "stb/stb_image.h"

#include <string>

#include "glm/vec4.hpp"

namespace Pequod {
 class Texture2D : public Property {
 public:
  Texture2D(); // Creates a blank white texture
  Texture2D(const std::string &file_path, int depth = 4);

  ~Texture2D();

  const stbi_uc *GetData() const { return raw_data; }
  int GetWidth() const { return x; }
  int GetHeight() const { return y; }

  std::string GetPath() const;

  void SetAtlasUV(glm::vec4);

  glm::vec4 GetAtlasUV() const { return atlas_uv_; }

 private:
  stbi_uc *raw_data = nullptr;
  int x, y, channels;
  std::string path_;

  glm::vec4 atlas_uv_;
 };
} // namespace Pequod

#endif  // PEQUODENGINE_TEXTURE2D_H
