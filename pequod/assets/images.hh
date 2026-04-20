#ifndef MERCURY_IMAGES_IMPL_HH
#define MERCURY_IMAGES_IMPL_HH

#include <stb/stb_image.h>

#include <string>

namespace Pequod {
class Image {
 public:
  Image(std::string);

  stbi_uc *raw_data;
  int x, y, channels;
};
}  // namespace Pequod
#endif
