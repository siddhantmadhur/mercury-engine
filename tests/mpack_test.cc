#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

#include "os/filesystem.h"
#include "pack/mpack.h"
#include "project/asset_database.h"
#include "project/map.h"
#include "project/project.h"
#include "properties/model.h"
#include "stb/stb_image_write.h"

using namespace Mercury;

namespace {

std::vector<uint8_t> Bytes(std::string_view text) {
  return {text.begin(), text.end()};
}

std::vector<uint8_t> Noise(size_t size, unsigned seed) {
  std::mt19937 random(seed);
  std::vector<uint8_t> out(size);
  for (auto& byte : out) byte = static_cast<uint8_t>(random());
  return out;
}

class MPackTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("mpack_test_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir_);
    // Project::Load canonicalizes, so mount at the same spelling.
    dir_ = std::filesystem::canonical(dir_);
  }
  void TearDown() override {
    os::UnmountAll();
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  std::filesystem::path dir_;
};

}  // namespace

TEST_F(MPackTest, RoundTripsStoredAndCompressedEntries) {
  const std::string text(4096, 'a');
  const auto noise = Noise(4096, 1);
  mpack::Writer writer;
  writer.AddBytes("maps/main.mmap", Bytes(text));
  writer.AddBytes("assets/noise.bin", noise);
  writer.AddBytes("empty.txt", {});
  std::string error;
  ASSERT_TRUE(writer.Write(dir_, "game", mpack::kDefaultMaxArchiveBytes, &error))
      << error;

  auto reader = mpack::Reader::Open(dir_ / "game.mpack", &error);
  ASSERT_TRUE(reader) << error;
  ASSERT_EQ(reader->GetEntries().size(), 3u);
  EXPECT_EQ(reader->Find("maps/main.mmap")->compression,
            mpack::Compression::kZlib);
  EXPECT_EQ(reader->Find("assets/noise.bin")->compression,
            mpack::Compression::kStored);

  EXPECT_EQ(reader->Read("maps/main.mmap"), Bytes(text));
  EXPECT_EQ(reader->Read("assets/noise.bin"), noise);
  EXPECT_EQ(reader->Read("empty.txt"), std::vector<uint8_t>{});
  EXPECT_FALSE(reader->Read("missing.txt"));
  // The small, compressible text makes the pack smaller than its contents.
  EXPECT_LT(std::filesystem::file_size(dir_ / "game.mpack"), 2 * 4096u);
}

TEST_F(MPackTest, RollsOverIntoNumberedArchives) {
  mpack::Writer writer;
  for (int i = 0; i < 5; i++) {
    writer.AddBytes("file" + std::to_string(i), Noise(1000, i));
  }
  std::string error;
  ASSERT_TRUE(writer.Write(dir_, "game", 2500, &error)) << error;
  EXPECT_TRUE(std::filesystem::exists(dir_ / "game.mpack"));
  EXPECT_TRUE(std::filesystem::exists(dir_ / "game_001.mpack"));
  EXPECT_TRUE(std::filesystem::exists(dir_ / "game_002.mpack"));
  EXPECT_FALSE(std::filesystem::exists(dir_ / "game_003.mpack"));

  auto reader = mpack::Reader::Open(dir_ / "game.mpack", &error);
  ASSERT_TRUE(reader) << error;
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(reader->Read("file" + std::to_string(i)), Noise(1000, i)) << i;
  }
  // Only archive 0 can be opened as a pack.
  EXPECT_FALSE(mpack::Reader::Open(dir_ / "game_001.mpack"));
}

TEST_F(MPackTest, DetectsCorruptData) {
  mpack::Writer writer;
  writer.AddBytes("noise.bin", Noise(256, 7));
  ASSERT_TRUE(writer.Write(dir_, "game"));
  auto reader = mpack::Reader::Open(dir_ / "game.mpack");
  ASSERT_TRUE(reader);
  const uint64_t offset = reader->Find("noise.bin")->offset;
  reader.reset();

  {
    std::fstream file(dir_ / "game.mpack",
                      std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(static_cast<std::streamoff>(offset + 10));
    file.put(static_cast<char>(Noise(256, 7)[10] ^ 0x5a));
  }
  reader = mpack::Reader::Open(dir_ / "game.mpack");
  ASSERT_TRUE(reader);
  std::string error;
  EXPECT_FALSE(reader->Read("noise.bin", &error));
  EXPECT_NE(error.find("checksum"), std::string::npos) << error;
}

TEST_F(MPackTest, RejectsUnsafePaths) {
  EXPECT_TRUE(mpack::IsValidEntryPath("maps/main.mmap"));
  EXPECT_FALSE(mpack::IsValidEntryPath(""));
  EXPECT_FALSE(mpack::IsValidEntryPath("/etc/passwd"));
  EXPECT_FALSE(mpack::IsValidEntryPath("../secret"));
  EXPECT_FALSE(mpack::IsValidEntryPath("a//b"));
  EXPECT_FALSE(mpack::IsValidEntryPath("a\\b"));
  EXPECT_FALSE(mpack::IsValidEntryPath("C:/x"));

  mpack::Writer writer;
  writer.AddBytes("../escape", Bytes("x"));
  std::string error;
  EXPECT_FALSE(writer.Write(dir_, "game", mpack::kDefaultMaxArchiveBytes, &error));
}

TEST_F(MPackTest, MountServesPathsUnderItsPrefixAndFallsBackToDisk) {
  mpack::Writer writer;
  writer.AddBytes("a.txt", Bytes("packed"));
  writer.AddBytes("ui/fonts/f.ttf", Bytes("font"));
  ASSERT_TRUE(writer.Write(dir_, "game"));
  {
    std::ofstream(dir_ / "loose.txt") << "on disk";
  }
  auto reader = mpack::Reader::Open(dir_ / "game.mpack");
  ASSERT_TRUE(reader);

  // The prefix doesn't have to exist on disk.
  const std::filesystem::path root = "/nowhere/game";
  os::Mount(root, reader);
  os::Mount("/nowhere/engine/resources", reader, "ui/");

  EXPECT_EQ(os::ReadText(root / "a.txt"), "packed");
  EXPECT_EQ(os::ReadText(root / "sub/../a.txt"), "packed");
  EXPECT_TRUE(os::IsPacked(root / "a.txt"));
  EXPECT_EQ(os::ReadText("/nowhere/engine/resources/fonts/f.ttf"), "font");
  EXPECT_FALSE(os::ReadText(root / "missing.txt"));
  EXPECT_FALSE(os::IsPacked("/nowhere/gameplay/a.txt"));

  EXPECT_EQ(os::ReadText(dir_ / "loose.txt"), "on disk");
  EXPECT_FALSE(os::IsPacked(dir_ / "loose.txt"));
}

TEST_F(MPackTest, LoadsAProjectFromAPack) {
  const auto source =
      std::filesystem::path(MERCURY_SOURCE_DIR) / "examples/monkey_ball";
  mpack::Writer writer;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
    const auto rel = entry.path().lexically_relative(source);
    if (!entry.is_regular_file() || rel.begin()->string().starts_with('.') ||
        *rel.begin() == "scripts") {
      continue;
    }
    writer.Add(rel.generic_string(), entry.path());
  }
  std::string error;
  ASSERT_TRUE(writer.Write(dir_, "game", mpack::kDefaultMaxArchiveBytes, &error))
      << error;
  auto reader = mpack::Reader::Open(dir_ / "game.mpack", &error);
  ASSERT_TRUE(reader) << error;

  const std::filesystem::path root = dir_ / "shipped";
  os::Mount(root, reader);
  auto project = Project::Load(root / "monkey_ball.mproj", &error);
  ASSERT_TRUE(project) << error;
  EXPECT_EQ(project->GetRoot(), root);

  Map map;
  ASSERT_TRUE(map.Load(project->Resolve(project->GetMainMap()), &error))
      << error;
  EXPECT_TRUE(map.GetRoot());

  AssetDatabase assets(project);
  EXPECT_TRUE(assets.Load("res://assets/player.masset"));
}

// A shipped game has no loose files: a model's textures, spelled with
// Windows separators and the wrong case as OBJ files often are, must still be
// found in the pack.
TEST_F(MPackTest, ModelFindsItsTexturesInAPack) {
  const uint8_t pixels[4 * 4 * 4] = {};
  ASSERT_TRUE(stbi_write_png((dir_ / "stone.png").string().c_str(), 4, 4, 4, pixels, 16));
  mpack::Writer writer;
  writer.AddBytes("models/box/box.obj",
                  Bytes("mtllib box.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\n"
                        "vt 0 1\nusemtl Stone\nf 1/1 2/2 3/3\nusemtl Brick\nf 1/1 2/2 3/3\n"));
  writer.AddBytes("models/box/box.mtl",
                  Bytes("newmtl Stone\nKd 1 1 1\nmap_Kd textures\\Stone.PNG\n"
                        "newmtl Brick\nKd 1 1 1\nmap_Kd textures/stone.png\n"));
  writer.Add("models/box/textures/stone.png", dir_ / "stone.png");
  std::string error;
  ASSERT_TRUE(writer.Write(dir_, "game", mpack::kDefaultMaxArchiveBytes, &error)) << error;
  auto reader = mpack::Reader::Open(dir_ / "game.mpack", &error);
  ASSERT_TRUE(reader) << error;

  const std::filesystem::path root = dir_ / "shipped";
  os::Mount(root, reader);
  EXPECT_EQ(os::ListDirectory(root / "models/box/textures"),
            std::vector<std::string>{"stone.png"});
  auto model = Model::Load((root / "models/box/box.obj").string(), ModelImportSettings{},
                           &error);
  ASSERT_TRUE(model) << error;
  int textured = 0;
  for (const auto& material : model->Materials()) {
    if (material.name != "Stone" && material.name != "Brick") continue;
    ASSERT_TRUE(material.texture) << material.name;
    EXPECT_EQ(material.texture->width, 4);
    textured++;
  }
  EXPECT_EQ(textured, 2);
}
