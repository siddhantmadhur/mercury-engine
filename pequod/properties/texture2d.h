//
// Created by smadhur on 4/25/2026.
//

#ifndef PEQUODENGINE_TEXTURE2D_H
#define PEQUODENGINE_TEXTURE2D_H
#include "property.h"
#include "stb/stb_image.h"

#include <string>

namespace Pequod {
class Texture2D : public Property {
 public:
  Texture2D(const std::string& file_path);
  ~Texture2D();

  const stbi_uc* GetData()   const { return raw_data; }
  int            GetWidth()  const { return x; }
  int            GetHeight() const { return y; }

 private:
  stbi_uc* raw_data = nullptr;
  int x, y, channels;
};
}  // namespace Pequod

#endif  // PEQUODENGINE_TEXTURE2D_H
