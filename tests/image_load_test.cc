// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

#include "properties/texture2d.h"
#include "stb/stb_image.h"

using namespace Mercury;

namespace {

std::filesystem::path WriteSvg(const std::string& name,
                               const std::string& text) {
  const auto dir =
      std::filesystem::temp_directory_path() /
      ("image_load_test_" + std::to_string(std::random_device{}()));
  std::filesystem::create_directories(dir);
  const auto file = dir / name;
  std::ofstream(file) << text;
  return file;
}

}  // namespace

// An SVG picks its own size; one that asks for gigabytes of pixels must be
// scaled down, not allocated (or overflow the size arithmetic).
TEST(ImageLoad, SvgSizeIsBounded) {
  for (const char* size : {"60000", "1e10", "536870912"}) {
    const auto file = WriteSvg(
        "huge.svg", std::string("<svg xmlns=\"http://www.w3.org/2000/svg\" "
                                "width=\"") +
                        size + "\" height=\"" + size +
                        "\"><rect width=\"10\" height=\"10\"/></svg>");
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = LoadImageFile(file.string(), &w, &h, &channels, 4);
    if (pixels) {  // a refusal is fine too; a huge allocation is not
      EXPECT_LE(w, 8192) << size;
      EXPECT_LE(h, 8192) << size;
      EXPECT_GT(w, 0);
      stbi_image_free(pixels);
    }
    std::filesystem::remove_all(file.parent_path());
  }
}

TEST(ImageLoad, SmallSvgStillScalesUp) {
  const auto file = WriteSvg(
      "icon.svg",
      "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
      "<rect width=\"16\" height=\"16\" fill=\"red\"/></svg>");
  int w = 0, h = 0, channels = 0;
  stbi_uc* pixels = LoadImageFile(file.string(), &w, &h, &channels, 4);
  ASSERT_NE(pixels, nullptr);
  EXPECT_EQ(w, 128);
  EXPECT_EQ(h, 128);
  stbi_image_free(pixels);
  std::filesystem::remove_all(file.parent_path());
}
